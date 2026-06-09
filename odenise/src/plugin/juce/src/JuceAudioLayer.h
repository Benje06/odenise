// ============================================================================
//  src/plugin/JuceAudioLayer.h  --  Couche JUCE audio pour odenise.
//
//  Possede :
//    - juce::AudioDeviceManager  : ressource audio systeme (WASAPI/ALSA/CoreAudio)
//    - odenise::audio::AudioProcessor : moteur odenise
//    - odenise::audio::AudioEditor    : logique UI / monitoring
//
//  scanDrivers()  : liste les drivers disponibles, peuple AudioEditor.
//                   Appele une seule fois au demarrage.
//  scanDevices()  : liste les noms des interfaces du driver selectionne
//                   via getDeviceNames() uniquement -- pas de createDevice().
//                   Appele a chaque selection de driver dans le combo.
//  probeDevice()  : interroge les capacites d'une interface nommee via
//                   createDevice(), et met a jour l'AudioInterfaceInfo
//                   correspondante dans AudioEditor via updateAudioInput/Output().
//                   Appele a chaque selection d'interface dans le combo.
//
//  Utilisee par JucePlugin (VST3/CLAP) comme membre interne.
// ============================================================================
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "AudioProcessor.h"
#include "AudioEditor.h"

namespace odenise::plugin {

class JuceAudioLayer {
public:
    JuceAudioLayer();
    ~JuceAudioLayer() = default;

    JuceAudioLayer(const JuceAudioLayer&)            = delete;
    JuceAudioLayer& operator=(const JuceAudioLayer&) = delete;

    // -----------------------------------------------------------------------
    //  Scan des drivers audio disponibles.
    //  Interroge getAvailableDeviceTypes() et pousse dans AudioEditor.
    //  A appeler une seule fois au demarrage, depuis le thread CTRL.
    // -----------------------------------------------------------------------
    void scanDrivers();

    // -----------------------------------------------------------------------
    //  Scan des noms d'interfaces du driver selectionne.
    //  Utilise uniquement getDeviceNames() -- aucun createDevice().
    //  Remplace les listes inputs/outputs dans AudioEditor (noms seuls,
    //  capacites a zero jusqu'au probeDevice()).
    //  A appeler sur selection de driver, depuis le thread CTRL.
    // -----------------------------------------------------------------------
    void scanDevices(int driver_id);

    // -----------------------------------------------------------------------
    //  Interroge les capacites d'une interface nommee (createDevice()).
    //  Met a jour l'AudioInterfaceInfo correspondante dans AudioEditor
    //  via updateAudioInput() ou updateAudioOutput().
    //  A appeler sur selection d'interface dans le combo, depuis le thread CTRL.
    // -----------------------------------------------------------------------
    void probeDevice(int driver_id, int interface_id,
                     const std::string& name, bool want_inputs);

    // -----------------------------------------------------------------------
    //  Accesseurs -- non-owning, valides pendant la duree de vie de la couche.
    // -----------------------------------------------------------------------
    odenise::audio::AudioProcessor* processor() noexcept { return &processor_; }
    odenise::audio::AudioEditor*    editor()    noexcept { return &editor_;    }
    juce::AudioDeviceManager&       deviceManager() noexcept { return device_manager_; }

private:
    juce::AudioDeviceManager        device_manager_;
    odenise::audio::AudioProcessor  processor_;
    odenise::audio::AudioEditor     editor_;
};

} // namespace odenise::plugin
