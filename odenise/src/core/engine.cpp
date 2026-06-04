// engine.cpp -- orchestration du moteur (STUB minimal, etape 1).
#include "ns_engine.h"
#include "module_registry.h"
#include "tools/logger.h"

#include <cstdlib>      // std::getenv / std::free / _dupenv_s
#include <filesystem>   // std::filesystem::path (dependance directe : IWYU)

namespace ns {

namespace {
// Dossier de modules : env var > "modules" a cote de l'executable.
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

    // 2. Convention dx7interface (build et deploy, identique Linux et Windows) :
    //    exe dans <prefix>/usr/bin/ (ou bin/)
    //    modules dans <prefix>/usr/share/odenise/<version>/modules/
    //    On remonte de bin/ vers le prefix, puis on descend dans share/.
    return exeDir() / ".." / "share" / ODENISE_VERSION_DIR / "modules";
}
} // namespace

class EngineImpl final : public Engine {
public:
    EngineImpl(const EngineCaps& caps, const RuntimeConfig& cfg)
        : caps_(caps), cfg_(cfg) {
        const auto dir = moduleDir();
        const int n = registry_.scanDirectory(dir);
        LOG(_("engine: created (n=") + std::to_string(cfg_.n)
            + _(", modules loaded: ") + std::to_string(n) + ")");

        // Liaison des modules par couche, du socle vers le haut :
        //   1. ComputeBackend (fournit le calcul, aucune dependance)
        //   2. Suppression    (consomme le calcul du backend)
        // L'ordre garantit que le socle est pret avant les couches au-dessus.
        bindBackend(caps_.backend_id);
        bindSuppression(cfg_.suppression_id);
    }

    ~EngineImpl() override {
        // Liberation en ordre inverse du bind : on detruit les couches hautes
        // avant le socle, pour qu'aucune couche n'utilise un backend deja detruit.
        releaseSuppression();
        releaseBackend();
    }

    int latencySamples() const noexcept override { return cfg_.n; }

    Status reconfigure(const RuntimeConfig& cfg, ApplyResult& how) override {
        const bool sup_changed = (cfg.suppression_id != cfg_.suppression_id);
        cfg_ = cfg;
        how  = ApplyResult::Hot;

        if (sup_changed)
            bindSuppression(cfg_.suppression_id);

        return Status::Ok;
    }

    BackendCaps backendCaps() const override { return {}; }

    Status process(std::span<const TrackIO> tracks,
                   int num_frames) noexcept override {
        if (!suppression_vt_ || !suppression_inst_)
            return Status::Unsupported;

        for (const auto& t : tracks) {
            if (!t.in || !t.out || t.in_channels < 1)
                return Status::InvalidArg;

            OdeniseProcessCtx ctx;
            ctx.in           = t.in;
            ctx.out          = t.out;
            ctx.in_channels  = t.in_channels;
            ctx.num_frames   = num_frames;

            int rc = suppression_vt_->process(suppression_inst_, &ctx);
            if (rc != static_cast<int>(Status::Ok))
                return static_cast<Status>(rc);
        }
        return Status::Ok;
    }

    Status setParam(ParamId, float) noexcept override { return Status::Ok; }
    float  getParam(ParamId) const noexcept override { return 0.0f; }
    Status setGminCurve(std::span<const float>) noexcept override { return Status::Ok; }
    Status setBandLayout(std::span<const float>) override { return Status::Ok; }

    Status captureBegin(ProfileLevel, float) override { return Status::Unsupported; }
    bool   captureActive() const noexcept override { return false; }
    std::vector<std::byte> saveProfile(ProfileLevel) const override { return {}; }
    Status loadProfile(ProfileLevel, std::span<const std::byte>) override { return Status::Unsupported; }

    std::vector<ModuleInfo> modules(ModuleKind kind) const override {
        return registry_.list(kind);
    }
    TestResult selfTest(ModuleKind kind, int id) const override {
        return registry_.selfTest(kind, id);
    }

    Metrics  metrics()  const override { return {}; }
    Spectrum spectrum() const override { return {}; }

private:
    void releaseSuppression() {
        if (suppression_vt_ && suppression_inst_) {
            suppression_vt_->destroy(suppression_inst_);
            suppression_inst_ = nullptr;
        }
        suppression_vt_ = nullptr;
    }

    void bindSuppression(int id) {
        releaseSuppression();
        suppression_vt_ = registry_.find(ModuleKind::Suppression, id);
        if (!suppression_vt_) {
            LOG(_("engine: no suppression module with id ") + std::to_string(id));
            return;
        }
        suppression_inst_ = suppression_vt_->create(caps_.sample_rate, caps_.n_max);
        if (!suppression_inst_) {
            LOG_ERR(error("engine", _("suppression module create failed"),
                          _("id=") + std::to_string(id)));
            suppression_vt_ = nullptr;
            return;
        }
        LOG(_("engine: bound suppression module id=") + std::to_string(id));
    }

    void releaseBackend() {
        if (backend_vt_ && backend_inst_) {
            backend_vt_->destroy(backend_inst_);
            backend_inst_ = nullptr;
        }
        backend_vt_ = nullptr;
    }

    void bindBackend(int id) {
        releaseBackend();

        // AUTO (-1) : premier ComputeBackend charge. Sinon : id precis.
        if (id < 0) {
            const auto backends = registry_.list(ModuleKind::ComputeBackend);
            if (backends.empty()) {
                LOG(_("engine: no compute backend available"));
                return;
            }
            id = backends.front().id;
        }

        backend_vt_ = registry_.find(ModuleKind::ComputeBackend, id);
        if (!backend_vt_) {
            LOG(_("engine: no compute backend with id ") + std::to_string(id));
            return;
        }
        backend_inst_ = backend_vt_->create(caps_.sample_rate, caps_.n_max);
        if (!backend_inst_) {
            LOG_ERR(error("engine", _("compute backend create failed"),
                          _("id=") + std::to_string(id)));
            backend_vt_ = nullptr;
            return;
        }
        LOG(_("engine: bound compute backend id=") + std::to_string(id));
    }

    EngineCaps                 caps_;
    RuntimeConfig              cfg_;
    detail::ModuleRegistry     registry_;
    const OdeniseModuleVTable* suppression_vt_   = nullptr;
    OdeniseModuleInstance      suppression_inst_  = nullptr;
    const OdeniseModuleVTable* backend_vt_       = nullptr;
    OdeniseModuleInstance      backend_inst_      = nullptr;
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
