// ============================================================================
//  backend_cpu.cpp -- Backend de calcul CPU (repli / fallback).
//
//  Phase 3a : implémente BackendBase (chemin C++) et expose create_backend.
//  Le chemin legacy (create/destroy/set_param/process/self_test) est conservé
//  pour la compatibilité avec les modules phase 1/2.
//
//  Ce module est compilé SÉPARÉMENT du cœur (pas de lien contre libodenise).
//  Il n'inclut que ns_engine.h (API publique). AudioChain est interne au
//  cœur et inaccessible ici : la liste plate de modules est gérée localement.
//
//  Derrière la frontière C (vtable), l'implémentation est du C++ normal.
// ============================================================================
#include "ns_engine.h"

#include <algorithm>    // std::find, std::min, std::max
#include <chrono>       // std::chrono::steady_clock
#include <cstdlib>      // std::rand, std::srand
#include <cstring>      // std::memcpy
#include <new>          // std::nothrow
#include <vector>       // std::vector

// ============================================================================
//  CpuBackendContext -- BackendContext concret pour le backend CPU.
//
//  scratch_buf() retourne un pointeur sur un buffer pré-alloué à la
//  construction (n_max * sizeof(float)). Hors RT uniquement : la taille est
//  fixée à la création du contexte et ne change pas.
//  compute_stream() retourne nullptr (CPU : pas de stream natif).
//  backend_type() retourne kBackendCPU.
// ============================================================================
class CpuBackendContext final : public ns::BackendContext {
public:
    explicit CpuBackendContext(int n_max)
        : scratch_(static_cast<std::size_t>(n_max > 0 ? n_max : 4096)
                   * sizeof(float)) {}

    // [CTRL] Retourne le début du scratch buffer pré-alloué.
    // Toujours le même pointeur : une seule région partagée par module.
    // Le module l'utilise pour placer son output_buf_.
    void* scratch_buf(std::size_t /*bytes*/) noexcept override {
        return scratch_.data();
    }

    // [RT/CTRL] Pas de stream CPU.
    void* compute_stream() noexcept override { return nullptr; }

    // [CTRL] Type de backend de ce contexte.
    int backend_type() const noexcept override { return ns::kBackendCPU; }

    // Pointeur brut sur le scratch (pour usage interne au backend).
    float* raw() noexcept {
        return reinterpret_cast<float*>(scratch_.data());
    }
    std::size_t size_bytes() const noexcept { return scratch_.size(); }

private:
    std::vector<std::byte> scratch_;
};

// ============================================================================
//  CpuBackendImpl -- BackendBase complet pour le backend CPU.
//
//  Gère une liste plate de ModuleBase* (chaîne de traitement).
//  Chaque module reçoit son propre CpuBackendContext au install_module().
//  Le câblage set_input/output_buf est résolu à chaque modification de liste.
//
//  process() :
//    - Cas chaîne vide : Status::Unsupported.
//    - Cas chaîne non vide :
//        1. modules_[0]->set_input(in[0])      // injection entrée audio
//        2. Pour chaque module : m->process(num_frames)
//        3. memcpy(out, modules_.back()->output_buf(), n*sizeof(float))
//
//  Câblage interne (rewire()) :
//    Pour i > 0 : modules_[i]->set_input(modules_[i-1]->output_buf())
//    Résolu après chaque install/uninstall (hors RT).
// ============================================================================
class CpuBackendImpl final : public ns::BackendBase {
public:
    CpuBackendImpl(int sample_rate, int n_max)
        : sample_rate_(sample_rate > 0 ? sample_rate : 48000)
        , n_max_(n_max > 0 ? n_max : 4096) {}

    ~CpuBackendImpl() override {
        // Désinstalle tous les modules dans l'ordre inverse du câblage.
        // Les modules sont possédés par l'engine, pas par le backend :
        // on appelle uninstall() sans delete.
        for (int i = static_cast<int>(entries_.size()) - 1; i >= 0; --i) {
            entries_[i].module->uninstall(entries_[i].ctx.get());
        }
    }

    // -----------------------------------------------------------------------
    //  install_module -- installe un module à la position donnee.
    //  Crée un CpuBackendContext dédié, installe le module dessus,
    //  insère dans la liste, recâble.
    //  Retourne false si le module refuse l'installation.
    // -----------------------------------------------------------------------
    bool install_module(ns::ModuleBase* mod,
                        ns::ModuleKind  /*kind*/,
                        int             position) override {
        if (!mod) return false;

        // Un contexte par module (scratch indépendant).
        auto ctx = std::make_unique<CpuBackendContext>(n_max_);

        if (!mod->install(ctx.get()))
            return false;

        // Insertion à la bonne position (tri par position).
        Entry e;
        e.module   = mod;
        e.position = position;
        e.ctx      = std::move(ctx);

        auto it = std::lower_bound(entries_.begin(), entries_.end(), e,
            [](const Entry& a, const Entry& b) {
                return a.position < b.position;
            });
        entries_.insert(it, std::move(e));

        rewire();
        return true;
    }

    // -----------------------------------------------------------------------
    //  uninstall_module -- retire le module à la position donnee.
    // -----------------------------------------------------------------------
    void uninstall_module(ns::ModuleKind /*kind*/,
                          int            position) noexcept override {
        auto it = std::find_if(entries_.begin(), entries_.end(),
            [position](const Entry& e) { return e.position == position; });

        if (it == entries_.end()) return;

        it->module->uninstall(it->ctx.get());
        entries_.erase(it);

        rewire();
    }

    // -----------------------------------------------------------------------
    //  process -- [RT] traitement d'un bloc audio.
    //  Injecte in[0] dans le premier module, itère la chaîne, copie la
    //  sortie du dernier module vers out.
    //  Zéro allocation, zéro branchement dynamique.
    // -----------------------------------------------------------------------
    ns::Status process(const float* const* in,
                       float*              out,
                       int                 num_frames) noexcept override {
        if (entries_.empty())
            return ns::Status::Unsupported;

        if (!in || !in[0] || !out || num_frames <= 0)
            return ns::Status::InvalidArg;

        // Injection de l'entrée audio dans le premier module de la chaîne.
        entries_.front().module->set_input(in[0]);

        // Exécution séquentielle de la chaîne.
        for (auto& e : entries_)
            e.module->process(num_frames);

        // Copie de la sortie du dernier module vers le buffer de sortie hôte.
        const void* tail_out = entries_.back().module->output_buf();
        if (tail_out && tail_out != static_cast<void*>(out)) {
            std::memcpy(out,
                        tail_out,
                        static_cast<std::size_t>(num_frames) * sizeof(float));
        }

        return ns::Status::Ok;
    }

    // -----------------------------------------------------------------------
    //  caps -- capabilities du backend CPU.
    // -----------------------------------------------------------------------
    ns::BackendCaps caps() const noexcept override {
        ns::BackendCaps bc;
        bc.name         = "cpu";
        bc.is_gpu       = false;
        bc.vram_bytes   = 0;
        bc.backend_type = ns::kBackendCPU;
        return bc;
    }

    // -----------------------------------------------------------------------
    //  measure -- mesure de latence réelle sur num_blocks blocs de bruit blanc.
    //  Hors RT uniquement. Remplit last_latency_ et last_stats_, puis pose
    //  measure_ready_ (release) pour signaler à l'engine/UI.
    // -----------------------------------------------------------------------
    void measure(int num_blocks) override {
        if (num_blocks <= 0) num_blocks = 16;

        // Bruit blanc normalisé en entrée (pire cas spectral pour la STFT).
        const int n = n_max_;
        std::vector<float> in_buf(static_cast<std::size_t>(n));
        std::vector<float> out_buf(static_cast<std::size_t>(n));

        std::srand(42u);   // reproductible
        for (auto& s : in_buf)
            s = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX))
                * 2.0f - 1.0f;

        const float* in_ptr = in_buf.data();

        // Budget temps réel : hop = n/4 (75 % de recouvrement, convention odenise).
        const int   hop        = n / 4;
        const float budget_ms  = (sample_rate_ > 0)
            ? (static_cast<float>(hop) / static_cast<float>(sample_rate_)) * 1000.0f
            : 0.0f;

        float min_ms  =  1e9f;
        float max_ms  = -1e9f;
        float sum_ms  =  0.0f;

        for (int b = 0; b < num_blocks; ++b) {
            const auto t0 = std::chrono::steady_clock::now();
            process(&in_ptr, out_buf.data(), n);
            const auto t1 = std::chrono::steady_clock::now();

            const float elapsed_ms =
                std::chrono::duration<float, std::milli>(t1 - t0).count();

            if (elapsed_ms < min_ms) min_ms = elapsed_ms;
            if (elapsed_ms > max_ms) max_ms = elapsed_ms;
            sum_ms += elapsed_ms;
        }

        const float mean_ms = sum_ms / static_cast<float>(num_blocks);

        // Latence déclarée : somme des latencies déclarées par les modules.
        int declared = 0;
        for (const auto& e : entries_)
            declared += e.module->latency_samples();

        // Écriture des résultats hors RT, puis signal release.
        last_latency_.declared_samples = declared;
        last_latency_.declared_ms      = (sample_rate_ > 0)
            ? (static_cast<float>(declared) / static_cast<float>(sample_rate_))
              * 1000.0f
            : 0.0f;
        last_latency_.measured_samples = 0;   // CPU : pas de mesure sample-précise
        last_latency_.measured_ms      = mean_ms;
        last_latency_.in_sync          = (declared == 0);

        last_stats_.min_ms    = (min_ms  > 1e8f) ? 0.0f : min_ms;
        last_stats_.max_ms    = (max_ms  < -1e8f) ? 0.0f : max_ms;
        last_stats_.mean_ms   = mean_ms;
        last_stats_.budget_ms = budget_ms;
        last_stats_.load_pct  = (budget_ms > 0.0f)
            ? (mean_ms / budget_ms) * 100.0f
            : 0.0f;

        // Signal : résultats disponibles (lu par engine/UI via measure_ready()).
        measure_ready_.store(true, std::memory_order_release);
    }

private:
    // -----------------------------------------------------------------------
    //  rewire -- recâble set_input/output_buf entre modules consécutifs.
    //  Appelé hors RT après toute modification de la liste.
    //  Module i reçoit comme entrée le output_buf() du module i-1.
    // -----------------------------------------------------------------------
    void rewire() noexcept {
        for (std::size_t i = 1; i < entries_.size(); ++i) {
            const void* prev_out = entries_[i - 1].module->output_buf();
            entries_[i].module->set_input(prev_out);
        }
    }

    // Un maillon de la liste plate.
    struct Entry {
        ns::ModuleBase*                module   = nullptr;
        int                            position = 0;
        std::unique_ptr<CpuBackendContext> ctx;
    };

    int                  sample_rate_;
    int                  n_max_;
    std::vector<Entry>   entries_;   // liste plate, triée par position
};

// ============================================================================
//  Chemin legacy (phase 1/2) -- conservé pour compatibilité.
//  Utilisé par l'engine si create_backend retourne nullptr (ne doit pas
//  arriver ici, mais le chemin legacy reste fonctionnel par principe).
// ============================================================================
struct CpuBackendInstance {
    int sample_rate = 48000;
    int n_max       = 4096;
};

static OdeniseModuleInstance cpu_create(int sample_rate, int n_max) {
    auto* inst = new (std::nothrow) CpuBackendInstance;
    if (!inst) return nullptr;
    inst->sample_rate = sample_rate;
    inst->n_max       = n_max;
    return static_cast<OdeniseModuleInstance>(inst);
}

static void cpu_destroy(OdeniseModuleInstance self) {
    delete static_cast<CpuBackendInstance*>(self);
}

static int cpu_set_param(OdeniseModuleInstance /*self*/,
                         int /*param_id*/, float /*value*/) {
    return static_cast<int>(ns::Status::Ok);
}

static int cpu_process(OdeniseModuleInstance /*self*/,
                       OdeniseProcessCtx* /*ctx*/) {
    // Le backend CPU ne route pas l'audio via le chemin legacy.
    // Le routage réel passe par CpuBackendImpl::process() (chemin C++).
    return static_cast<int>(ns::Status::Unsupported);
}

static OdeniseTestResultC cpu_self_test() {
    // Test 1 : chemin legacy -- allocation / destruction.
    auto* legacy = static_cast<CpuBackendInstance*>(cpu_create(48000, 1024));
    if (!legacy)
        return { 0, "echec allocation instance legacy" };
    cpu_destroy(legacy);

    // Test 2 : chemin C++ -- allocation CpuBackendImpl, caps(), destruction.
    auto* impl = new (std::nothrow) CpuBackendImpl(48000, 1024);
    if (!impl)
        return { 0, "echec allocation CpuBackendImpl" };

    const ns::BackendCaps bc = impl->caps();
    delete impl;

    if (bc.is_gpu)
        return { 0, "caps() : is_gpu inattendu (attendu false)" };
    if (bc.backend_type != ns::kBackendCPU)
        return { 0, "caps() : backend_type incorrect" };

    return { 1, "backend CPU OK : legacy + C++ instanciation/caps" };
}

// ============================================================================
//  Point d'entrée C++ (phase 3+) -- retourne un CpuBackendImpl.
// ============================================================================
static ns::BackendBase* cpu_create_backend(int sample_rate, int n_max) {
    return new (std::nothrow) CpuBackendImpl(sample_rate, n_max);
}

// ============================================================================
//  Vtable statique + point d'entrée du module.
// ============================================================================
static const OdeniseModuleVTable s_vtable = {
    /* abi_version */   ns::kAbiVersion,
    /* info */          { /* id */             0,
                          /* kind */           static_cast<int>(ns::ModuleKind::ComputeBackend),
                          /* name */           "cpu",
                          /* description */    "Backend de calcul CPU (repli, sans GPU)",
                          /* needs_gpu */      0,
                          /* backend_type_id*/ ns::kBackendCPU },
    /* create */        cpu_create,
    /* destroy */       cpu_destroy,
    /* set_param */     cpu_set_param,
    /* process */       cpu_process,
    /* self_test */     cpu_self_test,
    /* create_module */ nullptr,           // backend, pas de module de traitement
    /* create_backend*/ cpu_create_backend
};

extern "C" ODENISE_EXPORT const OdeniseModuleVTable* odenise_module_entry() {
    return &s_vtable;
}
