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
//  tryDestroyDevice -- detruit un AudioIODevice via un pointeur opaque.
//
//  Fonction C pure (pas de C++ dans le scope) pour contourner MSVC C2712 :
//  __try/__except est interdit dans toute fonction ayant des objets C++ a
//  destructeur dans le meme scope de compilation. En isolant le delete dans
//  une fonction C, on satisfait cette contrainte.
//
//  Retourne true si la destruction s'est passee sans exception SEH.
// ----------------------------------------------------------------------------
#if defined(_MSC_VER)
static int tryDestroyDevice(void* raw_dev)
{
    __try {
        delete static_cast<juce::AudioIODevice*>(raw_dev);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
#endif

// ----------------------------------------------------------------------------
//  probeDevice -- interroge un AudioIODevice sans open().
//
//  Cree le device, lit ses capacites, puis le detruit via tryDestroyDevice
//  (protege SEH sur MSVC). Si le device est absent ou leve une exception a
//  la destruction (ex. ASIO hors tension), retourne un AudioInterfaceInfo
//  vide (max_input_channels = max_output_channels = 0).
// ----------------------------------------------------------------------------
static odenise::audio::AudioInterfaceInfo probeDevice(
    juce::AudioIODeviceType* device_type,
    const juce::String&      input_name,
    const juce::String&      output_name,
    bool                     want_inputs)
{
    odenise::audio::AudioInterfaceInfo info;

    juce::AudioIODevice* dev = device_type->createDevice(output_name, input_name);
    if (dev == nullptr) return info;

    const int n_in  = dev->getInputChannelNames().size();
    const int n_out = dev->getOutputChannelNames().size();
    const int n     = want_inputs ? n_in : n_out;

    if (n > 0) {
        info.max_input_channels  = n_in;
        info.max_output_channels = n_out;

        for (double sr : dev->getAvailableSampleRates())
            info.supported_sample_rates.push_back(static_cast<int>(sr));
        for (int bs : dev->getAvailableBufferSizes())
            info.supported_buffer_sizes.push_back(bs);

        info.current_buffer_size = dev->getDefaultBufferSize();
        info.current_sample_rate = info.supported_sample_rates.empty()
                                   ? 0
                                   : info.supported_sample_rates.front();
        info.current_bit_depth   = 0; // non disponible sans open()

        const auto ch_names = want_inputs
                              ? dev->getInputChannelNames()
                              : dev->getOutputChannelNames();
        for (int ch = 0; ch < n; ++ch)
            info.channel_names.push_back(ch_names[ch].toStdString());
    }

    // Destruction protegee -- un device ASIO hors tension peut lever une
    // exception SEH a la destruction (handle COM invalide).
#if defined(_MSC_VER)
    if (!tryDestroyDevice(dev)) {
        // Exception SEH a la destruction : device invalide, on rejette les infos.
        info = odenise::audio::AudioInterfaceInfo{};
    }
#else
    delete dev;
#endif

    return info;
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
        auto info = probeDevice(device_type, name, "", true);
        if (info.max_input_channels <= 0) {
            std::string msg_err = error(__func__,
                _("JuceAudioLayer: input device unavailable"),
                name.toStdString());
            LOG_ERR(msg_err);
            continue;
        }

        info.id   = input_id++;
        info.name = name.toStdString();
        info.max_output_channels = 0;

        std::string msg = _("JuceAudioLayer: input  '");
        msg += info.name;
        msg += "' ch="; msg += std::to_string(info.max_input_channels);
        msg += " sr="; msg += std::to_string(info.current_sample_rate);
        msg += " buf="; msg += std::to_string(info.current_buffer_size);
        LOG(msg);

        inputs.push_back(std::move(info));
    }

    // --- Sorties ------------------------------------------------------------
    for (const auto& name : device_type->getDeviceNames(false))
    {
        auto info = probeDevice(device_type, "", name, false);
        if (info.max_output_channels <= 0) {
            std::string msg_err = error(__func__,
                _("JuceAudioLayer: output device unavailable"),
                name.toStdString());
            LOG_ERR(msg_err);
            continue;
        }

        info.id   = output_id++;
        info.name = name.toStdString();
        info.max_input_channels = 0;

        std::string msg = _("JuceAudioLayer: output '");
        msg += info.name;
        msg += "' ch="; msg += std::to_string(info.max_output_channels);
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
