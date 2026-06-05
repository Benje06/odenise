// engine.cpp -- orchestration du moteur.
//
// L'engine ordonnance le chargement/dechargement des modules sur ordre de
// la config ou de l'interface. Il ne possede pas la chaine audio : c'est le
// backend qui detient AudioChain en interne et recoit les ordres
// install_module / uninstall_module via BackendBase.
#include "ns_engine.h"
#include "registry/module_registry.h"
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

        // Liaison des modules par couche, du socle vers le haut.
        bindBackend(caps_.backend_id);
        bindSuppression(cfg_.suppression_id);
    }

    ~EngineImpl() override {
        // Liberation en ordre inverse du bind.
        releaseSuppression();
        releaseBackend();
    }

    // [RT] Latence declaree courante : lue depuis le backend.
    // Champ ecrit hors RT par le backend a chaque (un)install_module.
    int latencySamples() const noexcept override {
        if (!backend_base_) return 0;
        return backend_base_->last_latency_info().declared_samples;
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
        if (backend_base_)
            return backend_base_->caps();
        return {};
    }

    // [RT] Traitement : delegue au backend qui itere sur sa chaine interne.
    // Unsupported si pas de backend ou pas de module de suppression installe.
    Status process(std::span<const TrackIO> tracks,
                   int num_frames) noexcept override {

        if (!backend_base_ || !suppression_module_)
            return Status::Unsupported;

        for (const auto& t : tracks) {
            if (!t.in || !t.out || t.in_channels < 1)
                return Status::InvalidArg;

            const Status rc = backend_base_->process(t.in, t.out, num_frames);
            if (rc != Status::Ok) return rc;
        }
        return Status::Ok;
    }

    Status setParam(ParamId id, float value) noexcept override {
        // Transmet aux modules de la chaine via le backend (futur).
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
    // L'engine ne stocke aucun chiffre : il transmet la lecture atomique
    // du backend. La latence declaree est ecrite par le backend a chaque
    // (un)install_module ; la latence mesuree et les stats sont publiees
    // par measure() avec le drapeau measure_ready_ (release/acquire).

    LatencyInfo latencyInfo() const override {
        if (!backend_base_) return {};
        LatencyInfo li = backend_base_->last_latency_info();
        li.declared_ms = (caps_.sample_rate > 0)
            ? (static_cast<float>(li.declared_samples)
               / static_cast<float>(caps_.sample_rate)) * 1000.0f
            : 0.0f;
        li.in_sync = (li.declared_samples == li.measured_samples);
        return li;
    }

    ProcessingStats processingStats() const override {
        if (backend_base_ && backend_base_->measure_ready())
            return backend_base_->last_processing_stats();
        return {};
    }

    Metrics  metrics()  const override { return {}; }
    Spectrum spectrum() const override { return {}; }

private:
    // -----------------------------------------------------------------------
    //  Gestion du backend (chemin C++ uniquement)
    // -----------------------------------------------------------------------
    void releaseBackend() {
        // backend_base_ est cree par create_backend() du module. L'engine
        // en prend la propriete et le detruit via le destructeur virtuel.
        delete backend_base_;
        backend_base_ = nullptr;
        backend_vt_   = nullptr;
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

        backend_vt_ = registry_.find(ModuleKind::ComputeBackend, id);
        if (!backend_vt_) {
            LOG(_("engine: no compute backend with id ") + std::to_string(id));
            return;
        }

        if (!backend_vt_->create_backend) {
            std::string msg_err = error("engine",
                _("compute backend has no create_backend"),
                _("id=") + std::to_string(id));
            LOG_ERR(msg_err);
            backend_vt_ = nullptr;
            return;
        }

        backend_base_ = backend_vt_->create_backend(caps_.sample_rate, caps_.n_max);
        if (!backend_base_) {
            std::string msg_err = error("engine",
                _("create_backend returned null"),
                _("id=") + std::to_string(id));
            LOG_ERR(msg_err);
            backend_vt_ = nullptr;
            return;
        }
        LOG(_("engine: bound backend id=") + std::to_string(id));
    }

    // -----------------------------------------------------------------------
    //  Gestion du module de suppression (chemin C++ uniquement)
    //  L'engine ordonnance : il demande au backend d'installer/retirer le
    //  module. Le backend gere sa chaine en interne.
    // -----------------------------------------------------------------------
    void releaseSuppression() {
        if (suppression_module_) {
            if (backend_base_)
                backend_base_->uninstall_module(ModuleKind::Suppression, 0);
            delete suppression_module_;
            suppression_module_ = nullptr;
        }
        suppression_vt_ = nullptr;
    }

    void bindSuppression(int id) {
        releaseSuppression();

        // id == 0 : aucun module de suppression demande.
        if (id == 0) {
            LOG(_("engine: no suppression module requested (id=0)"));
            return;
        }

        // Pre-requis : un backend doit etre lie pour accueillir le module.
        if (!backend_base_) {
            std::string msg_err = error("engine",
                _("cannot bind suppression module without backend"),
                _("id=") + std::to_string(id));
            LOG_ERR(msg_err);
            return;
        }

        suppression_vt_ = registry_.find(ModuleKind::Suppression, id);
        if (!suppression_vt_) {
            LOG(_("engine: no suppression module with id ") + std::to_string(id));
            return;
        }

        if (!suppression_vt_->create_module) {
            std::string msg_err = error("engine",
                _("suppression module has no create_module"),
                _("id=") + std::to_string(id));
            LOG_ERR(msg_err);
            suppression_vt_ = nullptr;
            return;
        }

        suppression_module_ =
            suppression_vt_->create_module(caps_.sample_rate, caps_.n_max);
        if (!suppression_module_) {
            std::string msg_err = error("engine",
                _("create_module returned null"),
                _("id=") + std::to_string(id));
            LOG_ERR(msg_err);
            suppression_vt_ = nullptr;
            return;
        }

        // Ordre au backend : installer le module a la position 0.
        if (!backend_base_->install_module(suppression_module_,
                                           ModuleKind::Suppression, 0)) {
            std::string msg_err = error("engine",
                _("backend install_module failed"),
                _("id=") + std::to_string(id));
            LOG_ERR(msg_err);
            delete suppression_module_;
            suppression_module_ = nullptr;
            suppression_vt_     = nullptr;
            return;
        }
        LOG(_("engine: bound suppression module id=") + std::to_string(id));
    }

    // -----------------------------------------------------------------------
    //  Membres
    // -----------------------------------------------------------------------
    EngineCaps              caps_;
    RuntimeConfig           cfg_;
    detail::ModuleRegistry  registry_;

    // Backend et module de suppression actifs. L'engine en a la propriete :
    // crees via create_backend / create_module, detruits via delete.
    // La chaine audio est detenue par le backend, pas par l'engine.
    BackendBase*               backend_base_       = nullptr;
    ModuleBase*                suppression_module_ = nullptr;

    // Vtables conservees pour selfTest() et rebind eventuel.
    const OdeniseModuleVTable* backend_vt_         = nullptr;
    const OdeniseModuleVTable* suppression_vt_     = nullptr;
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
