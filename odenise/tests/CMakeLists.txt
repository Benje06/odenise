# tests/CMakeLists.txt -- harnais de test du coeur.
add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE odenise_core)
add_test(NAME core_smoke COMMAND test_core)
