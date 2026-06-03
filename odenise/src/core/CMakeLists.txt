# ============================================================================
#  src/core/CMakeLists.txt -- libodenise : le coeur DSP/CUDA.
#  Bibliotheque PARTAGEE reutilisable (gxinterface, autres freres via pkg-config).
#  INVARIANT : aucune dependance a JUCE, gtkmm ou GLib. Verifiable car la
#  cible 'tests' linke cette lib seule.
# ============================================================================

# Sources C++ communes (toujours compilees).
set(ODENISE_CORE_SOURCES
    engine.cpp          # orchestration, double-buffering, registre
    loader.cpp          # chargement dynamique des modules (dlopen/LoadLibrary)
    backends/backend_cpu.cpp   # repli toujours present
    tools/logger.cc            # LogManager (sans dependance GLib)
)

# Backend + chaine STFT CUDA : seulement si CUDA actif.
if(ODENISE_HAVE_CUDA)
    list(APPEND ODENISE_CORE_SOURCES
        stft_chain.cu
        backends/backend_cuda.cu)
endif()

add_library(odenise_core SHARED ${ODENISE_CORE_SOURCES})
set_target_properties(odenise_core PROPERTIES
    OUTPUT_NAME   odenise
    VERSION       ${PROJECT_VERSION}
    SOVERSION     ${PROJECT_VERSION_MAJOR}
    # -fvisibility=hidden : seul l'API marquee ODENISE_API sort de la lib.
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# Le header public s'inclut en direct : #include "ns_engine.h"
target_include_directories(odenise_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

target_compile_features(odenise_core PUBLIC cxx_std_20)

# Marque la phase de compilation de la lib (pour dllexport vs dllimport).
target_compile_definitions(odenise_core PRIVATE ODENISE_BUILD_LIBRARY=1 LOGGER_EXPORTS=1)

# --- CUDA -------------------------------------------------------------------
if(ODENISE_HAVE_CUDA)
    target_compile_definitions(odenise_core PUBLIC ODENISE_HAVE_CUDA=1)
    target_link_libraries(odenise_core PUBLIC CUDA::cufft)
else()
    target_compile_definitions(odenise_core PUBLIC ODENISE_HAVE_CUDA=0)
endif()

# Chargement dynamique : libdl sous Linux (Windows fournit LoadLibrary).
if(UNIX AND NOT APPLE)
    target_link_libraries(odenise_core PUBLIC ${CMAKE_DL_LIBS})
endif()

# Avertissements stricts -- C++ UNIQUEMENT (ne pas passer /W4 a nvcc via les .cu :
# il se propage au host compiler et casse la compilation CUDA).
if(MSVC)
    target_compile_options(odenise_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(odenise_core PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
endif()

# --- Installation (lib reutilisable) ---------------------------------------
include(GNUInstallDirs)
install(TARGETS odenise_core
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ns_engine.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
