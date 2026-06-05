// ============================================================================
//  backend_cpu.cpp -- Backend de calcul CPU (repli / fallback).
//
//  Implemente BackendBase (chemin C++ pur). Possede son AudioChain en
//  interne. L'engine ordonnance via install_module / uninstall_module.
//
//  Architecture :
//    CpuBackendContext : BackendContext concret CPU.
//      - scratch_buf() : buffer RAM pre-alloue a n_max * sizeof(float).
//      - compute_stream() : nullptr (CPU, pas de stream).
//      - backend_type() : kBackendCPU.
//
//    CpuBackendImpl : BackendBase complet.
//      - Possede AudioChain interne (compilee dans ce module).
//      - install_module() : delegue a chain_.install(), met a jour la
//        latence declaree dans last_latency_ (lue par l'engine).
//      - process() : injecte in[0] dans chain_.first_module() via set_input,
//        appelle chain_.process(), copie chain_.last_module()->output_buf()
//        vers out.
//      - measure() : N blocs de bruit blanc, chrono std::chrono,
//        remplit last_latency_.measured_* + last_stats_,
//        store(release) sur measure_ready_.
//
//  audio_chain.cpp est compile via odenise_chain (link partage avec le coeur).
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
//  Possede AudioChain et CpuBackendContext.
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
    //  Delegue a AudioChain. Apres succes, met a jour la latence declaree
    //  pour que l'engine la lise via last_latency_info().
    // -----------------------------------------------------------------------
    bool install_module(ns::ModuleBase* mod,
                        ns::ModuleKind  kind,
                        int             position) override {
        if (!mod) return false;
        if (!chain_.install(this, mod, kind, position))
            return false;
        publish_declared_latency();
        return true;
    }

    // -----------------------------------------------------------------------
    //  uninstall_module -- retire un module de la chaine.
    //  Recalcule la latence declaree apres retrait.
    // -----------------------------------------------------------------------
    void uninstall_module(ns::ModuleKind kind, int position) noexcept override {
        chain_.remove(this, kind, position);
        publish_declared_latency();
    }

    // -----------------------------------------------------------------------
    //  process -- [RT] traitement d'un bloc audio.
    //  Injecte in[0] dans chain_.first_module() via set_input(),
    //  execute la chaine, copie last_module()->output_buf() vers out.
    //  Zero decision, zero allocation.
    // -----------------------------------------------------------------------
    ns::Status process(const float* const* in,
                       float*              out,
                       int                 num_frames) noexcept override {
        if (!in || !out || num_frames <= 0)
            return ns::Status::InvalidArg;

        ns::ModuleBase* first = chain_.first_module();
        ns::ModuleBase* last  = chain_.last_module();
        if (!first || !last)
            return ns::Status::Unsupported;

        // Injecte l'entree dans le premier module de la chaine.
        first->set_input(in[0]);

        // Execute la chaine plate cablee.
        chain_.process(num_frames);

        // Copie la sortie du dernier module vers out.
        const void* src = last->output_buf();
        if (!src) return ns::Status::Unsupported;
        std::memcpy(out, src,
                    static_cast<std::size_t>(num_frames) * sizeof(float));

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
    //  context -- retourne le BackendContext CPU possede par ce backend.
    //  Transmis aux modules par AudioChain lors de l'install().
    // -----------------------------------------------------------------------
    ns::BackendContext* context() noexcept override { return &ctx_; }

    // -----------------------------------------------------------------------
    //  measure -- mesure de latence et de charge CPU hors RT.
    //  Injecte N blocs de bruit blanc, chronometre chaque process(),
    //  calcule min/max/mean, remplit last_latency_.measured_* + last_stats_.
    //  Appele hors RT uniquement -- jamais depuis process().
    //  La latence declaree (last_latency_.declared_*) est ecrite par
    //  publish_declared_latency() a chaque (un)install_module : pas ici.
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

        // Latence mesuree : pour l'instant, egale a la declaree (pas de
        // mesure reelle par injection sur le CPU sans STFT). Sera affinee
        // quand des modules a latence non triviale seront integres.
        const int declared = chain_.declared_latency_samples();
        last_latency_.measured_samples = declared;
        last_latency_.measured_ms = (sample_rate_ > 0)
            ? (static_cast<float>(declared) / static_cast<float>(sample_rate_)) * 1000.0f
            : 0.0f;

        // Publie les resultats -- lu par l'engine via measure_ready() acquire.
        measure_ready_.store(true, std::memory_order_release);
    }

private:
    // -----------------------------------------------------------------------
    //  publish_declared_latency -- ecrit la latence declaree dans
    //  last_latency_. Appele hors RT a chaque (un)install_module.
    //  Ne leve PAS measure_ready_ : ce drapeau reste reserve aux resultats
    //  d'une mesure reelle (measure()).
    // -----------------------------------------------------------------------
    void publish_declared_latency() noexcept {
        const int declared = chain_.declared_latency_samples();
        last_latency_.declared_samples = declared;
        last_latency_.declared_ms = (sample_rate_ > 0)
            ? (static_cast<float>(declared) / static_cast<float>(sample_rate_)) * 1000.0f
            : 0.0f;
    }

    int                    sample_rate_;
    int                    n_max_;
    CpuBackendContext      ctx_;
    ns::chain::AudioChain  chain_;
};

// ============================================================================
//  self_test -- valide CpuBackendImpl sans engine ni audio reel.
// ============================================================================
static OdeniseTestResultC cpu_self_test() {
    auto* impl = new (std::nothrow) CpuBackendImpl(48000, 1024);
    if (!impl)
        return { 0, "echec allocation CpuBackendImpl" };
    delete impl;
    return { 1, "backend CPU OK : instanciation/destruction" };
}

// ============================================================================
//  create_backend -- retourne un CpuBackendImpl* vu comme ns::BackendBase*.
//  L'objet est cree par le module, possede par l'engine.
// ============================================================================
static ns::BackendBase* cpu_create_backend(int sample_rate, int n_max) {
    return new (std::nothrow) CpuBackendImpl(sample_rate, n_max);
}

// ============================================================================
//  Vtable statique + point d'entree.
//  Chemin C++ uniquement : create/destroy/process/set_param a nullptr.
// ============================================================================
static const OdeniseModuleVTable s_vtable = {
    /* abi_version   */ ns::kAbiVersion,
    /* info          */ { /* id              */ 0,
                          /* kind            */ static_cast<int>(ns::ModuleKind::ComputeBackend),
                          /* name            */ "cpu",
                          /* description     */ "Backend de calcul CPU (repli, sans GPU)",
                          /* needs_gpu       */ 0,
                          /* backend_type_id */ ns::kBackendCPU },
    /* create        */ nullptr,
    /* destroy       */ nullptr,
    /* set_param     */ nullptr,
    /* process       */ nullptr,
    /* self_test     */ cpu_self_test,
    /* create_module */ nullptr,
    /* create_backend*/ cpu_create_backend
};

extern "C" ODENISE_EXPORT const OdeniseModuleVTable* odenise_module_entry() {
    return &s_vtable;
}
