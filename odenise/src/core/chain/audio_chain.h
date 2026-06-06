// ============================================================================
//  chain/audio_chain.h  --  Chaine de traitement audio cablee.
//
//  Header INTERNE au coeur : non installe, non expose dans l'API publique.
//  Gere la liste plate d'elements executes en RT (modules + transferts),
//  le double-buffer pour le recablage a chaud, et l'accumulation de latence.
//
//  Principe :
//    - Au cablage (hors RT) : tous les pointeurs src/dst et les fonctions
//      d'execution sont resolus une fois pour toutes en ChainElement.
//    - En RT : iteration sequentielle sur la liste plate, un appel de
//      pointeur de fonction par element. Zero decision, zero allocation.
//    - Recablage a chaud : la nouvelle chaine est construite dans le buffer
//      inactif, puis swappee atomiquement. Le RT ne voit jamais d'etat
//      intermediaire.
// ============================================================================
#pragma once

#include "engine.h"

#include <algorithm>
#include <cstring>      // std::memcpy
#include <atomic>
#include <cstddef>
#include <vector>

// ---------------------------------------------------------------------------
//  Visibilite des symboles de libodenise_chain.
//  ODENISE_CHAIN_API : exporte depuis la lib, importe chez le consommateur.
//  Meme patron que ODENISE_API / LOGGER dans le reste du projet.
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(__MINGW32__)
    #ifdef CHAIN_EXPORTS
        #define CHAIN __declspec(dllexport)
    #elif defined(CHAIN_IMPORTS)
        #define CHAIN __declspec(dllimport)
    #else
        #define CHAIN
    #endif
#else
    #ifdef CHAIN_EXPORTS
        #define CHAIN __attribute__((visibility("default")))
    #else
        #define CHAIN
    #endif
#endif

namespace ns::chain {

// ---------------------------------------------------------------------------
//  ChainElement -- un element atomique de la liste plate RT.
//
//  Chaque element embarque un pointeur de fonction execute() resolu au
//  cablage. En RT : e.execute(e, num_frames). Zero branchement, zero if.
//
//  Deux types d'elements :
//    MODULE   : appelle module->process(num_frames) avec le src cable.
//    TRANSFER : cudaMemcpyAsync ou memcpy selon la direction resolue.
//
//  Les champs src/dst/bytes/stream sont pre-resolus au cablage. Le type
//  n'est pas stocke en RT : c'est le pointeur de fonction qui encode le
//  comportement.
// ---------------------------------------------------------------------------
struct ChainElement {
    // Pointeur de fonction d'execution -- resolu au cablage, appele en RT.
    // Signature : (element courant, nombre de frames a traiter)
    void (*execute)(ChainElement&, int num_frames) noexcept = nullptr;

    // --- donnees pre-resolues au cablage ---

    // MODULE : module a appeler, entree cablee
    ModuleBase* module = nullptr;   // non nul si element de type MODULE

    // TRANSFER : src, dst, taille, direction, stream CUDA (ou nullptr CPU)
    void*       src    = nullptr;
    void*       dst    = nullptr;
    std::size_t bytes  = 0;
    void*       stream = nullptr;   // cudaStream_t* ou nullptr (CPU memcpy)
};

// ---------------------------------------------------------------------------
//  AudioChain -- gestionnaire de la chaine cablee.
//
//  Possede deux listes plates (double buffer). L'une est active en RT,
//  l'autre est libre pour le recablage. Le swap est atomique.
//
//  Utilisation :
//    // hors RT -- construction de la chaine
//    chain.install(backend, mod, kind, position);
//    chain.install(backend, mod2, kind2, position2);
//
//    // RT -- par le backend
//    chain.process(in, out, num_frames);
//
//    // hors RT -- recablage a chaud
//    chain.insert(backend, mod3, kind3, position3);
//    chain.replace(backend, mod4, kind4, position4);
//    chain.remove(backend, position);
// ---------------------------------------------------------------------------
class AudioChain {
public:
    AudioChain() = default;
    ~AudioChain() = default;

    AudioChain(const AudioChain&)            = delete;
    AudioChain& operator=(const AudioChain&) = delete;

    // -----------------------------------------------------------------------
    //  Cablage -- hors RT.
    //  Le backend est requis pour acceder aux contextes (CPU/GPU) et aux
    //  buffers de transfert pre-alloues.
    // -----------------------------------------------------------------------

    // Installe un module a la position donnee dans la chaine.
    // ctx est le BackendContext fourni par le backend au module (scratch, stream).
    // Recable les voisins, insere un noeud de transfert si la transition
    // de contexte l'exige (CPU->GPU ou GPU->CPU).
    // Retourne false si le module refuse l'installation (incompatibilite).
    CHAIN bool install(BackendBase*    backend,
                       BackendContext* ctx,
                       ModuleBase*     mod,
                       ModuleKind      kind,
                       int             position);

    // Insere un module a la position donnee (decale les suivants).
    CHAIN bool insert(BackendBase*    backend,
                      BackendContext* ctx,
                      ModuleBase*     mod,
                      ModuleKind      kind,
                      int             position);

    // Remplace le module a la position donnee.
    CHAIN bool replace(BackendBase*    backend,
                       BackendContext* ctx,
                       ModuleBase*     mod,
                       ModuleKind      kind,
                       int             position);

    // Retire le module a la position donnee et recable les voisins.
    CHAIN void remove(BackendBase* backend, int position) noexcept;

    // -----------------------------------------------------------------------
    //  Execution RT -- appelee par le backend a chaque bloc.
    //  Iteration sur la liste plate active. Zero decision, zero allocation.
    // -----------------------------------------------------------------------
    CHAIN void process(int num_frames) noexcept;

    // -----------------------------------------------------------------------
    //  Latence -- calculee au cablage, lue hors RT.
    // -----------------------------------------------------------------------

    // Somme des latences declarees par tous les modules de la chaine.
    CHAIN int declared_latency_samples() const noexcept { return declared_latency_; }

    // Callback declenche a chaque recablage avec la nouvelle latence declaree.
    // L'engine l'enregistre pour notifier l'hote audio (PDC).
    // Callback declenche a chaque recablage avec la nouvelle latence declaree.
    // L'engine l'enregistre pour notifier l'hote audio (PDC).
    // Pointeur de fonction brut : pas de std::function (C4251 MSVC).
    void (*on_latency_changed)(void* user, int samples) = nullptr;
    void*  on_latency_changed_user                      = nullptr;

private:
    // -----------------------------------------------------------------------
    //  Description d'un maillon logique (avant resolution en ChainElement).
    //  Sert a reconstruire la chaine lors d'un recablage partiel.
    // -----------------------------------------------------------------------
    struct ChainNode {
        ModuleBase* module   = nullptr;
        ModuleKind  kind     = ModuleKind::Suppression;
        int         position = 0;
        int         ctx_type = kBackendAny;  // type de contexte resolu
    };

    // Reconstruit la liste plate a partir des noeuds logiques.
    // Insere les noeuds de transfert aux transitions de contexte.
    // Appele par tous les mutateurs (install/insert/replace/remove).
    void rebuild(BackendBase* backend);

    // Resout le type de contexte d'un module (kBackendCPU, kBackendCUDA, ...).
    // Consulte le backend pour savoir quels contextes sont disponibles.
    static int resolve_context(BackendBase*  backend,
                               ModuleBase*   mod);

    // Fonctions d'execution pre-resolues (pointees par ChainElement::execute).
    // Separees selon le type d'element pour eviter tout branchement en RT.
    static void exec_module  (ChainElement& e, int num_frames) noexcept;
    static void exec_transfer_h2d(ChainElement& e, int num_frames) noexcept;
    static void exec_transfer_d2h(ChainElement& e, int num_frames) noexcept;
    static void exec_transfer_cpu(ChainElement& e, int num_frames) noexcept;

    // Recalcule et stocke la latence declaree totale.
    void recalculate_latency();

    // -----------------------------------------------------------------------
    //  Double buffer -- swap atomique hors RT.
    //  chain_[0] et chain_[1] sont les deux listes plates.
    //  active_ indique laquelle est lue par le RT (0 ou 1).
    // -----------------------------------------------------------------------
    std::vector<ChainElement> chain_[2];
    std::atomic<int>          active_{0};   // index de la liste active en RT

    // Liste logique des noeuds (source de verite pour les recablages).
    std::vector<ChainNode> nodes_;

    // Latence declaree totale (sommee au cablage).
    int declared_latency_ = 0;
};

} // namespace ns::chain
