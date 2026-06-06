// ============================================================================
//  src/core/audio/AudioEditor.h  --  Logique UI odenise.
//
//  Independante de JUCE, gtkmm et de tout framework graphique.
//  Delegue a Engine pour la liste des backends et modules disponibles
//  (c'est l'engine qui scanne les repertoires et maintient le registre).
//  Le wrapper JUCE (JuceAudioEditor) delegue ici.
//
//  Acces en lecture/ecriture via Engine uniquement -- zero couplage direct
//  avec BackendBase (pas de dependance UI/backend).
//
//  Responsabilites :
//    - cache de la liste des interfaces audio (fournie par la couche audio)
//    - selection courante : interface audio, backend, modules par kind
//    - application des selections via engine->reconfigure()
//    - monitoring : lecture latence + stats depuis engine (poll())
// ============================================================================
#pragma once

#include "engine.h"

#include <string>
#include <vector>

namespace odenise::audio {

// ---------------------------------------------------------------------------
//  AudioInterfaceInfo -- description d'une interface audio disponible.
//  Peuplee par la couche audio (JUCE DeviceManager, ALSA, WASAPI...) et
//  transmise a AudioEditor via setAudioInterfaces().
//  POD : pas de pointeur opaque vers le framework.
// ---------------------------------------------------------------------------
struct AudioInterfaceInfo {
    int         id;
    std::string name;
    int         max_input_channels;
    int         max_output_channels;
    std::vector<int> supported_sample_rates;
};

// ---------------------------------------------------------------------------
//  AudioEditor -- logique UI de configuration et de monitoring.
//
//  Ne possede pas l'engine -- obtient un pointeur non-owning depuis
//  AudioProcessor. L'engine reste possede par AudioProcessor.
// ---------------------------------------------------------------------------
class AudioEditor {
public:
    // -----------------------------------------------------------------------
    //  Construction
    // -----------------------------------------------------------------------

    // engine doit rester valide pendant toute la duree de vie d'AudioEditor.
    explicit AudioEditor(Engine* engine);
    ~AudioEditor() = default;

    AudioEditor(const AudioEditor&)            = delete;
    AudioEditor& operator=(const AudioEditor&) = delete;

    // -----------------------------------------------------------------------
    //  Interfaces audio
    //  La liste est fournie par la couche audio (JUCE, ALSA...).
    //  AudioProcessor ou le wrapper appelle setAudioInterfaces() apres
    //  enumeration des peripheriques disponibles.
    // -----------------------------------------------------------------------
    void setAudioInterfaces(std::vector<AudioInterfaceInfo> interfaces);
    const std::vector<AudioInterfaceInfo>& audioInterfaces() const noexcept;

    // Selectionne une interface par id. Retourne false si id inconnu.
    bool selectAudioInterface(int id);
    int  selectedAudioInterfaceId() const noexcept { return selected_interface_id_; }

    // -----------------------------------------------------------------------
    //  Backends et modules -- listes depuis engine (engine scanne les dirs).
    //  selectBackend / selectModule appliquent via engine->reconfigure().
    // -----------------------------------------------------------------------

    // Selectionne un backend par id. Reconfigure l'engine.
    // Retourne false si la reconfiguration echoue.
    bool selectBackend(int id);
    int  selectedBackendId() const noexcept { return selected_backend_id_; }

    // Selectionne un module par kind + id. Reconfigure l'engine.
    // id=0 pour Suppression = delie le module (aucun).
    // Retourne false si la reconfiguration echoue.
    bool selectModule(ModuleKind kind, int id);
    int  selectedModuleId(ModuleKind kind) const noexcept;

    // -----------------------------------------------------------------------
    //  Monitoring -- lecture depuis le cache engine (hors RT).
    //  A appeler depuis un timer UI (ex. juce::Timer, gtkmm signal_timeout).
    // -----------------------------------------------------------------------
    const LatencyInfo&     latencyInfo()     const noexcept;
    const ProcessingStats& processingStats() const noexcept;
    BackendCaps            backendCaps()     const;

    // -----------------------------------------------------------------------
    //  Callback de notification UI -- declenche par poll() quand les stats
    //  ont change depuis le dernier appel.
    //  Le wrapper (JUCE/gtkmm) branche son repaint ici.
    //  Pointeur brut : pas de std::function (portabilite ABI).
    // -----------------------------------------------------------------------
    void (*on_stats_changed)(void* user) = nullptr;
    void*  on_stats_changed_user         = nullptr;

    // Rafraichit le cache local depuis engine et declenche on_stats_changed
    // si les valeurs ont change. A appeler depuis le timer UI.
    void poll();

private:
    Engine* engine_;  // non-owning, possede par AudioProcessor

    std::vector<AudioInterfaceInfo> interfaces_;
    int selected_interface_id_ = -1;
    int selected_backend_id_   = -1;

    // Selections par kind : index = static_cast<int>(ModuleKind).
    // Valeur 0 = aucun module selectionne (semantique suppression_id=0).
    // Dimensionne au nombre de kinds connus a la construction.
    std::vector<int> selected_module_ids_;

    // Cache local des dernieres stats lues -- compare a poll() pour
    // detecter un changement et declencher on_stats_changed.
    LatencyInfo     cached_latency_;
    ProcessingStats cached_stats_;
};

} // namespace odenise::audio
