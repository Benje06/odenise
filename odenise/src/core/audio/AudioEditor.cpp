// ============================================================================
//  src/core/audio/AudioEditor.cpp
// ============================================================================
#include "AudioEditor.h"
#include "AudioProcessor.h"
#include "common.h"

namespace odenise::audio {

// ----------------------------------------------------------------------------
AudioEditor::AudioEditor(AudioProcessor* processor)
    : processor_(processor)
    , engine_(processor ? processor->engine() : nullptr) {}

// ----------------------------------------------------------------------------
//  Interfaces audio
// ----------------------------------------------------------------------------

void AudioEditor::setAudioInterfaces(std::vector<AudioInterfaceInfo> interfaces) {
    interfaces_ = std::move(interfaces);
}

const std::vector<AudioInterfaceInfo>& AudioEditor::audioInterfaces() const noexcept {
    return interfaces_;
}

bool AudioEditor::selectAudioInterface(int id) {
    for (const auto& iface : interfaces_) {
        if (iface.id == id) {
            selected_interface_id_ = id;
            return true;
        }
    }
    std::string msg_err = error(__func__,
        _("AudioEditor: unknown audio interface id"),
        std::to_string(id));
    LOG_ERR(msg_err);
    return false;
}

// ----------------------------------------------------------------------------
//  Backend
// ----------------------------------------------------------------------------

bool AudioEditor::selectBackend(size_t available_id) {
    if (!engine_) return false;
    // TODO : engine_->reconfigure(EngineCaps avec backend_id=available_id)
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
    // Equivalent a insertModule en derniere position.
    // La position -1 signifie "derniere" -- Engine resout la position reelle.
    const bool ok = insertModule(available_id, -1, cfg);
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
