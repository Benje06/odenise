// ============================================================================
//  src/core/audio/AudioEditor.cpp
// ============================================================================
#include "AudioEditor.h"
#include "AudioProcessor.h"
#include "common.h"

#include <cstring>

namespace odenise::audio {

// ----------------------------------------------------------------------------
AudioEditor::AudioEditor(AudioProcessor* processor)
    : processor_(processor) {
    // Dimensionne le vecteur de selections par kind.
    // Valeur 0 = aucun module selectionne pour ce kind.
    const int num_kinds = static_cast<int>(ModuleKind::Inference) + 1;
    selected_module_ids_.assign(num_kinds, 0);
}

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

bool AudioEditor::selectBackend(int id) {
    Engine* eng = processor_ ? processor_->engine() : nullptr;
    if (!eng) return false;

    // Reconfigure l'engine avec le nouveau backend.
    // TODO : Engine::reconfigure(EngineCaps) avec backend_id cible a ajouter.
    // Pour l'instant passe par reconfigure(RuntimeConfig) qui ne change pas
    // le backend -- le changement de backend necessite un reconfigure froid.
    ApplyResult how;
    RuntimeConfig cfg;  // cfg courante -- a recuperer depuis engine quand expose
    eng->reconfigure(cfg, how);

    selected_backend_id_ = id;
    std::string msg = _("AudioEditor: selected backend id=");
    msg += std::to_string(id);
    LOG(msg);
    return true;
}

// ----------------------------------------------------------------------------
//  Modules
// ----------------------------------------------------------------------------

bool AudioEditor::selectModule(ModuleKind kind, int id) {
    if (!processor_) return false;

    // Installe le module selectionne a la position 0 pour ce kind.
    // La position dans la chaine sera affinee quand la gestion multi-modules
    // sera completee dans Engine.
    const bool ok = processor_->installModule(kind, id, 0, RuntimeConfig{});
    if (ok) {
        selected_module_ids_[static_cast<int>(kind)] = id;
        std::string msg = _("AudioEditor: selected module kind=");
        msg += kindName(kind);
        msg += _(" id=");
        msg += std::to_string(id);
        LOG(msg);
    }
    return ok;
}

int AudioEditor::selectedModuleId(ModuleKind kind) const noexcept {
    const int idx = static_cast<int>(kind);
    if (idx < 0 || idx >= static_cast<int>(selected_module_ids_.size()))
        return 0;
    return selected_module_ids_[idx];
}

// ----------------------------------------------------------------------------
//  Configuration de la chaine audio -- delegue a AudioProcessor
// ----------------------------------------------------------------------------

bool AudioEditor::installModule(ModuleKind kind, int module_id, int position,
                                const RuntimeConfig& cfg) {
    if (!processor_) return false;
    return processor_->installModule(kind, module_id, position, cfg);
}

bool AudioEditor::insertModule(ModuleKind kind, int module_id, int position,
                               const RuntimeConfig& cfg) {
    if (!processor_) return false;
    return processor_->insertModule(kind, module_id, position, cfg);
}

bool AudioEditor::replaceModule(ModuleKind kind, int module_id, int position,
                                const RuntimeConfig& cfg) {
    if (!processor_) return false;
    return processor_->replaceModule(kind, module_id, position, cfg);
}

void AudioEditor::removeModule(ModuleKind kind, int position) {
    if (!processor_) return;
    processor_->removeModule(kind, position);
}

bool AudioEditor::reconfigureModule(int module_id, const RuntimeConfig& cfg) {
    if (!processor_) return false;
    return processor_->reconfigureModule(module_id, cfg);
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
    Engine* eng = processor_ ? processor_->engine() : nullptr;
    if (!eng) return {};
    return eng->backendCaps();
}

void AudioEditor::poll() {
    Engine* eng = processor_ ? processor_->engine() : nullptr;
    if (!eng) return;

    const LatencyInfo&     new_latency = eng->latencyInfo();
    const ProcessingStats& new_stats   = eng->processingStats();

    // Compare avec le cache local -- declenche on_stats_changed si changement.
    const bool changed =
        (std::memcmp(&new_latency, &cached_latency_, sizeof(LatencyInfo))     != 0) ||
        (std::memcmp(&new_stats,   &cached_stats_,   sizeof(ProcessingStats)) != 0);

    if (changed) {
        cached_latency_ = new_latency;
        cached_stats_   = new_stats;
        if (on_stats_changed)
            on_stats_changed(on_stats_changed_user);
    }
}

} // namespace odenise::audio
