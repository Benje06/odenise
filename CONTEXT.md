# odenise — Synthèse technique (contexte pour prompt)

## Identité du projet
**odenise** (open-denoise) : suppresseur de bruit spectral temps réel, modulaire, plugin VST3/CLAP + standalone, GPU (CUDA, dès GTX série 10 / Pascal) avec repli CPU. sur cœurs CUDA généralistes pour le premier module et Tensor Cores possible sur un autre module. Licence GPL-v3. Auteur : Jérôme Benhaïm (Uru).

## Architecture en couches

**Couche 1 — Cœur (`libodenise`)** : bibliothèque partagée C++ pur (C++20). ZERO dépendance JUCE/gtkmm/GLib. Compilée en GCC/UCRT64 (Linux et Windows). Contient : orchestration moteur, loader de modules (dlopen/LoadLibrary), LogManager, backends de calcul. Visibilité `hidden` + API marquée `ODENISE_API`. Le header public est `ns_engine.h` (namespace `ns`).

**Couche 2 — Modules dynamiques** : `.so`/`.dll` séparés, chargés au runtime. Frontière **C pure** (vtable `OdeniseModuleVTable`, symbole `odenise_module_entry`, POD uniquement, vérification `kAbiVersion`). Familles : `ComputeBackend`, `Suppression`, `Window`, `DualMic`, `Inference`. Ne linkent PAS contre le cœur. Un module CUDA sera compilé séparément en MSVC (deux compilateurs, frontière C les relie).

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
    modules/                            → passthrough.so/.dll, backend_cuda.dll, ...
    data/cfg/                           → profils de bruit (futur)
    data/ui/                            → XML GtkBuilder (futur)
  share/locale/                         → .mo gettext (futur)
  share/pkgconfig/                      → odenise.pc (futur)
```

Prefix selon l'environnement : `/usr` (Arch/Debian), `C:/Program Files/odenise/usr` (Windows NSIS), `/usr/local` (dev local). Le layout de build reproduit cette structure sous `build/<preset>/` pour que les tests tournent sans install.

`moduleDir()` dans `engine.cpp` résout : env `ODENISE_MODULE_PATH` > `<exe_dir>/../share/odenise/<version>/modules/` (fonctionne en build et après install, quel que soit le cwd).

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

**Découpage compilateurs** : UCRT64/GCC compile tout (cœur + modules CPU). MSVC+nvcc compile uniquement le module CUDA (`.dll` séparé, chargé par dlopen, frontière C). Un seul arbre source, la compilation aiguille.

**Variables CMake clés** :
- `ODENISE_VERSION_DIR` = `odenise/<version>` (passé comme define C++ pour `moduleDir()`)
- `ODENISE_MODULE_OUTPUT_DIR` / `ODENISE_MODULE_INSTALL_DIR` — sortie/install des modules
- `ODENISE_DATA_OUTPUT_DIR` / `ODENISE_DATA_INSTALL_DIR` — données applicatives
- `ODENISE_HAVE_CUDA` — détecté automatiquement, conditionne les sources `.cu`
- `LOGGER_EXPORTS=1` — défini à la compilation du cœur pour exporter `LogManager`

## Fichiers existants (phase 1 + début phase 2)

```
CMakeLists.txt                          — racine, layout, détection CUDA, sous-répertoires
CMakePresets.json                       — presets linux/ucrt64/msvc-cuda
src/core/
  CMakeLists.txt                        — cible odenise_core SHARED
  ns_engine.h                           — API publique (Engine, enums, vtable C modules)
  module_registry.h                     — header interne (ModuleRegistry)
  engine.cpp                            — EngineImpl (stub), moduleDir(), createEngine()
  loader.cpp                            — dlopen/LoadLibrary, ABI check, LOG_ERR
  backends/backend_cpu.cpp              — stub CPU
  backends/backend_cuda.cu              — stub CUDA (namespace classique ns { namespace cuda {} })
  stft_chain.cu                         — stub STFT GPU
  tools/
    common.h                            — utilitaires communs sans GLib, gettext pur
    debug.h                             — macros debug (PRE_LOG, LOG_IN, etc.)
    logger.h                            — LogManager/Logger, garde LOGGER
    logger.cc                           — implémentation LogManager
src/modules/
  CMakeLists.txt                        — cibles MODULE, PREFIX ""
  passthrough/passthrough.cpp           — module neutre (in=out), vtable C, self-test
tests/
  CMakeLists.txt                        — cible test_core
  main.cc                               — point d'entrée tests, init_nls, LogManager, try/catch
```

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

**Phase 2 — EN COURS** : module passthrough chargé et self-testé. Layout de déploiement dx7interface en place.

**Prochaines étapes** :
1. `process()` dans le moteur qui route l'audio à travers le module passthrough (valide la plomberie RT)
2. Interface des backends de calcul (séparation orchestration / calcul)
3. Chaîne STFT réelle (feneêtrage → FFT → gain par bande → iFFT → overlap-add)
4. Profils de bruit 3 couches + adaptation
5. Module CUDA isolé (compilé MSVC, chargé par le cœur UCRT64)
6. Boucle auto-correctrice
7. Plugin JUCE (VST3/CLAP)
8. Extension 2-micros
9. Standalone gtkmm

## Règles de travail établies
- **Pas de présupposition** : ne faire que ce qui est demandé, annoncer le plan avant de coder.
- **Fichiers de référence** : les versions uploadées par l'utilisateur font autorité. Ne pas les réécrire sans demande. et se servir des fichiers mis a jour dans le projets comme depart
- **En-têtes de licence** : ne jamais supprimer ceux qui existent ; pas de propagation pour le moment.
- **Includes** : privilégier `common.h` quand un include se retrouve dans plusieurs fichiers (centralisation). Dépendances directes spécifiques restent dans leur fichier (IWYU).
- **Garde d'export** : `LOGGER` (générique, pas de ref odenise ni gx). `ODENISE_API` pour l'API publique du cœur.
- **`.cu`** : namespace en forme classique (`namespace ns { namespace cuda { } }`, pas `ns::cuda`) pour compatibilité nvcc device frontend. `/W4` conditionné au C++ uniquement.
- **Langue** : conversation en français, code et commentaires techniques en mix fr/en.
