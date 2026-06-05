# ============================================================================
#  src/modules/CMakeLists.txt -- modules dynamiques (.so/.dll) charges par
#  le coeur via dlopen/LoadLibrary.
#
#  PREFIX "" : produit "passthrough.dll/.so" (pas "lib" devant).
#  Sortie rangee PAR KIND, miroir de l'arbre source :
#    modules/suppression/  -> passthrough
#    modules/backends/     -> backend_cpu, backend_cuda
#  Le loader scanne recursivement modules/ et retrouve donc chaque module
#  quel que soit son sous-dossier. ODENISE_MODULE_OUTPUT_DIR / _INSTALL_DIR
#  pointent sur la racine modules/ ; on suffixe le sous-dossier de kind.
#
#  Include : les modules incluent ns_engine.h via interfaces/ du coeur.
#  Ils ne linkent PAS contre le coeur (frontiere C : dlopen uniquement).
#  L'include est prive : seul ns_engine.h est necessaire pour la vtable.
# ============================================================================

# Chemin vers interfaces/ du coeur (ns_engine.h public).
# Utilise par tous les modules de ce CMakeLists.
set(ODENISE_CORE_INTERFACES ${CMAKE_SOURCE_DIR}/src/core/interfaces)

# --- Passthrough (suppression neutre, validation de la chaine) ---------------
add_library(passthrough MODULE suppression/passthrough.cpp)
set_target_properties(passthrough PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${ODENISE_MODULE_OUTPUT_DIR}/suppression"
    RUNTIME_OUTPUT_DIRECTORY "${ODENISE_MODULE_OUTPUT_DIR}/suppression")

target_include_directories(passthrough PRIVATE ${ODENISE_CORE_INTERFACES})
target_compile_features(passthrough PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(passthrough PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(passthrough PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS passthrough
        LIBRARY DESTINATION ${ODENISE_MODULE_INSTALL_DIR}/suppression
        RUNTIME DESTINATION ${ODENISE_MODULE_INSTALL_DIR}/suppression)

# --- Backend CPU (calcul de repli / fallback) --------------------------------
add_library(backend_cpu MODULE backends/backend_cpu.cpp)
set_target_properties(backend_cpu PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${ODENISE_MODULE_OUTPUT_DIR}/backends"
    RUNTIME_OUTPUT_DIRECTORY "${ODENISE_MODULE_OUTPUT_DIR}/backends")

target_include_directories(backend_cpu PRIVATE ${ODENISE_CORE_INTERFACES})
target_compile_features(backend_cpu PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(backend_cpu PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(backend_cpu PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS backend_cpu
        LIBRARY DESTINATION ${ODENISE_MODULE_INSTALL_DIR}/backends
        RUNTIME DESTINATION ${ODENISE_MODULE_INSTALL_DIR}/backends)

# --- Backend CUDA (calcul GPU) -----------------------------------------------
# Compile uniquement quand un toolchain CUDA est present (preset
# windows-msvc-cuda). Le coeur GCC/UCRT64 ne compile jamais ce module :
# NVIDIA preconise MSVC sous Windows. Module unique regroupant les deux .cu
# (backend + chaine STFT GPU), comme le faisait precedemment le coeur.
# Range dans le meme sous-dossier "backends/" que le backend CPU.
if(ODENISE_HAVE_CUDA)
    add_library(backend_cuda MODULE
        backends/backend_cuda.cu
        backends/stft_chain.cu)
    set_target_properties(backend_cuda PROPERTIES
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${ODENISE_MODULE_OUTPUT_DIR}/backends"
        RUNTIME_OUTPUT_DIRECTORY "${ODENISE_MODULE_OUTPUT_DIR}/backends")

    target_include_directories(backend_cuda PRIVATE ${ODENISE_CORE_INTERFACES})
    target_compile_features(backend_cuda PRIVATE cxx_std_20)

    # CUDA : le module porte sa propre dependance cuFFT (le coeur n'en a plus).
    target_link_libraries(backend_cuda PRIVATE CUDA::cufft)

    # /W4 conditionne au C++ uniquement (jamais transmis a nvcc).
    if(MSVC)
        target_compile_options(backend_cuda PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
    endif()

    install(TARGETS backend_cuda
            LIBRARY DESTINATION ${ODENISE_MODULE_INSTALL_DIR}/backends
            RUNTIME DESTINATION ${ODENISE_MODULE_INSTALL_DIR}/backends)
endif()
