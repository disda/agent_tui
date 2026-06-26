cmake_minimum_required(VERSION 3.19)

project(agent_tui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(agent_tui_core INTERFACE)
target_include_directories(agent_tui_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(agent_tui src/main.cpp)
target_link_libraries(agent_tui PRIVATE agent_tui_core)

add_executable(agent_tui_tests tests/test_agent_runner.cpp)
target_link_libraries(agent_tui_tests PRIVATE agent_tui_core)

add_executable(agent_tui_file_tools_tests tests/test_file_tools.cpp)
target_link_libraries(agent_tui_file_tools_tests PRIVATE agent_tui_core)

enable_testing()
add_test(NAME agent_tui_tests COMMAND agent_tui_tests)
add_test(NAME agent_tui_file_tools_tests COMMAND agent_tui_file_tools_tests)
