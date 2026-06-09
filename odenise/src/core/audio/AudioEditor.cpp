// ============================================================================
//  src/core/audio/AudioEditor.cpp
// ============================================================================
#include "common.h"
#include "AudioProcessor.h"
#include "AudioEditor.h"

namespace odenise::audio {

// ----------------------------------------------------------------------------
AudioEditor::AudioEditor(AudioProcessor* processor)
    : processor_(processor)
    , engine_(processor ? processor->engine() : nullptr) {}

// ----------------------------------------------------------------------------
AudioEditor::~AudioEditor() {}

// ----------------------------------------------------------------------------
//  Drivers audio
// ----------------------------------------------------------------------------

void AudioEditor::setAudioDrivers(std::vector<AudioDriver> drivers) {
    drivers_ = std::move(drivers);
}

const std::vector<AudioDriver>& AudioEditor::audioDrivers() const noexcept {
    return drivers_;
}

bool AudioEditor::selectDriver(int id) {
    for (const auto& drv : drivers_) {
        if (drv.id == id) {
            selected_driver_id_ = id;
            // reset interfaces et selections sur changement de driver
            inputs_.clear();
            outputs_.clear();
            selected_input_id_  = -1;
            selected_input_ch_  =  0;
            selected_output_id_ = -1;
            selected_output_ch_ =  0;
            return true;
        }
    }
    std::string msg_err = error(__func__,
        _("AudioEditor: unknown driver id"),
        std::to_string(id));
    LOG_ERR(msg_err);
    return false;
}

// ----------------------------------------------------------------------------
//  Interfaces d'entree
// ----------------------------------------------------------------------------

void AudioEditor::setAudioInputs(std::vector<AudioInterfaceInfo> inputs) {
    inputs_ = std::move(inputs);
}

const std::vector<AudioInterfaceInfo>& AudioEditor::audioInputs() const noexcept {
    return inputs_;
}

bool AudioEditor::selectInputInterface(int id) {
    for (const auto& iface : inputs_) {
        if (iface.id == id) {
            selected_input_id_ = id;
            selected_input_ch_ = 0;  // reset canal a la selection d'interface
            return true;
        }
    }
    std::string msg_err = error(__func__,
        _("AudioEditor: unknown input interface id"),
        std::to_string(id));
    LOG_ERR(msg_err);
    return false;
}

bool AudioEditor::selectInputChannel(int channel) {
    for (const auto& iface : inputs_) {
        if (iface.id == selected_input_id_) {
            if (channel < 0 || channel >= iface.max_input_channels) {
                std::string msg_err = error(__func__,
                    _("AudioEditor: input channel out of range"),
                    std::to_string(channel));
                LOG_ERR(msg_err);
                return false;
            }
            selected_input_ch_ = channel;
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
//  Interfaces de sortie
// ----------------------------------------------------------------------------

void AudioEditor::setAudioOutputs(std::vector<AudioInterfaceInfo> outputs) {
    outputs_ = std::move(outputs);
}

const std::vector<AudioInterfaceInfo>& AudioEditor::audioOutputs() const noexcept {
    return outputs_;
}

bool AudioEditor::selectOutputInterface(int id) {
    for (const auto& iface : outputs_) {
        if (iface.id == id) {
            selected_output_id_ = id;
            selected_output_ch_ = 0;  // reset canal a la selection d'interface
            return true;
        }
    }
    std::string msg_err = error(__func__,
        _("AudioEditor: unknown output interface id"),
        std::to_string(id));
    LOG_ERR(msg_err);
    return false;
}

bool AudioEditor::selectOutputChannel(int channel) {
    for (const auto& iface : outputs_) {
        if (iface.id == selected_output_id_) {
            if (channel < 0 || channel >= iface.max_output_channels) {
                std::string msg_err = error(__func__,
                    _("AudioEditor: output channel out of range"),
                    std::to_string(channel));
                LOG_ERR(msg_err);
                return false;
            }
            selected_output_ch_ = channel;
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
//  Backend
// ----------------------------------------------------------------------------

bool AudioEditor::selectBackend(size_t available_id) {
    if (!engine_) return false;
    selected_backend_id_ = available_id;
    std::string msg = _("AudioEditor: selected backend available_id=");
    msg += std::to_string(available_id);
    LOG(msg);
    return true;
}

// ----------------------------------------------------------------------------
//  Module
// ----------------------------------------------------------------------------

bool AudioEditor::selectModule(size_t available_id, const RuntimeConfig& cfg) {
    const bool ok = insertModule(available_id, 0, cfg);
    if (ok) selected_module_id_ = available_id;
    return ok;
}

// ----------------------------------------------------------------------------
//  Configuration de la chaine
// ----------------------------------------------------------------------------

bool AudioEditor::insertModule(size_t available_id, size_t position,
                               const RuntimeConfig& cfg) {
    if (!processor_) return false;
    return processor_->insertModule(available_id, position, cfg);
}

bool AudioEditor::replaceModule(size_t available_id, size_t position,
                                const RuntimeConfig& cfg) {
    if (!processor_) return false;
    return processor_->replaceModule(available_id, position, cfg);
}

void AudioEditor::removeModule(size_t position) {
    if (!processor_) return;
    processor_->removeModule(position);
}

bool AudioEditor::reconfigureModule(size_t loaded_id, const RuntimeConfig& cfg) {
    if (!processor_) return false;
    return processor_->reconfigureModule(loaded_id, cfg);
}

// ----------------------------------------------------------------------------
//  Monitoring
// ----------------------------------------------------------------------------

const LatencyInfo& AudioEditor::latencyInfo() const noexcept {
    return cached_latency_;
}

const ProcessingStats& AudioEditor::processingStats() const noexcept {
    return cached_stats_;
}

BackendCaps AudioEditor::backendCaps() const {
    if (!engine_) return {};
    return engine_->backendCaps();
}

void AudioEditor::poll() {
    if (!engine_) return;
    cached_latency_ = engine_->latencyInfo();
    cached_stats_   = engine_->processingStats();
}

} // namespace odenise::audio
