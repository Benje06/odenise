// ============================================================================
//  src/core/audio/AudioProcessor.cpp
// ============================================================================
#include "AudioProcessor.h"
#include "common.h"

namespace odenise::audio {

// ----------------------------------------------------------------------------
AudioProcessor::AudioProcessor()
    : AudioProcessor(EngineCaps{}, RuntimeConfig{}) {}

// ----------------------------------------------------------------------------
AudioProcessor::AudioProcessor(const EngineCaps& caps, const RuntimeConfig& cfg) {
    odenise::Status st;
    engine_ = odenise::createEngine(caps, cfg, &st);
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

bool AudioProcessor::prepare(int sample_rate, int block_size) {
    if (!engine_) {
        std::string msg_err = error(__func__,
            _("AudioProcessor: no engine"),
            _("prepare() skipped"));
        LOG_ERR(msg_err);
        return false;
    }

    sample_rate_ = sample_rate;
    block_size_  = block_size;

    // Met a jour les caps avec les valeurs fournies par la couche audio,
    // puis reconfigure l'engine (et par cascade le backend).
    EngineCaps caps;
    caps.sample_rate = sample_rate;
    caps.n_max       = block_size;
    caps.backend_id  = -1;  // conserve le backend actif

    // TODO : engine_.reconfigure(caps, cfg) avec les EngineCaps mis a jour.
    // Engine::reconfigure() accepte aujourd'hui uniquement RuntimeConfig ;
    // la surcharge reconfigure(EngineCaps, RuntimeConfig) est a ajouter.
    ApplyResult how;
    engine_->reconfigure(cfg_, how);

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
    // TODO : Engine doit exposer un setAudioIO() qui delegue au backend.
    // engine_->setAudioIO(io);
    (void)io;
    return true;
}

// ----------------------------------------------------------------------------
void AudioProcessor::release() {
    if (!engine_) return;
    // TODO : Engine::release() a ajouter -- suspend le backend sans le detruire.
    // engine_->release();
    std::string msg = _("AudioProcessor: released");
    LOG(msg);
}

// ----------------------------------------------------------------------------
//  Configuration de la chaine audio
//  Delegue a Engine qui propage au backend puis aux modules.
// ----------------------------------------------------------------------------

bool AudioProcessor::installModule(ModuleKind kind, int module_id,
                                   int position, const RuntimeConfig& cfg) {
    if (!engine_) return false;
    // Lie le module via l'engine (charge depuis le registry, installe dans la chaine).
    // Engine::bindModule(kind, id, position, cfg) a ajouter.
    // TODO : engine_->bindModule(kind, module_id, position, cfg);
    (void)kind; (void)module_id; (void)position; (void)cfg;
    return true;
}

bool AudioProcessor::insertModule(ModuleKind kind, int module_id,
                                  int position, const RuntimeConfig& cfg) {
    if (!engine_) return false;
    // TODO : engine_->insertModule(kind, module_id, position, cfg);
    (void)kind; (void)module_id; (void)position; (void)cfg;
    return true;
}

bool AudioProcessor::replaceModule(ModuleKind kind, int module_id,
                                   int position, const RuntimeConfig& cfg) {
    if (!engine_) return false;
    // TODO : engine_->replaceModule(kind, module_id, position, cfg);
    (void)kind; (void)module_id; (void)position; (void)cfg;
    return true;
}

void AudioProcessor::removeModule(ModuleKind kind, int position) {
    if (!engine_) return;
    // TODO : engine_->removeModule(kind, position);
    (void)kind; (void)position;
}

bool AudioProcessor::reconfigureModule(int module_id, const RuntimeConfig& cfg) {
    if (!engine_) return false;
    // TODO : engine_->reconfigure(module_id, cfg)
    // -> BackendBase::reconfigure(module_id, cfg)
    // -> module->reconfigure(cfg) (avec cast vers config concrete dans le module)
    (void)module_id; (void)cfg;
    return true;
}

} // namespace odenise::audio
