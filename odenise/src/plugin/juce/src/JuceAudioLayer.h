// ============================================================================
//  src/plugin/JuceAudioLayer.h  --  Couche JUCE audio pour odenise.
//
//  Possede :
//    - juce::AudioDeviceManager  : ressource audio systeme (WASAPI/ALSA/CoreAudio)
//    - odenise::audio::AudioProcessor : moteur odenise
//    - odenise::audio::AudioEditor    : logique UI / monitoring
//
//  scanDevices() interroge le DeviceManager et pousse les AudioInterfaceInfo
//  dans AudioEditor. AudioProcessor et AudioEditor ignorent JUCE.
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
    //  Scan des interfaces du driver selectionne.
    //  Interroge uniquement le device type correspondant a driver_id.
    //  Remplace les listes inputs/outputs dans AudioEditor.
    //  A appeler sur selection de driver, depuis le thread CTRL.
    // -----------------------------------------------------------------------
    void scanDevices(int driver_id);

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
