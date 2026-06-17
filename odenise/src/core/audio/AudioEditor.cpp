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
        rebuildGraph();
    }
    return status;
}
bool AudioEditor::replaceModule(size_t available_id, size_t position,
                                const RuntimeConfig& cfg) {
    if (!processor_) return false;
    const bool status = processor_->replaceModule(available_id, position, cfg);
    if (status) {
        get_loaded_modules();
        rebuildGraph();
    }
    return status;
}
void AudioEditor::removeModule(size_t position) {
    if (!processor_) return;
    processor_->unBindModule(position);
    get_loaded_modules();
    rebuildGraph();
}
bool AudioEditor::reconfigureModule(size_t loaded_id, const RuntimeConfig& cfg) {
    if (!processor_) return false;
    return processor_->reconfigureModule(loaded_id, cfg);
}


// ----------------------------------------------------------------------------
//  Graphe UI
// ----------------------------------------------------------------------------
 
void AudioEditor::rebuildGraph() {
    graph_.nodes.clear();
    graph_.edges.clear();
    if (!engine_) return;
 
    // Reconstruit les noeuds depuis la liste des modules charges.
    // Les positions UI sont initialisees en disposition horizontale par defaut
    // (l'utilisateur peut ensuite les deplacer).

    int x = 20;
    for (const auto& info : engine_->get_chain()) {
        NodeDesc nd;
        nd.loaded_id = info.id;
        nd.x         = x;
        nd.y         = 60;
        graph_.nodes.push_back(nd);
        x += 160; // espacement horizontal initial
    }
 
    // Reconstruit les aretes depuis le cablage serie courant de l'AudioChain.
    // Dans le modele serie (cablage lineaire), chaque noeud[i].audio_out
    // est connecte a noeud[i+1].audio_in.
    for (size_t i = 0; i + 1 < graph_.nodes.size(); ++i) {
        EdgeDesc ed;
        ed.from.node_loaded_id = graph_.nodes[i].loaded_id;
        ed.from.port_id        = 1; // audio_out (id=1 par convention)
        ed.to.node_loaded_id   = graph_.nodes[i + 1].loaded_id;
        ed.to.port_id          = 0; // audio_in  (id=0 par convention)
        graph_.edges.push_back(ed);
    }
}
 
void AudioEditor::moveNode(size_t loaded_id, int x, int y) {
    for (auto& nd : graph_.nodes) {
        if (nd.loaded_id == loaded_id) {
            nd.x = x;
            nd.y = y;
            return;
        }
    }
}
 
bool AudioEditor::connectPorts(size_t from_loaded_id, int from_port_id,
                               size_t to_loaded_id,   int to_port_id) {
    // Verifie que les deux noeuds existent dans le graphe.
    bool from_ok = false, to_ok = false;
    for (const auto& nd : graph_.nodes) {
        if (nd.loaded_id == from_loaded_id) from_ok = true;
        if (nd.loaded_id == to_loaded_id)   to_ok   = true;
    }
    if (!from_ok || !to_ok) {
        std::string msg_err = error(__func__,
            _("AudioEditor: connectPorts unknown loaded_id"),
            std::to_string(from_loaded_id) + " -> " + std::to_string(to_loaded_id));
        LOG_ERR(msg_err);
        return false;
    }
 
    // Supprime une eventuelle arete existante arrivant sur le meme port d'entree.
    disconnectPort(to_loaded_id, to_port_id);
 
    EdgeDesc ed;
    ed.from.node_loaded_id = from_loaded_id;
    ed.from.port_id        = from_port_id;
    ed.to.node_loaded_id   = to_loaded_id;
    ed.to.port_id          = to_port_id;
    graph_.edges.push_back(ed);
 
    // TODO Phase 3b : pousser le recablage dans AudioChain via processor_.
    return true;
}
 
void AudioEditor::disconnectPort(size_t to_loaded_id, int to_port_id) {
    graph_.edges.erase(
        std::remove_if(graph_.edges.begin(), graph_.edges.end(),
            [&](const EdgeDesc& ed) {
                return ed.to.node_loaded_id == to_loaded_id
                    && ed.to.port_id        == to_port_id;
            }),
        graph_.edges.end());
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
