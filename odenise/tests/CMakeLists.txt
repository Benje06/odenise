# tests/CMakeLists.txt -- harnais de test du coeur.
add_executable(test_core main.cc)
if(MSVC)
    target_compile_options(test_core PRIVATE $<$<CONFIG:Debug>:/Zi>)
    target_link_options(test_core PRIVATE $<$<CONFIG:Debug>:/DEBUG:FULL>)
endif()
target_compile_definitions(test_core PRIVATE AUDIO_IMPORTS CHAIN_IMPORTS THREAD_IMPORTS LOGGER_IMPORTS)
target_link_libraries(test_core PRIVATE odenise_core odenise_audio odenise_chain odenise_log odenise_thread)

# ns_engine.h est expose via odenise_core PUBLIC (interfaces/).
# Les headers internes du coeur (tools/, chain/, registry/) sont PRIVATE
# et ne transitent pas vers les consommateurs -- on les ajoute explicitement
# pour les tests qui en ont besoin (logger.h, common.h, debug.h).
target_include_directories(test_core PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/tools
    ${CMAKE_SOURCE_DIR}/src/core/engine
    ${CMAKE_SOURCE_DIR}/src/core/registry
    ${CMAKE_SOURCE_DIR}/src/core/chain
    ${CMAKE_SOURCE_DIR}/src/core/audio 
)

add_test(NAME core_load_chain COMMAND test_core)
