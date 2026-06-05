// engine.cpp -- orchestration du moteur.
//
// Phase 3a : l'engine utilise AudioChain pour la chaine de traitement.
// Compatibilite descendante : les modules de la phase 1/2 (vtable C sans
// create_module/create_backend) continuent de fonctionner via le chemin
// legacy (suppression_vt_ / backend_vt_).
#include "ns_engine.h"
#include "chain/audio_chain.h"
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

    // [RT] Latence declaree courante.
    int latencySamples() const noexcept override {
        return chain_.declared_latency_samples() > 0
               ? chain_.declared_latency_samples()
               : cfg_.n;  // repli sur cfg_.n si chaine vide (legacy)
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

    Status process(std::span<const TrackIO> tracks,
                   int num_frames) noexcept override {

        for (const auto& t : tracks) {
            if (!t.in || !t.out || t.in_channels < 1)
                return Status::InvalidArg;

            // --- chemin phase 3+ : backend C++ ---
            if (backend_base_) {
                const Status rc = backend_base_->process(t.in, t.out, num_frames);
                if (rc != Status::Ok) return rc;
                continue;
            }

            // --- chemin legacy phase 1/2 : vtable C ---
            if (!suppression_vt_ || !suppression_inst_)
                return Status::Unsupported;

            OdeniseProcessCtx ctx;
            ctx.in          = t.in;
            ctx.out         = t.out;
            ctx.in_channels = t.in_channels;
            ctx.num_frames  = num_frames;

            const int rc = suppression_vt_->process(suppression_inst_, &ctx);
            if (rc != static_cast<int>(Status::Ok))
                return static_cast<Status>(rc);
        }
        return Status::Ok;
    }

    Status setParam(ParamId id, float value) noexcept override {
        // Transmet aux modules de la chaine (phase 3+).
        // Phase legacy : ignore (pas de set_param en RT sur la vtable C ici).
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
        // Latence declaree : sommee par AudioChain au cablage.
        li.declared_samples = chain_.declared_latency_samples();
        li.declared_ms = (caps_.sample_rate > 0)
            ? (static_cast<float>(li.declared_samples)
               / static_cast<float>(caps_.sample_rate)) * 1000.0f
            : 0.0f;
        // Latence mesuree : lue depuis le backend si disponible.
        if (backend_base_ && backend_base_->measure_ready()) {
            const auto& bli      = backend_base_->last_latency_info();
            li.measured_samples  = bli.measured_samples;
            li.measured_ms       = bli.measured_ms;
        }
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
    //  Gestion du backend
    //  Tente d'abord le chemin C++ (create_backend), repli sur vtable C.
    // -----------------------------------------------------------------------
    void releaseBackend() {
        // Chemin C++ : backend_base_ est possede par le module via la vtable C.
        // Sa destruction se fait via destroy() de la vtable, qui detruit l'objet
        // C++ retourne par create_backend().
        backend_base_ = nullptr;

        // Chemin legacy
        if (backend_vt_ && backend_inst_) {
            backend_vt_->destroy(backend_inst_);
            backend_inst_ = nullptr;
        }
        backend_vt_ = nullptr;
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

        // Chemin C++ (phase 3+) : create_backend disponible ?
        if (backend_vt_->create_backend) {
            backend_base_ = backend_vt_->create_backend(caps_.sample_rate, caps_.n_max);
            if (backend_base_) {
                LOG(_("engine: bound C++ backend id=") + std::to_string(id));
                return;
            }
            // Echec create_backend : repli sur vtable C.
            LOG(_("engine: create_backend returned null, falling back to legacy"));
        }

        // Chemin legacy (phase 1/2) : vtable C.
        backend_inst_ = backend_vt_->create(caps_.sample_rate, caps_.n_max);
        if (!backend_inst_) {
            LOG_ERR(error("engine", _("compute backend create failed"),
                          _("id=") + std::to_string(id)));
            backend_vt_ = nullptr;
            return;
        }
        LOG(_("engine: bound legacy backend id=") + std::to_string(id));
    }

    // -----------------------------------------------------------------------
    //  Gestion du module de suppression
    //  Tente d'abord le chemin C++ (create_module + AudioChain), repli legacy.
    // -----------------------------------------------------------------------
    void releaseSuppression() {
        // Chemin C++ : retire de la chaine, uninstall via AudioChain.
        if (suppression_module_) {
            chain_.remove(backend_base_, ModuleKind::Suppression, 0);
            suppression_module_ = nullptr;
        }

        // Chemin legacy
        if (suppression_vt_ && suppression_inst_) {
            suppression_vt_->destroy(suppression_inst_);
            suppression_inst_ = nullptr;
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

        suppression_vt_ = registry_.find(ModuleKind::Suppression, id);
        if (!suppression_vt_) {
            LOG(_("engine: no suppression module with id ") + std::to_string(id));
            return;
        }

        // Chemin C++ (phase 3+) : create_module disponible ?
        if (suppression_vt_->create_module) {
            suppression_module_ =
                suppression_vt_->create_module(caps_.sample_rate, caps_.n_max);
            if (suppression_module_) {
                // Installe dans la chaine (position 0 : seul module pour l'instant).
                if (!chain_.install(backend_base_,
                                    suppression_module_,
                                    ModuleKind::Suppression, 0)) {
                    LOG_ERR(error("engine",
                        _("suppression module chain install failed"),
                        _("id=") + std::to_string(id)));
                    suppression_module_ = nullptr;
                    suppression_vt_ = nullptr;
                    return;
                }
                LOG(_("engine: bound C++ suppression module id=") + std::to_string(id));
                return;
            }
            LOG(_("engine: create_module returned null, falling back to legacy"));
        }

        // Chemin legacy (phase 1/2) : vtable C.
        suppression_inst_ = suppression_vt_->create(caps_.sample_rate, caps_.n_max);
        if (!suppression_inst_) {
            LOG_ERR(error("engine", _("suppression module create failed"),
                          _("id=") + std::to_string(id)));
            suppression_vt_ = nullptr;
            return;
        }
        LOG(_("engine: bound legacy suppression module id=") + std::to_string(id));
    }

    // -----------------------------------------------------------------------
    //  Membres
    // -----------------------------------------------------------------------
    EngineCaps              caps_;
    RuntimeConfig           cfg_;
    detail::ModuleRegistry  registry_;
    chain::AudioChain       chain_;

    // Chemin C++ (phase 3+)
    BackendBase*            backend_base_       = nullptr; // possede par le module
    ModuleBase*             suppression_module_ = nullptr;

    // Chemin legacy (phase 1/2 -- compatibilite)
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
