// ============================================================================
//  chain/audio_chain.cpp  --  Implementation de la chaine cablee.
// ============================================================================
#include "audio_chain.h"

namespace odenise {

// ---------------------------------------------------------------------------
//  Fonctions d'execution pre-resolues -- appelees en RT via pointeur.
//  Chacune encode un comportement unique : zero branchement en RT.
// ---------------------------------------------------------------------------

// Appel d'un module : lit depuis e.src (cable au cablage), ecrit dans
// le output_buf() du module. e.src est deja pointe sur le bon buffer.
void AudioChain::exec_module(ChainElement& e, int num_frames) noexcept {
    e.module->process(num_frames);
}

// Transfert H2D (RAM -> device) : CPU vers GPU.
// e.src = pointeur RAM, e.dst = pointeur device, e.stream = cudaStream_t*.
// Le cast vers cudaStream_t est fait dans l'implementation CUDA du backend ;
// ici on passe le stream opaque a une fonction registree au cablage.
// Pour l'etape 3a (CPU seul) : ne devrait pas etre appele.
void AudioChain::exec_transfer_h2d(ChainElement& e, int /*num_frames*/) noexcept {
    // implementation CUDA : cudaMemcpyAsync(e.dst, e.src, e.bytes, H2D, stream)
    // Pour l'instant : chemin CPU de secours (ne devrait pas arriver sans GPU)
    if (e.src && e.dst && e.bytes)
        std::memcpy(e.dst, e.src, e.bytes);
}

// Transfert D2H (device -> RAM) : GPU vers CPU.
void AudioChain::exec_transfer_d2h(ChainElement& e, int /*num_frames*/) noexcept {
    // implementation CUDA : cudaMemcpyAsync(e.dst, e.src, e.bytes, D2H, stream)
    if (e.src && e.dst && e.bytes)
        std::memcpy(e.dst, e.src, e.bytes);
}

// Transfert CPU->CPU (memcpy RAM) : deux modules CPU consecutifs dont les
// buffers ne sont pas contigus (cas rare, mais gere proprement).
void AudioChain::exec_transfer_cpu(ChainElement& e, int /*num_frames*/) noexcept {
    if (e.src && e.dst && e.bytes)
        std::memcpy(e.dst, e.src, e.bytes);
}

// ---------------------------------------------------------------------------
//  resolve_context -- determine le type de contexte du module.
//  Consulte le backend_type_id declare par le module (via ModuleInfo) et
//  verifie que le backend peut fournir ce contexte.
//  Retourne kBackendAny si le module accepte tout (le backend choisit).
// ---------------------------------------------------------------------------
size_t AudioChain::resolve_context(BackendBase*  backend,
                                ModuleBase*   mod) {
    // Le type de contexte est declare dans OdeniseModuleInfoC::backend_type_id.
    // A ce stade, on ne peut pas l'interroger via ModuleBase* seul -- c'est
    // l'engine qui passe l'info lors de l'install (via ChainNode::ctx_type).
    // Cette fonction est un point d'extension pour la phase 3b (CUDA).
    const OdeniseBackendCapsC *bc = backend->caps_c();
    const OdeniseModuleInfoC *mi = mod->info_c();
    if ( bc->backend_type == kBackendAny ){
        return mi->backend_type_id;
    }else if ( bc->backend_type == mi->backend_type_id || mi->backend_type_id == kBackendAny ){
        return bc->backend_type;
    }else{
        return 0;
    }
}

// ---------------------------------------------------------------------------
//  rebuild -- reconstruit la liste plate inactive a partir des noeuds logiques,
//             puis swape atomiquement. Appele hors RT a chaque modification.
// ---------------------------------------------------------------------------
void AudioChain::rebuild(BackendBase* backend) {
    // Index de la liste inactive (celle que le RT ne lit pas actuellement).
    const int inactive = 1 - active_.load(std::memory_order_acquire);
    auto& flat = chain_[inactive];
    // clear() sans shrink_to_fit : conserve la capacite allouee du buffer
    // inactif pour eviter toute reallocation au prochain rebuild. Voulu.
    flat.clear();

    if (nodes_.empty()) {
        // Chaine vide : swap immediat, rien a faire.
        active_.store(inactive, std::memory_order_release);
        recalculate_latency();
        return;
    }

    // Construction de la liste plate :
    //   Pour chaque noeud, on resout ses connexions entrantes (ChainNode::inputs)
    //   en appelant set_input() avec le bon output_buf() du noeud source.
    //   Si une transition de contexte est detectee sur une connexion,
    //   un noeud de transfert H2D ou D2H est insere avant le module.
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        auto& node = nodes_[i];

        // Resout chaque connexion entrante declaree dans node.inputs.
        for (const auto& conn : node.inputs) {
            void* src_buf = nullptr;
            size_t src_ctx = kBackendCPU; // hardware = CPU par defaut

            if (conn.from_loaded_id == 0) {
                // Source hardware : le buffer d'entree est fourni par le backend.
                // set_input() sera appele par le backend lui-meme avant process().
                // Rien a resoudre ici au cablage.
                src_buf = nullptr;
                src_ctx = kBackendCPU;
            } else {
                // Cherche le noeud source par loaded_id.
                for (const auto& src_node : nodes_) {
                    if (src_node.loaded_id == conn.from_loaded_id) {
                        src_buf = src_node.module->output_buf();
                        src_ctx = src_node.ctx_type;
                        break;
                    }
                }
            }

            if (!src_buf) continue; // source hardware ou noeud source introuvable

            // Transition de contexte sur cette connexion.
            if (src_ctx == kBackendCPU && node.ctx_type == kBackendCUDA) {
                ChainElement transfer;
                transfer.execute = &AudioChain::exec_transfer_h2d;
                transfer.src     = src_buf;
                transfer.dst     = node.module->output_buf();
                transfer.bytes   = 0; // resolu par le backend a l'install
                transfer.stream  = nullptr; // stream CUDA : resolu par le backend
                flat.push_back(transfer);
                std::string msg = _("audio_chain: inserted H2D transfer before loaded_id=");
                msg += std::to_string(node.loaded_id);
                LOG(msg);
            } else if (src_ctx == kBackendCUDA && node.ctx_type == kBackendCPU) {
                ChainElement transfer;
                transfer.execute = &AudioChain::exec_transfer_d2h;
                transfer.src     = src_buf;
                transfer.dst     = node.module->output_buf();
                transfer.bytes   = 0;
                transfer.stream  = nullptr;
                flat.push_back(transfer);
                std::string msg = _("audio_chain: inserted D2H transfer before loaded_id=");
                msg += std::to_string(node.loaded_id);
                LOG(msg);
            } else {
                // Meme contexte : cablage direct via set_input().
                node.module->set_input(src_buf);
            }
        }

        // Noeud module.
        ChainElement elem;
        elem.execute = &AudioChain::exec_module;
        elem.module  = node.module;
        flat.push_back(elem);
    }

    // Swap atomique : le RT voit la nouvelle chaine des le prochain bloc.
    active_.store(inactive, std::memory_order_release);

    recalculate_latency();
    std::string msg = _("audio_chain: rebuilt, ");
    msg += std::to_string(flat.size());
    msg += _(" elements, declared latency ");
    msg += std::to_string(declared_latency_);
    msg += _(" samples");
    LOG(msg);
}

// ---------------------------------------------------------------------------
//  connect -- etablit une connexion entre deux ports et rebuild.
// ---------------------------------------------------------------------------
bool AudioChain::connect(BackendBase* backend,
                         size_t from_loaded_id, int from_port_id,
                         size_t to_loaded_id,   int to_port_id) {
    // Cherche le noeud destination.
    for (auto& node : nodes_) {
        if (node.loaded_id != to_loaded_id) continue;

        // Supprime une connexion existante sur ce port d'entree.
        node.inputs.erase(
            std::remove_if(node.inputs.begin(), node.inputs.end(),
                [&](const ChainConnection& c) { return c.to_port_id == to_port_id; }),
            node.inputs.end());

        ChainConnection conn;
        conn.from_loaded_id = from_loaded_id;
        conn.from_port_id   = from_port_id;
        conn.to_port_id     = to_port_id;
        node.inputs.push_back(conn);

        rebuild(backend);
        return true;
    }
    std::string msg_err = error(__func__,
        _("audio_chain: connect unknown to_loaded_id"),
        std::to_string(to_loaded_id));
    LOG_ERR(msg_err);
    return false;
}

// ---------------------------------------------------------------------------
//  disconnect -- supprime la connexion sur un port d'entree et rebuild.
// ---------------------------------------------------------------------------
void AudioChain::disconnect(BackendBase* backend,
                            size_t to_loaded_id, int to_port_id) {
    for (auto& node : nodes_) {
        if (node.loaded_id != to_loaded_id) continue;
        node.inputs.erase(
            std::remove_if(node.inputs.begin(), node.inputs.end(),
                [&](const ChainConnection& c) { return c.to_port_id == to_port_id; }),
            node.inputs.end());
        rebuild(backend);
        return;
    }
}
// ---------------------------------------------------------------------------
//  recalculate_latency -- somme les latences declarees de tous les modules.
// ---------------------------------------------------------------------------
void AudioChain::recalculate_latency() {
    int total = 0;
    for (const auto& node : nodes_)
        total += node.module->latency_samples();
    declared_latency_ = total;
    if (on_latency_changed)
        on_latency_changed(on_latency_changed_user, declared_latency_);
}

std::vector<ModuleInfo> AudioChain::get_chain() const {
    std::vector<ModuleInfo> out;
    out.reserve(nodes_.size());
    for (const auto& node : nodes_) {
        const OdeniseModuleInfoC* c = node.module->info_c();
        if (!c) continue;
        ModuleInfo mi;
        mi.id              = c->id;
        mi.kind            = static_cast<ModuleKind>(c->kind);
        mi.name            = c->name        ? c->name        : "";
        mi.description     = c->description ? c->description : "";
        mi.needs_gpu       = (c->needs_gpu != 0);
        mi.backend_type_id = c->backend_type_id;
        mi.ports           = c->ports;
        mi.port_count      = c->port_count;
        mi.inputs          = node.inputs; // connexions entrantes
        out.push_back(mi);
    }
    return out;
}
// ---------------------------------------------------------------------------
//  install -- installe un module a la position donnee.
// ---------------------------------------------------------------------------
bool AudioChain::install(BackendBase*    backend,
                         BackendContext* ctx,
                         ModuleBase*     mod,
                         ModuleKind      kind,
                         size_t          loaded_id) {
    return insert(backend, ctx, mod, kind, nodes_.size(), loaded_id);
}

// ---------------------------------------------------------------------------
//  insert -- alias semantique de install (insere sans remplacer).
// ---------------------------------------------------------------------------
bool AudioChain::insert(BackendBase*    backend,
                        BackendContext* ctx,
                        ModuleBase*     mod,
                        ModuleKind      kind,
                        size_t          position,
                        size_t          loaded_id) {
    if (!mod) return false;

    // Determine le contexte du module.
    const size_t ctx_type = resolve_context(backend, mod);
    // unsupported mix
    if (ctx_type == 0) return false;

    // Installation sur le contexte backend : fournit le scratch buffer
    // et le stream de calcul au module via BackendContext.
    if (!mod->install(ctx)) {
        std::string msg_err = error(__func__,
            _("audio_chain: module install failed"),
            std::string(_("kind=")) + kindName(kind)
            + _(" position=") + std::to_string(position));
        LOG_ERR(msg_err);
        return false;
    }

    // Insere le noeud logique a la bonne position.
    ChainNode node;
    node.module   = mod;
    node.kind     = kind;
    node.position = position;
    node.ctx_type = ctx_type;
    node.loaded_id = loaded_id;

    auto it = nodes_.begin() + position;
    nodes_.insert(it, node);
    // Mettre à jour tous les positions = index
    for (size_t i = 0; i < nodes_.size(); ++i) {
        nodes_[i].position = i;
    }

    rebuild(backend);
    return true;
}

// ---------------------------------------------------------------------------
//  replace -- remplace le module a la position donnee.
// ---------------------------------------------------------------------------
bool AudioChain::replace(BackendBase*    backend,
                         BackendContext* ctx,
                         ModuleBase*     mod,
                         ModuleKind      kind,
                         size_t          position,
                         size_t          loaded_id) {

    auto it = nodes_.begin() + position;
    if (it != nodes_.end()) {
        it->module->uninstall(nullptr);
        nodes_.erase(it);
    }

    return insert(backend, ctx, mod, kind, position, loaded_id);
}

// ---------------------------------------------------------------------------
//  remove -- retire le module a la position donnee.
// ---------------------------------------------------------------------------
void AudioChain::remove(BackendBase* backend, size_t position) noexcept {
    auto it = nodes_.begin() + position;
    if (it == nodes_.end()) return;
    it->module->uninstall(nullptr);
    nodes_.erase(it);

    rebuild(backend);
}

// ---------------------------------------------------------------------------
//  process -- execution RT. Appelee par le backend a chaque bloc.
//  Iteration sur la liste plate active. Zero decision, zero allocation.
// ---------------------------------------------------------------------------
void AudioChain::process(int num_frames) noexcept {
    const int idx = active_.load(std::memory_order_acquire);
    for (auto& e : chain_[idx])
        e.execute(e, num_frames);
}

} // namespace odenise::chain
