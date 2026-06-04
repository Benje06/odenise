# ============================================================================
#  src/modules/CMakeLists.txt -- modules dynamiques (.so/.dll) charges par
#  le coeur via dlopen/LoadLibrary.
#
#  PREFIX "" : produit "passthrough.dll/.so" (pas "lib" devant).
#  Sortie dans ODENISE_MODULE_OUTPUT_DIR (= lib/odenise/modules/ dans le build,
#  miroir du deploiement FHS).
# ============================================================================

# --- Passthrough (suppression neutre, validation de la chaine) ---------------
add_library(passthrough MODULE passthrough/passthrough.cpp)
set_target_properties(passthrough PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${ODENISE_MODULE_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${ODENISE_MODULE_OUTPUT_DIR}")

# Le module inclut ns_engine.h pour la vtable mais ne linke PAS contre le coeur.
target_include_directories(passthrough PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
target_compile_features(passthrough PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(passthrough PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
    target_compile_options(passthrough PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Installation
install(TARGETS passthrough
        LIBRARY DESTINATION ${ODENISE_MODULE_INSTALL_DIR}
        RUNTIME DESTINATION ${ODENISE_MODULE_INSTALL_DIR})
