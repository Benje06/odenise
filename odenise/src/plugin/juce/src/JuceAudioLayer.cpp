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
//  scanDrivers -- liste les drivers audio disponibles.
//  Un driver = un AudioDeviceType JUCE (WASAPI, WASAPI Exclusive, ASIO...).
// ----------------------------------------------------------------------------
void JuceAudioLayer::scanDrivers()
{
    std::vector<odenise::audio::AudioDriver> drivers;
    int id = 0;

    for (auto* device_type : device_manager_.getAvailableDeviceTypes())
    {
        if (!device_type) continue;
        odenise::audio::AudioDriver drv;
        drv.id   = id++;
        drv.name = device_type->getTypeName().toStdString();
        drivers.push_back(std::move(drv));

        std::string msg = _("JuceAudioLayer: driver '");
        msg += drivers.back().name;
        msg += "'";
        LOG(msg);
    }

    editor_.setAudioDrivers(std::move(drivers));

    std::string msg = _("JuceAudioLayer: ");
    msg += std::to_string(editor_.audioDrivers().size());
    msg += " driver(s) found";
    LOG(msg);
}

// ----------------------------------------------------------------------------
//  scanDevices -- scanne les interfaces du driver identifie par driver_id.
//  driver_id correspond a l'index dans getAvailableDeviceTypes().
//  Remplace entierement les listes inputs/outputs dans AudioEditor.
// ----------------------------------------------------------------------------
void JuceAudioLayer::scanDevices(int driver_id)
{
    const auto& device_types = device_manager_.getAvailableDeviceTypes();
    if (driver_id < 0 || driver_id >= device_types.size()) {
        std::string msg_err = error(__func__,
            _("JuceAudioLayer: invalid driver_id"),
            std::to_string(driver_id));
        LOG_ERR(msg_err);
        return;
    }

    auto* device_type = device_types[driver_id];
    if (!device_type) return;
    device_type->scanForDevices();

    std::vector<odenise::audio::AudioInterfaceInfo> inputs;
    std::vector<odenise::audio::AudioInterfaceInfo> outputs;
    int input_id  = 0;
    int output_id = 0;

    // --- Entrees ------------------------------------------------------------
    for (const auto& name : device_type->getDeviceNames(true))
    {
        std::unique_ptr<juce::AudioIODevice> dev(
            device_type->createDevice("", name));
        if (!dev) continue;

        const int n_in = dev->getInputChannelNames().size();
        if (n_in <= 0) continue;

        odenise::audio::AudioInterfaceInfo info;
        info.id                  = input_id++;
        info.name                = name.toStdString();
        info.max_input_channels  = n_in;
        info.max_output_channels = 0;

        for (double sr : dev->getAvailableSampleRates())
            info.supported_sample_rates.push_back(static_cast<int>(sr));
        for (int bs : dev->getAvailableBufferSizes())
            info.supported_buffer_sizes.push_back(bs);

        juce::BigInteger ch_mask;
        ch_mask.setRange(0, n_in, true);
        auto err = dev->open(ch_mask, juce::BigInteger{},
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

        std::string msg = _("JuceAudioLayer: input  '");
        msg += info.name;
        msg += "' ch="; msg += std::to_string(n_in);
        msg += " sr="; msg += std::to_string(info.current_sample_rate);
        msg += " buf="; msg += std::to_string(info.current_buffer_size);
        LOG(msg);

        inputs.push_back(std::move(info));
    }

    // --- Sorties ------------------------------------------------------------
    for (const auto& name : device_type->getDeviceNames(false))
    {
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

    editor_.setAudioInputs (std::move(inputs));
    editor_.setAudioOutputs(std::move(outputs));

    std::string msg = _("JuceAudioLayer: scan driver ");
    msg += std::to_string(driver_id);
    msg += " in="; msg += std::to_string(editor_.audioInputs().size());
    msg += " out="; msg += std::to_string(editor_.audioOutputs().size());
    LOG(msg);
}


} // namespace odenise::plugin
