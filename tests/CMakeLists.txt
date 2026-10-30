add_executable(pokercore_tests
  test_main.cpp
  test_cards.cpp
  test_rules.cpp
  test_evaluator.cpp
  test_wild.cpp
  test_simulator.cpp
  test_analysis.cpp
  test_table.cpp
)
target_link_libraries(pokercore_tests PRIVATE pokercore pf_options)
target_compile_definitions(pokercore_tests PRIVATE PF_RULES_DIR="${CMAKE_SOURCE_DIR}/rules")
add_test(NAME pokercore_tests COMMAND pokercore_tests)

# Slow, on-demand checks (exhaustive C(52,7) distribution and throughput).
# Run with: ctest --preset <preset> -C Exhaustive -L exhaustive
add_executable(pokercore_exhaustive exhaustive.cpp)
target_link_libraries(pokercore_exhaustive PRIVATE pokercore pf_options)
add_test(NAME evaluator_exhaustive_c52_7 COMMAND pokercore_exhaustive CONFIGURATIONS Exhaustive)
add_test(NAME evaluator_benchmark COMMAND pokercore_exhaustive --bench CONFIGURATIONS Exhaustive)
add_test(NAME evaluator_wild_exhaustive COMMAND pokercore_exhaustive --wild CONFIGURATIONS Exhaustive)
add_test(NAME simulation_benchmark COMMAND pokercore_exhaustive --sim CONFIGURATIONS Exhaustive)
set_tests_properties(evaluator_exhaustive_c52_7 evaluator_benchmark evaluator_wild_exhaustive simulation_benchmark
                     PROPERTIES LABELS exhaustive)
