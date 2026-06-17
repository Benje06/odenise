// ============================================================================
//  backend_cpu.cpp -- Backend de calcul CPU (repli / fallback).
//
//  Phase 3 : implemente BackendBase + ModuleBase (pour le loader).
//  Structs POD (info_c, caps_c, self_test_c) pour la frontiere ABI-safe.
//
//  Architecture :
//    CpuBackendContext : BackendContext concret CPU.
//      - scratch_buf() : buffer RAM pre-alloue a window_size_max * sizeof(float).
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
#include "backend_cpu.h"

// ============================================================================
//  CpuBackendContext -- contexte de ressource CPU.
//  Fourni par CpuBackendImpl a chaque module lors de l'installation.
//  Possede le scratch buffer pre-alloue a ring_size_max * sizeof(float).
// ============================================================================
CpuBackendContext::CpuBackendContext(size_t ring_size_max)
    : scratch_(ring_size_max * sizeof(float), std::byte{0}) {}

// [CTRL] Retourne le debut du scratch buffer.
void* CpuBackendContext::scratch_buf(std::size_t /*bytes*/) noexcept {
    return scratch_.data();
}

// [RT/CTRL] Pas de stream CPU.
void* CpuBackendContext::compute_stream() noexcept { return nullptr; }

// [CTRL] Type de backend : CPU. TO BE DEFINE IN BACKENDBASE
int CpuBackendContext::backend_type() const noexcept { return odenise::kBackendCPU; }

// [CTRL] Redimensionne le scratch buffer (appele par reconfigure).
void CpuBackendContext::resize(size_t ring_size) {
    const auto new_size = ring_size * sizeof(float);
    if (scratch_.size() != new_size)
        scratch_.assign(new_size, std::byte{0});
}

// ============================================================================
//  CpuBackendImpl -- implementation complete de BackendBase pour le CPU.
//  Herite aussi de ModuleBase pour satisfaire la signature de l'entry point
//  (OdeniseModuleEntryFn retourne ModuleBase*). Le loader caste vers
//  BackendBase* via dynamic_cast apres avoir verifie le kind.
// ============================================================================
CpuBackendImpl::CpuBackendImpl(odenise::EngineCaps e_caps)
    : ring_size_max_(e_caps.ring_size_max),
    ctx_(e_caps.ring_size_max) {
        S_Thread();
        P_Thread();
        //S_Thread2();
    }

CpuBackendImpl::~CpuBackendImpl() {
    //R_Thread2();
    R_Thread();
    //T_Thread2();
    T_Thread();

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
// -----------------------------------------------------------------------
bool CpuBackendImpl::Run() {

#ifdef _MSC_VER
    if (stop_requested()) return false;
#endif
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { 
        // temporary for emptyy run
        std::this_thread::yield();
        return !pause_requested();
    });
    // TODO : traitement RT
    // t_in_  = std::chrono::steady_clock::now();
    // chain_.process(n_frames_);
    // t_out_ = std::chrono::steady_clock::now();


    return true;
}

// -----------------------------------------------------------------------
//  Run2() : thread de mesure hors RT.
//    - Calcule une moyenne glissante depuis t_in_/t_out_ poses par Run().
//    - Peut etre mis en pause via P_Thread2() pendant un reconfigure.
//    - yield() entre deux calculs : cede le CPU, pas de busy loop.
//    - TODO : brancher le calcul reel quand Run() posera les timestamps.
// -----------------------------------------------------------------------
bool CpuBackendImpl::Run2() {

#ifdef _MSC_VER
    if (stop2_requested()) return false;
#endif
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] {
        std::this_thread::yield();
        return !pause_requested();
    });
    // TODO : moyenne glissante depuis t_in_ / t_out_
        // Lit les timestamps atomiques poses par Run(), accumule sur une
        // fenetre glissante, ecrit last_stats_ + last_latency_,
        // pose measure_ready_(release).


    return true;
}

// -----------------------------------------------------------------------
//  info_c -- metadonnees POD (frontiere inter-compilateurs).
// -----------------------------------------------------------------------
const OdeniseModuleInfoC* CpuBackendImpl::info_c() const noexcept {
    static const OdeniseModuleInfoC s_info = {
        /* abi_version    */ odenise::kAbiVersion,
        /* id             */ 0,
        /* kind           */ static_cast<int>(odenise::ModuleKind::ComputeBackend),
        /* name           */ "backend_cpu",
        /* description    */ "Backend de calcul CPU (repli, sans GPU)",
        /* needs_gpu      */ 0,
        /* backend_type_id*/ odenise::kBackendCPU
    };
    return &s_info;
}

// -----------------------------------------------------------------------
//  caps_c -- capabilities POD (frontiere inter-compilateurs).
// -----------------------------------------------------------------------
const OdeniseBackendCapsC* CpuBackendImpl::caps_c() const noexcept {
    static const OdeniseBackendCapsC s_caps = {
        /* backend_id     */ 0,
        /* backend_type   */ odenise::kBackendCPU,
        /* prealloc_c2c   */ false,
        /* share_fft_work */ true,
        /* is_gpu         */ false,
        /* cc_major       */ 0,
        /* cc_minor       */ 0,
        /* vram_bytes     */ 0,
        /* has_fp16       */ false,
        /* has_tensor     */ false,
        /* gpu_family     */ "",
        /* backend_name   */ "backend_cpu"
    };
    return &s_caps;
}

// -----------------------------------------------------------------------
//  self_test_c -- self-test POD (frontiere inter-compilateurs).
// -----------------------------------------------------------------------
const OdeniseTestResultC* CpuBackendImpl::self_test_c() const noexcept {
    static OdeniseTestResultC s_result = { 0, nullptr };

    auto* impl = new (std::nothrow) CpuBackendImpl(e_caps_);
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
odenise::Status CpuBackendImpl::reconfigure(const odenise::EngineCaps&    e_caps,
                                            const odenise::RuntimeConfig& cfg) {
    LOG(LOG_IN());
    P_Thread();
    P_Thread2();

    sample_rate_   = e_caps.sample_rate;
    ring_size_max_ = e_caps.window_size_max;
    ctx_.resize(ring_size_max_);

    R_Thread2();
    R_Thread();
    LOG(LOG_OUT());
    return odenise::Status::Ok;
}

// module inherit
odenise::Status CpuBackendImpl::reconfigure(const odenise::BackendCaps& b_caps,
                                            const odenise::RuntimeConfig& cfg,
                                            odenise::ApplyResult& how) {

    return odenise::Status::Ok;
}

// -----------------------------------------------------------------------
//  install_module / uninstall_module
// -----------------------------------------------------------------------
std::vector<odenise::ModuleInfo> CpuBackendImpl::get_chain() const noexcept {
    return chain_.get_chain();
}

bool CpuBackendImpl::install_module(odenise::ModuleBase*  mod,
                                    odenise::ModuleKind  kind,
                                    size_t               position,
                                    size_t               loaded_id) {
    if (!mod) return false;
    const bool ok = chain_.insert(this, &ctx_, mod, kind, position, loaded_id);
    if (ok) {
        first_module_ = chain_.get_first();
        last_module_  = chain_.get_last();
        nodes_empty_  = false;
        last_latency_.declared_samples = chain_.declared_latency_samples();
    }
    return ok;
}

void CpuBackendImpl::uninstall_module(size_t position) noexcept {
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
odenise::Status CpuBackendImpl::process(const float* const* in,
                                         float*             out,
                                         size_t             num_frames) noexcept {
    if (nodes_empty_)
        return odenise::Status::Unsupported;
    return odenise::Status::Ok;
}

// -----------------------------------------------------------------------
//  setAudioIO
// -----------------------------------------------------------------------
void CpuBackendImpl::setAudioIO(odenise::TrackIO io) {
    LOG(LOG_IN());
    P_Thread();
    io_ = io;
    R_Thread();
    LOG(LOG_OUT());
}

// -----------------------------------------------------------------------
//  measure -- mesure de latence et de charge CPU hors RT.
// -----------------------------------------------------------------------
void CpuBackendImpl::measure(int num_blocks) {
    if (num_blocks <= 0) num_blocks = 16;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    std::vector<float> in_buf(ring_size_max_);
    std::vector<float> out_buf(ring_size_max_);
    for (auto& s : in_buf) s = dist(rng);

    const float* in_ptr  = in_buf.data();
    float*       out_ptr = out_buf.data();

    float min_ms = 1e9f;
    float max_ms = -1e9f;
    float sum_ms = 0.0f;

    for (int i = 0; i < num_blocks; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        process(&in_ptr, out_ptr, ring_size_max_);
        const auto t1 = std::chrono::steady_clock::now();

        const float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
        min_ms  = std::min(min_ms, ms);
        max_ms  = std::max(max_ms, ms);
        sum_ms += ms;
    }

    const float mean_ms   = sum_ms / static_cast<float>(num_blocks);
    const float budget_ms = (sample_rate_ > 0)
        ? (ring_size_max_ / static_cast<float>(sample_rate_)) * 1000.0f
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
//  Methodes ModuleBase requises par l'heritage (non utilisees).
// -----------------------------------------------------------------------
int             CpuBackendImpl::latency_samples()    const noexcept { return 0; }
int             CpuBackendImpl::latency_samples_rt() const noexcept { return 0; }
bool            CpuBackendImpl::install(odenise::BackendContext*)    { return true; }
void            CpuBackendImpl::uninstall(odenise::BackendContext*) noexcept {}
void            CpuBackendImpl::set_param(odenise::ParamId, float)  noexcept {}
void*           CpuBackendImpl::output_buf()   noexcept { return nullptr; }
void            CpuBackendImpl::set_input(const void*) noexcept {}
void            CpuBackendImpl::process(size_t)   noexcept {}

// ============================================================================
//  Point d'entree du module.
// ============================================================================
extern "C" ODENISE_EXPORT odenise::ModuleBase* odenise_module_entry(odenise::EngineCaps e_caps) {
    return new (std::nothrow) CpuBackendImpl(e_caps);
}
