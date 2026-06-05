# odenise — Synthèse technique (contexte pour prompt)

## Identité du projet
**odenise** (open-denoise) : suppresseur de bruit spectral temps réel, modulaire, plugin VST3/CLAP + standalone, GPU (CUDA, dès GTX série 10 / Pascal) avec repli CPU. sur cœurs CUDA généralistes pour le premier module et Tensor Cores possible sur un autre module. Licence GPL-v3. Auteur : Jérôme Benhaïm (Uru).

## Architecture en couches

**Couche 1 — Cœur (`libodenise`)** : bibliothèque partagée C++ pur (C++20). ZERO dépendance JUCE/gtkmm/GLib, et désormais ZERO dépendance CUDA (tout le code GPU est dans des modules). Compilée en GCC/UCRT64 (Linux et Windows). Contient : orchestration moteur, loader de modules (dlopen/LoadLibrary), LogManager. Les backends de calcul ne sont plus dans le cœur : ce sont des modules. Visibilité `hidden` + API marquée `ODENISE_API`. Le header public est `ns_engine.h` (namespace `ns`).

**Couche 2 — Modules dynamiques** : `.so`/`.dll` séparés, chargés au runtime. Frontière **C pure** (vtable `OdeniseModuleVTable`, symbole `odenise_module_entry`, POD uniquement, vérification `kAbiVersion`). Familles (`ModuleKind`) : `ComputeBackend`, `Suppression`, `Window`, `DualMic`, `Inference`. Ne linkent PAS contre le cœur. Le module CUDA est compilé séparément en MSVC (deux compilateurs, la frontière C les relie).

**Couche 3 — Enveloppes** (futur) : plugin JUCE (VST3/CLAP multiplateforme), standalone gtkmm (homogène avec gxinterface/dx7interface).

## Filiation avec l'écosystème gxinterface/dx7interface
Les outils communs (`common.h`, `debug.h`, `logger.h`, `logger.cc`) sont dérivés de gxinterface, nettoyés de GLib (pas de gtkmm/glibmm/giomm). Le `LogManager` (singleton thread-safe, niveaux 0/1/2, macros `LOG`/`LOG_ERR`) est identique dans son API, garde d'export générique `LOGGER`. Le `common.h` fournit `get_time()`, `error(from,what,why)`, `tostr<T>`, `str_const_hash`/`""_hash`, `help_format`, `init_nls()`, macros `DS`/`EOL`, i18n via gettext pur (`_()`). Le `<getopt.h>` est conditionné `#ifndef _MSC_VER`. Les en-têtes de licence GPL sont préservés et ne doivent pas être supprimés.

## Convention de déploiement (calquée sur dx7interface)
Le layout suit la structure FHS, identique sous Linux et Windows, piloté par `CMAKE_INSTALL_PREFIX` + `GNUInstallDirs` :

```
<prefix>/
  bin/                                  → odenise(.exe), test_core(.exe)
                                          + .dll sous Windows (à côté de l'exe)
  lib/                                  → libodenise.so (Linux)
  include/odenise/<version>/            → ns_engine.h
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

Les sous-dossiers de `modules/` portent le nom du **kind** (miroir de l'arbre source). Le loader **scanne récursivement** `modules/` et retrouve donc chaque module quel que soit son sous-dossier.

Prefix selon l'environnement : `/usr` (Arch/Debian), `C:/Program Files/odenise/usr` (Windows NSIS), `/usr/local` (dev local). Le layout de build reproduit cette structure sous `build/<preset>/` pour que les tests tournent sans install.

`moduleDir()` dans `engine.cpp` résout : env `ODENISE_MODULE_PATH` > `<exe_dir>/../share/odenise/<version>/modules/` (fonctionne en build et après install, quel que soit le cwd). Il retourne la racine `modules/` ; le scan récursif descend dans les sous-dossiers de kind.

## Build — CMake + presets

**Presets** :
- `linux` / `linux-core` — Linux GCC, CPU (Ninja)
- `ucrt64` / `ucrt64-core` — Windows MSYS2/UCRT64 GCC, CPU (Ninja)
- `windows-msvc-cuda` — Windows, **générateur Visual Studio 17 2022** + toolset v143 + CUDA 12.9
- `windows-msvc-tensor` — Windows, **générateur Dernier Visual Studio** + toolset à determiner + CUDA derniere version afin d'utiliser les fonctionnalitéses des dernieres cartes
- preset TODO:
  - `windows-msvc-amd` — Windows, **générateur Dernier Visual Studio** + toolset à determiner + utilisataire a determiner afin d'utiliser les dernieres version afin d'utiliser les fonctionnalitéses des dernieres cartes
  - `windows-msvc-intel` — Windows, **générateur Dernier Visual Studio** + toolset à determiner + utilisataire a determiner afin d'utiliser les dernieres version afin d'utiliser les fonctionnalitéses des dernieres cartes


**CUDA 12.9** : dernière version supportant Pascal (sm_61). Archs : `61;75;86;89;90`. Exige toolset MSVC 2017-2022 (v143) — VS 2026 (v14.51) fait crasher `cudafe++`. L'intégration Visual Studio de CUDA doit être installée (sinon « No CUDA toolset found »). Le `/W4` est conditionné `$<$<COMPILE_LANGUAGE:CXX>:...>` pour ne pas être transmis à nvcc.

**Découpage compilateurs** : UCRT64/GCC compile tout (cœur + modules CPU). MSVC+nvcc compile uniquement le module CUDA (`.dll` séparé, chargé par dlopen, frontière C). Un seul arbre source, la compilation aiguille. Le cœur ne référence plus aucune dépendance CUDA (ni `CUDA::cufft`, ni sources `.cu`) : tout est porté par le module `backend_cuda`.

**Variables CMake clés** :
- `ODENISE_VERSION_DIR` = `odenise/<version>` (passé comme define C++ pour `moduleDir()`)
- `ODENISE_MODULE_OUTPUT_DIR` / `ODENISE_MODULE_INSTALL_DIR` — racine sortie/install des modules ; chaque cible y est suffixée par son sous-dossier de kind (`/suppression`, `/backends`, ...)
- `ODENISE_DATA_OUTPUT_DIR` / `ODENISE_DATA_INSTALL_DIR` — données applicatives
- `ODENISE_HAVE_CUDA` — détecté automatiquement au niveau racine ; conditionne la compilation de la cible module `backend_cuda` (plus aucune source `.cu` dans le cœur)
- `LOGGER_EXPORTS=1` — défini à la compilation du cœur pour exporter `LogManager`

## Fichiers existants (phase 1 + phase 2 en cours)

```
CMakeLists.txt                          — racine, layout, détection CUDA, sous-répertoires
CMakePresets.json                       — presets linux/ucrt64/msvc-cuda
cmake/
  CTestCustom.cmake.in                  — relève les limites de troncature de sortie CTest
src/core/
  CMakeLists.txt                        — cible odenise_core SHARED (aucune source CUDA)
  ns_engine.h                           — API publique (Engine, enums, kindName(), vtable C modules)
  module_registry.h                     — header interne (ModuleRegistry)
  engine.cpp                            — EngineImpl, moduleDir(), createEngine(),
                                          bind/release backend + suppression (par couche)
  loader.cpp                            — dlopen/LoadLibrary, ABI check, scan récursif, LOG_ERR
  tools/
    common.h                            — utilitaires communs sans GLib, gettext pur
    debug.h                             — macros debug (PRE_LOG, LOG_IN, etc.)
    logger.h                            — LogManager/Logger, garde LOGGER
    logger.cc                           — implémentation LogManager
src/modules/
  CMakeLists.txt                        — cibles MODULE, PREFIX "", sortie/install par kind
  suppression/
    passthrough.cpp                     — module neutre (in=out), Suppression id=1, vtable C, self-test
  backends/
    backend_cpu.cpp                     — backend de repli CPU, ComputeBackend id=0, vtable C, self-test
    backend_cuda.cu                     — stub CUDA (namespace ns::cuda) — module compilé MSVC
    stft_chain.cu                       — stub STFT GPU (namespace ns::cuda) — joint au module CUDA
tests/
  CMakeLists.txt                        — cible test_core
  main.cc                               — point d'entrée tests, init_nls, LogManager, try/catch ;
                                          tests : load chain, process passthrough, compute backend
```

Note : les `.cu` (`backend_cuda.cu`, `stft_chain.cu`) sont regroupés en **un seul module** `backend_cuda`, conditionné `if(ODENISE_HAVE_CUDA)`, rangé dans `modules/backends/`. Le `backend_cuda.dll` actuel est un stub sans `odenise_module_entry` : le loader le signale en erreur et poursuit (comportement attendu).

## Conventions phase 2 (en place)

- **Sélection de module par `id`, propre à chaque kind** : l'espace d'id est relatif au `ModuleKind`. `find(kind, id)` filtre sur les deux. Ex. `ComputeBackend` id=0 (cpu) et `Suppression` id=1 (passthrough) ne collisionnent pas.
- **`suppression_id = 0`** (défaut de `RuntimeConfig`) = « aucun module de suppression lié » ; `process()` renvoie alors `Unsupported`. Un module se demande via `reconfigure()` (l'id passe à celui du module choisi). Idem `backend_id = -1` (défaut de `EngineCaps`) = AUTO → premier `ComputeBackend` chargé.
- **Liaison par couche** : au démarrage, bind du socle vers le haut — `ComputeBackend` d'abord, puis `Suppression`. Libération en ordre inverse (suppression avant backend) pour qu'aucune couche n'utilise un backend déjà détruit.
- **`kindName(ModuleKind)`** : helper public `inline const char*` dans `ns_engine.h` (littéral statique, zéro alloc, portable). Utilisé pour préciser le kind dans les logs.
- **Logs uniformisés** : on construit la chaîne dans une variable locale (`msg` pour `LOG`, `msg_err` pour `LOG_ERR`) AVANT l'appel, jamais en imbrication. `error(from, what, why)` avec `from = __func__` (portable GCC/MSVC). Dans le loader, les diagnostics identifient le module fautif par son sous-dossier (kind présumé) et son nom de fichier, ex. : « Loader cannot resolve entry symbol of 'backends' module 'backend_cuda.dll' ».

## Modèle de traitement (conçu, non implémenté)

- Suppression sur **amplitude spectrale** uniquement (pas de phase, mono-micro)
- **3 couches soustractives** : Card (figée), Mic (figée), Env (figée + adaptative par minimum-statistics)
- **Adaptation** gated par niveau (bas=bruit=MAJ, haut=voix=gel)
- **Plancher G_min** : scalaire (standard) ou courbe par bande (avancé), jamais zéro
- **Boucle auto-correctrice** : agressivité dans corridor borné (résidu silence, kurtosis=bruit musical, harmonicité=voix)
- **STFT** : N et Hop parametrable default a N=1024, hop=N/4 (75%), WOLA root-Hann, convolution linéaire (qualité) / gain-smoothing (léger), fenêtres asymétriques (faible latence), 24-40 bandes Bark/ERB
- **GPU** : 1 stream CUDA/piste, profils read-only VRAM, pré-allocation N_max (hot reconfig sans cudaMalloc en RT)
- **Extension 2-micros** : NLMS/RLS/beamforming + post-filtrage spectral (même horloge requise)
- **Paramètres hot** : double-buffer atomique + crossfade, pas de destruction UI
- **Backend GTX vs RTX** : résolu par détection de capacité (compute capability, Tensor Cores), pas par choix utilisateur

## État et prochaines étapes

**Phase 1 — TERMINÉE** : ossature, LogManager, loader, frontière C, smoke test, 3 chaînes de compilation validées.

**Phase 2 — EN COURS** :
1. ✅ **Routage audio** : `process()` traverse le module de suppression actif (validé par `passthrough`, in=out).
2. ✅ **Backends de calcul comme modules** : `ComputeBackend` chargé dynamiquement comme la suppression ; repli CPU (`backend_cpu` id=0) ; cœur sans dépendance CUDA ; bind par couche. `backend_cuda` sorti du cœur, regroupé avec `stft_chain` en un module compilé MSVC.

Conventions adoptées en route : modules rangés par kind + loader récursif ; id propre au kind ; logs uniformisés ; `kindName()` dans `ns_engine.h`.

**Prochaines étapes** :
3. Chaîne STFT réelle (fenêtrage → FFT → gain par bande → iFFT → overlap-add) — **prochaine**
4. Profils de bruit 3 couches + adaptation
5. Module CUDA isolé (code GPU réel, compilé MSVC, chargé par le cœur UCRT64)
6. Boucle auto-correctrice
7. Plugin JUCE (VST3/CLAP)
8. Extension 2-micros
9. Standalone gtkmm

## Règles de travail établies
- **Pas de présupposition** : ne faire que ce qui est demandé, annoncer le plan avant de coder.
- **Fichiers de référence** : les versions uploadées par l'utilisateur font autorité. Ne pas les réécrire sans demande. et se servir des fichiers mis a jour dans le projets comme depart
- **En-têtes de licence** : ne jamais supprimer ceux qui existent ; pas de propagation pour le moment.
- **Commentaires** : ne pas supprimer les commentaires existants ; en ajouter quand c'est utile.
- **Includes** : privilégier `common.h` quand un include se retrouve dans plusieurs fichiers (centralisation). Dépendances directes spécifiques restent dans leur fichier (IWYU).
- **Garde d'export** : `LOGGER` (générique, pas de ref odenise ni gx). `ODENISE_API` pour l'API publique du cœur.
- **`.cu`** : les stubs actuels utilisent `namespace ns::cuda` (forme condensée) — convention conservée telle quelle, on ne change pas les namespaces. `/W4` conditionné au C++ uniquement (jamais transmis à nvcc).
- **Portabilité GCC/MSVC** : éviter l'addition de deux `const char*` (la macro `_()` rend un `const char*`) ; concaténer via une `std::string` ou des `+=` successifs. `__func__` plutôt que `__PRETTY_FUNCTION__` (ce dernier absent sous MSVC).
- **Langue** : conversation en français, code et commentaires techniques en mix fr/en.
