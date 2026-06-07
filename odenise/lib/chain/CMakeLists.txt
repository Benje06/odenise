# ============================================================================
#  lib/chain/CMakeLists.txt -- libodenise_chain : AudioChain.
#
#  Bibliotheque partagee utilitaire, linkee par :
#    - libodenise (coeur)
#    - les modules qui gerent leur propre chaine de traitement (backend_cpu, ...)
#
#  AudioChain est un utilitaire de cablage (liste plate de ChainElement,
#  double buffer, swap atomique, accumulation de latence). Elle depend de
#  LogManager (LOG/LOG_ERR) -- donc linke contre odenise_log.
#
#  Sources : audio_chain.cpp + audio_chain.h depuis src/core/chain/ --
#  les sources restent a leur emplacement d'origine.
# ============================================================================

add_library(odenise_chain SHARED
    ${CMAKE_SOURCE_DIR}/src/core/chain/audio_chain.h
    ${CMAKE_SOURCE_DIR}/src/core/chain/audio_chain.cpp)

set_target_properties(odenise_chain PROPERTIES
    OUTPUT_NAME               odenise_chain
    VERSION                   ${PROJECT_VERSION}
    SOVERSION                 ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON)

# PUBLIC : expose audio_chain.h et ns_engine.h aux consommateurs.
# PRIVATE : tools/ pour logger.h (via odenise_log en link).
target_include_directories(odenise_chain
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/core>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/core/interfaces>
        $<INSTALL_INTERFACE:include/${ODENISE_VERSION_DIR}>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/registry
        ${CMAKE_SOURCE_DIR}/src/core/tools)

target_compile_features(odenise_chain PUBLIC cxx_std_20)
target_compile_definitions(odenise_chain PRIVATE
    ODENISE_BUILD_LIBRARY=1
    CHAIN_EXPORTS=1
    DEBUG_TOOL=1)

# Depend de odenise_log pour LOG/LOG_ERR dans audio_chain.cpp.
target_link_libraries(odenise_chain PUBLIC odenise_log)

if(MSVC)
    target_compile_options(odenise_chain PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(odenise_chain PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
endif()

include(GNUInstallDirs)
install(TARGETS odenise_chain
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
