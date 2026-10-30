add_executable(pokercore_tests
  test_main.cpp
  test_cards.cpp
  test_rules.cpp
)
target_link_libraries(pokercore_tests PRIVATE pokercore pf_options)
target_compile_definitions(pokercore_tests PRIVATE PF_RULES_DIR="${CMAKE_SOURCE_DIR}/rules")
add_test(NAME pokercore_tests COMMAND pokercore_tests)
