// ============================================================================
//  backend_cpu.cpp -- Backend de calcul CPU (repli / fallback).
//
//  Phase 3 : implemente BackendBase + ModuleBase (pour le loader).
//  Structs POD (info_c, caps_c, self_test_c) pour la frontiere ABI-safe.
//
//  Architecture :
//    CpuBackendContext : BackendContext concret CPU.
//      - scratch_buf() : buffer RAM pre-alloue a n_max * sizeof(float).
//      - compute_stream() : nullptr (CPU, pas de stream).
//      - backend_type() : kBackendCPU.
//
//    CpuBackendImpl : BackendBase complet + ModuleBase (pour le loader).
//      - info_c()  : retourne OdeniseModuleInfoC statique.
//      - caps_c()  : retourne OdeniseBackendCapsC statique.
//      - self_test_c() : retourne OdeniseTestResultC (test instanciation).
//      - reconfigure() : redimensionne le scratch, propage aux modules.
//      - install_module() : installe le module via AudioChain.
//      - process() : injecte in[0] dans le premier module, execute AudioChain.
//      - measure() : N blocs de bruit blanc, chrono std::chrono,
//        remplit last_latency_ + last_stats_, store(release) sur measure_ready_.
// ============================================================================
#include "engine.h"
#include "audio_chain.h"

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
    void* scratch_buf(std::size_t /*bytes*/) noexcept override {
        return scratch_.data();
    }

    // [RT/CTRL] Pas de stream CPU.
    void* compute_stream() noexcept override { return nullptr; }

    // [CTRL] Type de backend : CPU.
    int   backend_type()   const noexcept override { return ns::kBackendCPU; }

    // [CTRL] Redimensionne le scratch buffer (appele par reconfigure).
    void resize(int n_max) {
        const auto new_size = static_cast<std::size_t>(n_max) * sizeof(float);
        if (scratch_.size() != new_size)
            scratch_.assign(new_size, std::byte{0});
    }

private:
    std::vector<std::byte> scratch_;
};

// ============================================================================
//  CpuBackendImpl -- implementation complete de BackendBase pour le CPU.
//  Herite aussi de ModuleBase pour satisfaire la signature de l'entry point
//  (OdeniseModuleEntryFn retourne ModuleBase*). Le loader caste vers
//  BackendBase* via dynamic_cast apres avoir verifie le kind.
// ============================================================================
class CpuBackendImpl final : public ns::BackendBase, public ns::ModuleBase {
public:
    explicit CpuBackendImpl(int sample_rate, int n_max)
        : sample_rate_(sample_rate)
        , n_max_(n_max)
        , ctx_(n_max) {}

    ~CpuBackendImpl() override = default;

    // -----------------------------------------------------------------------
    //  info_c -- metadonnees POD (frontiere inter-compilateurs).
    // -----------------------------------------------------------------------
    const OdeniseModuleInfoC* info_c() const noexcept override {
        static const OdeniseModuleInfoC s_info = {
            /* abi_version    */ ns::kAbiVersion,
            /* id             */ 0,
            /* kind           */ static_cast<int>(ns::ModuleKind::ComputeBackend),
            /* name           */ "cpu",
            /* description    */ "Backend de calcul CPU (repli, sans GPU)",
            /* needs_gpu      */ 0,
            /* backend_type_id*/ ns::kBackendCPU
        };
        return &s_info;
    }

    // -----------------------------------------------------------------------
    //  caps_c -- capabilities POD (frontiere inter-compilateurs).
    // -----------------------------------------------------------------------
    const OdeniseBackendCapsC* caps_c() const noexcept override {
        static const OdeniseBackendCapsC s_caps = {
            /* name        */ "cpu",
            /* is_gpu      */ 0,
            /* vram_bytes  */ 0,
            /* cc_major    */ 0,
            /* cc_minor    */ 0,
            /* has_fp16    */ 0,
            /* has_tensor  */ 0,
            /* backend_type*/ ns::kBackendCPU
        };
        return &s_caps;
    }

    // -----------------------------------------------------------------------
    //  self_test_c -- self-test POD (frontiere inter-compilateurs).
    // -----------------------------------------------------------------------
    const OdeniseTestResultC* self_test_c() const noexcept override {
        static OdeniseTestResultC s_result = { 0, nullptr };

        auto* impl = new (std::nothrow) CpuBackendImpl(48000, 1024);
        if (!impl) {
            s_result = { 0, "echec allocation CpuBackendImpl" };
        } else {
            delete impl;
            s_result = { 1, "backend CPU OK : instanciation/destruction" };
        }
        return &s_result;
    }

    // -----------------------------------------------------------------------
    //  reconfigure -- redimensionne les ressources du backend et propage
    //  aux modules installes. Appele par l'engine au bind et a chaque
    //  Engine::reconfigure(). Hors RT uniquement.
    // -----------------------------------------------------------------------
    void reconfigure(const ns::EngineCaps& caps,
                     const ns::RuntimeConfig& cfg) override {
        sample_rate_ = caps.sample_rate;
        n_max_       = caps.n_max;

        // Redimensionne le scratch buffer si n_max a change.
        ctx_.resize(n_max_);

        // Propage aux modules installes.
        // Pour l'instant, les modules sont reinstalles via uninstall/install
        // pour qu'ils recoivent le nouveau scratch. A terme, un
        // module->reconfigure(cfg) suffira si le scratch est inchange.
        (void)cfg;  // sera utilise quand les modules exploiteront n, hop, etc.
    }

    // -----------------------------------------------------------------------
    //  install_module -- installe un module dans la chaine.
    // -----------------------------------------------------------------------
    bool install_module(ns::ModuleBase* mod,
                        ns::ModuleKind  kind,
                        int             position) override {
        if (!mod) return false;
        const bool ok = chain_.install(this, &ctx_, mod, kind, position);
        if (ok) {
            first_module_ = mod;
            last_module_  = mod;
            nodes_empty_  = false;
            last_latency_.declared_samples = chain_.declared_latency_samples();
        }
        return ok;
    }

    void uninstall_module(ns::ModuleKind kind, int position) noexcept override {
        chain_.remove(this, kind, position);
        first_module_ = nullptr;
        last_module_  = nullptr;
        nodes_empty_  = true;
        last_latency_.declared_samples = 0;
    }

    // -----------------------------------------------------------------------
    //  process -- [RT] traitement d'un bloc audio.
    // -----------------------------------------------------------------------
    ns::Status process(const float* const* in,
                       float*              out,
                       int                 num_frames) noexcept override {
        if (!in || !out || num_frames <= 0)
            return ns::Status::InvalidArg;

        if (nodes_empty_)
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
    //  measure -- mesure de latence et de charge CPU hors RT.
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

        const int declared = chain_.declared_latency_samples();
        last_latency_.measured_samples = declared;
        last_latency_.measured_ms = (sample_rate_ > 0)
            ? (static_cast<float>(declared) / static_cast<float>(sample_rate_)) * 1000.0f
            : 0.0f;

        measure_ready_.store(true, std::memory_order_release);
    }

    // -----------------------------------------------------------------------
    //  Methodes ModuleBase requises par l'heritage (non utilisees par le backend).
    // -----------------------------------------------------------------------
    int    latency_samples() const noexcept override { return 0; }
    bool   install(ns::BackendContext*) override { return true; }
    void   uninstall(ns::BackendContext*) noexcept override {}
    void   set_param(ns::ParamId, float) noexcept override {}
    void   reconfigure(const ns::RuntimeConfig&) override {}
    void*  output_buf() noexcept override { return nullptr; }
    void   set_input(const void*) noexcept override {}
    void   process(int) noexcept override {}

private:
    int                    sample_rate_;
    int                    n_max_;
    CpuBackendContext      ctx_;
    ns::chain::AudioChain  chain_;
    ns::ModuleBase*        first_module_ = nullptr;
    ns::ModuleBase*        last_module_  = nullptr;
    bool                   nodes_empty_  = true;
};

// ============================================================================
//  Point d'entree du module.
// ============================================================================
extern "C" ODENISE_EXPORT ns::ModuleBase* odenise_module_entry(int sample_rate, int n_max) {
    return new (std::nothrow) CpuBackendImpl(sample_rate, n_max);
}
