# ============================================================================
#  odenise -- debruitage spectral GPU/CPU (plugin VST3/CLAP + standalone)
# ============================================================================
cmake_minimum_required(VERSION 3.25)   # CUDA_ARCHITECTURES, FetchContent, CMP0141

project(odenise
    VERSION     0.1.0
    LANGUAGES   CXX
    DESCRIPTION "Spectral noise suppressor (GPU/CPU), VST3/CLAP + standalone")

# CMP0141 : controle du format d'info debug MSVC (evite le -Fd qui casse nvcc).
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

# ---------------------------------------------------------------------------
#  Standard et reglages globaux
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
#  Layout de deploiement (convention dx7interface/gxinterface).
#
#  Le prefix est passe a cmake via -DCMAKE_INSTALL_PREFIX :
#    Arch/Manjaro : /usr          (par defaut ou via makepkg)
#    Debian       : /usr          (via checkinstall/dpkg)
#    Windows NSIS : C:/Program Files/odenise/usr  (structure dx7interface)
#    Dev local    : /usr/local    (defaut CMake si non specifie)
#
#  Layout sous <prefix> :
#    bin/                              -> odenise(.exe), test_core(.exe)
#    lib/                              -> libodenise.so, libodenise_log.so,
#                                         libodenise_chain.so
#                                         (.dll va dans bin/ sous Windows)
#    include/odenise/<version>/        -> ns_engine.h (depuis src/core/interfaces/)
#    share/odenise/<version>/modules/  -> passthrough.so/.dll, ...
#    share/odenise/<version>/data/     -> cfg/, ui/ (futur)
#    share/locale/                     -> .mo gettext (futur)
#    share/pkgconfig/                  -> odenise.pc (futur)
#
#  Le build reproduit cette structure sous <builddir>/ pour que les tests
#  tournent sans install. L'install (cmake --install) utilise le prefix reel.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

set(ODENISE_VERSION_DIR "odenise/${PROJECT_VERSION}")

if(MSVC)
    set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel CACHE STRING "" FORCE)
endif()

# Layout de build : meme arbre que le prefix, sous le builddir.
# GNUInstallDirs fournit les noms relatifs (bin, lib, share, include).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/$<0:>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/$<0:>)

if(WIN32)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/$<0:>)
else()
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/$<0:>)
endif()

# Modules dynamiques : share/odenise/<version>/modules/
set(ODENISE_MODULE_OUTPUT_DIR  ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/${ODENISE_VERSION_DIR}/modules/$<0:>)
set(ODENISE_MODULE_INSTALL_DIR ${CMAKE_INSTALL_DATADIR}/${ODENISE_VERSION_DIR}/modules/$<0:>)

# Donnees applicatives : share/odenise/<version>/data/
set(ODENISE_DATA_OUTPUT_DIR    ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/${ODENISE_VERSION_DIR}/data/$<0:>)
set(ODENISE_DATA_INSTALL_DIR   ${CMAKE_INSTALL_DATADIR}/${ODENISE_VERSION_DIR}/data/$<0:>)

# ---------------------------------------------------------------------------
#  Options du projet
# ---------------------------------------------------------------------------
option(ODENISE_WITH_CUDA      "Compile le backend CUDA" ON)
option(ODENISE_BUILD_PLUGIN   "Compile le frontal JUCE (VST3/CLAP)" ON)
option(ODENISE_BUILD_MODULES  "Compile les modules dynamiques" ON)
option(ODENISE_BUILD_TESTS    "Compile le harnais de test" ON)

# ---------------------------------------------------------------------------
#  Detection CUDA (optionnelle)
# ---------------------------------------------------------------------------
include(CheckLanguage)
check_language(CUDA)
if(ODENISE_WITH_CUDA AND CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 20)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES "61;75;86;89;90")
    endif()
    find_package(CUDAToolkit REQUIRED)
    set(ODENISE_HAVE_CUDA TRUE)
    message(STATUS "odenise: CUDA active -- archs = ${CMAKE_CUDA_ARCHITECTURES}")
else()
    set(ODENISE_HAVE_CUDA FALSE)
    if(ODENISE_WITH_CUDA)
        message(STATUS "odenise: aucun compilateur CUDA -> repli CPU")
    else()
        message(STATUS "odenise: CUDA desactive par option")
    endif()
endif()

# ---------------------------------------------------------------------------
#  JUCE (si plugin demande et present)
# ---------------------------------------------------------------------------
if(ODENISE_BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/juce/CMakeLists.txt")
    include(FetchContent)
    set(JUCE_TAG "8.0.13" CACHE STRING "Tag JUCE")
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
    message(STATUS "juce_SOURCE_DIR = ${juce_SOURCE_DIR}")
    message(STATUS "FETCHCONTENT_SOURCE_DIR_JUCE = ${FETCHCONTENT_SOURCE_DIR_JUCE}")
    set(ODENISE_PLUGIN_READY TRUE)
else()
    set(ODENISE_PLUGIN_READY FALSE)
endif()

if(ODENISE_BUILD_PLUGIN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_subdirectory(
        ${juce_SOURCE_DIR}/extras/AudioPluginHost
        ${CMAKE_BINARY_DIR}/AudioPluginHost)
endif()
# ---------------------------------------------------------------------------
#  Sous-repertoires
#  Ordre : libs utilitaires d'abord (odenise_log, odenise_chain, odenise_thread),
#  puis le coeur (odenise_core) qui les consomme,
#  puis les modules et les tests.
# ---------------------------------------------------------------------------
add_subdirectory(lib/log)
add_subdirectory(lib/chain)
add_subdirectory(lib/thread)
#add_subdirectory(lib/audio)
add_subdirectory(src/core)

if(ODENISE_BUILD_MODULES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/modules/CMakeLists.txt")
    add_subdirectory(src/modules)
endif()

if(ODENISE_PLUGIN_READY)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui_shared/CMakeLists.txt")
        add_subdirectory(src/ui_shared)
    endif()
    add_subdirectory(src/plugin/juce)
endif()

if(ODENISE_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CTestCustom.cmake.in"
        "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
        @ONLY)
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
#  Recapitulatif
# ---------------------------------------------------------------------------
message(STATUS "---------------------------------------------------------")
message(STATUS " odenise ${PROJECT_VERSION}")
message(STATUS "   CUDA backend   : ${ODENISE_HAVE_CUDA}")
message(STATUS "   Plugin (JUCE)  : ${ODENISE_PLUGIN_READY}")
message(STATUS "   Modules        : ${ODENISE_BUILD_MODULES}")
message(STATUS "   Tests          : ${ODENISE_BUILD_TESTS}")
message(STATUS "   Module dir     : ${ODENISE_MODULE_OUTPUT_DIR}")
message(STATUS "---------------------------------------------------------")
