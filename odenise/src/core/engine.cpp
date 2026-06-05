// engine.cpp -- orchestration du moteur.
//
// Phase 3 : suppression du double chemin legacy/C++.
// L'engine utilise uniquement ModuleBase* et BackendBase* obtenus via le
// registre. La frontiere C (vtable legacy) a disparu : les modules exposent
// directement leurs interfaces C++, avec structs POD pour la frontiere
// inter-compilateurs (info_c(), caps_c(), self_test_c()).
// La AudioChain est interne au backend : l'engine ne la manipule pas
// directement, il passe par install_module() / uninstall_module().
#include "ns_engine.h"
#include "registry/module_registry.h"
#include "tools/logger.h"

#include <cstdlib>      // std::getenv / std::free / _dupenv_s
#include <filesystem>   // std::filesystem::path (dependance directe : IWYU)

namespace ns {

namespace {
// Dossier de modules : env var > convention dx7interface.
// Sous Windows, on localise l'exe via GetModuleFileName ; sous Linux via
// /proc/self/exe. Le chemin relatif au dossier courant ne marche pas
// quand l'IDE lance l'exe depuis la racine du projet.
std::filesystem::path exeDir() {
#if defined(_WIN32)
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
    return std::filesystem::current_path();   // repli
#endif
}

std::filesystem::path moduleDir() {
    // 1. Variable d'environnement (prioritaire, pour les tests manuels)
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

    // 2. Convention dx7interface (build et deploy) :
    //    exe dans <prefix>/bin/ -> modules dans <prefix>/share/odenise/<version>/modules/
    return exeDir() / ".." / "share" / ODENISE_VERSION_DIR / "modules";
}

// Construit BackendCaps (type coeur, avec std::string) depuis la struct POD
// retournee par caps_c(). Jamais appele depuis un module MSVC.
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
} // namespace

class EngineImpl final : public Engine {
public:
    EngineImpl(const EngineCaps& caps, const RuntimeConfig& cfg)
        : caps_(caps), cfg_(cfg) {

        const auto dir = moduleDir();
        const int n = registry_.scanDirectory(dir);
        std::string msg = _("engine: created (n=");
        msg += std::to_string(cfg_.n);
        msg += _(", modules loaded: ");
        msg += std::to_string(n);
        msg += ")";
        LOG(msg);

        // Liaison des modules par couche, du socle vers le haut.
        bindBackend(caps_.backend_id);
        bindSuppression(cfg_.suppression_id);
    }

    ~EngineImpl() override {
        // Liberation en ordre inverse du bind.
        releaseSuppression();
        releaseBackend();
    }

    // [RT] Latence declaree courante.
    int latencySamples() const noexcept override {
        if (backend_) {
            const int declared = backend_->last_latency_info().declared_samples;
            return declared > 0 ? declared : cfg_.n;
        }
        return cfg_.n;
    }

    Status reconfigure(const RuntimeConfig& cfg, ApplyResult& how) override {
        const bool sup_changed = (cfg.suppression_id != cfg_.suppression_id);
        cfg_ = cfg;
        how  = ApplyResult::Hot;

        if (sup_changed)
            bindSuppression(cfg_.suppression_id);

        return Status::Ok;
    }

    BackendCaps backendCaps() const override {
        if (backend_)
            return toBackendCaps(*backend_->caps_c());
        return {};
    }

    Status process(std::span<const TrackIO> tracks,
                   int num_frames) noexcept override {

        for (const auto& t : tracks) {
            if (!t.in || !t.out || t.in_channels < 1)
                return Status::InvalidArg;

            if (!backend_)
                return Status::Unsupported;

            const Status rc = backend_->process(t.in, t.out, num_frames);
            if (rc != Status::Ok) return rc;
        }
        return Status::Ok;
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
        return registry_.list(kind);
    }
    TestResult selfTest(ModuleKind kind, int id) const override {
        return registry_.selfTest(kind, id);
    }

    // --- mesures de performance -----------------------------------------

    LatencyInfo latencyInfo() const override {
        LatencyInfo li;
        if (backend_) {
            // declared_samples : mis a jour par le backend a chaque install_module().
            // Lisible sans attendre measure().
            li.declared_samples = backend_->last_latency_info().declared_samples;
            li.declared_ms = (caps_.sample_rate > 0)
                ? (static_cast<float>(li.declared_samples)
                   / static_cast<float>(caps_.sample_rate)) * 1000.0f
                : 0.0f;
            // measured_samples : disponible uniquement apres measure().
            if (backend_->measure_ready()) {
                li.measured_samples = backend_->last_latency_info().measured_samples;
                li.measured_ms      = backend_->last_latency_info().measured_ms;
            }
        }
        li.in_sync = (li.declared_samples == li.measured_samples);
        return li;
    }

    ProcessingStats processingStats() const override {
        if (backend_ && backend_->measure_ready())
            return backend_->last_processing_stats();
        return {};
    }

    Metrics  metrics()  const override { return {}; }
    Spectrum spectrum() const override { return {}; }

private:
    // -----------------------------------------------------------------------
    //  Gestion du backend
    // -----------------------------------------------------------------------
    void releaseBackend() {
        // L'engine possede les objets crees via make()/make_backend().
        delete backend_;
        backend_ = nullptr;
    }

    void bindBackend(int id) {
        releaseBackend();

        // AUTO (-1) : premier ComputeBackend charge.
        if (id < 0) {
            const auto backends = registry_.list(ModuleKind::ComputeBackend);
            if (backends.empty()) {
                LOG(_("engine: no compute backend available"));
                return;
            }
            id = backends.front().id;
        }

        backend_ = registry_.make_backend(id, caps_.sample_rate, caps_.n_max);
        if (!backend_) {
            std::string msg = _("engine: no compute backend with id ");
            msg += std::to_string(id);
            LOG(msg);
            return;
        }
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
            delete suppression_;
            suppression_ = nullptr;
        }
    }

    void bindSuppression(int id) {
        releaseSuppression();

        if (id == 0) {
            LOG(_("engine: no suppression module requested (id=0)"));
            return;
        }

        suppression_ = registry_.make(ModuleKind::Suppression, id,
                                      caps_.sample_rate, caps_.n_max);
        if (!suppression_) {
            std::string msg = _("engine: no suppression module with id ");
            msg += std::to_string(id);
            LOG(msg);
            return;
        }

        if (!backend_ || !backend_->install_module(suppression_, ModuleKind::Suppression, 0)) {
            std::string msg_err = error("engine",
                _("suppression module chain install failed"),
                _("id=") + std::to_string(id));
            LOG_ERR(msg_err);
            delete suppression_;
            suppression_ = nullptr;
            return;
        }
        std::string msg = _("engine: bound suppression module id=");
        msg += std::to_string(id);
        LOG(msg);
    }

    // -----------------------------------------------------------------------
    //  Membres
    // -----------------------------------------------------------------------
    EngineCaps              caps_;
    RuntimeConfig           cfg_;
    detail::ModuleRegistry  registry_;

    BackendBase*  backend_     = nullptr;  // pointe vers l'objet du registre
    ModuleBase*   suppression_ = nullptr;  // pointe vers l'objet du registre
};

std::unique_ptr<Engine> createEngine(const EngineCaps& caps,
                                     const RuntimeConfig& cfg,
                                     Status* status) {
    if (status) *status = Status::Ok;
    return std::make_unique<EngineImpl>(caps, cfg);
}

std::vector<ModuleInfo> availableBackends() {
    detail::ModuleRegistry reg;
    reg.scanDirectory(moduleDir());
    return reg.list(ModuleKind::ComputeBackend);
}

} // namespace ns
