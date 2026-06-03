// ============================================================================
//  ns_engine.h  --  Coeur de traitement (debruitage spectral GPU/CPU)
//
//  Couche 1 du projet odenise. C++ pur : ignore JUCE, gtkmm et GLib. Compile
//  en bibliotheque PARTAGEE (libodenise) reutilisable. Requiert C++20.
//
//  S'inclut en direct :  #include "ns_engine.h"
//
//  Modele de threads, marque sur chaque methode :
//    [RT]   appelable depuis le thread audio temps reel, non bloquant.
//    [CTRL] thread de controle/UI ; peut allouer / bloquer.
// ============================================================================
#pragma once

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

namespace ns {

inline constexpr int kAbiVersion = 1;

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
    bool        is_gpu     = false;
    std::size_t vram_bytes = 0;
    int         cc_major   = 0;       // compute capability (6,1 = Pascal)
    int         cc_minor   = 0;
    bool        has_fp16   = false;
    bool        has_tensor = false;   // false sur Pascal -> chemin FP32
};

// ---------------------------------------------------------------------------
//  Description d'un module (peuplement des listes UI)
// ---------------------------------------------------------------------------
struct ModuleInfo {
    int         id        = 0;
    ModuleKind  kind      = ModuleKind::Suppression;
    std::string name;
    std::string description;
    bool        needs_gpu = false;
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
    //        Non bloquant : aucun malloc, aucune synchro device imposee.
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
//  un autre compilateur. On ne traverse donc cette frontiere qu'en C : un
//  unique point d'entree extern "C" renvoie une table de pointeurs de
//  fonctions. Derriere ces pointeurs, l'implementation reste du C++ normal.
//
//  (Pour le chargement de odenise PAR l'ecosysteme gxinterface, une autre
//   convention -- extern "C" renvoyant des types C++ -- sera utilisee
//   separement ; elle n'est pas definie ici.)
//
//  Regles imposees par la frontiere binaire :
//   - ABI : la table commence par 'abi_version', verifiee au chargement.
//   - Exceptions : aucune ne franchit la frontiere. Tout est noexcept et
//     renvoie un code ; le module convertit ses exceptions en interne.
//   - Memoire : tout objet cree par le module est detruit par le module.
//   - Types : seuls des POD / const char* traversent. Les chaines sont
//     possedees par le module et valides tant qu'il est charge.
// ===========================================================================
extern "C" {

typedef struct {
    int          id;
    int          kind;          // valeur de ns::ModuleKind
    const char*  name;
    const char*  description;
    int          needs_gpu;     // 0/1
} OdeniseModuleInfoC;

typedef struct {
    int          passed;        // 0/1
    const char*  detail;
} OdeniseTestResultC;

// Buffers detenus par l'appelant (le coeur), valides le temps de l'appel.
typedef struct {
    const float* const* in;     // in[ch]
    float*              out;     // sortie mono
    int                 in_channels;
    int                 num_frames;
} OdeniseProcessCtx;

typedef void* OdeniseModuleInstance;

// Table de fonctions d'un module. Tout pointeur est noexcept cote module ;
// les fonctions de traitement renvoient un int = valeur de ns::Status.
typedef struct {
    int                  abi_version;   // DOIT valoir ns::kAbiVersion

    OdeniseModuleInfoC   info;          // metadonnees (sans instance)

    OdeniseModuleInstance (*create)(int sample_rate, int n_max);
    void                  (*destroy)(OdeniseModuleInstance self);
    int  (*set_param)(OdeniseModuleInstance self, int param_id, float value);
    int  (*process)(OdeniseModuleInstance self, OdeniseProcessCtx* ctx);
    OdeniseTestResultC (*self_test)(void);

} OdeniseModuleVTable;

// Unique symbole exporte par chaque module. Le loader fait dlsym sur ce nom.
typedef const OdeniseModuleVTable* (*OdeniseModuleEntryFn)(void);

#define ODENISE_MODULE_ENTRY_SYMBOL "odenise_module_entry"

} // extern "C"
