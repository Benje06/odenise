// ============================================================================
//  src/core/audio/AudioEditor.cpp
// ============================================================================
#include "common.h"
#include "AudioProcessor.h"
#include "AudioEditor.h"

namespace odenise::audio {

// ----------------------------------------------------------------------------
AudioEditor::AudioEditor(AudioProcessor* processor)
    : processor_(processor)
    , engine_(processor ? processor->engine() : nullptr)
    , cfg_(processor->get_config()) {}

// ----------------------------------------------------------------------------
AudioEditor::~AudioEditor() {}

// ---------------------------------------------------------------------------
//  Drivers audio
// ----------------------------------------------------------------------------

void AudioEditor::setAudioDrivers(std::vector<AudioDriver> drivers) {
    drivers_ = std::move(drivers);
}

const std::vector<AudioDriver>& AudioEditor::audioDrivers() const noexcept {
    return drivers_;
}

bool AudioEditor::selectDriver(int id) {
    for (const auto& drv : drivers_) {
        if (drv.id == id) {
            selected_driver_id_ = id;
            // reset interfaces et selections sur changement de driver
            inputs_.clear();
            outputs_.clear();
            selected_input_id_  = -1;
            selected_input_ch_  =  0;
            selected_output_id_ = -1;
            selected_output_ch_ =  0;
            return true;
        }
    }
    std::string msg_err = error(__func__,
        _("AudioEditor: unknown driver id"),
        std::to_string(id));
    LOG_ERR(msg_err);
    return false;
}

// ----------------------------------------------------------------------------
//  Interfaces d'entree
//  TODO: update cfg_
// ----------------------------------------------------------------------------

const std::vector<AudioInterfaceInfo>& AudioEditor::audioInputs() const noexcept {
    return inputs_;
}

void AudioEditor::setAudioInputs(std::vector<AudioInterfaceInfo> inputs) {
    inputs_ = std::move(inputs);
}

bool AudioEditor::updateAudioInput(int id, const AudioInterfaceInfo& info) {
    for (auto& iface : inputs_) {
        if (iface.id == id) {
            iface = info;
            return true;
        }
    }
    std::string msg_err = error(__func__,
        _("AudioEditor: unknown input interface id"),
        std::to_string(id));
    LOG_ERR(msg_err);
    return false;
}

bool AudioEditor::selectInputInterface(int id) {
    for (const auto& iface : inputs_) {
        if (iface.id == id) {
            selected_input_id_ = id;
            selected_input_ch_ = 0;  // reset canal a la selection d'interface
            return true;
        }
    }
    std::string msg_err = error(__func__,
        _("AudioEditor: unknown input interface id"),
        std::to_string(id));
    LOG_ERR(msg_err);
    return false;
}

bool AudioEditor::selectInputChannel(int channel) {
    for (const auto& iface : inputs_) {
        if (iface.id == selected_input_id_) {
            if (channel < 0 || channel >= iface.max_input_channels) {
                std::string msg_err = error(__func__,
                    _("AudioEditor: input channel out of range"),
                    std::to_string(channel));
                LOG_ERR(msg_err);
                return false;
            }
            selected_input_ch_ = channel;
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
//  Interfaces de sortie
// ----------------------------------------------------------------------------

void AudioEditor::setAudioOutputs(std::vector<AudioInterfaceInfo> outputs) {
    outputs_ = std::move(outputs);
}
const std::vector<AudioInterfaceInfo>& AudioEditor::audioOutputs() const noexcept {
    return outputs_;
}
bool AudioEditor::updateAudioOutput(int id, const AudioInterfaceInfo& info) {
    for (auto& iface : outputs_) {
        if (iface.id == id) {
            iface = info;
            return true;
        }
    }
    std::string msg_err = error(__func__,
        _("AudioEditor: unknown output interface id"),
        std::to_string(id));
    LOG_ERR(msg_err);
    return false;
}
bool AudioEditor::selectOutputInterface(int id) {
    for (const auto& iface : outputs_) {
        if (iface.id == id) {
            selected_output_id_ = id;
            selected_output_ch_ = 0;  // reset canal a la selection d'interface
            return true;
        }
    }
    std::string msg_err = error(__func__,
        _("AudioEditor: unknown output interface id"),
        std::to_string(id));
    LOG_ERR(msg_err);
    return false;
}
bool AudioEditor::selectOutputChannel(int channel) {
    for (const auto& iface : outputs_) {
        if (iface.id == selected_output_id_) {
            if (channel < 0 || channel >= iface.max_output_channels) {
                std::string msg_err = error(__func__,
                    _("AudioEditor: output channel out of range"),
                    std::to_string(channel));
                LOG_ERR(msg_err);
                return false;
            }
            selected_output_ch_ = channel;
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
//  Backend
// ----------------------------------------------------------------------------
void AudioEditor::get_backends(){
    backends_ = std::move(engine_->modules(odenise::ModuleKind::ComputeBackend));
};
const  std::vector<odenise::ModuleInfo>& AudioEditor::backends() const noexcept{
    return backends_;
};
bool AudioEditor::selectBackend(size_t bcknd_combo_id){
    if (!engine_) return false;

    selected_backend_id_ = bcknd_combo_id;
    processor_->bindBackend(bcknd_combo_id,*cfg_);
    std::string msg = _("AudioEditor: selected backend available_id=");
    msg += std::to_string(bcknd_combo_id);
    LOG(msg);

    return true;
}

// ----------------------------------------------------------------------------
//  Module (available)
// ----------------------------------------------------------------------------

void AudioEditor::get_modules(){
    modules_ = std::move(engine_->modules());
};
std::string AudioEditor::get_module_info(int module_id){
    return modules_[module_id].description;
}
const  std::vector<odenise::ModuleInfo>& AudioEditor::modules() const noexcept{
    return modules_;
};
bool AudioEditor::selectModule(int mods_combo_id, const RuntimeConfig& cfg){
    if (insertModule(mods_combo_id, 0, cfg)){
        selected_module_id_ = mods_combo_id;
    } 
    // TODO: return insertmoduel return
    return true;
}

// ----------------------------------------------------------------------------
//  Module (loaded)
// ----------------------------------------------------------------------------
void AudioEditor::get_loaded_modules() {
    if (!engine_) return;
    loaded_modules_ = engine_->loaded_modules();
}
 
const std::vector<ModuleInfo>& AudioEditor::loaded_modules() const noexcept {
    return loaded_modules_;
}
// ----------------------------------------------------------------------------
//  Configuration de la chaine
// ----------------------------------------------------------------------------

bool AudioEditor::insertModule(size_t available_id, size_t position,
                               const RuntimeConfig& cfg) {
    if (!processor_) return false;
    const bool status = processor_->insertModule(available_id, position, cfg);
    if (status) {
        get_loaded_modules();
    }
    return status;
}

bool AudioEditor::replaceModule(size_t available_id, size_t position,
                                const RuntimeConfig& cfg) {
    if (!processor_) return false;
    const bool status = processor_->replaceModule(available_id, position, cfg);
    if (status) {
        get_loaded_modules();
    }
    return status;
}

void AudioEditor::removeModule(size_t position) {
    if (!processor_) return;
    processor_->unBindModule(position);
    get_loaded_modules();
}

bool AudioEditor::reconfigureModule(size_t loaded_id, const RuntimeConfig& cfg) {
    if (!processor_) return false;
    return processor_->reconfigureModule(loaded_id, cfg);
}


// ----------------------------------------------------------------------------
//  Chaine
// ----------------------------------------------------------------------------
ChainDesc AudioEditor::get_chain() const {
    ChainDesc desc;
    if (!engine_) return desc;

    // Noeuds : enrichis par engine_->get_chain() qui remonte les ModuleInfo
    // avec leurs connexions entrantes (inputs) depuis l'AudioChain.
    desc.nodes = engine_->get_chain();

    // Connexions : liste plate construite depuis les inputs de chaque noeud,
    // pour faciliter le parcours par la couche UI sans re-parcourir les nodes.
    for (const auto& mi : desc.nodes) {
        for (const auto& conn : mi.inputs) {
            ChainConnection c;
            c.from_loaded_id = conn.from_loaded_id;
            c.from_port_id   = conn.from_port_id;
            c.to_port_id     = conn.to_port_id;
            c.to_port_id     = conn.to_port_id;
            desc.connections.push_back(c);
        }
    }
    return desc;
}

bool AudioEditor::connectPorts(size_t from_loaded_id, int from_port_id,
                               size_t to_loaded_id,   int to_port_id) {
    if (!processor_) return false;
    return processor_->connectPorts(from_loaded_id, from_port_id,
                                    to_loaded_id,   to_port_id);
}

void AudioEditor::disconnectPort(size_t to_loaded_id, int to_port_id) {
    if (!processor_) return;
    processor_->disconnectPort(to_loaded_id, to_port_id);
}

// ----------------------------------------------------------------------------
//  Monitoring
// ----------------------------------------------------------------------------

const LatencyInfo& AudioEditor::latencyInfo() const noexcept {
    return cached_latency_;
}
const ProcessingStats& AudioEditor::processingStats() const noexcept {
    return cached_stats_;
}
BackendCaps AudioEditor::backendCaps() const {
    if (!engine_) return {};
    return engine_->backendCaps();
}

void AudioEditor::poll() {
    if (!engine_) return;
    cached_latency_ = engine_->latencyInfo();
    cached_stats_   = engine_->processingStats();
}

} // namespace odenise::audio
