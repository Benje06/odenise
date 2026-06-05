# ============================================================================
#  lib/log/CMakeLists.txt -- libodenise_log : LogManager singleton.
#
#  Bibliotheque partagee utilitaire, linkee par :
#    - libodenise (coeur)
#    - les modules qui ont besoin de LOG/LOG_ERR (backend_cpu, ...)
#
#  Le singleton LogManager est unique dans le processus grace au link
#  dynamique : toutes les instances qui linkent contre libodenise_log
#  partagent le meme objet en memoire.
#
#  Sources : logger.cc + headers communs (logger.h, common.h, debug.h)
#  depuis src/core/tools/ -- les sources restent a leur emplacement d'origine.
# ============================================================================

add_library(odenise_log SHARED
    ${CMAKE_SOURCE_DIR}/src/core/tools/logger.cc)

set_target_properties(odenise_log PROPERTIES
    OUTPUT_NAME               odenise_log
    VERSION                   ${PROJECT_VERSION}
    SOVERSION                 ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON)

# PUBLIC : expose logger.h (et par transitivite common.h, debug.h) aux
# consommateurs. BUILD_INTERFACE pour le build, INSTALL_INTERFACE apres install.
target_include_directories(odenise_log
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/core>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/core/interfaces>
        $<INSTALL_INTERFACE:include/${ODENISE_VERSION_DIR}>)

target_compile_features(odenise_log PUBLIC cxx_std_20)
target_compile_definitions(odenise_log PRIVATE
    LOGGER_EXPORTS=1
    ODENISE_BUILD_LIBRARY=1)

if(MSVC)
    target_compile_options(odenise_log PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(odenise_log PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
endif()

include(GNUInstallDirs)
install(TARGETS odenise_log
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
