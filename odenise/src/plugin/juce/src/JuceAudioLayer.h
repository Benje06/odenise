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
    //  Scan des peripheriques audio disponibles.
    //  Interroge le DeviceManager JUCE, construit les AudioInterfaceInfo
    //  (sample rates, bits, canaux in/out detectes), et les pousse dans
    //  AudioEditor via setAudioInterfaces().
    //  A appeler depuis le thread CTRL, jamais depuis le thread RT.
    // -----------------------------------------------------------------------
    void scanDevices();

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
