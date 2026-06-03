# ============================================================================
#  odenise -- debruitage spectral GPU/CPU (plugin VST3/CLAP + standalone)
#  Build principal : CMake. Un squelette autotools (lib/*.am, *.pc.in) est
#  laisse possible en parallele (build hybride), mais CMake est le chemin
#  maintenu.
# ============================================================================
cmake_minimum_required(VERSION 3.25)   # CUDA_ARCHITECTURES, FetchContent, CMP0141

project(odenise
    VERSION     0.1.0
    LANGUAGES   CXX                    # CUDA active conditionnellement plus bas
    DESCRIPTION "Spectral noise suppressor (GPU/CPU), VST3/CLAP + standalone")

# CMP0141 (CMake >= 3.25) : active le controle du format d'info debug MSVC via
# MSVC_DEBUG_INFORMATION_FORMAT. Necessaire pour embarquer le debug (/Z7) cote
# CUDA et eviter le -Fd qui casse nvcc (cf. src/core/CMakeLists.txt).
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

# ---------------------------------------------------------------------------
#  Standard et reglages globaux
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)             # std::span, etc.
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # lib partagee + modules .so/.dll
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Regrouper tous les binaires au meme endroit : sous Windows une .exe doit
# trouver la .dll a cote d'elle (pas de rpath). exe + dll -> bin/ ; .lib/.a -> lib/.
# (Generateur multi-config VS : sous-dossier bin/<Config>/, exe et dll ensemble.)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Export compile_commands.json pour clangd / IntelliSense (VSCode).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
#  Options du projet
# ---------------------------------------------------------------------------
option(ODENISE_WITH_CUDA      "Compile le backend CUDA si un compilateur CUDA est present" ON)
option(ODENISE_BUILD_PLUGIN   "Compile le frontal JUCE (VST3/CLAP/Standalone JUCE)"        ON)
option(ODENISE_BUILD_MODULES  "Compile les modules dynamiques (.so/.dll)"                  ON)
option(ODENISE_BUILD_TESTS    "Compile le harnais de self-test par module"                 ON)
# Le standalone gtkmm n'est PAS gere ici (ecosysteme gxinterface/dx7interface) :
# il sera ajoute separement et ne dependra que de l'interface Engine.

# ---------------------------------------------------------------------------
#  Detection CUDA (optionnelle : absence -> repli CPU pur)
# ---------------------------------------------------------------------------
include(CheckLanguage)
check_language(CUDA)
if(ODENISE_WITH_CUDA AND CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 20)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    # Fatbinary : sm_61 natif (Pascal / GTX serie 10) + Turing/Ampere/Ada/Hopper,
    # la derniere arch emet aussi du PTX pour le JIT forward (generations futures).
    # IMPORTANT : sm_61 (Pascal) n'est compilable que jusqu'a CUDA 12.x (deprecie
    # en 12.8/12.9, SUPPRIME en 13.0). Pour cibler la GTX serie 10, utiliser le
    # toolkit CUDA 12.9 (dernier palier Pascal). Avec CUDA >= 13, retirer le 61.
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 61 75 86 89 90)
    endif()
    # CUDAToolkit fournit les cibles importees (CUDA::cufft, etc.).
    find_package(CUDAToolkit REQUIRED)
    set(ODENISE_HAVE_CUDA TRUE)
    # NB : CUDA 12.9 exige un hote MSVC 2017-2022. Le preset MSVC pointe
    # CMAKE_CUDA_HOST_COMPILER sur le cl.exe du toolset v143 (VS 2022), pas
    # sur celui de VS 2026 (14.51) qui fait crasher cudafe++.
    message(STATUS "odenise: CUDA active -- archs = ${CMAKE_CUDA_ARCHITECTURES}")
else()
    set(ODENISE_HAVE_CUDA FALSE)
    if(ODENISE_WITH_CUDA)
        message(STATUS "odenise: aucun compilateur CUDA trouve -> repli CPU pur")
    else()
        message(STATUS "odenise: CUDA desactive par option -> repli CPU pur")
    endif()
endif()

# ---------------------------------------------------------------------------
#  JUCE (frontal plugin) -- via FetchContent, version epinglee.
#  Recupere uniquement si on construit le plugin ET si le sous-repertoire
#  existe deja (le frontal sera ecrit ulterieurement).
#  NB: ajuster JUCE_TAG a la version stable voulue avant le premier build.
# ---------------------------------------------------------------------------
if(ODENISE_BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/CMakeLists.txt")
    include(FetchContent)
    set(JUCE_TAG "8.0.13" CACHE STRING "Tag/branche JUCE a recuperer")
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
    set(ODENISE_PLUGIN_READY TRUE)
else()
    set(ODENISE_PLUGIN_READY FALSE)
endif()

# ---------------------------------------------------------------------------
#  Sous-repertoires (toutes les sources sont sous src/).
#  Gardes EXISTS : le projet configure des maintenant avec le seul coeur ;
#  chaque sous-repertoire s'active automatiquement quand son CMakeLists existe.
# ---------------------------------------------------------------------------
add_subdirectory(src/core)             # libodenise : coeur C++/CUDA, zero UI

if(ODENISE_BUILD_MODULES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/modules/CMakeLists.txt")
    add_subdirectory(src/modules)
endif()

if(ODENISE_PLUGIN_READY)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui_shared/CMakeLists.txt")
        add_subdirectory(src/ui_shared)
    endif()
    add_subdirectory(src/plugin)
endif()

if(ODENISE_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
#  Recapitulatif de configuration
# ---------------------------------------------------------------------------
message(STATUS "---------------------------------------------------------")
message(STATUS " odenise ${PROJECT_VERSION}")
message(STATUS "   CUDA backend   : ${ODENISE_HAVE_CUDA}")
message(STATUS "   Plugin (JUCE)  : ${ODENISE_PLUGIN_READY}")
message(STATUS "   Modules .so    : ${ODENISE_BUILD_MODULES}")
message(STATUS "   Tests          : ${ODENISE_BUILD_TESTS}")
message(STATUS "---------------------------------------------------------")
