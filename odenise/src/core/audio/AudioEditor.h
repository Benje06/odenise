// ============================================================================
//  src/core/audio/AudioEditor.h  --  Logique UI odenise.
//
//  Independante de JUCE, gtkmm et de tout framework graphique.
//  Ne connait ni BackendBase ni ModuleBase directement.
//
//  Acces :
//    listes disponibles    -> engine_->modules(kind) / backendCaps()
//    configuration chaine  -> processor_ -> Engine -> BackendBase -> chaine
//    monitoring            -> engine_ (cache mis a jour par poll())
//
//  Identifiants :
//    available_id : id dans registry.available_ (insert, replace)
//    loaded_id    : id dans registry.loaded_     (reconfigure)
//    position     : rang dans la chaine (remove)
//
//  La chaine est unique -- tous les kinds coexistent dans la meme chaine.
//  Le kind est une metadonnee du module, pas un axe d'organisation.
// ============================================================================
#pragma once

#include "engine.h"

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
    // processor et son engine doivent rester valides pendant toute la duree
    // de vie d'AudioEditor.
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
    //  Backend -- selectionne depuis registry.available_.
    // -----------------------------------------------------------------------
    bool selectBackend(size_t available_id);
    int  selectedBackendId() const noexcept { return selected_backend_id_; }

    // -----------------------------------------------------------------------
    //  Module -- selectionne depuis registry.available_.
    //  Equivalent a insertModule en derniere position avec config par defaut.
    // -----------------------------------------------------------------------
    bool selectModule(size_t available_id, const RuntimeConfig& cfg);
    int  selectedModuleId() const noexcept { return selected_module_id_; }

    // -----------------------------------------------------------------------
    //  Configuration de la chaine (unique, tous kinds confondus).
    //  Delegue a AudioProcessor -> Engine -> BackendBase -> chaine.
    //
    //  insert  : charge depuis available_, deplace et insere a la position.
    //  replace : charge depuis available_, remplace le module a la position.
    //  remove  : retire le module a la position, le decharge de loaded_.
    //  reconfigure : reconfigure un module loaded_ par son loaded_id.
    // -----------------------------------------------------------------------
    bool insertModule    (size_t available_id, size_t position, const RuntimeConfig& cfg);
    bool replaceModule   (size_t available_id, size_t position, const RuntimeConfig& cfg);
    void removeModule    (size_t position);
    bool reconfigureModule(size_t loaded_id, const RuntimeConfig& cfg);

    // -----------------------------------------------------------------------
    //  Monitoring -- cache local mis a jour par poll().
    //  A appeler depuis un timer UI (juce::Timer, gtkmm signal_timeout...).
    // -----------------------------------------------------------------------
    const LatencyInfo&     latencyInfo()     const noexcept;
    const ProcessingStats& processingStats() const noexcept;
    BackendCaps            backendCaps()     const;

    // Rafraichit le cache et declenche on_stats_changed si changement.
    void poll();

   // Le wrapper (JUCE/gtkmm) branche son repaint ici.
    // Pointeur brut : pas de std::function (portabilite ABI).
    void (*on_stats_changed)(void* user) = nullptr;
    void*  on_stats_changed_user         = nullptr;

private:
    AudioProcessor* processor_;  // non-owning
    Engine*         engine_;     // non-owning, cache depuis processor_->engine()

    std::vector<AudioInterfaceInfo> interfaces_;
    int selected_interface_id_ = -1;
    int selected_backend_id_   = -1;
    int selected_module_id_    = -1;

    LatencyInfo     cached_latency_;
    ProcessingStats cached_stats_;
};

} // namespace odenise::audio
