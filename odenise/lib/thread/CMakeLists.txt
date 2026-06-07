# ============================================================================
#  lib/thread/CMakeLists.txt -- libodenise_thread : thread facility
#
#  Bibliotheque partagee utilitaire, linkee par :
#    - libodenise (coeur)
#    - les modules qui ont besoin de thread (backend_cpu, ...)
#
#  thread facility using pthread
#
#  Sources : gxthread.cc + headers communs (logger.h, common.h, debug.h)
#  depuis src/core/tools/ -- les sources restent a leur emplacement d'origine.
# ============================================================================

add_library(odenise_thread SHARED
    ${CMAKE_SOURCE_DIR}/src/core/tools/gxthread.cc)

set_target_properties(odenise_thread PROPERTIES
    OUTPUT_NAME               odenise_thread
    VERSION                   ${PROJECT_VERSION}
    SOVERSION                 ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON)

# PUBLIC : expose gxthread.h (et par transitivite common.h, debug.h) aux
# consommateurs. BUILD_INTERFACE pour le build, INSTALL_INTERFACE apres install.
target_include_directories(odenise_thread
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/core>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/core/tools>
        $<INSTALL_INTERFACE:include/${ODENISE_VERSION_DIR}>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core/tools)

target_compile_features(odenise_thread PUBLIC cxx_std_20)
target_compile_definitions(odenise_thread PRIVATE
    THREAD_EXPORTS=1
    ODENISE_BUILD_LIBRARY=1
    DEBUG_TOOL=1)

target_link_libraries(odenise_thread PUBLIC odenise_log)

if(MSVC)
    target_compile_options(odenise_thread PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(odenise_thread PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
endif()

include(GNUInstallDirs)
install(TARGETS odenise_thread
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
