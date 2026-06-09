// ============================================================================
//  src/plugin/juce/src/JuceAudioLayer.cpp
// ============================================================================
#include "JuceAudioLayer.h"
#include "common.h"

namespace odenise::plugin {

// ----------------------------------------------------------------------------
JuceAudioLayer::JuceAudioLayer()
    : processor_()
    , editor_(&processor_)
{
    auto result = device_manager_.initialise(0, 0, nullptr, false);
    if (result.isNotEmpty()) {
        std::string msg_err = error(__func__,
            _("JuceAudioLayer: DeviceManager init failed"),
            result.toStdString());
        LOG_ERR(msg_err);
    }
}

// ----------------------------------------------------------------------------
//  scanDevices -- deux passes (entrees / sorties), deduplication par nom.
//
//  Chaque device_type (WASAPI, DirectSound, WASAPI Exclusive...) peut exposer
//  les memes peripheriques physiques. On deduplique par nom pour n'avoir
//  chaque peripherique qu'une seule fois dans chaque liste.
//
//  On ouvre le device brievement (open() / close()) pour lire les valeurs
//  courantes (sample rate, buffer size, bit depth). Sans open(), JUCE retourne
//  des valeurs par defaut non representativas du driver reel.
// ----------------------------------------------------------------------------
void JuceAudioLayer::scanDevices()
{
    std::vector<odenise::audio::AudioInterfaceInfo> inputs;
    std::vector<odenise::audio::AudioInterfaceInfo> outputs;

    // Ensembles de noms deja vus -- deduplication inter device_type
    juce::StringArray seen_inputs;
    juce::StringArray seen_outputs;

    int input_id  = 0;
    int output_id = 0;

    for (auto* device_type : device_manager_.getAvailableDeviceTypes())
    {
        if (!device_type) continue;
        device_type->scanForDevices();

        // --- Entrees --------------------------------------------------------
        for (const auto& name : device_type->getDeviceNames(true))
        {
            if (seen_inputs.contains(name)) continue;
            seen_inputs.add(name);

            std::unique_ptr<juce::AudioIODevice> dev(
                device_type->createDevice("", name));
            if (!dev) continue;

            const int n_in = dev->getInputChannelNames().size();
            if (n_in <= 0) continue;

            odenise::audio::AudioInterfaceInfo info;
            info.id                 = input_id++;
            info.name               = name.toStdString();
            info.max_input_channels = n_in;
            info.max_output_channels= 0;

            // Plage disponible (sans open)
            for (double sr : dev->getAvailableSampleRates())
                info.supported_sample_rates.push_back(static_cast<int>(sr));
            for (int bs : dev->getAvailableBufferSizes())
                info.supported_buffer_sizes.push_back(bs);

            // Valeurs courantes : necessitent open()
            juce::BigInteger ch_mask;
            ch_mask.setRange(0, n_in, true);
            auto err = dev->open(ch_mask, juce::BigInteger{},
                                 dev->getAvailableSampleRates().getLast(),
                                 dev->getDefaultBufferSize());
            if (err.isEmpty()) {
                info.current_sample_rate = static_cast<int>(dev->getCurrentSampleRate());
                info.current_buffer_size = dev->getCurrentBufferSizeSamples();
                info.current_bit_depth   = dev->getCurrentBitDepth();
                for (double sr : dev->getAvailableSampleRates())
                    info.supported_sample_rates.push_back(static_cast<int>(sr));
                for (int bs : dev->getAvailableBufferSizes())
                    info.supported_buffer_sizes.push_back(bs);

                dev->close();
            } else {
                // Fallback : premier sample rate disponible, pas de bit depth
                if (!info.supported_sample_rates.empty())
                    info.current_sample_rate = info.supported_sample_rates.front();
                info.current_bit_depth = 0;
            }

            std::string msg = _("JuceAudioLayer: input  '");
            msg += info.name;
            msg += "' ch="; msg += std::to_string(n_in);
            msg += " sr="; msg += std::to_string(info.current_sample_rate);
            msg += " buf="; msg += std::to_string(info.current_buffer_size);
            LOG(msg);

            inputs.push_back(std::move(info));
        }

        // --- Sorties --------------------------------------------------------
        for (const auto& name : device_type->getDeviceNames(false))
        {
            if (seen_outputs.contains(name)) continue;
            seen_outputs.add(name);

            std::unique_ptr<juce::AudioIODevice> dev(
                device_type->createDevice(name, ""));
            if (!dev) continue;

            const int n_out = dev->getOutputChannelNames().size();
            if (n_out <= 0) continue;

            odenise::audio::AudioInterfaceInfo info;
            info.id                  = output_id++;
            info.name                = name.toStdString();
            info.max_input_channels  = 0;
            info.max_output_channels = n_out;

            for (double sr : dev->getAvailableSampleRates())
                info.supported_sample_rates.push_back(static_cast<int>(sr));
            for (int bs : dev->getAvailableBufferSizes())
                info.supported_buffer_sizes.push_back(bs);

            juce::BigInteger ch_mask;
            ch_mask.setRange(0, n_out, true);
            auto err = dev->open(juce::BigInteger{}, ch_mask,
                                 dev->getAvailableSampleRates().getLast(),
                                 dev->getDefaultBufferSize());
            if (err.isEmpty()) {
                info.current_sample_rate = static_cast<int>(dev->getCurrentSampleRate());
                info.current_buffer_size = dev->getCurrentBufferSizeSamples();
                info.current_bit_depth   = dev->getCurrentBitDepth();
                dev->close();
            } else {
                if (!info.supported_sample_rates.empty())
                    info.current_sample_rate = info.supported_sample_rates.front();
                info.current_bit_depth = 0;
            }

            std::string msg = _("JuceAudioLayer: output '");
            msg += info.name;
            msg += "' ch="; msg += std::to_string(n_out);
            msg += " sr="; msg += std::to_string(info.current_sample_rate);
            msg += " buf="; msg += std::to_string(info.current_buffer_size);
            LOG(msg);

            outputs.push_back(std::move(info));
        }
    }

    editor_.setAudioInputs (std::move(inputs));
    editor_.setAudioOutputs(std::move(outputs));

    std::string msg = _("JuceAudioLayer: scan complete in=");
    msg += std::to_string(editor_.audioInputs().size());
    msg += " out=";
    msg += std::to_string(editor_.audioOutputs().size());
    LOG(msg);
}

} // namespace odenise::plugin
