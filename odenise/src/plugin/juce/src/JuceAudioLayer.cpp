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
    //processor_.engine()->pause_backend();    
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
//  scanDevices -- liste les noms des interfaces via getDeviceNames() uniquement.
//  Aucun createDevice() -- pas d'ouverture de driver ASIO.
//  Les capacites (sample rates, buffers, channel names) restent a zero jusqu'a
//  ce que probeDevice() soit appele sur selection d'une interface.
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

    for (const auto& name : device_type->getDeviceNames(true))
    {
        odenise::audio::AudioInterfaceInfo info;
        info.id                 = input_id++;
        info.name               = name.toStdString();
        info.max_input_channels = 1; // inconnu avant probe -- non nul pour apparaitre dans le combo
        inputs.push_back(std::move(info));
    }

    for (const auto& name : device_type->getDeviceNames(false))
    {
        odenise::audio::AudioInterfaceInfo info;
        info.id                  = output_id++;
        info.name                = name.toStdString();
        info.max_output_channels = 1; // inconnu avant probe -- non nul pour apparaitre dans le combo
        outputs.push_back(std::move(info));
    }

    editor_.setAudioInputs (std::move(inputs));
    editor_.setAudioOutputs(std::move(outputs));
    //processor_.engine()->pause_backend();  
    std::string msg = _("JuceAudioLayer: scan driver ");
    msg += std::to_string(driver_id);
    msg += " in="; msg += std::to_string(editor_.audioInputs().size());
    msg += " out="; msg += std::to_string(editor_.audioOutputs().size());
    LOG(msg);
}

// ----------------------------------------------------------------------------
//  probeDevice -- interroge les capacites d'une interface via createDevice().
//  Appele uniquement sur selection d'une interface dans le combo UI.
//  Met a jour l'AudioInterfaceInfo dans AudioEditor via updateAudioInput/Output.
// ----------------------------------------------------------------------------
void JuceAudioLayer::probeDevice(int driver_id, int interface_id,
                                 const std::string& name, bool want_inputs)
{

    const auto& device_types = device_manager_.getAvailableDeviceTypes();
    if (driver_id < 0 || driver_id >= device_types.size()) return;

    auto* device_type = device_types[driver_id];
    if (!device_type) return;

    const juce::String jname(name);
    juce::AudioIODevice* dev = want_inputs
        ? device_type->createDevice("", jname)
        : device_type->createDevice(jname, "");

    if (dev == nullptr) {
        std::string msg_err = error(__func__,
            _("JuceAudioLayer: createDevice returned null"),
            name);
        LOG_ERR(msg_err);
        return;
    }
               
    odenise::audio::AudioInterfaceInfo info;
    info.id   = interface_id;
    info.name = name;

    const int n_in  = dev->getInputChannelNames().size();
    const int n_out = dev->getOutputChannelNames().size();
    info.max_input_channels  = n_in;
    info.max_output_channels = n_out;

    for (double sr : dev->getAvailableSampleRates())
        info.supported_sample_rates.push_back(static_cast<int>(sr));
    for (int bs : dev->getAvailableBufferSizes())
        info.supported_buffer_sizes.push_back(bs);

    const int n = want_inputs ? n_in : n_out;
    const auto ch_names = want_inputs
                          ? dev->getInputChannelNames()
                          : dev->getOutputChannelNames();
    for (int ch = 0; ch < n; ++ch)
        info.channel_names.push_back(ch_names[ch].toStdString());

    // Valeurs courantes rapportees par le driver sans open().
    // Aucun fallback -- on affiche uniquement ce que le driver retourne.
    info.current_sample_rate = static_cast<int>(dev->getCurrentSampleRate());

    juce::BigInteger ch_mask;
    ch_mask.setRange(0, n, true);
    auto err = dev->open(ch_mask, juce::BigInteger{},
                     info.current_sample_rate,
                     dev->getDefaultBufferSize());
    if(! err.isEmpty()) {
        LOG("Cannot open device");
        return;
    }  

    if( want_inputs ){
        info.input_latency_samples =  dev->getInputLatencyInSamples();
    }else{
        info.output_latency_samples =  dev->getOutputLatencyInSamples();
    }
    info.default_buffer_size = dev->getDefaultBufferSize();
    info.current_buffer_size = dev->getCurrentBufferSizeSamples();
    info.current_bit_depth   = dev->getCurrentBitDepth();
    info.xrun_count          = dev->getXRunCount();

    dev->close();    
    delete dev;

    if (want_inputs)
        editor_.updateAudioInput(interface_id, info);
    else
        editor_.updateAudioOutput(interface_id, info);

    std::string msg = want_inputs
                      ? _("JuceAudioLayer: probed input  '")
                      : _("JuceAudioLayer: probed output '");
    msg += name;
    msg += "' ch="; msg += std::to_string(n);
    msg += " current_sample_rate="; msg += std::to_string(info.current_sample_rate);
    msg += " input_latency_samples="; msg += std::to_string(info.input_latency_samples);
    msg += " output_latency_samples="; msg += std::to_string(info.output_latency_samples);
    msg += " default_buffer_size="; msg += std::to_string(info.default_buffer_size);
    msg += " current_buffer_size="; msg += std::to_string(info.current_buffer_size);
    msg += " current_bit_depth="; msg += std::to_string(info.current_bit_depth);
    msg += " xrun_count="; msg += std::to_string(info.xrun_count);
    LOG(msg);

}

} // namespace odenise::plugin
