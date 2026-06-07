// ============================================================================
//  src/plugin/JuceAudioLayer.cpp
// ============================================================================
#include "JuceAudioLayer.h"
#include "common.h"

namespace odenise::plugin {

// ----------------------------------------------------------------------------
JuceAudioLayer::JuceAudioLayer()
    : processor_()
    , editor_(&processor_)
{
    // Initialise le DeviceManager sans peripherique par defaut :
    // l'utilisateur choisit depuis l'UI via selectAudioInterface().
    // Canaux in/out a 0 : pas d'ouverture automatique de flux.
    auto result = device_manager_.initialise(0, 0, nullptr, false);
    if (result.isNotEmpty()) {
        std::string msg_err = error(__func__,
            _("JuceAudioLayer: DeviceManager init failed"),
            result.toStdString());
        LOG_ERR(msg_err);
    }
}

// ----------------------------------------------------------------------------
void JuceAudioLayer::scanDevices()
{
    std::vector<odenise::audio::AudioInterfaceInfo> infos;
    int next_id = 0;

    // Itere sur tous les types de peripheriques disponibles sur cette plateforme
    // (ex. : WASAPI, DirectSound, ASIO sur Windows ; ALSA, JACK sur Linux).
    for (auto* device_type : device_manager_.getAvailableDeviceTypes())
    {
        if (!device_type) continue;
        device_type->scanForDevices();

        const auto device_names = device_type->getDeviceNames(false);

        for (const auto& device_name : device_names)
        {
            // Ouvre le peripherique en mode sondage (sans flux actif)
            // pour lire ses capacites reelles.
            std::unique_ptr<juce::AudioIODevice> dev(
                device_type->createDevice(device_name, device_name));

            if (!dev) continue;

            odenise::audio::AudioInterfaceInfo info;
            info.id   = next_id++;
            info.name = device_name.toStdString();

            // Canaux detectes
            info.max_input_channels  = dev->getInputChannelNames().size();
            info.max_output_channels = dev->getOutputChannelNames().size();

            // Sample rates supportes
            for (double sr : dev->getAvailableSampleRates())
                info.supported_sample_rates.push_back(static_cast<int>(sr));

            // Tailles de buffer supportees
            for (int bs : dev->getAvailableBufferSizes())
                info.supported_buffer_sizes.push_back(bs);

            // Profondeur de bits detectee (0 si non reportee par le driver)
            info.current_bit_depth = dev->getCurrentBitDepth();

            infos.push_back(std::move(info));

            std::string msg = _("JuceAudioLayer: found device '");
            msg += info.name;
            msg += "'";
            LOG(msg);
        }
    }

    editor_.setAudioInterfaces(std::move(infos));

    std::string msg = _("JuceAudioLayer: scan complete");
    LOG(msg);
}

} // namespace odenise::plugin
