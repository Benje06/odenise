## Identité du projet
**odenise** (open-denoise) : suppresseur de bruit spectral temps réel,
modulaire
plugin VST3/CLAP + standalone
GPU (CUDA, tensor) avec repli CPU

## Architecture en couches

                                                                           CPU -- GPU --------- \
                                                       module                \    /              \
    UI <--------------------> AudioEditor ---\     loader/registry    compute module dispatcher   |
    |                              |          \          |                     \/                 |
    |                        AudioProcessor ---\->     engine     <->        backend     ->    modules
    |                              |  \-----------------???--------------------/\                 |
    |                              |                                             \                |
   VST <----------------->    JUCE / ALSA                                         \ ----- \    thread
                                   |                                                       \      |
                                   |                                                        \     |
                             Audio Hardware  ->  direct mapping of audio buffer in   ->    audio_chain
                                    \                                                             |
                                     \ ___________________________________________________  buffer mapper

**Couche 1 — Cœur (`libodenise`)** :
bibliothèque partagée C++ pur (C++20).
ZERO dépendance JUCE/gtkmm/GLib/CUDA tout le code GPU est dans des modules.
Compilée en GCC/UCRT64 (Linux) et MSVC windows et lesmodules gpu (CUDA).
Contients: orchestration/moteur(engine.cpp),
loader de modules(module_registry.cpp dlopen),
AudioProcessor(interface entre l'audioeditor et l'engine),
AudioEditor(interface entre l'ui et l'audioprocessor)
s'aide des Librairies:
- gxthread (std::thread ou pthread),
- audio_chain (routeur inter module),
- logger (LogManager).

Les backends de calcul ne sont pas dans le cœur :
 ce sont des modules.
Le backend est un dispatcheur.

Visibilité `hidden` + API marquée `ODENISE_API`.
Le header public est `engine.h` (namespace `odenise`).

**Couche 2 — Modules dynamiques**
`.so`/`.dll` séparés, chargés au runtime.
Frontière **C pure** (vtable `OdeniseModuleVTable`, symbole `odenise_module_entry`, POD uniquement, vérification `kAbiVersion`).
Familles (`ModuleKind`) :
  `ComputeBackend`, `Suppression`, `Window`, `DualMic`, `Inference`.
  Ne linkent PAS contre le cœur.
  Les modules peuvent etre compilé séparément (deux compilateurs, la frontière C les relie).

**Couche 3 — Enveloppes** :
plugin JUCE (VST3/CLAP multiplateforme),
standalone gtkmm (homogène avec gxinterface/dx7interface).

## Filiation avec l'écosystème gxinterface/dx7interface
Les outils communs (`common.h`, `debug.h`, `logger.h`, `logger.cc`) sont dérivés de gxinterface,
nettoyés de GLib (pas de gtkmm/glibmm/giomm).
Le `LogManager` (singleton thread-safe, niveaux 0/1/2, macros `LOG`/`LOG_ERR`) est identique dans son API, garde d'export générique `LOGGER`.
Le `common.h` fournit `get_time()`, `error(from,what,why)`, `tostr<T>`, `str_const_hash`/`""_hash`, `help_format`, `init_nls()`,
  macros `DS`/`EOL`, i18n via gettext pur (`_()`). Le `<getopt.h>` est conditionné `#ifndef _MSC_VER`.
Les en-têtes de licence GPL sont préservés et ne doivent pas être supprimés.

## Convention de déploiement (calquée sur dx7interface)
Le layout suit la structure FHS, identique sous Linux et Windows, piloté par `CMAKE_INSTALL_PREFIX` + `GNUInstallDirs` :

```
<prefix>/
  bin/                                  → odenise(.exe), test_core(.exe)
                                          + .dll sous Windows (à côté de l'exe)
  lib/                                  → libodenise.so (Linux)
                                          odenise_log (a déplacer dans lib/odenise/ ?)
                                          odenise_chain (a déplacer dans lib/odenise/ ?)
                                          odenise_thread
                                          odenise_audio
  include/odenise/<version>/            → engine.h 
                                          logger.h audio_chain.h gxthread.h ( a rajouter ? )
  share/odenise/<version>/
    modules/                            → modules rangés PAR KIND (sous-dossiers) :
      suppression/                      → passthrough.so/.dll, ...
      backends/                         → backend_cpu.so/.dll, backend_cuda.dll, ...
      window/  dualmic/  inference/     → (futur)
    data/cfg/                           → profils de bruit (futur)
    data/ui/                            → XML GtkBuilder (futur)
  share/locale/                         → .mo gettext (futur)
  share/pkgconfig/                      → odenise.pc (futur)
```
Les sous-dossiers de `modules/` portent le nom du **kind** (miroir de l'arbre source).
Le loader **scanne récursivement** `modules/` et retrouve donc chaque module quel que soit son sous-dossier.

Prefix selon l'environnement :
 `/usr` (Arch/Debian), `C:/Program Files/odenise/usr` (Windows NSIS), `/usr/local` (dev local).
  Le layout de build reproduit cette structure sous `build/<preset>/` pour que les tests tournent sans install.

`moduleDir()` dans `engine.cpp` résout :
 env `ODENISE_MODULE_PATH` > `<exe_dir>/../share/odenise/<version>/modules/` (fonctionne en build et après install, quel que soit le cwd).
 Il retourne la racine `modules/` ; le scan récursif descend dans les sous-dossiers de kind.

## Build — CMake + presets

**Presets** :
- `linux` / `linux-core` — Linux GCC, CPU (Ninja)
- `ucrt64` / `ucrt64-core` — Windows MSYS2/UCRT64 GCC, CPU (Ninja)
- `windows-msvc-cuda` — Windows, **générateur Visual Studio 17 2022** + toolset v143 + CUDA 12.9
- `windows-msvc-tensor` — Windows, **générateur Dernier Visual Studio** + toolset à determiner + CUDA derniere version afin d'utiliser les fonctionnalitéses des dernieres cartes
- preset TODO:
  - `windows-msvc-amd` — Windows, **générateur Dernier Visual Studio** + toolset à determiner + utilisataire a determiner afin d'utiliser les dernieres version afin d'utiliser les fonctionnalitéses des dernieres cartes
  - `windows-msvc-intel` — Windows, **générateur Dernier Visual Studio** + toolset à determiner + utilisataire a determiner afin d'utiliser les dernieres version afin d'utiliser les fonctionnalitéses des dernieres cartes

**CUDA 12.9** : 
 dernière version supportant Pascal (sm_61).
 Archs : `61;75;86;89;90`. Exige toolset MSVC 2017-2022 (v143) — VS 2026 (v14.51) fait crasher `cudafe++`.
  L'intégration Visual Studio de CUDA doit être installée (sinon « No CUDA toolset found »).
  Le `/W4` est conditionné `$<$<COMPILE_LANGUAGE:CXX>:...>` pour ne pas être transmis à nvcc.

**Découpage compilateurs** :
 UCRT64/GCC ou MSVC compile tout (cœur + modules CPU).
 MSVC+nvcc compile le module CUDA (`.dll` séparé, chargé par dlopen, frontière C).
 Un seul arbre source, la compilation aiguille.
 Le cœur ne référence plus aucune dépendance CUDA (ni `CUDA::cufft`, ni sources `.cu`) : tout est porté par le module `backend_cuda`.

**Variables CMake clés** :
- `ODENISE_VERSION_DIR` = `odenise/<version>` (passé comme define C++ pour `moduleDir()`)
- `ODENISE_MODULE_OUTPUT_DIR` / `ODENISE_MODULE_INSTALL_DIR` — racine sortie/install des modules ;
    chaque cible y est suffixée par son sous-dossier de kind (`/suppression`, `/backends`, ...)
- `ODENISE_DATA_OUTPUT_DIR` / `ODENISE_DATA_INSTALL_DIR` — données applicatives
- `ODENISE_HAVE_CUDA` — détecté automatiquement au niveau racine ; 
    conditionne la compilation de la cible module `backend_cuda` (plus aucune source `.cu` dans le cœur)
- `LOGGER_EXPORTS=1` — défini à la compilation du cœur pour exporter `LogManager`
- `CHAIN_EXPORTS=1` — défini à la compilation du cœur pour exporter `AudioChain`

## Fichiers existants (phase 1 + phase 2 + phase 3)

```
CMakeLists.txt                          — racine, layout, détection CUDA, sous-répertoires
CMakePresets.json                       — presets linux/ucrt64/msvc-cuda
cmake/
  CTestCustom.cmake.in                  — relève les limites de troncature de sortie CTest
lib/audio
  CMakeLists.txt                        — cible odenise_audio SHARED
                                          sources : src/core/audio/audioEditor.cpp
                                                    src/core/audio/audioprocessor.cpp
                                          defines PRIVATE : AUDIO_EXPORTS
                                          includes PUBLIC : src/core/,
                                                            src/core/engine
                                          includes PRIVATE : src/core/tools,
                                                             src/core/chain
                                          link PUBLIC : odenise_log odenise_chain odenise_core
lib/chain
  CMakeLists.txt                        — cible odenise_chain SHARED
                                          sources : src/core/chain/audio_chain.cpp
                                          defines PRIVATE : CHAIN_EXPORTS
                                          includes PUBLIC : src/core/,
                                                            src/core/engine
                                          includes PRIVATE : src/core/tools,
                                                             src/core/registry
                                          link PUBLIC : odenise_log
lib/log
  CMakeLists.txt                        — cible odenise_log SHARED
                                          sources : src/core/tools/logger.cc
                                          defines PRIVATE : LOGGER_EXPORTS
                                          includes PUBLIC : src/core/tools,
                                                            src/core/engine
lib/thread
  CMakeLists.txt                        — cible odenise_thread SHARED
                                          sources : src/core/chain/gxthread.cpp
                                          defines PRIVATE : THREAD_EXPORTS
                                          includes PUBLIC : src/core/,
                                                            src/core/tools
                                          includes PRIVATE : src/core/tools
                                          link PUBLIC : odenise_log
src/core/
  CMakeLists.txt                        — cible odenise_core SHARED (aucune source CUDA)
                                          sources : engine/engine.cpp, registry/loader.cpp, 
                                                    audio/AudioEditor.cpp, audio/AudioProcessor.cpp
                                          link PUBLIC : odenise_log, odenise_chain, ${CMAKE_DL_LIBS}
  audio/AudioEditor.cpp        - interface (ui <-> audioProcessor)
  audio/AudioProcessor.cpp     - interface (audioEditor <-> engine/backend <-> Audio hardware)
  chain/audio_chain.h                   — macro CHAIN sur méthodes publiques
                                          uniquement (install, insert, replace, remove,
                                          process, declared_latency_samples)
                                          on_latency_changed : pointeur brut
                                          void(*)(void* user, int samples)
                                          (pas std::function, C4251 MSVC)
  chain/audio_chain.cpp                 — nouveau
  engine/engine.h                       — API publique (Engine, enums, kindName(), vtable C modules)
                                          BackendContext, ModuleBase, BackendBase
                                          (abstraites pures, sans ODENISE_API)
                                          LatencyInfo, ProcessingStats
                                          kBackendAny/CPU/CUDA/ROCm
                                          backend_type_id entier libre (jamais enum)
                                          measure_ready_ atomic protected dans BackendBase
                                          last_latency_, last_stats_ protected
                                          accesseurs measure_ready(), last_latency_info(),
                                          last_processing_stats()
  engine/engine.cpp                     — latencyInfo() construit à la volée depuis
                                          chain_ + backend_base_ sans membre stocké
                                          EngineImpl, moduleDir(), createEngine(),
                                          bind/release backend + suppression (par couche)
  registry/module_registry.h            — header interne (ModuleRegistry)
  registry/loader.cpp                   — dlopen/LoadLibrary, ABI check, scan récursif, LOG_ERR
  tools/
    common.h                            — utilitaires communs sans GLib, gettext pur
    debug.h                             — macros debug (PRE_LOG, LOG_IN, etc.)
    logger.h                            — LogManager/Logger, garde LOGGER
    logger.cc                           — implémentation LogManager
src/modules/
  CMakeLists.txt                        — cibles MODULE, PREFIX "", sortie/install par kind
                                          sources : backends/backend_cpu.cpp uniquement
                                          includes PRIVATE : ODENISE_CORE_INTERFACES + ODENISE_CORE_INTERNAL
                                          link PRIVATE : odenise_log, odenise_chain
  suppression/
    passthrough.cpp                     — module neutre (in=out), Suppression id=1, vtable C, self-test
  backends/
    backend_cpu.cpp                     — backend de repli CPU, ComputeBackend id=0, vtable C, self-test
                                          CpuBackendContext (BackendContext CPU,
                                            scratch_buf, compute_stream=nullptr,
                                            backend_type=kBackendCPU)
                                          CpuBackendImpl (BackendBase complet,
                                            AudioChain interne, install_module,
                                            uninstall_module, process, caps, measure)
                                          measure() : N blocs bruit blanc std::chrono,
                                            last_stats_ + last_latency_,
                                            store(release) sur measure_ready_
                                          chemin legacy conservé intact
                                          vtable : create_backend non nul,
                                                  create_module = nullptr
    backend_cuda.cu                     — stub CUDA (namespace ns::cuda) — module compilé MSVC
    stft_chain.cu                       — stub STFT GPU (namespace ns::cuda) — joint au module CUDA
src/plugin/juce
  src/
    JuceAudioLayer.cpp                  - hold processor/editor/devicemanager, scan drivers + devices
    Juceplugin.cpp                      - plugin interface juce (herite de juce::AudioProcessor)
  ui/
    JuceEditorComponent.cpp             - UI component
tests/
  CMakeLists.txt                        — cible test_core
                                          target_include_directories PRIVATE ${CMAKE_SOURCE_DIR}/src/core
  main.cc                               — point d'entrée tests, init_nls, LogManager, try/catch ;
                                          tests : load chain, process passthrough, compute backend
                                          run_latency_test() : valide latencyInfo(), processingStats(), backendCaps()
```

Note : les `.cu` (`backend_cuda.cu`, `stft_chain.cu`) sont regroupés en **un seul module** `backend_cuda`,
 conditionné `if(ODENISE_HAVE_CUDA)`, rangé dans `modules/backends/`.
 Le `backend_cuda.dll` actuel est un stub sans `odenise_module_entry` : le loader le signale en erreur et poursuit (comportement attendu).

## Conventions

- **Sélection de module par `id`** : l'espace d'id n'est pas relatif au `ModuleKind`.
- **`suppression_id = 0`** (défaut de `RuntimeConfig`) = « aucun module de suppression lié » ;
 `process()` renvoie alors `Unsupported`.
  Un module se demande via `loadmodule(id)` (l'id passe à celui du module choisi) 
  `reconfigure()` demande au backend de reconfigurer le module et l'audiochain.
- **Liaison par couche** : 
  au démarrage, bind du socle vers le haut — `ComputeBackend` d'abord, puis `Suppression`.
  Libération en ordre inverse (suppression avant backend) pour qu'aucune couche n'utilise un backend déjà détruit.
- **`kindName(ModuleKind)`** : helper public `inline const char*` dans `engine.h` (littéral statique, zéro alloc, portable).
  Utilisé pour préciser le kind dans les logs.
- **Logs uniformisés** : on construit la chaîne dans une variable locale (`msg` pour `LOG`, `msg_err` pour `LOG_ERR`) AVANT l'appel, jamais en imbrication.
 `error(from, what, why)` avec `from = __func__` (portable GCC/MSVC).
  Dans le loader, les diagnostics identifient le module fautif par son sous-dossier (kind présumé) et son nom de fichier,
  ex. : « Loader cannot resolve entry symbol of 'backends' module 'backend_cuda.dll' ».
- ODENISE_API : uniquement Engine, createEngine(), availableBackends()
    BackendBase/ModuleBase/BackendContext : jamais ODENISE_API
- std::function dans header public exporté : jamais (C4251 MSVC)
    → pointeur de fonction brut ou méthode virtuelle
- Macro d'export sur méthodes publiques uniquement
- backend_type_id/module_type_id : entier libre extensible, jamais enum fixe
- Libs partagées utilitaires (modèle gxinterface) :
    odenise_log   → singleton LogManager unique dans le processus
    odenise_thread -> implementation du threading extensible pour le coeur et les modules
    odenise_chain → AudioChain, linkée par les modules (backend_cpu, ...)
    odenise_audio -> Interface d'abstraction pour la couche audio
    Les modules linkent contre ces libs
- backendbase derive de modulebase qui herite de thread: fonction de traitement dans run() et non dans process();
- implementation d'un lock de traitement afin de mettre en pause le thread de traitement.

## Modèle de traitement (conçu, non implémenté)
- Suppression sur **amplitude spectrale** uniquement (pas de phase, mono-micro ou par phase dual-mic)
- **3 couches soustractives** : Card (figée par learning), Mic (figée par learning), Env (figée + adaptative par minimum-statistics)
- **Adaptation** gated par niveau (bas=bruit=MAJ, haut=voix=gel)
- **Plancher G_min** : scalaire (standard) ou courbe par bande (avancé), jamais zéro
- **Boucle auto-correctrice** : agressivité dans corridor borné (résidu silence, kurtosis=bruit musical, harmonicité=voix)
- **STFT** : N et Hop parametrable default a N=1024, hop=N/4 (75%), WOLA root-Hann,
             convolution linéaire (qualité) / gain-smoothing (léger), fenêtres asymétriques (faible latence), 24-40 bandes Bark/ERB
- **GPU** : 1 stream CUDA/piste, profils read-only VRAM, pré-allocation N_max (hot reconfig sans cudaMalloc en RT)
- **Extension 2-micros** : NLMS/RLS/beamforming + post-filtrage spectral (même horloge requise)
- **Paramètres hot** : double-buffer atomique + crossfade, pas de destruction UI
- **Backend GTX vs RTX** : résolu par détection de capacité (compute capability, Tensor Cores), et par choix utilisateur

## État et prochaines étapes

**Phase 1 — TERMINÉE** : ossature, LogManager, loader, frontière C, smoke test, 3 chaînes de compilation validées.

**Phase 2 — EN COURS** :
1. ✅ **Routage audio** : validé par `passthrough`, in=out.
2. ✅ **Backends de calcul comme modules** : `ComputeBackend` chargé dynamiquement comme la suppression ;
3. ✅ **Plugin JUCE (VST3/CLAP)**: vst3 vu et chargé, interface dans audiopluginhost (mais pas d'interface visible dans cubase a corriger plus tard)
4.    **Ajout supression de module et recablage de l'audio chain**:
          l'interface juce permet de selectionner le backend et un à la fois, 
          RAF(Reste A Faire): 
          - presenter graphiquement l'audio chain dans l'interface juce
          - permettre d'ouvrir une fenetre des options d'un module
          - presenter la/les plus importantes dans la representation du module dans l'audiochain
          - reconfigurer l'audiochaine et le/les modules
5. test complet avec le module passthrough
  passthrough.cpp implémente ModuleBase :
  - latency_samples() = 0
  - install(ctx) : alloue output_buf_ via ctx->scratch_buf(n_max*sizeof(float))
  - uninstall() : ne libère pas (scratch_buf appartient au backend)
  - set_input(src) : stocke input_ = src
  - output_buf() : retourne output_buf_
  - process(n) : memcpy(output_buf_, input_, n*sizeof(float))
  - Vtable : create_module retourne new PassthroughModule,
            create_backend = nullptr
           
**Prochaines étapes** :
1. Chaîne STFT réelle (fenêtrage → FFT → gain par bande → iFFT → overlap-add) — **prochaine**
exemple de filtre :

[Capture CB]
     │
     │  alloue bloc, incrémente refcount × 3
     │
     ▼
[ptr] entrée
     │
     ├─────────────────────┬─────────────────────┐
     │                     │                     │
     ▼                     ▼                     ▼
[VAD / Gate]       [Analyse spectrale]       [ptr entrée]
  (async)             (async)                
     │                     │                     │
     ▼                     ▼                     ▼
┌─────────────────────────────────────────────────────┐
│                 NR - Noise Reduction                │
└─────────────────────────────────────────────────────┘
                          │
                          ▼
                 [Filtre passe-bande]
                          │
                          ▼
                   [AGC / Limiter]
                          │
                          ▼
                    [ptr] sortie
    
2. Profils de bruit 3 couches + adaptation
3. Module CUDA isolé (code GPU réel, compilé MSVC, chargé par le cœur UCRT64)
4. Boucle auto-correctrice
5. Extension 2-micros
6. Standalone gtkmm

## Règles de travail établies
- **Pas de présupposition** : ne faire que ce qui est demandé, annoncer le plan avant de coder.
- **Fichiers de référence** : 
        - lire les fichier concerné par la demande avant de coder
        - les versions uploadées par l'utilisateur font autorité.
        - Ne pas les réécrire sans demande.
        - Se servir des fichiers mis a jour dans le projets comme depart
- **En-têtes de licence** : ne jamais supprimer ceux qui existent ; pas de propagation pour le moment.
- **Commentaires** : ne pas supprimer les commentaires existants ; en ajouter quand c'est utile.
- **Includes** : privilégier `common.h` quand un include se retrouve dans plusieurs fichiers (centralisation).
        - Dépendances directes spécifiques restent dans leur fichier (IWYU).
- **Garde d'export** : `LOGGER` (générique, pas de ref odenise ni gx). `ODENISE_API` pour l'API publique du cœur.
- **`.cu`** : les stubs actuels doivent utiliser `namespace odenise::....` — convention conservée telle quelle, on ne change pas les namespaces.
- **Portabilité GCC/MSVC** : pas d'addition deux const char* (la macro `_()` rend un `const char*`) ;
                             concaténer via une `std::string` ou des `+=` successifs.
                             new(std::nothrow) pour les allocations modules
- **Langue** : conversation en français, code et commentaires techniques en mix fr/en.
## Règles de travail

## Stack
- Cœur : libodenise, C++20, GCC/UCRT64, zéro CUDA, zéro JUCE/GLib
- Modules : .so/.dll dynamiques, frontière C (vtable + dlsym), C++ au-delà
- Build : CMake + presets (linux, ucrt64, windows-msvc-cuda)

## Raisonnements et choix établis — NE PAS REMETTRE EN QUESTION

### Pourquoi le module de suppression est externe au backend
Le backend est la ressource de calcul (CPU threads, CUDA stream). Le module
de suppression est la règle audio (wiener, spectral sub, passthrough...).
Les deux sont séparés pour que la règle soit swappable à chaud sans toucher
au backend. Le module s'exécute SUR la ressource du backend via BackendContext,
mais son code lui appartient. Un module wiener_cuda embarque ses propres
kernels CUDA précompilés, les installe sur le stream du backend à l'install().

### Pourquoi pas de ODENISE_API sur BackendBase/ModuleBase/BackendContext
MSVC C4251 : tout membre non-POD dans une classe ODENISE_API déclenche un
warning bloquant. Ces trois classes sont des interfaces abstraites pures —
aucun symbole à exporter. Seuls Engine, createEngine(), availableBackends()
portent ODENISE_API. Ne jamais remettre ODENISE_API sur les abstraites.

### Pourquoi std::atomic membres protégés dans BackendBase
Pas de std::function (C4251), pas de callback virtuel (couplage),
pas de shared_ptr (compteur atomique + ownership flou), pas d'adaptateur
(complexité inutile). Le backend écrit last_latency_ / last_stats_ une seule
fois à la fin de measure() hors RT via store(release). L'engine/UI lit via
measure_ready() load(acquire) depuis un timer — zéro blocage, zéro attente,
zéro impact sur process(). Ne jamais toucher ces membres depuis process().

### Pourquoi le câblage pré-résout tout
En RT, zéro décision. Tout branchement (quel module ? quel contexte ?
H2D ou D2H ?) est résolu au câblage hors RT. La liste plate ne contient
que des pointeurs de fonctions pré-résolus. En RT : itération séquentielle,
un appel par élément. Les transferts H2D/D2H sont insérés comme ChainElement
aux transitions CPU↔GPU au câblage — invisibles pour l'engine et les modules.

### Pourquoi backend_type_id est un entier libre
Un enum fixe dans engine.h ne peut pas être étendu sans modifier le cœur.
Un entier libre permet à un backend tiers de déclarer son propre id (>99)
sans toucher au cœur. Le cœur fait correspondre module et backend par cet
entier. Constantes prédéfinies : kBackendAny=0, kBackendCPU=1, kBackendCUDA=2,
kBackendROCm=3. Ne jamais remplacer par un enum.

### Modèle de latence à deux niveaux
- Latence déclarée : sommée au câblage depuis latency_samples() de chaque
  module. Sert à la PDC (compensation hôte audio). Promesse théorique.
- Latence mesurée : N blocs de bruit blanc (pire cas spectral), chrono
  std::chrono, remplit last_latency_ + last_stats_. Réalité mesurée.
- Les deux sont exposées par Engine::latencyInfo() et processingStats().
- latencyInfo() construit la struct à la volée depuis chain_ et backend_base_,
  sans membre stocké dans EngineImpl.

### Progression architecture suppression (chemin de pensée à conserver)
1. "suppression dans le backend" → rejeté : pas swappable
2. "module externe calcule les gains, backend applique" → rejeté : transit
   de données spectrales entre modules via CPU = transferts PCIe inutiles
3. "module externe, calcul sur ressource backend" → retenu : le module
   apporte son code (kernels CUDA précompilés pour wiener_cuda, C++ pour
   wiener_cpu), le backend lui fournit BackendContext (stream, scratch).
   Chaque module passe l'audio traité au suivant directement via output_buf().
   Les transferts H2D/D2H sont insérés par AudioChain au câblage, invisibles.

### Chaîne hétérogène CPU/GPU
Un module wiener_cuda et un module dualmic_cpu peuvent coexister dans la
même chaîne. AudioChain détecte les transitions de contexte au câblage et
insère les transferts. En RT : zéro décision. Le buffer audio PCM est le
seul flux entre modules — pas de données spectrales inter-modules.
Chaque module fait sa propre STFT en interne si nécessaire.

### Modules séparés par backend
wiener_cpu.so et wiener_cuda.so sont deux modules distincts.
Si le backend actif est CUDA, l'engine charge wiener_cuda.so.
L'engine charge les deux modules (backend + suppression), transmet
ModuleBase* au backend via install_module(). Le backend ne sait pas
d'où vient le ModuleBase*, il l'installe et l'exécute.

## Interfaces clés (engine.h)

class BackendContext {          // fourni par le backend au module à l'install
    
class ModuleBase : public Thread  {              // tout module hérite de ça
   
class BackendBase : public ModuleBase{             // tout backend hérite de ça
    
## AudioChain (interne cœur)
- ChainElement{ void(*execute)(ChainElement&,int) } — pointeur de fonction pré-résolu
- Double buffer std::atomic<int> active_ — swap atomique hors RT
- install/insert/replace/remove à chaud — recâblage local + rebuild()
- Accumulation latence déclarée — callback on_latency_changed
- Transferts H2D/D2H insérés comme ChainElement aux transitions de contexte

## UI vst
- bouton rescan (scan_device)
- add checkbox:
    - use host driver (cubase, audiopluginhost,... host of vst plugin)