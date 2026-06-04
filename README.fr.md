## 🌐 Autres Languages
[English](README.md)

- [Introduction](#introduction)
- [Déscription](#déscription)
- [HOWTO](#howto)
    - [Windows](#windows)
    - [Linux](#linux)
    - [Sources](#construction-à-partir-des-sources)
    - [Debug](#debug)
- [État du projet](#état-du-projet)
- [Historique](#historique)
- [Remerciements](#remerciements)
- [Réferences](#références)
- [Licenses](#licenses)
- [Auteurs et Contributeurs](#auteurs-et-contributeurs)

# Introduction
**odenise** (open-denoise) est un **suppresseur de bruit spectral temps réel**, destiné à fonctionner comme **plugin** (**VST3** / **CLAP**) et en **application autonome**.\
Il est **accéléré sur GPU** via **CUDA**, dès les cartes **NVIDIA GTX série 10** (architecture **Pascal**) et au-delà, avec un **repli CPU** toujours disponible.

Son principe s'inspire de **RTX Voice**, mais odenise est conçu pour s'exécuter sur les **cœurs CUDA généralistes** (et non les Tensor Cores), ce qui le rend utilisable sur du matériel **Pascal** que NVIDIA Broadcast / Maxine excluent.

> ⚠️ **Projet en cours de développement.** À ce stade, seule l'**ossature** (chargement, journalisation, chaîne de compilation sur les trois cibles) est fonctionnelle. Le traitement audio n'est pas encore implémenté. Voir [État du projet](#état-du-projet).

# Déscription
odenise applique une **suppression de bruit dans le domaine spectral**, sur l'**amplitude** uniquement — l'inversion de phase est impossible avec un seul micro, faute de référence de bruit instantanée.

Le bruit est modélisé en **trois couches soustractives** :
- **Carte** : carte son + préampli (figée)
- **Micro** : capsule du microphone (figée)
- **Environnement** : bruit ambiant (**adaptative**)

Chaque couche est **enregistrable** et **rechargeable** sous forme de profil, et les couches se **recomposent**.

Il prendra en charge :
- L'**adaptation** de la couche environnement par **statistiques de minimum** sur fenêtre glissante, déclenchée par le niveau (niveau bas = pas de voix = mise à jour ; au-dessus du seuil = voix = gel).
- Un **plancher spectral** (G_min) évitant de viser le silence, source de **bruit musical**, en mode scalaire (standard) ou par **courbe par bande** (avancé).
- Une **boucle auto-correctrice** ajustant l'agressivité dans un corridor borné (résidu de bruit, kurtosis pour le bruit musical, harmonicité pour la préservation de la voix).
- Une **chaîne STFT** configurable : N = 1024 par défaut, recouvrement 75 %, fenêtres racine-de-Hann **WOLA**, convolution linéaire propre, fenêtres asymétriques pour le mode faible latence, **24 à 40 bandes perceptuelles** (Bark / ERB).
- Un **parallélisme GPU** : un **stream CUDA par piste**, profils partagés en lecture seule en VRAM, **pré-allocation** au format maximal pour les changements de configuration à chaud sans allocation dans le chemin temps réel.
- Une **extension double-micro** (NLMS / RLS / beamforming + post-filtrage spectral) permettant la vraie **annulation de phase**, sous réserve de deux entrées sur la **même horloge**.
- La **journalisation** dans un **fichier** et dans la **console**.

Il est écrit en **C++** et organisé en **couches** :
- un **cœur** réutilisable (`libodenise`), en C++ pur, **sans dépendance** à JUCE, gtkmm ou GLib ;
- des **modules** chargés dynamiquement (dlopen / LoadLibrary), via une **frontière C** (table de fonctions, vérification d'ABI) ;
- des **enveloppes** plugin (JUCE) et autonome (gtkmm), à venir.

Le **cœur** fournit les fonctions de base telles que :
- **Créer / Charger** des modules dynamiquement
- La gestion des **backends de calcul** (CPU, CUDA, extensibles)
- L'orchestration du **traitement** et le **double-buffering** des paramètres à chaud
- La gestion des **journaux** (LogManager)
- La gestion des **profils** de bruit

Chaque **module** fournira :
- un type de **suppression** (passthrough, soustraction spectrale, etc.)
- ou un **backend** de calcul (implémentation CPU / CUDA d'une chaîne de traitement)
- ou un mode **double-micro**

# HOWTO
## Windows
odenise vise une intégration comme **plugin** dans les hôtes audio (OBS via VST, ou tout DAW prenant en charge VST3 / CLAP), ainsi qu'un mode **autonome**.\
L'accélération GPU nécessite un pilote **NVIDIA** récent et une carte **Pascal (GTX série 10)** ou ultérieure. À défaut, le **repli CPU** est utilisé automatiquement (détection de capacité).

> Les binaires de distribution ne sont pas encore publiés (projet en cours).

## Linux
odenise est multiplateforme par conception. Sous Linux, le cœur se compile en CPU pur (GCC), et l'accélération CUDA est disponible avec le toolkit NVIDIA installé.

> Les paquets de distribution ne sont pas encore publiés.

## Construction à partir des sources

Vous aurez besoin :
- **Commun** de DEV :
    - **CMake** >= 3.25 (policy CMP0141 pour le format d'info debug MSVC)
    - **base-devel** / **build-essential** ( gcc / g++ >= 13, make, ninja )
      - Windows : + mingw-w64-ucrt-x86_64-toolchain
    - **Ninja** (générateur par défaut pour les builds CPU)
    - **gettext** >= 0.21 (i18n, prêt côté code, non encore activé dans le build)
- Spécifique **CUDA** (optionnel, pour l'accélération GPU) :
    - **CUDA Toolkit 12.9** — **dernière version supportant Pascal/sm_61** ; CUDA 13.x abandonne Pascal.
    - Architectures générées : `61;75;86;89;90` (Pascal → Blackwell).
    - **Windows** :
        - **Visual Studio 2022** (ou Build Tools 2022) avec le **toolset MSVC v143**.\
          CUDA 12.9 n'accepte que les toolsets MSVC **2017-2022** ; un toolset 2026 (v14.5x) fait échouer `cudafe++`.
        - Le **composant d'intégration Visual Studio de CUDA** doit être installé (sinon CMake signale « No CUDA toolset found »).
- Spécifique à **Windows/UCRT64** (build CPU) :
    - MSYS2/UCRT64 avec mingw-w64-ucrt-x86_64-toolchain

### Environnements de développement
#### <ins>VS Code pour Windows</ins>
Le projet se pilote via l'extension **CMake Tools** et les **presets** CMake.\
Après clonage, ajustez dans `.vscode/settings.json` le `cmake.sourceDirectory` vers votre répertoire, et vérifiez dans `CMakePresets.json` le chemin du compilateur CUDA (`CMAKE_CUDA_COMPILER`) et, pour le préset MSVC, le toolset `v143`.

Pour le build CUDA sous Windows, lancez VS Code depuis un **« x64 Native Tools Command Prompt for VS 2022 »** afin que l'environnement MSVC (variable `INCLUDE`, etc.) soit complet.

### <ins>À la main</ins>

#### Presets disponibles
- `linux` / `linux-core` — Linux, GCC, CPU (Ninja)
- `ucrt64` / `ucrt64-core` — Windows MSYS2/UCRT64, GCC, CPU (Ninja)
- `windows-msvc-cuda` — Windows, MSVC v143 + CUDA 12.9 (générateur Visual Studio)

#### Construction (CPU, le plus simple)
```sh
$ cmake --preset ucrt64-core      # ou linux-core sous Linux
$ cmake --build --preset ucrt64-core
```

#### Construction (Windows + CUDA)
```sh
# depuis un "x64 Native Tools Command Prompt for VS 2022"
$ cmake --preset windows-msvc-cuda
$ cmake --build --preset windows-msvc-cuda
```

#### Exécuter le test de chaîne de chargement
Les binaires sont regroupés dans `build/<preset>/bin/` (et `bin/Release/` avec le générateur Visual Studio). L'exécutable de test y trouve la bibliothèque à côté de lui.
```sh
$ ./build/<preset>/bin/test_core
```
Sortie attendue (console et `odenise_test.log`) :
```
=== test: load chain ===
loader: module directory not found: modules
engine: created (n=1024, modules loaded: 0)
engine created, latency = 1024 samples
dynamic backends found: 0
suppression modules: 0
=== load chain test passed ===
```

# DEBUG
- La **journalisation** se règle à trois niveaux via le LogManager :\
<code>0 = aucun log, 1 = log fichier, 2 = log fichier + console</code>
- Le fichier de log par défaut des tests est `odenise_test.log`, créé dans le répertoire d'exécution.
- Pour un build CPU de débogage, utilisez les presets `*-core` (sans CUDA) : ils isolent le travail sur le cœur des contraintes de la chaîne GPU.

# État du projet
**Phase 1 — terminée et validée** sur les trois chaînes (Linux/GCC, Windows UCRT64/GCC, Windows MSVC+CUDA) :
- ossature en couches, cœur `libodenise` sans dépendance JUCE/gtkmm/GLib ;
- chargement dynamique des modules (dlopen / LoadLibrary) via frontière C + vérification d'ABI ;
- **LogManager** unifié (singleton thread-safe, niveaux 0/1/2, `LOG` / `LOG_ERR`), repris de gxinterface et nettoyé de GLib ;
- point d'entrée des tests avec **try/catch généralisé** (hors chemin temps réel) ;
- chaîne de compilation **CMake** + presets ; build CUDA via le générateur Visual Studio ;
- i18n (gettext) en place côté code, en mode pass-through.

À ce stade, le traitement audio (`process`) renvoie volontairement « non supporté » : il n'y a pas encore de DSP.

**Suite** : premier module dynamique « passthrough », puis routage audio, interface des backends de calcul, chaîne STFT réelle, profils de bruit, module CUDA isolé, et enveloppes plugin / autonome.

# Historique
odenise est né du besoin d'un suppresseur de bruit pour le **streaming en direct**, dans la lignée du travail mené autour de **RTX Voice** sur cartes **GTX 1080** : RTX Voice a démontré que le débruitage GPU fonctionne sur Pascal via les cœurs CUDA généralistes, alors que NVIDIA Broadcast / Maxine exigent des Tensor Cores et excluent donc ce matériel.

Le projet réutilise l'**écosystème** d'outils développé pour **gxinterface** / **dx7interface** : le LogManager, les utilitaires communs, le principe de chargement de modules dynamiques. Le cœur a toutefois été conçu **sans dépendance GLib**, afin de rester utilisable aussi bien dans un plugin audio multiplateforme que dans l'écosystème GTK existant.

# Remerciements
- À l'écosystème **gxinterface / dx7interface** pour les outils communs (LogManager, debug, conventions).

# Références
- **CUDA**
  - [CUDA Toolkit 12.9](https://developer.nvidia.com/cuda-toolkit)
  - [Pascal Compatibility Guide](https://docs.nvidia.com/cuda/pascal-compatibility-guide/)
- **Audio / plugins**
  - [JUCE](https://juce.com/)
  - [CLAP](https://cleveraudio.org/)
- **Traitement du signal**
  - STFT / WOLA, soustraction spectrale, statistiques de minimum (minimum statistics).

# Licenses
## odenise
Copyright (C) 2025-2026 Jérôme BENHAÏM, sous [GPL-v3](https://www.gnu.org/licenses/gpl-3.0.html)

Les outils communs (`common.h`, `debug.h`, `logger.h`, `logger.cc`) sont dérivés de **GxInterface**, également sous GPL-v3.

# Auteurs et Contributeurs
## Auteurs :
[BENHAÏM Jérôme](https://github.com/Benje06)

## Contributeurs :
ENNAIME Mirsal — merci pour le `debug.h` d'origine (via gxinterface).
