// ============================================================================
//  src/core/audio/AudioEditor.h  --  Logique UI odenise.
//
//  Independante de JUCE, gtkmm et de tout framework graphique.
//  Delegue a Engine (via AudioProcessor) pour toutes les operations :
//    - liste backends/modules   : engine->modules(kind)
//    - configuration chaine     : AudioProcessor -> Engine -> BackendBase
//    - monitoring               : engine->latencyInfo() / processingStats()
//  AudioEditor ne connait ni BackendBase ni ModuleBase directement.
// ============================================================================
#pragma once

#include "engine.h"

#include <cstring>
#include <string>
#include <vector>

namespace odenise::audio {

class AudioProcessor;

// ---------------------------------------------------------------------------
//  AudioInterfaceInfo -- description d'une interface audio disponible.
//  Peuplee par la couche audio (JUCE DeviceManager, ALSA, WASAPI...).
// ---------------------------------------------------------------------------
struct AudioInterfaceInfo {
    int              id;
    std::string      name;
    int              max_input_channels;
    int              max_output_channels;
    std::vector<int> supported_sample_rates;
};

// ---------------------------------------------------------------------------
//  AudioEditor -- logique UI de configuration et de monitoring.
// ---------------------------------------------------------------------------
class AudioEditor {
public:
    // processor doit rester valide pendant toute la duree de vie d'AudioEditor.
    explicit AudioEditor(AudioProcessor* processor);
    ~AudioEditor() = default;

    AudioEditor(const AudioEditor&)            = delete;
    AudioEditor& operator=(const AudioEditor&) = delete;

    // -----------------------------------------------------------------------
    //  Interfaces audio -- liste fournie par la couche audio.
    // -----------------------------------------------------------------------
    void setAudioInterfaces(std::vector<AudioInterfaceInfo> interfaces);
    const std::vector<AudioInterfaceInfo>& audioInterfaces() const noexcept;

    bool selectAudioInterface(int id);
    int  selectedAudioInterfaceId() const noexcept { return selected_interface_id_; }

    // -----------------------------------------------------------------------
    //  Backend -- liste depuis engine->modules(ComputeBackend).
    //  selectBackend() reconfigure l'engine.
    // -----------------------------------------------------------------------
    bool selectBackend(int id);
    int  selectedBackendId() const noexcept { return selected_backend_id_; }

    // -----------------------------------------------------------------------
    //  Modules -- liste depuis engine->modules(kind).
    //  selectModule() installe le module via AudioProcessor.
    //  id=0 pour Suppression = delie le module (aucun).
    // -----------------------------------------------------------------------
    bool selectModule(ModuleKind kind, int id);
    int  selectedModuleId(ModuleKind kind) const noexcept;

    // -----------------------------------------------------------------------
    //  Configuration de la chaine audio
    //  Delegue a AudioProcessor -> Engine -> BackendBase.
    // -----------------------------------------------------------------------
    bool installModule(ModuleKind kind, int module_id,
                       int position, const RuntimeConfig& cfg);
    bool insertModule (ModuleKind kind, int module_id,
                       int position, const RuntimeConfig& cfg);
    bool replaceModule(ModuleKind kind, int module_id,
                       int position, const RuntimeConfig& cfg);
    void removeModule (ModuleKind kind, int position);

    // Reconfigure un module specifique par son id.
    // cfg peut etre une sous-classe de RuntimeConfig (cast dans le module).
    bool reconfigureModule(int module_id, const RuntimeConfig& cfg);

    // -----------------------------------------------------------------------
    //  Monitoring -- cache local mis a jour par poll().
    //  A appeler depuis un timer UI (juce::Timer, gtkmm signal_timeout...).
    // -----------------------------------------------------------------------
    const LatencyInfo&     latencyInfo()     const noexcept;
    const ProcessingStats& processingStats() const noexcept;
    BackendCaps            backendCaps()     const;

    // Rafraichit le cache et declenche on_stats_changed si changement.
    void poll();

    // Callback declenche par poll() quand les stats ont change.
    // Le wrapper (JUCE/gtkmm) branche son repaint ici.
    // Pointeur brut : pas de std::function (portabilite ABI).
    void (*on_stats_changed)(void* user) = nullptr;
    void*  on_stats_changed_user         = nullptr;

private:
    AudioProcessor* processor_;  // non-owning, possede par le wrapper

    std::vector<AudioInterfaceInfo> interfaces_;
    int selected_interface_id_ = -1;
    int selected_backend_id_   = -1;

    // Index = static_cast<int>(ModuleKind). Valeur 0 = aucun module selectionne.
    std::vector<int> selected_module_ids_;

    // Cache local -- compare a poll() pour detecter les changements.
    LatencyInfo     cached_latency_;
    ProcessingStats cached_stats_;
};

} // namespace odenise::audio
