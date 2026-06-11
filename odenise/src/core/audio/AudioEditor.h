// ============================================================================
//  src/core/audio/AudioEditor.h  --  Logique UI odenise.
//
//  Independante de JUCE, gtkmm et de tout framework graphique.
//  Ne connait ni BackendBase ni ModuleBase directement.
//
//  Acces :
//    listes disponibles    -> engine_->modules(kind) / backendCaps()
//    configuration audio   -> processor_ -> Engine -> BackendBase -> audio
//    monitoring            -> engine_ (cache mis a jour par poll())
//
//  Routage audio :
//    Les interfaces d'entree et de sortie sont gerees separement.
//    Une interface d'entree peut etre differente de l'interface de sortie
//    (ex. : micro sur ID24, monitoring sur Realtek).
//    setAudioInputs()  / audioInputs()  : interfaces ayant des canaux d'entree.
//    setAudioOutputs() / audioOutputs() : interfaces ayant des canaux de sortie.
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

#if defined(_WIN32) || defined(__MINGW32__)
    #ifdef AUDIO_EXPORTS
        #define AUDIO __declspec(dllexport)
    #elif defined(AUDIO_IMPORTS)
        #define AUDIO __declspec(dllimport)
    #else
        #define AUDIO
    #endif
#else
    #ifdef AUDIO_EXPORTS
        #define AUDIO __attribute__((visibility("default")))
    #else
        #define AUDIO
    #endif
#endif

namespace odenise::audio {

class AudioProcessor;

// ---------------------------------------------------------------------------
//  AudioInterfaceInfo -- description d'une interface audio disponible.
//  Peuplee par la couche audio (JUCE DeviceManager, ALSA, WASAPI...).
//  Une meme interface physique peut apparaitre dans les deux listes
//  (entrees et sorties) si elle possede des canaux dans les deux sens.
// ---------------------------------------------------------------------------
struct AudioInterfaceInfo {
    int                      id;
    int                      max_input_channels;
    int                      max_output_channels;
    int                      input_latency_samples = 0; // valeur active (0 = inconnue)
    int                      output_latency_samples = 0; // valeur active (0 = inconnue)
    int                      current_sample_rate  = 0; // valeur active (0 = inconnue)
    int                      default_buffer_size  = 0; 
    int                      current_buffer_size  = 0; // valeur active en samples
    int                      default_bit_depth    = 0; // 0 si non reporte par le driver
    int                      current_bit_depth    = 0; // 0 si non reporte par le driver
    size_t                   xrun_count           = 0;
    std::string              name;
    std::vector<int>         supported_sample_rates;   // plage complete supportee
    std::vector<int>         supported_buffer_sizes;   // plage complete supportee
    std::vector<std::string> channel_names;            // noms reels des canaux (driver)
};

// ---------------------------------------------------------------------------
//  AudioDriver -- driver audio disponible (WASAPI, WASAPI Exclusive, ASIO...).
//  Peuple par la couche audio (JuceAudioLayer::scanDrivers()).
// ---------------------------------------------------------------------------
struct AudioDriver {
    int         id;
    std::string name;
};

// ---------------------------------------------------------------------------
//  AudioEditor -- logique UI de configuration et de monitoring.
// ---------------------------------------------------------------------------
class AudioEditor {
public:
    AUDIO explicit AudioEditor(AudioProcessor* processor);
    AUDIO ~AudioEditor();

    AudioEditor(const AudioEditor&)            = delete;
    AudioEditor& operator=(const AudioEditor&) = delete;

    // -----------------------------------------------------------------------
    //  Drivers audio -- peuples par JuceAudioLayer::scanDrivers() au demarrage.
    // -----------------------------------------------------------------------
    AUDIO void setAudioDrivers(std::vector<AudioDriver> drivers);
    AUDIO const std::vector<AudioDriver>& audioDrivers() const noexcept;
    AUDIO bool selectDriver(int id);
    AUDIO int  selectedDriverId() const noexcept { return selected_driver_id_; }

    // -----------------------------------------------------------------------
    //  Interfaces audio d'entree -- interfaces ayant max_input_channels > 0.
    //  Fournies par la couche audio (JuceAudioLayer::scanDevices()).
    // -----------------------------------------------------------------------
    AUDIO void setAudioInputs(std::vector<AudioInterfaceInfo> inputs);
    AUDIO const std::vector<AudioInterfaceInfo>& audioInputs() const noexcept;

    // Met a jour les capacites d'une interface d'entree deja listee (id connu).
    // Appele par JuceAudioLayer::probeDevice() apres selection dans le combo.
    AUDIO bool updateAudioInput (int id, const AudioInterfaceInfo& info);

    AUDIO bool selectInputInterface(int id);
    AUDIO int  selectedInputInterfaceId()  const noexcept { return selected_input_id_;  }
    AUDIO int  selectedInputChannel()      const noexcept { return selected_input_ch_;  }
    AUDIO bool selectInputChannel(int channel);

    // -----------------------------------------------------------------------
    //  Interfaces audio de sortie -- interfaces ayant max_output_channels > 0.
    // -----------------------------------------------------------------------
    AUDIO void setAudioOutputs(std::vector<AudioInterfaceInfo> outputs);
    AUDIO const std::vector<AudioInterfaceInfo>& audioOutputs() const noexcept;

    // Met a jour les capacites d'une interface de sortie deja listee (id connu).
    // Appele par JuceAudioLayer::probeDevice() apres selection dans le combo.
    AUDIO bool updateAudioOutput(int id, const AudioInterfaceInfo& info);

    AUDIO bool selectOutputInterface(int id);
    AUDIO int  selectedOutputInterfaceId() const noexcept { return selected_output_id_; }
    AUDIO int  selectedOutputChannel()     const noexcept { return selected_output_ch_; }
    AUDIO bool selectOutputChannel(int channel);

    // -----------------------------------------------------------------------
    //  Backend -- selectionne depuis registry.available_.
    // -----------------------------------------------------------------------
    AUDIO void   get_backends();
    AUDIO const  std::vector<odenise::ModuleInfo>& backends() const noexcept;

    AUDIO bool   selectBackend(size_t bcknd_combo_id);
    AUDIO size_t selectedBackendId() const noexcept { return selected_backend_id_; }

    // -----------------------------------------------------------------------
    //  Module -- selectionne depuis registry.available_.
    // -----------------------------------------------------------------------
    AUDIO void   get_modules();
    AUDIO const  std::vector<odenise::ModuleInfo>& modules() const noexcept;
    AUDIO bool   selectModule(int mods_combo_id, const RuntimeConfig& cfg);
    AUDIO size_t selectedModuleId() const noexcept { return selected_module_id_; }

    // -----------------------------------------------------------------------
    //  Configuration de la chaine (unique, tous kinds confondus).
    // -----------------------------------------------------------------------
    AUDIO bool insertModule    (size_t available_id, size_t position, const RuntimeConfig& cfg);
    AUDIO bool replaceModule   (size_t available_id, size_t position, const RuntimeConfig& cfg);
    AUDIO void removeModule    (size_t position);
    AUDIO bool reconfigureModule(size_t loaded_id,  const RuntimeConfig& cfg);

    // -----------------------------------------------------------------------
    //  Monitoring -- cache local mis a jour par poll().
    // -----------------------------------------------------------------------
    const LatencyInfo&     latencyInfo()     const noexcept;
    const ProcessingStats& processingStats() const noexcept;
    AUDIO BackendCaps      backendCaps()     const;

    AUDIO void poll();

    void (*on_stats_changed)(void* user) = nullptr;
    void*  on_stats_changed_user         = nullptr;

private:
    AudioProcessor* processor_;
    Engine*         engine_;
    RuntimeConfig*  cfg_;

    std::vector<ModuleInfo>         backends_;
    std::vector<ModuleInfo>         modules_;
    std::vector<AudioDriver>        drivers_;
    std::vector<AudioInterfaceInfo> inputs_;
    std::vector<AudioInterfaceInfo> outputs_;

    int    selected_driver_id_  = -1;

    int    selected_input_id_   = -1;
    int    selected_input_ch_   =  0;
    int    selected_output_id_  = -1;
    int    selected_output_ch_  =  0;

    size_t selected_backend_id_ = 0;
    size_t selected_module_id_  = 1;

    LatencyInfo     cached_latency_;
    ProcessingStats cached_stats_;
};

} // namespace odenise::audio
