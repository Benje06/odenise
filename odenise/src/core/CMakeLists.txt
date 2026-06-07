# ============================================================================
#  src/core/CMakeLists.txt -- libodenise : le coeur DSP.
#  Bibliotheque PARTAGEE reutilisable. ZERO dependance JUCE/gtkmm/GLib.
#  ZERO dependance CUDA : tout le calcul GPU est dans des modules dynamiques.
#
#  Layout des sources :
#    interfaces/ns_engine.h        -> API publique (ModuleBase, BackendBase, ...)
#    chain/audio_chain.h/.cpp      -> chaine cablee (interne, non installe)
#    registry/module_registry.h    -> registre des modules (interne)
#    registry/loader.cpp           -> chargement dlopen/LoadLibrary
#    tools/logger.h/.cc            -> LogManager (interne + export LOGGER)
#    tools/common.h / debug.h      -> utilitaires communs
#    engine.cpp                    -> EngineImpl, createEngine()
# ============================================================================

set(ODENISE_CORE_SOURCES
    engine.cpp
    registry/module_registry.cpp
    audio/AudioEditor.cpp
    audio/AudioProcessor.cpp
)

add_library(odenise_core SHARED ${ODENISE_CORE_SOURCES})
set_target_properties(odenise_core PROPERTIES
    OUTPUT_NAME   odenise
    VERSION       ${PROJECT_VERSION}
    SOVERSION     ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# ---------------------------------------------------------------------------
#  Includes
#  PUBLIC  : interfaces/ expose ns_engine.h aux consommateurs (modules, tests,
#            plugin). Le chemin BUILD_INTERFACE permet d'inclure sans install ;
#            INSTALL_INTERFACE est le chemin apres cmake --install.
#  PRIVATE : chain/, registry/, tools/ sont internes au coeur uniquement.
# ---------------------------------------------------------------------------
target_include_directories(odenise_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interfaces>
        $<INSTALL_INTERFACE:include/${ODENISE_VERSION_DIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/audio
        ${CMAKE_CURRENT_SOURCE_DIR}/chain
        ${CMAKE_CURRENT_SOURCE_DIR}/tools
        ${CMAKE_CURRENT_SOURCE_DIR}/registry)

target_compile_features(odenise_core PUBLIC cxx_std_20)
target_compile_definitions(odenise_core PRIVATE
    ODENISE_BUILD_LIBRARY=1
    LOGGER_EXPORTS=1
    DEBUG_TOOL=1
    ODENISE_VERSION_DIR="${ODENISE_VERSION_DIR}")

# libdl sous Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(odenise_core PUBLIC ${CMAKE_DL_LIBS})
endif()

# Libs utilitaires partagees : LogManager singleton et AudioChain, gxthread.
# PUBLIC : transitent vers les consommateurs de odenise_core (tests, plugin).
target_link_libraries(odenise_core PUBLIC odenise_log odenise_chain odenise_thread)

# Avertissements stricts, C++ uniquement (pas vers nvcc)
if(MSVC)
    target_compile_options(odenise_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /Zi /DEBUG>)
else()
    target_compile_options(odenise_core PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
endif()

# ---------------------------------------------------------------------------
#  Installation (convention gxinterface/dx7interface)
#  Linux  : /usr/lib/libodenise-<version>.so
#           /usr/include/odenise/<version>/ns_engine.h
#  Windows: usr/bin/odenise.dll
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS odenise_core
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})

# ns_engine.h est le seul header public installe.
# Les headers internes (chain/, registry/, tools/) ne sont pas installes.
install(FILES interfaces/engine.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${ODENISE_VERSION_DIR})
