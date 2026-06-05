# tests/CMakeLists.txt -- harnais de test du coeur.
add_executable(test_core main.cc)

target_link_libraries(test_core PRIVATE odenise_core)

# ns_engine.h est expose via odenise_core PUBLIC (interfaces/).
# Les headers internes du coeur (tools/, chain/, registry/) sont PRIVATE
# et ne transitent pas vers les consommateurs -- on les ajoute explicitement
# pour les tests qui en ont besoin (logger.h, common.h, debug.h).
target_include_directories(test_core PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core)

add_test(NAME core_load_chain COMMAND test_core)
