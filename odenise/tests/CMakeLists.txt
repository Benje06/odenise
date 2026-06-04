# tests/CMakeLists.txt -- harnais de test du coeur.
add_executable(test_core main.cc)
target_link_libraries(test_core PRIVATE odenise_core)
add_test(NAME core_load_chain COMMAND test_core)

