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

    // TODO : engine_->reconfigure(EngineCaps, RuntimeConfig) a ajouter dans Engine.
    // La surcharge actuelle n'accepte que RuntimeConfig.
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
    // TODO : engine_->setAudioIO(io) a ajouter dans Engine.
    (void)io;
    return true;
}

// ----------------------------------------------------------------------------
void AudioProcessor::release() {
    if (!engine_) return;
    // TODO : engine_->release() a ajouter dans Engine.
    std::string msg = _("AudioProcessor: released");
    LOG(msg);
}

// ----------------------------------------------------------------------------
//  Configuration de la chaine audio
// ----------------------------------------------------------------------------

bool AudioProcessor::insertModule(int available_id, int position,
                                  const RuntimeConfig& cfg) {
    if (!engine_) return false;
    // TODO : engine_->insertModule(available_id, position, cfg)
    (void)available_id; (void)position; (void)cfg;
    return true;
}

bool AudioProcessor::replaceModule(int available_id, int position,
                                   const RuntimeConfig& cfg) {
    if (!engine_) return false;
    // TODO : engine_->replaceModule(available_id, position, cfg)
    (void)available_id; (void)position; (void)cfg;
    return true;
}

void AudioProcessor::removeModule(int position) {
    if (!engine_) return;
    // TODO : engine_->removeModule(position)
    (void)position;
}

bool AudioProcessor::reconfigureModule(int loaded_id, const RuntimeConfig& cfg) {
    if (!engine_) return false;
    // TODO : engine_->reconfigure(loaded_id, cfg)
    // -> BackendBase::reconfigure(loaded_id, cfg)
    // -> module->reconfigure(cfg) avec cast vers config concrete dans le module
    (void)loaded_id; (void)cfg;
    return true;
}

} // namespace odenise::audio
