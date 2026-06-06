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
//      - reconfigure() : suspend threads, redimensionne le scratch, reprend.
//      - install_module() : installe le module via AudioChain.
//      - process() : guard RT -- verifie nodes_empty_, retourne Ok/Unsupported.
//        Le traitement reel est effectue par Run() dans le thread dedie.
//      - Run()  : boucle RT -- pause cooperative + stop + TODO chain_.process().
//      - Run2() : thread mesure hors RT -- moyenne glissante + TODO timestamps.
//      - measure() : N blocs bruit blanc, chrono std::chrono,
//        remplit last_latency_ + last_stats_, store(release) sur measure_ready_.
// ============================================================================
#include "engine.h"
#include "audio_chain.h"

#include <algorithm>    // std::min, std::max
#include <chrono>       // std::chrono::steady_clock
#include <cstring>      // std::memcpy
#include <new>          // std::nothrow
#include <random>       // std::mt19937, std::uniform_real_distribution
#include <thread>       // std::this_thread::yield
#include <vector>       // std::vector

// ============================================================================
//  CpuBackendContext -- contexte de ressource CPU.
//  Fourni par CpuBackendImpl a chaque module lors de l'installation.
//  Possede le scratch buffer pre-alloue a n_max * sizeof(float).
// ============================================================================
class CpuBackendContext final : public odenise::BackendContext {
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
    int   backend_type()   const noexcept override { return odenise::kBackendCPU; }

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
class CpuBackendImpl final : public odenise::BackendBase, public odenise::ModuleBase {
public:
    explicit CpuBackendImpl(int sample_rate, int n_max)
        : sample_rate_(sample_rate)
        , n_max_(n_max)
        , ctx_(n_max) {}

    ~CpuBackendImpl() override {
        // Arrete les threads avant destruction.
        T_Thread();
        T_Thread2();
    }

    // -----------------------------------------------------------------------
    //  Thread RT -- Run() / Run2()
    //
    //  Run() : boucle RT principale.
    //    - Pause cooperative (pause_requested / paused_) sans mutex, RT-safe.
    //    - Arret propre via stop_requested() (MSVC) ou pthread_cancel (pthread).
    //    - TODO : lire buffer d'entree cable, appeler chain_.process(),
    //      poser t_in_/t_out_ pour Run2(). Stub tant que le modele de buffer
    //      n'est pas determine.
    //
    //  Run2() : thread de mesure hors RT.
    //    - Calcule une moyenne glissante depuis t_in_/t_out_ poses par Run().
    //    - Peut etre mis en pause via P_Thread2() pendant un reconfigure.
    //    - yield() entre deux calculs : cede le CPU, pas de busy loop.
    //    - TODO : brancher le calcul reel quand Run() posera les timestamps.
    // -----------------------------------------------------------------------
    bool Run() override {
        // --- pause cooperative (RT-safe, zero mutex) -------------------------
        if (pause_requested()) {
            paused_.store(true, std::memory_order_release);
            while (pause_requested()) { std::this_thread::yield(); }
            paused_.store(false, std::memory_order_release);
            return true;
        }
#ifdef _MSC_VER
        // --- arret propre (MSVC : pas de pthread_cancel) ---------------------
        if (stop_requested()) return false;
#endif
        // --- TODO : traitement RT --------------------------------------------
        // t_in_  = std::chrono::steady_clock::now();
        // chain_.process(n_frames_);
        // t_out_ = std::chrono::steady_clock::now();
        return true;
    }

    bool Run2() override {
        // --- pause cooperative -----------------------------------------------
        if (pause2_requested()) {
            paused2_.store(true, std::memory_order_release);
            while (pause2_requested()) { std::this_thread::yield(); }
            paused2_.store(false, std::memory_order_release);
            return true;
        }
#ifdef _MSC_VER
        if (stop2_requested()) return false;
#endif
        // --- TODO : moyenne glissante depuis t_in_ / t_out_ ------------------
        // Lit les timestamps atomiques poses par Run(), accumule sur une
        // fenetre glissante, ecrit last_stats_ + last_latency_,
        // pose measure_ready_(release).
        std::this_thread::yield();  // hors RT : cede le CPU entre deux calculs
        return true;
    }

    // -----------------------------------------------------------------------
    //  info_c -- metadonnees POD (frontiere inter-compilateurs).
    // -----------------------------------------------------------------------
    const OdeniseModuleInfoC* info_c() const noexcept override {
        static const OdeniseModuleInfoC s_info = {
            /* abi_version    */ odenise::kAbiVersion,
            /* id             */ 0,
            /* kind           */ static_cast<int>(odenise::ModuleKind::ComputeBackend),
            /* name           */ "cpu",
            /* description    */ "Backend de calcul CPU (repli, sans GPU)",
            /* needs_gpu      */ 0,
            /* backend_type_id*/ odenise::kBackendCPU
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
            /* backend_type*/ odenise::kBackendCPU
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
    //  reconfigure -- suspend threads, redimensionne les ressources, reprend.
    //  Appele par l'engine au bind et a chaque Engine::reconfigure().
    //  Hors RT uniquement. Pas de destruction/recreation du thread RT.
    // -----------------------------------------------------------------------
    void reconfigure(const odenise::EngineCaps& caps,
                     const odenise::RuntimeConfig& cfg) override {
        // Suspend les threads sans les detruire.
        P_Thread();
        P_Thread2();

        sample_rate_ = caps.sample_rate;
        n_max_       = caps.n_max;
        ctx_.resize(n_max_);
        (void)cfg;  // sera utilise quand les modules exploiteront n, hop, etc.

        // Reprend Run2 avant Run : mesure prete avant le traitement RT.
        R_Thread2();
        R_Thread();
    }

    // -----------------------------------------------------------------------
    //  install_module -- installe un module dans la chaine.
    // -----------------------------------------------------------------------
    bool install_module(odenise::ModuleBase* mod,
                        odenise::ModuleKind  kind,
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

    void uninstall_module(odenise::ModuleKind kind, int position) noexcept override {
        chain_.remove(this, position);
        first_module_ = nullptr;
        last_module_  = nullptr;
        nodes_empty_  = true;
        last_latency_.declared_samples = 0;
    }

    // -----------------------------------------------------------------------
    //  process -- [RT] guard : verifie que la chaine est prete.
    //  Le traitement reel est effectue par Run() dans le thread dedie.
    //  Les buffers in/out sont cables une fois pour toutes a l'init/reconfigure.
    // -----------------------------------------------------------------------
    odenise::Status process(const float* const* in,
                       float*              out,
                       int                 num_frames) noexcept override {
        (void)in; (void)out; (void)num_frames;
        if (nodes_empty_)
            return odenise::Status::Unsupported;
        return odenise::Status::Ok;
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
    bool   install(odenise::BackendContext*) override { return true; }
    void   uninstall(odenise::BackendContext*) noexcept override {}
    void   set_param(odenise::ParamId, float) noexcept override {}
    void   reconfigure(const odenise::RuntimeConfig&) override {}
    void*  output_buf() noexcept override { return nullptr; }
    void   set_input(const void*) noexcept override {}
    void   process(int) noexcept override {}

private:
    int                    sample_rate_;
    int                    n_max_;
    CpuBackendContext      ctx_;
    odenise::chain::AudioChain  chain_;
    odenise::ModuleBase*        first_module_ = nullptr;
    odenise::ModuleBase*        last_module_  = nullptr;
    bool                   nodes_empty_  = true;
};

// ============================================================================
//  Point d'entree du module.
// ============================================================================
extern "C" ODENISE_EXPORT odenise::ModuleBase* odenise_module_entry(int sample_rate, int n_max) {
    return new (std::nothrow) CpuBackendImpl(sample_rate, n_max);
}
