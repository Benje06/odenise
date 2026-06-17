// ============================================================================
//  src/core/audio/AudioProcessor.cpp
// ============================================================================
#include "common.h"
#include "AudioProcessor.h"

namespace odenise::audio {
    // -----------------------------------------------------------------------
    //  Construction / destruction
    // -----------------------------------------------------------------------
    // Basic empty construtor (futur use) ---------------------------------------------------
    AudioProcessor::AudioProcessor()
        : AudioProcessor(EngineCaps{}, RuntimeConfig{}) {}

    AudioProcessor::AudioProcessor(const EngineCaps& caps, const RuntimeConfig& cfg)
        : cfg_(cfg) {
        Status st;
        engine_ = createEngine(caps, cfg, &st);
        if (!engine_) {
            std::string msg_err = error(__func__,
                _("AudioProcessor: createEngine failed"),
                std::to_string(static_cast<int>(st)));
            LOG_ERR(msg_err);
        }
    }

    // ----------------------------------------------------------------------------
    //  Cycle de vie audio
    // ----------------------------------------------------------------------------
    bool AudioProcessor::prepare(double sample_rate, int block_size) {
        if (!engine_) {
            std::string msg_err = error(__func__,
                _("AudioProcessor: no engine"),
                _("prepare() skipped"));
            LOG_ERR(msg_err);
            return false;
        }

        sample_rate_ = sample_rate;
        block_size_  = block_size;

        ApplyResult how;
        engine_->reconfigure(cfg_, how);
        engine_->pause_backend();
        std::string msg = _("AudioProcessor: prepared sr=");
        msg += std::to_string(sample_rate);
        msg += _(" block=");
        msg += std::to_string(block_size);
        LOG(msg);
        return true;
    }
    // ----------------------------------------------------------------------------
    bool AudioProcessor::setAudioIO(TrackIO io) {
        if (!engine_) {
            std::string msg_err = error(__func__,
                _("AudioProcessor: no engine"),
                _("setAudioIO() skipped"));
            LOG_ERR(msg_err);
            return false;
        }
        engine_->setAudioIO(io);
        return true;
    }
    // ----------------------------------------------------------------------------
    void AudioProcessor::release() {
        if (!engine_) return;
        // La suspension du backend est geree en interne par celui-ci lors des
        // operations qui le necessitent (setAudioIO, reconfigure...).
        // Aucun appel explicite de release() requis sur l'engine.
        engine_->pause_backend();
        std::string msg = _("AudioProcessor: released");
        LOG(msg);
    }

    bool AudioProcessor::connectPorts(size_t from_loaded_id, int from_port_id,
                                    size_t to_loaded_id,   int to_port_id) {
        if (!engine_) return false;
        return engine_->connectPorts(from_loaded_id, from_port_id,
                                    to_loaded_id,   to_port_id);
    }

    void AudioProcessor::disconnectPort(size_t to_loaded_id, int to_port_id) {
        if (!engine_) return;
        engine_->disconnectPort(to_loaded_id, to_port_id);
    }
    
    // ----------------------------------------------------------------------------
    //  Gestion des modules
    // ----------------------------------------------------------------------------
    // load backend to use
    bool AudioProcessor::bindBackend(size_t available_id, const RuntimeConfig& cfg){
        if (!engine_) return false;
        return engine_->bindBackend(available_id);;
    }

    // Load / unload module
    bool AudioProcessor::bindModule(size_t available_id) {
        if (!engine_) return false;
        return engine_->bindModule(available_id);
    }
    bool AudioProcessor::unBindModule(size_t position) {
        if (!engine_) return false;
        //engine_->unBindModule(position);
        return true;
    }
    
    //reconfigure modules
    bool AudioProcessor::reconfigureModule(size_t loaded_id, const RuntimeConfig& cfg) {
        if (!engine_) return false;
        // TODO : engine_->reconfigure(loaded_id, cfg)
        // -> BackendBase::reconfigure(loaded_id, cfg)
        // -> module->reconfigure(cfg) avec cast vers config concrete dans le module
        (void)loaded_id; (void)cfg;
        return true;
    }
    // get list availables / loaded modules
    std::vector<odenise::ModuleInfo> AudioProcessor::get_available_backends(){
        return engine_->modules(odenise::ModuleKind::ComputeBackend);
    }
    std::vector<odenise::ModuleInfo> AudioProcessor::get_available_modules(){
        return engine_->modules();
    }
    
    // ----------------------------------------------------------------------------
    //  Configuration de la chaine audio
    // ----------------------------------------------------------------------------
    // insert in audio_chain 
    bool AudioProcessor::insertModule(size_t available_id, size_t position,
                                    const RuntimeConfig& cfg) {
        if (!engine_) return false;
        return engine_->bindModule(available_id);
    }
    // replace in audio_chain
    bool AudioProcessor::replaceModule(size_t available_id, size_t position,
                                    const RuntimeConfig& cfg) {
        if (!engine_) return false;
        // TODO : engine_->replaceModule(available_id, position, cfg)
        (void)available_id; (void)position; (void)cfg;
        return true;
    }
    // remve from audio chain
    bool AudioProcessor::removeModule(size_t available_id, size_t position,
                                    const RuntimeConfig& cfg) {
        if (!engine_) return false;
        // TODO : engine_->removeModule(position)
        return true;
    }

} // namespace odenise::audio
