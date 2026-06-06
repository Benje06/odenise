// ============================================================================
//  engine.h  --  Coeur de traitement (debruitage spectral GPU/CPU)
//
//  Couche 1 du projet odenise. C++ pur : ignore JUCE, gtkmm et GLib. Compile
//  en bibliotheque PARTAGEE (libodenise) reutilisable. Requiert C++20.
//
//  S'inclut en direct :  #include "engine.h"
//
//  Modele de threads, marque sur chaque methode :
//    [RT]   appelable depuis le thread audio temps reel, non bloquant.
//    [CTRL] thread de controle/UI ; peut allouer / bloquer.
// ============================================================================
#pragma once
#include "logger.h"
#include <cstdlib>
#include <filesystem>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
//  Visibilite des symboles
//   ODENISE_API    : API publique de la lib partagee libodenise. Exportee a la
//                    compilation de la lib, importee chez le consommateur.
//   ODENISE_EXPORT : symbole d'entree d'un MODULE dynamique (toujours exporte ;
//                    un module est un .so/.dll a part).
//  La lib se compile avec -fvisibility=hidden : seul l'API marquee sort.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
  #if defined(ODENISE_BUILD_LIBRARY)
    #define ODENISE_API __declspec(dllexport)
  #else
    #define ODENISE_API __declspec(dllimport)
  #endif
  #define ODENISE_EXPORT __declspec(dllexport)
#else
  #define ODENISE_API    __attribute__((visibility("default")))
  #define ODENISE_EXPORT __attribute__((visibility("default")))
#endif

// ===========================================================================
//  STRUCTS POD FRONTIERE INTER-COMPILATEURS
//
//  Ces structs traversent la frontiere entre compilateurs differents
//  (GCC/UCRT64 pour CPU, MSVC+nvcc pour CUDA). Types POD uniquement.
//  Possedees par le module (litteraux statiques, valides tant que le module
//  est charge). Declarees globalement pour etre accessibles sans qualification
//  depuis les modules externes quel que soit leur compilateur.
//
//  Le coeur construit les types riches (ModuleInfo, BackendCaps) a partir
//  de ces structs -- std::string ne traverse jamais la frontiere.
// ===========================================================================

struct OdeniseModuleInfoC {
    int         abi_version;     // DOIT valoir ns::kAbiVersion
    int         id;
    int         kind;            // valeur de ns::ModuleKind
    const char* name;
    const char* description;
    int         needs_gpu;       // 0/1
    int         backend_type_id; // ns::kBackendAny, kBackendCPU, kBackendCUDA, ...
                                 // ou valeur libre pour un backend tiers (>99)
};

struct OdeniseBackendCapsC {
    const char*   name;
    int           is_gpu;
    unsigned long vram_bytes;
    int           cc_major;
    int           cc_minor;
    int           has_fp16;
    int           has_tensor;
    int           backend_type;
};

struct OdeniseTestResultC {
    int         passed; // 0/1
    const char* detail;
};

namespace ns {

inline constexpr int kAbiVersion = 1;

// ---------------------------------------------------------------------------
//  Identifiants de type de backend (extensibles par les modules tiers).
//  Un module declare son backend_type_id dans OdeniseModuleInfoC.
//  kBackendAny = compatible avec tout backend (le module s'adapte).
//  Un backend tiers choisit un id libre (ex. 100, 200...) sans modifier ce
//  fichier : le coeur fait correspondre module et backend par cet entier.
// ---------------------------------------------------------------------------
inline constexpr int kBackendAny  = 0;
inline constexpr int kBackendCPU  = 1;
inline constexpr int kBackendCUDA = 2;
inline constexpr int kBackendROCm = 3;
// reservez des plages > 99 pour les backends tiers

// ---------------------------------------------------------------------------
//  Enums
// ---------------------------------------------------------------------------
enum class Status {
    Ok = 0,
    InvalidArg,
    OutOfEnvelope,   // changement hors enveloppe pre-allouee
    NoDevice,        // backend GPU demande, aucun device dispo
    AllocFailed,
    Unsupported
};

enum class FftMode { R2C, C2C };          // R2C = signal reel, ~2x moins cher

enum class ProfileLevel {                 // profils de bruit en couches
    Card = 0,   // carte son + preampli (electronique, fige)
    Mic  = 1,   // capsule micro (fige)
    Env  = 2    // environnement (adaptatif a chaud)
};

enum class ModuleKind {                   // familles de modules enfichables
    ComputeBackend,   // CPU / CUDA / ROCm / Vulkan ...  (qui calcule)
    Suppression,      // wiener / soustraction spectrale / DNN ...
    Window,           // profil de fenetre : symetrique / asymetrique
    DualMic,          // pre-traitement 2 micros : NLMS / RLS / beamforming
    Inference         // moteur d'inference : TensorRT / ONNX / NNRT (futur ML)
};

// Libelle lisible d'un ModuleKind (logs, UI). Renvoie un litteral statique :
// aucune allocation, identique sur tous les compilateurs.
inline const char* kindName(ModuleKind kind) {
    switch (kind) {
        case ModuleKind::ComputeBackend: return "ComputeBackend";
        case ModuleKind::Suppression:    return "Suppression";
        case ModuleKind::Window:         return "Window";
        case ModuleKind::DualMic:        return "DualMic";
        case ModuleKind::Inference:      return "Inference";
    }
    return "Unknown";
}

enum class ApplyResult {
    Hot,          // re-pointage + crossfade, sans glitch ni changement latence
    HotRelatency, // a chaud, mais latence changee -> re-declarer a l'hote
    Cold          // a necessite une reconstruction de fond + swap
};

enum class ParamId {
    Bypass = 0,
    Aggressiveness,     // facteur d'over-subtraction global
    GminScalar,         // plancher de gain (mode standard), lineaire
    GminMaxDb,          // reduction max autorisee, en dB (UX)
    GainSmooth,         // lissage du gain en frequence
    EnvAdapt,           // 0/1 : adaptation continue du profil Env
    EnvWindowSec,       // fenetre du minimum-statistics, en s
    VadThresh,          // gate niveau bruit/voix
    AutoCtrl,           // 0/1 : boucle auto-correctrice active
    AutoCtrlLo,         // borne basse du couloir d'agressivite
    AutoCtrlHi,         // borne haute du couloir d'agressivite
    ActiveSuppression,  // id de module (cf. registre)
    ActiveWindow,       // id de profil de fenetre
    ActiveDualMic,      // id de module 2-micros (0 = mono)
    FftModeSel,         // FftMode (a chaud si C2C pre-alloue)
    Count
};

// ---------------------------------------------------------------------------
//  Enveloppe de pre-allocation (fige le coeur a la creation)
//  Tout reconfigure tenant dans l'enveloppe se fait a chaud, sans cudaMalloc
//  dans le chemin temps reel. En sortir force le chemin froid.
// ---------------------------------------------------------------------------
struct EngineCaps {
    int  sample_rate    = 48000;
    int  n_max          = 4096;   // taille de fenetre max pre-allouee
    int  max_bands      = 48;     // nb de bandes perceptives max
    int  max_tracks     = 16;
    int  max_block      = 2048;   // taille de bloc hote max (dim. rings)
    bool prealloc_c2c   = false;  // autorise bascule R2C<->C2C a chaud
    bool share_fft_work = true;   // workspace cuFFT unique partage
    int  backend_id     = -1;     // -1 = AUTO ; sinon id du registre
};

// ---------------------------------------------------------------------------
//  Configuration d'execution (modifiable, a chaud dans l'enveloppe)
// ---------------------------------------------------------------------------
struct RuntimeConfig {
    int     n              = 1024;   // <= n_max
    int     hop            = 256;    // n/4 = 75 % de recouvrement
    float   window_ratio   = 1.0f;   // 1.0 = synthese symetrique ; <1 = asym.
    int     num_bands      = 32;     // <= max_bands
    FftMode fft_mode       = FftMode::R2C;
    int     suppression_id = 0;
    int     window_id      = 0;
    int     dualmic_id     = 0;      // 0 = mono
};

// ---------------------------------------------------------------------------
//  Capabilities du backend detecte -- c'est ICI que se resout GTX vs RTX :
//  l'utilisateur choisit "CUDA", le coeur expose ce qu'il a trouve.
// ---------------------------------------------------------------------------
struct BackendCaps {
    std::string name;                 // ex. "NVIDIA GeForce GTX 1080"
    bool        is_gpu       = false;
    std::size_t vram_bytes   = 0;
    int         cc_major     = 0;    // compute capability (6,1 = Pascal)
    int         cc_minor     = 0;
    bool        has_fp16     = false;
    bool        has_tensor   = false; // false sur Pascal -> chemin FP32
    int         backend_type = kBackendAny; // identifiant du type de backend
};

// ---------------------------------------------------------------------------
//  Description d'un module (peuplement des listes UI)
// ---------------------------------------------------------------------------
struct ModuleInfo {
    int         id               = 0;
    ModuleKind  kind             = ModuleKind::Suppression;
    std::string name;
    std::string description;
    bool        needs_gpu        = false;
    int         backend_type_id  = kBackendAny; // backend requis (kBackendAny = tous)
};

struct TestResult {                   // self-test embarque par chaque module
    bool        passed = false;
    std::string detail;
};

// ---------------------------------------------------------------------------
//  Vue d'E/S pour une piste (resout le cas 2-micros : in_channels == 2,
//  out toujours mono = voix nettoyee). Pointeurs detenus par l'appelant.
// ---------------------------------------------------------------------------
struct TrackIO {
    const float* const* in;          // in[ch], ch dans [0, in_channels)
    float*              out;          // sortie mono
    int                 in_channels;  // 1 = mono ; 2 = dual-mic
};

// ---------------------------------------------------------------------------
//  Snapshots de lecture (remontee UI, hors temps reel). Le coeur les publie
//  via double-buffer atomique : lecture coherente sans verrou.
// ---------------------------------------------------------------------------
struct Metrics {                      // un element par bande
    std::vector<float> aggressiveness;
    std::vector<float> kurtosis;        // indicateur de musical noise
    std::vector<float> harmonicity;     // preservation de la voix
    std::vector<float> silence_residual;// residuel mesure pendant les silences
};

struct Spectrum {                     // magnitude par bin (n/2+1 en R2C)
    std::vector<float> in_mag;
    std::vector<float> out_mag;
};

// ---------------------------------------------------------------------------
//  Mesures de performance de la chaine de traitement.
//
//  latency_declared_samples : somme des latences declarees par les modules
//                             au cablage (promesse theorique).
//  latency_measured_samples : latence reelle mesuree sur le pipeline par
//                             injection de N blocs de bruit blanc.
//  processing_min/max/mean_ms : temps de traitement reel par bloc sur N blocs
//                               de bruit blanc (pire cas spectral).
//  budget_ms  : budget temps reel = hop / sample_rate * 1000.
//  load_pct   : mean_ms / budget_ms * 100 (charge du thread audio).
// ---------------------------------------------------------------------------
struct LatencyInfo {
    int   declared_samples = 0;
    int   measured_samples = 0;
    float declared_ms      = 0.0f;
    float measured_ms      = 0.0f;
    bool  in_sync          = false;  // declared == measured a +/-1 sample
};

struct ProcessingStats {
    float min_ms    = 0.0f;
    float max_ms    = 0.0f;
    float mean_ms   = 0.0f;
    float budget_ms = 0.0f;  // hop / sample_rate * 1000
    float load_pct  = 0.0f;  // mean / budget * 100
};

// ===========================================================================
//  INTERFACES ABSTRAITES -- contrats que tout module et backend implementent.
//
//  BackendContext : contexte de ressource fourni par le backend a un module.
//                  Permet au module d'acceder a la ressource de calcul
//                  (stream CUDA, pool de threads CPU, scratch buffer) sans
//                  connaitre le backend concret.
//
//  ModuleBase     : interface que tout module de traitement implemente.
//                  Un module recoit un BackendContext a l'installation, alloue
//                  ses ressources internes via le scratch buffer, et expose
//                  son buffer de sortie au module suivant dans la chaine.
//
//  BackendBase    : interface que tout backend de calcul implemente.
//                  Le backend possede la ressource de calcul, installe les
//                  modules, cable la chaine, et execute process() en RT.
//
//  Cycle de vie :
//    [CTRL] engine charge backend et modules via le registre
//    [CTRL] engine appelle BackendBase::install_module() pour chaque module
//    [CTRL] BackendBase cable la chaine (pointeurs, transferts) -- une fois
//    [RT]   l'hote audio (JUCE, ALSA, ASIO...) appelle Engine::process() ;
//           Engine::process() delègue au BackendBase::process() du backend actif
//    [CTRL] engine appelle BackendBase::uninstall_module() au recablage
// ===========================================================================

// ---------------------------------------------------------------------------
//  BackendContext -- contexte de ressource (CPU thread pool ou CUDA stream).
//  Fourni par le backend a chaque module lors de l'installation.
//  Le module l'utilise pour acceder au scratch buffer pre-alloue et au
//  contexte de calcul natif (cudaStream_t, pool id, etc.).
// ---------------------------------------------------------------------------
class BackendContext {
public:
    virtual ~BackendContext() = default;

    // [CTRL] Memoire de travail pre-allouee par le backend a N_max.
    // Le module y taille ses buffers internes a l'installation (hors RT).
    // Zéro allocation en RT.
    virtual void*  scratch_buf(std::size_t bytes) noexcept = 0;

    // [RT/CTRL] Contexte natif de la ressource de calcul.
    // CPU : nullptr ou pointeur vers un pool de threads.
    // CUDA : pointeur vers un cudaStream_t actif.
    // Le module caste selon son type de backend declare.
    virtual void*  compute_stream() noexcept = 0;

    // [CTRL] Type de backend de ce contexte (kBackendCPU, kBackendCUDA, ...).
    // Permet au module de valider la compatibilite a l'installation.
    virtual int    backend_type() const noexcept = 0;
};

// ---------------------------------------------------------------------------
//  ModuleBase -- interface commune a tous les modules de traitement.
//  Chaque famille (Suppression, Window, DualMic, Inference) herite de cette
//  base. La chaine de traitement manipule des ModuleBase* uniformement.
// ---------------------------------------------------------------------------
class ModuleBase {
public:
    virtual ~ModuleBase() = default;

    // [CTRL] Metadonnees POD -- frontiere inter-compilateurs.
    // Retourne un pointeur vers une struct statique possedee par le module.
    // Valide tant que le module est charge. Jamais nul.
    virtual const OdeniseModuleInfoC* info_c() const noexcept = 0;

    // [CTRL] Self-test embarque -- frontiere inter-compilateurs.
    // Retourne un pointeur vers une struct statique possedee par le module.
    // Valide tant que le module est charge. Jamais nul.
    virtual const OdeniseTestResultC* self_test_c() const noexcept = 0;

    // [CTRL] Latence algorithmique declaree par ce module, en samples.
    // Sommee au cablage pour la PDC. Doit etre constante apres creation.
    virtual int latency_samples() const noexcept = 0;

    // [CTRL] Installation sur un contexte backend.
    // Le module alloue ses buffers internes via ctx->scratch_buf(),
    // valide la compatibilite via ctx->backend_type().
    // Retourne false si incompatible ou echec d'allocation.
    virtual bool install(BackendContext* ctx) = 0;

    // [CTRL] Liberation des ressources associees au contexte.
    virtual void uninstall(BackendContext* ctx) noexcept = 0;

    // [RT] Parametre a chaud. Zéro allocation, zéro verrou.
    virtual void set_param(ParamId id, float value) noexcept = 0;

    // [CTRL] Reconfiguration structurelle du module.
    // Appele par le backend lors d'un reconfigure() global. Le module
    // extrait de cfg ce qui le concerne (n, hop, num_bands, ...).
    // Peut reallouer des buffers internes -- jamais appele depuis le RT.
    virtual void reconfigure(const RuntimeConfig& cfg) = 0;

    // [RT] Buffer de sortie du module (RAM ou device ptr selon le contexte).
    // Le backend cable ce pointeur comme entree du module suivant au cablage.
    // Valide apres install(), invalide apres uninstall().
    virtual void* output_buf() noexcept = 0;

    // [CTRL] Cablage : le backend indique au module quelle est son entree.
    // Appele une fois au cablage, avant le premier process().
    // src est le output_buf() du module precedent, ou le buffer d'entree
    // du backend pour le premier module de la chaine.
    virtual void set_input(const void* src) noexcept = 0;

    // [RT] Traitement d'un bloc. Le module lit depuis son entree cablee,
    // ecrit dans son output_buf(). Zéro allocation, zéro verrou.
    virtual void process(int num_frames) noexcept = 0;
};

// ---------------------------------------------------------------------------
//  BackendBase -- interface que tout backend de calcul implemente.
//  Le backend est charge par le registre exactement comme un module, via
//  odenise_module_entry(). Il implemente BackendBase en plus de la vtable C.
//  Il possede les contextes de calcul (CPU pool ou CUDA stream), les buffers
//  de transfert pre-alloues, et la liste plate des elements de la chaine.
// ---------------------------------------------------------------------------
class BackendBase {
public:
    virtual ~BackendBase() = default;

    // [CTRL] Metadonnees POD -- frontiere inter-compilateurs.
    // Retourne un pointeur vers une struct statique possedee par le module.
    // Valide tant que le module est charge. Jamais nul.
    virtual const OdeniseModuleInfoC* info_c() const noexcept = 0;

    // [CTRL] Capabilities POD -- frontiere inter-compilateurs.
    // Le coeur construit BackendCaps (avec std::string) depuis cette struct.
    // Valide tant que le module est charge. Jamais nul.
    virtual const OdeniseBackendCapsC* caps_c() const noexcept = 0;

    // [CTRL] Self-test embarque -- frontiere inter-compilateurs.
    // Retourne un pointeur vers une struct statique possedee par le module.
    // Valide tant que le module est charge. Jamais nul.
    virtual const OdeniseTestResultC* self_test_c() const noexcept = 0;

    // [CTRL] Installe un module dans la chaine a la position donnee.
    // Le backend choisit le contexte adequat (CPU ou GPU) selon le
    // backend_type_id declare par le module, cable les pointeurs,
    // insere les noeuds de transfert H2D/D2H si necessaire.
    // position = 0 : premier de la chaine.
    virtual bool install_module(ModuleBase* mod,
                                ModuleKind  kind,
                                int         position) = 0;

    // [CTRL] Retire un module de la chaine et recable les voisins.
    virtual void uninstall_module(ModuleKind kind, int position) noexcept = 0;

    // [CTRL] Reconfiguration structurelle du backend et de ses modules.
    // Appele par l'engine lors d'un reconfigure() ou au premier bind.
    // Le backend redimensionne ses ressources (scratch, streams) selon les
    // caps et cfg, puis propage aux modules installes via module->reconfigure().
    // Peut reallouer -- jamais appele depuis le RT.
    virtual void reconfigure(const EngineCaps& caps, const RuntimeConfig& cfg) = 0;

    // [RT] Traitement d'un bloc audio. Le backend itere sur la liste plate
    // cablee : modules et noeuds de transfert, dans l'ordre, via pointeurs
    // de fonctions pre-resolus. Zéro decision, zéro allocation.
    virtual Status process(const float* const* in,
                           float*              out,
                           int                 num_frames) noexcept = 0;

    // [CTRL] Declenche la mesure de latence reelle sur N blocs de bruit blanc.
    // Hors RT uniquement. Ecrit last_latency_ et last_stats_ a la fin,
    // puis leve measure_ready_ (release). Jamais appele par process().
    virtual void measure(int num_blocks = 16) = 0;

    // [CTRL] Lecture des resultats de la derniere mesure. Hors RT.
    // L'UI ou l'engine appellent measure_ready() depuis un timer ;
    // si true, ils lisent last_latency_info() et last_processing_stats().
    // Zéro blocage, zéro attente : si false, on reviendra au prochain tick.
    bool measure_ready() const noexcept {
        return measure_ready_.load(std::memory_order_acquire);
    }
    const LatencyInfo&     last_latency_info()     const noexcept { return last_latency_; }
    const ProcessingStats& last_processing_stats()  const noexcept { return last_stats_; }

protected:
    // Ecrit par measure() hors RT, lu par l'engine/UI hors RT via les
    // accesseurs publics. Jamais touche par process() -- zéro impact RT.
    LatencyInfo             last_latency_;
    ProcessingStats         last_stats_;
    std::atomic<bool>       measure_ready_{false};
};

// ===========================================================================
//  Interface du moteur. Instanciee par createEngine() ; detruite par RAII.
//  Le backend concret (CPU/CUDA/...) est interne, selectionne via
//  EngineCaps::backend_id ; l'interface ne change pas selon le backend.
// ===========================================================================
class ODENISE_API Engine {
public:
    virtual ~Engine() = default;

    // --- cycle de vie / structure ---------------------------------------
    // [RT]  latence algorithmique courante (echantillons), a declarer en PDC.
    virtual int latencySamples() const noexcept = 0;

    // [CTRL] applique une nouvelle config ; choisit seul chaud vs froid.
    //        Ne detruit jamais le moteur. 'out_how' detaille le chemin pris.
    virtual Status reconfigure(const RuntimeConfig& cfg,
                               ApplyResult& out_how) = 0;

    // [CTRL] capabilities du backend reellement actif (GTX vs RTX, VRAM...).
    virtual BackendCaps backendCaps() const = 0;

    // --- traitement temps reel ------------------------------------------
    // [RT]  traite les pistes fournies. Chaque piste route sur son stream.
    //       Non bloquant : aucun malloc, aucune synchro device imposee.
    virtual Status process(std::span<const TrackIO> tracks,
                           int num_frames) noexcept = 0;

    // --- parametres "a chaud" -------------------------------------------
    // [RT]
    virtual Status setParam(ParamId id, float value) noexcept = 0;
    virtual float  getParam(ParamId id) const noexcept = 0;
    virtual Status setGminCurve(std::span<const float> curve) noexcept = 0;
    // [CTRL] decoupage des bandes (Bark/ERB par defaut, editable). Frontieres
    //        croissantes alignees sur les bins. A chaud si <= max_bands.
    virtual Status setBandLayout(std::span<const float> edges_hz) = 0;

    // --- calibration / profils 3 niveaux --------------------------------
    // [CTRL] demarre une capture : le coeur accumule via process() (mode
    //        capture) puis construit le profil par soustraction des couches.
    virtual Status captureBegin(ProfileLevel level, float seconds) = 0;
    // [RT]  vrai tant qu'une capture est en cours.
    virtual bool   captureActive() const noexcept = 0;
    // [CTRL] (de)serialisation des presets materiel/env.
    virtual std::vector<std::byte> saveProfile(ProfileLevel level) const = 0;
    virtual Status loadProfile(ProfileLevel level,
                               std::span<const std::byte> data) = 0;

    // --- registre de modules --------------------------------------------
    // [CTRL] modules reellement charges pour une famille (peuple l'UI).
    virtual std::vector<ModuleInfo> modules(ModuleKind kind) const = 0;
    // [CTRL] execute le self-test embarque d'un module.
    virtual TestResult selfTest(ModuleKind kind, int module_id) const = 0;

    // --- mesures de performance -----------------------------------------
    // [CTRL] latence declaree + mesuree de la chaine courante.
    virtual LatencyInfo    latencyInfo()    const = 0;
    // [CTRL] stats de traitement (min/max/mean/budget/load).
    virtual ProcessingStats processingStats() const = 0;

    // --- remontee de metriques / spectres -------------------------------
    // [CTRL] copies coherentes des snapshots publies par le thread audio.
    virtual Metrics  metrics()  const = 0;
    virtual Spectrum spectrum() const = 0;
};

// ---------------------------------------------------------------------------
//  Fabrique. Renvoie nullptr en cas d'echec ; '*status' (optionnel) detaille.
// ---------------------------------------------------------------------------
ODENISE_API std::unique_ptr<Engine>
createEngine(const EngineCaps& caps,
             const RuntimeConfig& cfg,
             Status* status = nullptr);

// Enumeration des backends disponibles SANS instancier de moteur (pour que
// l'UI propose la liste avant creation). Peuplee par les modules charges.
ODENISE_API std::vector<ModuleInfo> availableBackends();

} // namespace ns

// ===========================================================================
//  FRONTIERE DES MODULES DYNAMIQUES (dlopen / LoadLibrary)
//
//  Un module est un .so/.dll compile SEPAREMENT du coeur, eventuellement par
//  un autre compilateur (GCC/UCRT64 pour CPU, MSVC+nvcc pour CUDA).
//
//  Principe : odenise_module_entry(sample_rate, n_max) retourne un ModuleBase*.
//  Pour ComputeBackend, l'objet implemente aussi BackendBase ; le loader
//  le caste via dynamic_cast apres avoir lu info_c()->kind.
//  Les structs POD (OdeniseModuleInfoC, OdeniseBackendCapsC, OdeniseTestResultC)
//  sont declarees globalement avant namespace ns -- accessibles sans
//  qualification depuis tout compilateur.
//
//  (Pour le chargement de odenise PAR l'ecosysteme gxinterface, une autre
//   convention sera utilisee separement ; elle n'est pas definie ici.)
// ===========================================================================

extern "C" {

// Unique symbole exporte par chaque module (.so/.dll).
// Le loader fait dlsym sur ce nom puis appelle entry(sample_rate, n_max).
typedef ns::ModuleBase* (*OdeniseModuleEntryFn)(int sample_rate, int n_max);

#define ODENISE_MODULE_ENTRY_SYMBOL "odenise_module_entry"

} // extern "C"
