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

std::filesystem::path moduleDir() {
    std::string str = ODENISE_MODULE_INSTALL_DIR;
    std::filesystem::path p(str);
    return p;
}

namespace odenise {

BackendCaps toBackendCaps(const OdeniseBackendCapsC& b_caps) {
    BackendCaps bc;

    bc.backend_id   =  b_caps.backend_id;
    bc.backend_type =  b_caps.backend_type;
    bc.backend_name =  b_caps.backend_name ? b_caps.backend_name : "";
    bc.gpu_family   =  b_caps.gpu_family ? b_caps.gpu_family : "";
    bc.is_gpu       = (b_caps.is_gpu != 0);
    bc.vram_bytes   = static_cast<std::size_t>(b_caps.vram_bytes);
    bc.cc_major     =  b_caps.cc_major;
    bc.cc_minor     =  b_caps.cc_minor;
    bc.has_fp16     = (b_caps.has_fp16 != 0);
    bc.has_tensor   = (b_caps.has_tensor != 0);

    return bc;
}

class EngineImpl final : public Engine {
/*  EngineCaps
        int     sample_rate     = 48000;
        int     window_size_max = 4096;   // taille de fenetre max pre-allouee
        int     max_bands       = 48;     // nb de bandes perceptives max
        int     max_tracks      = 16;
        int     max_block       = 2048;   // taille de bloc hote max (dim. rings)
        bool    prealloc_c2c    = false;  // autorise bascule R2C<->C2C a chaud
        bool    share_fft_work  = true;   // workspace cuFFT unique partage
        size_t  backend_id      = 0;      // 0 = AUTO ; sinon id du registry available

    RuntimeConfig
        size_t               backend_id = 0;    // 0 = AUTO ; sinon un des id du registre
        std::vector<odenise::ChainElement>  modules;           // TODO: reflete l'audio_chain liste

        int     window_size    = 1024;   // <= window_size
        int     hop            = 256;    // n/4 = 75 % de recouvrement
        float   window_ratio   = 1.0f;   // 1.0 = synthese symetrique ; <1 = asym.
        int     num_bands      = 32;     // <= max_bands
        FftMode fft_mode       = FftMode::R2C;
        int     window_id      = 0;
        int     dualmic_id     = 0;      // 0 = mono
*/
public:
    EngineImpl(const EngineCaps& e_caps, const RuntimeConfig& cfg)
        : caps_(e_caps), cfg_(cfg) {
        // Decouverte des modules disponibles (sans chargement).
        const auto dir = moduleDir();
        const int nb_module = registry_.scan_modules(dir);
        std::string msg;
        msg = __func__ ;
        msg += _(": created with windows_size=");
        msg += ( cfg_.window_size ? std::to_string(cfg_.window_size) : "Unspecified windows_size");
        msg += " , ";
        msg += std::to_string(nb_module);
        msg += _(" modules founds.");
        LOG(msg);
        msg = __func__ ;
        msg += _(": availables_module:\n");
        for( auto ava : registry_.list_available()){
            msg += " id=";
            msg += std::to_string(ava.id);
            msg += " name=";
            msg += ava.name;
            msg += "\n";
        }
        LOG(msg);
        msg = __func__ ;
        msg += _(": Loaded_modules:\n");
        for( auto ava : registry_.list_loaded()){
            msg += " id=";
            msg += std::to_string(ava.id);
            msg += " name=";
            msg += ava.info.name;
            msg += "\n";
        }
        LOG(msg);


        /*
        msg = __func__;
        msg += _(": Load Backend:");
        LOG(msg);
        bindBackend(caps_.backend_id);
        backend_->pause();
        
        msg = __func__;
        msg += _(": Load Module:");
        LOG(msg);
        if (cfg_.modules.size() != 0 ){
            for ( auto mod = cfg_.modules.begin(); mod != cfg_.modules.end() -1; mod++){
                bindModule(mod->module->info_c()->id);
            }
        }else{
            for (const auto& module : registry_.list_available())
                if(module.kind != ModuleKind::ComputeBackend){
                    bindModule(module.id);
                }
        }
        */
    }

    ~EngineImpl() override {
        // Liberation en ordre inverse. Les objets sont possedes par le registry.
        releaseAllModule();
        releaseBackend();
    }

    int latency_samples() const noexcept override {
        return cached_latency_.declared_samples > 0
            ? cached_latency_.declared_samples : cfg_.window_size;
    }
        
    int latency_samples_rt() const noexcept override {
        return cached_latency_.measured_samples > 0
            ? cached_latency_.measured_samples : cfg_.window_size;
    }

    Status reconfigure(const RuntimeConfig& cfg, ApplyResult& how) override {
        /*
        EngineCaps
            int     sample_rate        = 48000;
            int     window_size_max    = 4096;   // taille de fenetre max pre-allouee
            int     max_bands          = 48;     // nb de bandes perceptives max
            int     max_tracks         = 16;
            int     max_block          = 2048;   // taille de bloc hote max (dim. rings)
            size_t  backend_id      = 0;      // 0 = AUTO ; sinon id du registre

        RuntimeConfig :
            size_t                     backend_id = 0;      // 0 = AUTO ; sinon id du registre
            std::vector<ChainElement>  modules;           // TODO: reflete l'audio_chain liste
            int     window_size     = 1024;    // <= window_size
            int     hop             = 256;    // n/4 = 75 % de recouvrement
            float   window_ratio    = 1.0f;   // 1.0 = synthese symetrique ; <1 = asym.
            int     num_bands       = 32;     // <= max_bands
            FftMode fft_mode        = FftMode::R2C;
            int     window_id       = 0;
            int     dualmic_id      = 0;      // 0 = mono
        */
        const bool backend_changed = (cfg.backend_id != cfg_.backend_id);
        cfg_ = cfg;

        if (backend_changed){
            how  = ApplyResult::Cold;
            bindModule(cfg_.backend_id);
        }

        if(backend_)
            backend_->reconfigure(caps_, cfg_);

        return Status::Ok;
    }

    BackendCaps backendCaps() const override {
        if (backend_)
            return toBackendCaps(*backend_->caps_c());
        return {};
    }

    void setAudioIO(TrackIO io) const noexcept override {
        LOG(LOG_IN());
        if (!backend_) {
            std::string msg_err = error(__func__,
                _("engine: setAudioIO called without backend"),
                _("ignored"));
            LOG_ERR(msg_err);
            return;
        }
        backend_->setAudioIO(io);
        LOG(LOG_OUT());
    }

    /* Param to be set by the ui to the specialized module */
    Status setParam(ParamId id, float value) noexcept override {
        // TODO : real set param
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
    std::vector<ModuleInfo> modules() const override {
        return registry_.list_available();
    }
    std::vector<ModuleInfo> loaded_modules(ModuleKind kind) const override {
        std::vector<ModuleInfo> out;
        for (const auto& a : registry_.list_loaded(kind))
            out.push_back(a.info);
        return out;
    }
    std::vector<ModuleInfo> loaded_modules() const override {
        std::vector<ModuleInfo> out;
        for (const auto& a : registry_.list_loaded())
            if (a.info.kind != ModuleKind::ComputeBackend)
                out.push_back(a.info);
        return out;
    }

    std::vector<ModuleInfo> get_chain() const override {
        if (!backend_) return {};
        return backend_->get_chain();
    }
    bool connectPorts(size_t from_loaded_id, int from_port_id,
                      size_t to_loaded_id,   int to_port_id) override {
        if (!backend_) {
            std::string msg_err = error(__func__,
                _("engine: connectPorts called without backend"), "");
            LOG_ERR(msg_err);
            return false;
        }
        return backend_->connect(from_loaded_id, from_port_id,
                                 to_loaded_id,   to_port_id);
    }

    void disconnectPort(size_t to_loaded_id, int to_port_id) override {
        if (!backend_) return;
        backend_->disconnect(to_loaded_id, to_port_id);
    }

    TestResult selfTest(size_t available_id) const override {
        return const_cast<EngineImpl*>(this)->registry_.self_test(available_id);
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
    static void on_declared_latency_changed(void* user, int declared_samples) noexcept {
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
    static void on_latency_updated(void* user,
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
    void pause_backend() const{
        if(backend_){
            backend_->pause();
        }
    };
    void restart_backend() const{
        if(backend_){        
            backend_->restart();
        }
    };
    void releaseBackend() {
        if (backend_) {
            // Debranche les callbacks avant dechargement.
            backend_->on_declared_latency_changed = nullptr;
            backend_->on_latency_updated          = nullptr;
            backend_->callback_user               = nullptr;
            registry_.unload_module(backend_id_);
            backend_    = nullptr;
            backend_id_ = 0;
        }
    }
    int bindBackend(size_t available_id) {
        releaseBackend();

        // AUTO (0) : premier ComputeBackend disponible.
        if (available_id == 0) {
            available_id = registry_.first_available_id(ModuleKind::ComputeBackend);
            if (available_id == 65535) {
                LOG(_("engine: no compute backend available"));
                return 0;
            }
        }

        if (!registry_.load_module(available_id)) {
            std::string msg = _("engine: cannot load backend id=");
            msg += std::to_string(available_id);
            LOG(msg);
            return 0;
        }

        backend_ = registry_.find_backend();
        if (!backend_) {
            std::string msg = _("engine: could not find a loaded backend with the requested available_id=");
            msg += std::to_string(available_id);
            LOG(msg);
            return 0;
        }
        backend_id_ = available_id;

        // Enregistre les callbacks avant reconfigure().
        backend_->on_declared_latency_changed   = &EngineImpl::on_declared_latency_changed;
        backend_->on_latency_updated            = &EngineImpl::on_latency_updated;
        backend_->callback_user                 = this;

        // Reconfigure avec les caps et cfg reelles.
        // Le backend demarre son thread de traitement ici.
        backend_->reconfigure(caps_, cfg_);

        std::string msg;
        msg = __func__;
        msg += _(": loaded backend with available_id=");
        msg += std::to_string(available_id);
        msg += _(" name='");
        msg += (backend_->info_c()->name ? backend_->info_c()->name : "name not set");
        msg += "'";
        LOG(msg);
        return 1;
    }

 
   // -----------------------------------------------------------------------
    //  Gestion du module
    // -----------------------------------------------------------------------
    void releaseModule(size_t loaded_id) {
        backend_->uninstall_module(loaded_id);
        registry_.unload_module(loaded_id);
        module_    = nullptr;
        module_id_ = 0;
    }
    void releaseAllModule() {
        auto loaded = registry_.list_loaded();
        if(loaded.size() > 0){
            for( auto lm = loaded.end() -1 ; lm != loaded.begin(); lm-- ){
                if(backend_){
                    backend_->uninstall_module(lm->id);
                }
                registry_.unload_module(lm->id);
            }
        };
        module_    = nullptr;
        module_id_ = 0;
    }

    int bindModule(size_t available_id) {
        if (!backend_) {
            LOG(_("engine: Cannot load module without backend"));
            return 0;
        }
        if (available_id == 65535) {
            LOG(_("engine: no module requested (id=65535)"));
            return 0;
        }
        auto asked_module=registry_.get_available_module_info(available_id);
        if( asked_module.kind == ModuleKind::ComputeBackend ){
            std::string msg = _("engine: cannot load ComputeBackend as module, requested id=");
            msg += std::to_string(available_id);
            LOG(msg);
            return 0;
        }
        size_t loaded_id;
        loaded_id = registry_.get_last_loaded_id() + 1;
        if (!registry_.load_module(available_id)) {
            std::string msg = _("engine: cannot load module id=");
            msg += std::to_string(available_id);
            LOG(msg);
            return 0;
        }       
        module_ = registry_.find_module(loaded_id);
        if (!module_) {
            std::string msg = _("engine: find_module returned null for module avaiblable id=");
            msg += std::to_string(available_id);
            msg += _(" at position id=");
            msg += std::to_string(loaded_id);
            LOG(msg);
            registry_.unload_module(loaded_id);
            return 0;
        }
        module_id_ = loaded_id;

        if (!backend_ || !backend_->install_module(module_, static_cast<ModuleKind>(module_->info_c()->kind), 0, loaded_id) ) {
            std::string msg_err = error(__func__,
                _("Module install chainning failed"),
                _("id=") + std::to_string(loaded_id));
            LOG_ERR(msg_err);
            registry_.unload_module(loaded_id);
            module_    = nullptr;
            module_id_ = 0;
            return 0;
        }

        std::string msg = _("engine: bound module id=");
        msg += std::to_string(module_id_);
        msg += _(" name=");
        msg += module_->info_c()->name;
        LOG(msg);
        return 1;
    }

    // -----------------------------------------------------------------------
    //  Membres
    // -----------------------------------------------------------------------
    EngineCaps      caps_;
    RuntimeConfig   cfg_;
    ModuleRegistry  registry_;

    BackendBase* backend_        = nullptr;  // pointeur non-owning (registry)
    size_t       backend_id_     = 0;       // id du backend charge
    ModuleBase*  module_         = nullptr;  // pointeur non-owning (registry)
    size_t       module_id_      = 0;        // id du module de suppression charge

    // Cache des mesures -- mis a jour par callbacks depuis le backend, hors RT.
    // Lu par l'UI via latencyInfo() / processingStats() (retours const ref).
    LatencyInfo     cached_latency_;
    ProcessingStats cached_stats_;
};

std::unique_ptr<Engine> createEngine(const EngineCaps& e_caps,
                                     const RuntimeConfig& cfg,
                                     Status* status) {
    if (status) *status = Status::Ok;
    return std::make_unique<EngineImpl>(e_caps, cfg);
}

std::vector<ModuleInfo> availableBackends() {
    ModuleRegistry reg;
    reg.scan_modules(moduleDir());
    return reg.list_available(ModuleKind::ComputeBackend);
}

} // namespace odenise
