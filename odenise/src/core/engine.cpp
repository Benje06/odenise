// engine.cpp -- orchestration du moteur.
//
// Phase 3 : suppression du double chemin legacy/C++.
// L'engine utilise ModuleBase* et BackendBase* obtenus via le registry.
// Seuls les modules selectionnes (backend actif + modules de la chaine audio)
// sont charges. Les autres sont connus (available) mais pas en memoire.
// La AudioChain est interne au backend : l'engine passe par
// install_module() / uninstall_module().
#include "engine.h"
#include "module_registry.h"

namespace odenise {

std::filesystem::path exeDir() {
#if defined(_WIN32)
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
    return std::filesystem::current_path();
#endif
}

std::filesystem::path moduleDir() {
#if defined(_WIN32)
    char*  buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "ODENISE_MODULE_PATH") == 0 && buf) {
        std::filesystem::path p = buf;
        std::free(buf);
        return p;
    }
#else
    if (const char* e = std::getenv("ODENISE_MODULE_PATH"))
        return e;
#endif
    return exeDir() / ".." / "share" / ODENISE_VERSION_DIR / "modules";
}

BackendCaps toBackendCaps(const OdeniseBackendCapsC& c) {
    BackendCaps bc;
    bc.name         = c.name ? c.name : "";
    bc.is_gpu       = (c.is_gpu != 0);
    bc.vram_bytes   = static_cast<std::size_t>(c.vram_bytes);
    bc.cc_major     = c.cc_major;
    bc.cc_minor     = c.cc_minor;
    bc.has_fp16     = (c.has_fp16 != 0);
    bc.has_tensor   = (c.has_tensor != 0);
    bc.backend_type = c.backend_type;
    return bc;
}

class EngineImpl final : public Engine {
public:
    EngineImpl(const EngineCaps& caps, const RuntimeConfig& cfg)
        : caps_(caps), cfg_(cfg) {

        // Decouverte des modules disponibles (sans chargement).
        const auto dir = moduleDir();
        const int n = registry_.scan_modules(dir);
        std::string msg = _("engine: created (n=");
        msg += std::to_string(cfg_.n);
        msg += _(", modules available: ");
        msg += std::to_string(n);
        msg += ")";
        LOG(msg);

        // Chargement et liaison par couche : backend d'abord, modules ensuite.
        bindBackend(caps_.backend_id);
        bindSuppression(cfg_.suppression_id);
    }

    ~EngineImpl() override {
        // Liberation en ordre inverse. Les objets sont possedes par le registry.
        releaseSuppression();
        releaseBackend();
    }

    int latencySamples() const noexcept override {
        return cached_latency_.declared_samples > 0
            ? cached_latency_.declared_samples : cfg_.n;
    }

    Status reconfigure(const RuntimeConfig& cfg, ApplyResult& how) override {
        const bool sup_changed = (cfg.suppression_id != cfg_.suppression_id);
        cfg_ = cfg;
        how  = ApplyResult::Hot;

        if (backend_)
            backend_->reconfigure(caps_, cfg_);

        if (sup_changed)
            bindSuppression(cfg_.suppression_id);

        return Status::Ok;
    }

    BackendCaps backendCaps() const override {
        if (backend_)
            return toBackendCaps(*backend_->caps_c());
        return {};
    }

    Status setParam(ParamId id, float value) noexcept override {
        (void)id; (void)value;
        return Status::Ok;
    }
    float  getParam(ParamId) const noexcept override { return 0.0f; }
    Status setGminCurve(std::span<const float>) noexcept override { return Status::Ok; }
    Status setBandLayout(std::span<const float>) override { return Status::Ok; }

    Status captureBegin(ProfileLevel, float) override { return Status::Unsupported; }
    bool   captureActive() const noexcept override { return false; }
    std::vector<std::byte> saveProfile(ProfileLevel) const override { return {}; }
    Status loadProfile(ProfileLevel, std::span<const std::byte>) override {
        return Status::Unsupported;
    }

    std::vector<ModuleInfo> modules(ModuleKind kind) const override {
        return registry_.list_available(kind);
    }

    TestResult selfTest(ModuleKind kind, int id) const override {
        return const_cast<EngineImpl*>(this)->registry_.self_test(kind, id);
    }

    // Retourne une ref sur le membre mis a jour par callback depuis le backend.
    const LatencyInfo&     latencyInfo()     const noexcept override { return cached_latency_; }
    const ProcessingStats& processingStats() const noexcept override { return cached_stats_; }

    Metrics  metrics()  const override { return {}; }
    Spectrum spectrum() const override { return {}; }

private:
    // -----------------------------------------------------------------------
    //  Callbacks statiques -- enregistres dans le backend a bindBackend().
    //  Hors RT uniquement. Ecrivent dans les membres cached_* de l'engine.
    // -----------------------------------------------------------------------

    // Declenche par le backend au cablage : met a jour declared_* de cached_latency_.
    static void on_latency_changed(void* user, int declared_samples) noexcept {
        auto* self = static_cast<EngineImpl*>(user);
        self->cached_latency_.declared_samples = declared_samples;
        self->cached_latency_.declared_ms =
            (self->caps_.sample_rate > 0)
            ? (static_cast<float>(declared_samples)
               / static_cast<float>(self->caps_.sample_rate)) * 1000.0f
            : 0.0f;
    }

    // Declenche par le backend apres mesure : met a jour cached_stats_
    // et measured_* de cached_latency_.
    static void on_stats_updated(void* user,
                                 const ProcessingStats& stats,
                                 int measured_samples) noexcept {
        auto* self = static_cast<EngineImpl*>(user);
        self->cached_stats_ = stats;
        self->cached_latency_.measured_samples = measured_samples;
        self->cached_latency_.measured_ms =
            (self->caps_.sample_rate > 0)
            ? (static_cast<float>(measured_samples)
               / static_cast<float>(self->caps_.sample_rate)) * 1000.0f
            : 0.0f;
        self->cached_latency_.in_sync =
            (self->cached_latency_.declared_samples == measured_samples);
    }

    // -----------------------------------------------------------------------
    //  Gestion du backend
    // -----------------------------------------------------------------------
    void releaseBackend() {
        if (backend_) {
            // Debranche les callbacks avant dechargement.
            backend_->on_latency_changed = nullptr;
            backend_->on_stats_updated   = nullptr;
            backend_->callback_user      = nullptr;
            registry_.unload_module(ModuleKind::ComputeBackend, backend_id_);
            backend_    = nullptr;
            backend_id_ = -1;
        }
    }

    void bindBackend(int id) {
        releaseBackend();

        // AUTO (-1) : premier ComputeBackend disponible.
        if (id < 0) {
            id = registry_.first_available_id(ModuleKind::ComputeBackend);
            if (id < 0) {
                LOG(_("engine: no compute backend available"));
                return;
            }
        }

        if (!registry_.load_module(ModuleKind::ComputeBackend, id)) {
            std::string msg = _("engine: cannot load backend id=");
            msg += std::to_string(id);
            LOG(msg);
            return;
        }

        backend_ = registry_.find_backend(id);
        if (!backend_) {
            std::string msg = _("engine: find_backend returned null for id=");
            msg += std::to_string(id);
            LOG(msg);
            return;
        }
        backend_id_ = id;

        // Enregistre les callbacks avant reconfigure().
        backend_->on_latency_changed = &EngineImpl::on_latency_changed;
        backend_->on_stats_updated   = &EngineImpl::on_stats_updated;
        backend_->callback_user      = this;

        // Reconfigure avec les caps et cfg reelles.
        // Le backend demarre son thread de traitement ici.
        backend_->reconfigure(caps_, cfg_);

        std::string msg = _("engine: bound backend id=");
        msg += std::to_string(id);
        msg += _(" name='");
        msg += (backend_->caps_c()->name ? backend_->caps_c()->name : "");
        msg += "'";
        LOG(msg);
    }

    // -----------------------------------------------------------------------
    //  Gestion du module de suppression
    // -----------------------------------------------------------------------
    void releaseSuppression() {
        if (suppression_) {
            if (backend_)
                backend_->uninstall_module(ModuleKind::Suppression, 0);
            registry_.unload_module(ModuleKind::Suppression, suppression_id_);
            suppression_    = nullptr;
            suppression_id_ = 0;
        }
    }

    void bindSuppression(int id) {
        releaseSuppression();

        if (id == 0) {
            LOG(_("engine: no suppression module requested (id=0)"));
            return;
        }

        if (!registry_.load_module(ModuleKind::Suppression, id)) {
            std::string msg = _("engine: cannot load suppression module id=");
            msg += std::to_string(id);
            LOG(msg);
            return;
        }

        suppression_ = registry_.find_module(ModuleKind::Suppression, id);
        if (!suppression_) {
            std::string msg = _("engine: find_module returned null for suppression id=");
            msg += std::to_string(id);
            LOG(msg);
            registry_.unload_module(ModuleKind::Suppression, id);
            return;
        }
        suppression_id_ = id;

        if (!backend_ || !backend_->install_module(suppression_, ModuleKind::Suppression, 0)) {
            std::string msg_err = error(__func__,
                _("suppression module chain install failed"),
                _("id=") + std::to_string(id));
            LOG_ERR(msg_err);
            registry_.unload_module(ModuleKind::Suppression, id);
            suppression_    = nullptr;
            suppression_id_ = 0;
            return;
        }

        std::string msg = _("engine: bound suppression module id=");
        msg += std::to_string(id);
        LOG(msg);
    }

    // -----------------------------------------------------------------------
    //  Membres
    // -----------------------------------------------------------------------
    EngineCaps             caps_;
    RuntimeConfig          cfg_;
    detail::ModuleRegistry registry_;

    BackendBase* backend_        = nullptr;  // pointeur non-owning (registry)
    int          backend_id_     = -1;       // id du backend charge
    ModuleBase*  suppression_    = nullptr;  // pointeur non-owning (registry)
    int          suppression_id_ = 0;        // id du module de suppression charge

    // Cache des mesures -- mis a jour par callbacks depuis le backend, hors RT.
    // Lu par l'UI via latencyInfo() / processingStats() (retours const ref).
    LatencyInfo     cached_latency_;
    ProcessingStats cached_stats_;
};

std::unique_ptr<Engine> createEngine(const EngineCaps& caps,
                                     const RuntimeConfig& cfg,
                                     Status* status) {
    if (status) *status = Status::Ok;
    return std::make_unique<EngineImpl>(caps, cfg);
}

std::vector<ModuleInfo> availableBackends() {
    detail::ModuleRegistry reg;
    reg.scan_modules(moduleDir());
    return reg.list_available(ModuleKind::ComputeBackend);
}

} // namespace odenise
