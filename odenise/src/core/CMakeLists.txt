# ============================================================================
#  src/core/CMakeLists.txt -- libodenise : le coeur DSP.
#  Bibliotheque PARTAGEE reutilisable. ZERO dependance JUCE/gtkmm/GLib.
#  ZERO dependance CUDA : tout le calcul GPU est dans des modules dynamiques.
# ============================================================================

set(ODENISE_CORE_SOURCES
    engine.cpp
    loader.cpp
    tools/logger.cc
)

add_library(odenise_core SHARED ${ODENISE_CORE_SOURCES})
set_target_properties(odenise_core PROPERTIES
    OUTPUT_NAME   odenise
    VERSION       ${PROJECT_VERSION}
    SOVERSION     ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

target_include_directories(odenise_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

target_compile_features(odenise_core PUBLIC cxx_std_20)
target_compile_definitions(odenise_core PRIVATE
    ODENISE_BUILD_LIBRARY=1
    LOGGER_EXPORTS=1
    ODENISE_VERSION_DIR="${ODENISE_VERSION_DIR}")

# libdl sous Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(odenise_core PUBLIC ${CMAKE_DL_LIBS})
endif()

# Avertissements stricts, C++ uniquement (pas vers nvcc)
if(MSVC)
    target_compile_options(odenise_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(odenise_core PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
endif()

# --- Installation (convention gxinterface/dx7interface) ---------------------
# Linux : /usr/lib/libodenise-<version>.so
#         /usr/include/odenise/<version>/ns_engine.h
# Windows : usr/bin/odenise.dll
include(GNUInstallDirs)
install(TARGETS odenise_core
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ns_engine.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${ODENISE_VERSION_DIR})
