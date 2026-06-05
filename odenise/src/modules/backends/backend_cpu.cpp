// ============================================================================
//  backend_cpu.cpp -- Backend de calcul CPU (repli / fallback).
//
//  Phase 3a : implemente BackendBase (chemin C++) + expose create_backend.
//  Le chemin legacy (create/destroy/process/self_test) est conserve pour
//  la compatibilite avec les modules phase 1/2.
//
//  Architecture :
//    CpuBackendContext : BackendContext concret CPU.
//      - scratch_buf() : buffer RAM pre-alloue a n_max * sizeof(float).
//      - compute_stream() : nullptr (CPU, pas de stream).
//      - backend_type() : kBackendCPU.
//
//    CpuBackendImpl : BackendBase complet.
//      - Possede une AudioChain interne (compilee directement dans ce module,
//        pas de link contre libodenise).
//      - install_module() : installe le module sur CpuBackendContext,
//        cable via AudioChain.
//      - process() : injecte in[0] dans le premier module via set_input(),
//        appelle AudioChain::process().
//      - measure() : N blocs de bruit blanc, chrono std::chrono,
//        remplit last_latency_ + last_stats_, store(release) sur measure_ready_.
//
//  audio_chain.cpp est compile directement dans ce module (pas de link contre
//  libodenise). Meme compilateur que le coeur (GCC/UCRT64 pour CPU).
// ============================================================================
#include "ns_engine.h"
#include "chain/audio_chain.h"

#include <algorithm>    // std::min, std::max
#include <chrono>       // std::chrono::steady_clock
#include <cstring>      // std::memcpy
#include <new>          // std::nothrow
#include <random>       // std::mt19937, std::uniform_real_distribution
#include <vector>       // std::vector

// ============================================================================
//  CpuBackendContext -- contexte de ressource CPU.
//  Fourni par CpuBackendImpl a chaque module lors de l'installation.
//  Possede le scratch buffer pre-alloue a n_max * sizeof(float).
// ============================================================================
class CpuBackendContext final : public ns::BackendContext {
public:
    explicit CpuBackendContext(int n_max)
        : scratch_(static_cast<std::size_t>(n_max) * sizeof(float), std::byte{0}) {}

    // [CTRL] Retourne le debut du scratch buffer.
    // Pour l'instant : allocation unique partagee entre tous les modules.
    // Extension future : allocateur par regions pour modules multiples.
    void* scratch_buf(std::size_t /*bytes*/) noexcept override {
        return scratch_.data();
    }

    // [RT/CTRL] Pas de stream CPU.
    void* compute_stream() noexcept override { return nullptr; }

    // [CTRL] Type de backend : CPU.
    int   backend_type()   const noexcept override { return ns::kBackendCPU; }

private:
    std::vector<std::byte> scratch_;   // buffer de travail pre-alloue
};

// ============================================================================
//  CpuBackendImpl -- implementation complete de BackendBase pour le CPU.
//  Possede AudioChain (compilee dans ce module) et CpuBackendContext.
// ============================================================================
class CpuBackendImpl final : public ns::BackendBase {
public:
    explicit CpuBackendImpl(int sample_rate, int n_max)
        : sample_rate_(sample_rate)
        , n_max_(n_max)
        , ctx_(n_max) {}

    ~CpuBackendImpl() override = default;

    // -----------------------------------------------------------------------
    //  install_module -- installe un module dans la chaine.
    //  Delegue a AudioChain::install() qui gere le cablage et le swap atomique.
    // -----------------------------------------------------------------------
    bool install_module(ns::ModuleBase* mod,
                        ns::ModuleKind  kind,
                        int             position) override {
        if (!mod) return false;
        return chain_.install(this, mod, kind, position);
    }

    // -----------------------------------------------------------------------
    //  uninstall_module -- retire un module de la chaine.
    // -----------------------------------------------------------------------
    void uninstall_module(ns::ModuleKind kind, int position) noexcept override {
        chain_.remove(this, kind, position);
    }

    // -----------------------------------------------------------------------
    //  process -- [RT] traitement d'un bloc audio.
    //  Injecte in[0] dans le premier module, execute la liste plate cablee.
    //  Zero decision, zero allocation.
    // -----------------------------------------------------------------------
    ns::Status process(const float* const* in,
                       float*              out,
                       int                 num_frames) noexcept override {
        if (!in || !out || num_frames <= 0)
            return ns::Status::InvalidArg;

        if (chain_.declared_latency_samples() == 0 && nodes_empty_)
            return ns::Status::Unsupported;

        // Injecte l'entree dans le premier module (cable a l'install).
        if (first_module_)
            first_module_->set_input(in[0]);

        // Execute la chaine plate cablee.
        chain_.process(num_frames);

        // Copie la sortie du dernier module vers out.
        if (last_module_) {
            const void* src = last_module_->output_buf();
            if (src)
                std::memcpy(out, src,
                    static_cast<std::size_t>(num_frames) * sizeof(float));
        }

        return ns::Status::Ok;
    }

    // -----------------------------------------------------------------------
    //  caps -- capabilities du backend CPU.
    // -----------------------------------------------------------------------
    ns::BackendCaps caps() const noexcept override {
        ns::BackendCaps c;
        c.name         = "cpu";
        c.is_gpu       = false;
        c.backend_type = ns::kBackendCPU;
        return c;
    }

    // -----------------------------------------------------------------------
    //  measure -- mesure de latence et de charge CPU hors RT.
    //  Injecte N blocs de bruit blanc, chronometre chaque process(),
    //  calcule min/max/mean, remplit last_latency_ + last_stats_.
    //  Appele hors RT uniquement -- jamais depuis process().
    // -----------------------------------------------------------------------
    void measure(int num_blocks) override {
        if (num_blocks <= 0) num_blocks = 16;

        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        std::vector<float> in_buf(static_cast<std::size_t>(n_max_));
        std::vector<float> out_buf(static_cast<std::size_t>(n_max_));
        for (auto& s : in_buf) s = dist(rng);

        const float* in_ptr = in_buf.data();
        float* out_ptr = out_buf.data();

        float min_ms = 1e9f;
        float max_ms = -1e9f;
        float sum_ms = 0.0f;

        for (int i = 0; i < num_blocks; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            process(&in_ptr, out_ptr, n_max_);
            const auto t1 = std::chrono::steady_clock::now();

            const float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
            min_ms  = std::min(min_ms, ms);
            max_ms  = std::max(max_ms, ms);
            sum_ms += ms;
        }

        const float mean_ms   = sum_ms / static_cast<float>(num_blocks);
        const float budget_ms = (sample_rate_ > 0)
            ? (static_cast<float>(n_max_) / static_cast<float>(sample_rate_)) * 1000.0f
            : 0.0f;

        last_stats_.min_ms    = min_ms;
        last_stats_.max_ms    = max_ms;
        last_stats_.mean_ms   = mean_ms;
        last_stats_.budget_ms = budget_ms;
        last_stats_.load_pct  = (budget_ms > 0.0f)
            ? (mean_ms / budget_ms) * 100.0f : 0.0f;

        // Latence mesuree : somme des latences declarees au cablage.
        const int declared = chain_.declared_latency_samples();
        last_latency_.measured_samples = declared;
        last_latency_.measured_ms = (sample_rate_ > 0)
            ? (static_cast<float>(declared) / static_cast<float>(sample_rate_)) * 1000.0f
            : 0.0f;

        // Publie les resultats -- lu par l'engine via measure_ready() acquire.
        measure_ready_.store(true, std::memory_order_release);
    }

    // -----------------------------------------------------------------------
    //  Accesseurs utilises par AudioChain au cablage pour noter le premier
    //  et le dernier module de la chaine.
    // -----------------------------------------------------------------------
    void set_first_module(ns::ModuleBase* m) noexcept {
        first_module_ = m;
        nodes_empty_  = (m == nullptr);
    }
    void set_last_module(ns::ModuleBase* m) noexcept { last_module_ = m; }

private:
    int                    sample_rate_;
    int                    n_max_;
    CpuBackendContext      ctx_;
    ns::chain::AudioChain  chain_;
    ns::ModuleBase*        first_module_ = nullptr;  // premier module de la chaine
    ns::ModuleBase*        last_module_  = nullptr;  // dernier module de la chaine
    bool                   nodes_empty_  = true;     // vrai si chaine vide
};

// ============================================================================
//  Chemin legacy (phase 1/2) -- conserve pour compatibilite.
//  Les modules qui n'exposent pas encore create_module continuent de
//  fonctionner via ce chemin.
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
    return static_cast<int>(ns::Status::Ok);   // pas de parametre a cette etape
}

static int cpu_process(OdeniseModuleInstance /*self*/,
                       OdeniseProcessCtx* /*ctx*/) {
    // Backend de calcul : ne route pas l'audio comme un module de suppression.
    // Les primitives de calcul sont branchees via le chemin C++ (BackendBase).
    return static_cast<int>(ns::Status::Unsupported);
}

static OdeniseTestResultC cpu_self_test() {
    // Test chemin legacy : instanciation/destruction.
    auto* inst = static_cast<CpuBackendInstance*>(cpu_create(48000, 1024));
    if (!inst)
        return { 0, "echec allocation instance legacy" };
    cpu_destroy(inst);

    // Test chemin C++ : instanciation CpuBackendImpl.
    auto* impl = new (std::nothrow) CpuBackendImpl(48000, 1024);
    if (!impl)
        return { 0, "echec allocation CpuBackendImpl" };
    delete impl;

    return { 1, "backend CPU OK : legacy + C++ instanciation/destruction" };
}

// ============================================================================
//  Extension C++ (phase 3+) -- create_backend.
//  Retourne un CpuBackendImpl* vu comme ns::BackendBase*.
//  L'objet est gere par le module : cree ici, detruit par l'engine via
//  le destructeur virtuel quand le backend est remplace ou detruit.
// ============================================================================
static ns::BackendBase* cpu_create_backend(int sample_rate, int n_max) {
    return new (std::nothrow) CpuBackendImpl(sample_rate, n_max);
}

// ============================================================================
//  Vtable statique + point d'entree.
// ============================================================================
static const OdeniseModuleVTable s_vtable = {
    /* abi_version   */ ns::kAbiVersion,
    /* info          */ { /* id           */ 0,
                          /* kind         */ static_cast<int>(ns::ModuleKind::ComputeBackend),
                          /* name         */ "cpu",
                          /* description  */ "Backend de calcul CPU (repli, sans GPU)",
                          /* needs_gpu    */ 0,
                          /* backend_type */ ns::kBackendCPU },
    /* create        */ cpu_create,
    /* destroy       */ cpu_destroy,
    /* set_param     */ cpu_set_param,
    /* process       */ cpu_process,
    /* self_test     */ cpu_self_test,
    /* create_module */ nullptr,
    /* create_backend*/ cpu_create_backend
};

extern "C" ODENISE_EXPORT const OdeniseModuleVTable* odenise_module_entry() {
    return &s_vtable;
}
