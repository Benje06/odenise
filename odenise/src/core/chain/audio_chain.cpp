// ============================================================================
//  chain/audio_chain.cpp  --  Implementation de la chaine cablee.
// ============================================================================
#include "chain/audio_chain.h"
#include "tools/logger.h"

#include <algorithm>
#include <cstring>      // std::memcpy

namespace ns::chain {

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
int AudioChain::resolve_context(BackendBase*  backend,
                                ModuleBase*   /*mod*/) {
    // Le type de contexte est declare dans OdeniseModuleInfoC::backend_type_id.
    // A ce stade, on ne peut pas l'interroger via ModuleBase* seul -- c'est
    // l'engine qui passe l'info lors de l'install (via ChainNode::ctx_type).
    // Cette fonction est un point d'extension pour la phase 3b (CUDA).
    (void)backend;
    return kBackendAny;
}

// ---------------------------------------------------------------------------
//  rebuild -- reconstruit la liste plate inactive a partir des noeuds logiques,
//             puis swape atomiquement. Appele hors RT a chaque modification.
// ---------------------------------------------------------------------------
void AudioChain::rebuild(BackendBase* backend) {
    // Index de la liste inactive (celle que le RT ne lit pas actuellement).
    const int inactive = 1 - active_.load(std::memory_order_acquire);
    auto& flat = chain_[inactive];
    flat.clear();

    if (nodes_.empty()) {
        // Chaine vide : swap immediat, rien a faire.
        active_.store(inactive, std::memory_order_release);
        recalculate_latency();
        return;
    }

    // Construction de la liste plate :
    //   Pour chaque noeud, on regarde si une transition de contexte est
    //   necessaire avec le noeud precedent. Si oui, on insere un noeud
    //   de transfert pre-resolu avant le module.
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        auto& node = nodes_[i];

        if (i > 0) {
            auto& prev = nodes_[i - 1];
            const int prev_ctx = prev.ctx_type;
            const int curr_ctx = node.ctx_type;

            // Transition CPU -> GPU : H2D
            if (prev_ctx == kBackendCPU && curr_ctx == kBackendCUDA) {
                ChainElement transfer;
                transfer.execute = &AudioChain::exec_transfer_h2d;
                transfer.src     = prev.module->output_buf();
                transfer.dst     = node.module->output_buf(); // sera set_input
                transfer.bytes   = 0; // resolu par le backend a l'install
                transfer.stream  = backend ? backend->caps().is_gpu ?
                                   nullptr : nullptr : nullptr; // stream CUDA
                flat.push_back(transfer);
                std::string msg = _("audio_chain: inserted H2D transfer at position ");
                msg += std::to_string(i);
                LOG(msg);
            }
            // Transition GPU -> CPU : D2H
            else if (prev_ctx == kBackendCUDA && curr_ctx == kBackendCPU) {
                ChainElement transfer;
                transfer.execute = &AudioChain::exec_transfer_d2h;
                transfer.src     = prev.module->output_buf();
                transfer.dst     = node.module->output_buf();
                transfer.bytes   = 0;
                transfer.stream  = nullptr;
                flat.push_back(transfer);
                std::string msg = _("audio_chain: inserted D2H transfer at position ");
                msg += std::to_string(i);
                LOG(msg);
            }
            // Meme contexte : cablage direct, zero copie.
            // set_input est appele sur le module pour pointer sur le bon buffer.
            else {
                node.module->set_input(prev.module->output_buf());
            }
        }

        // Noeud module
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

// ---------------------------------------------------------------------------
//  install -- installe un module a la position donnee.
// ---------------------------------------------------------------------------
bool AudioChain::install(BackendBase*  backend,
                         ModuleBase*   mod,
                         ModuleKind    kind,
                         int           position) {
    if (!mod) return false;

    // Determine le contexte du module.
    const int ctx = resolve_context(backend, mod);

    // Installation sur le contexte backend.
    // Le backend fournit son BackendContext (scratch, stream) ; le module
    // y alloue ses buffers internes et valide la compatibilite.
    ns::BackendContext* bctx = backend ? backend->context() : nullptr;
    if (!mod->install(bctx)) {
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
    node.ctx_type = ctx;

    // Tri par position pour garantir l'ordre.
    auto it = std::lower_bound(nodes_.begin(), nodes_.end(), node,
        [](const ChainNode& a, const ChainNode& b) {
            return a.position < b.position;
        });
    nodes_.insert(it, node);

    rebuild(backend);
    return true;
}

// ---------------------------------------------------------------------------
//  insert -- alias semantique de install (insere sans remplacer).
// ---------------------------------------------------------------------------
bool AudioChain::insert(BackendBase*  backend,
                        ModuleBase*   mod,
                        ModuleKind    kind,
                        int           position) {
    // Decale les positions des noeuds existants a partir de 'position'.
    for (auto& n : nodes_)
        if (n.position >= position)
            ++n.position;

    return install(backend, mod, kind, position);
}

// ---------------------------------------------------------------------------
//  replace -- remplace le module a la position donnee.
// ---------------------------------------------------------------------------
bool AudioChain::replace(BackendBase*  backend,
                         ModuleBase*   mod,
                         ModuleKind    kind,
                         int           position) {
    // Retire l'ancien module a cette position s'il existe.
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [position](const ChainNode& n) { return n.position == position; });

    if (it != nodes_.end()) {
        it->module->uninstall(nullptr);
        nodes_.erase(it);
    }

    return install(backend, mod, kind, position);
}

// ---------------------------------------------------------------------------
//  remove -- retire le module a la position donnee.
// ---------------------------------------------------------------------------
void AudioChain::remove(BackendBase* backend,
                        ModuleKind   /*kind*/,
                        int          position) noexcept {
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [position](const ChainNode& n) { return n.position == position; });

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

} // namespace ns::chain
