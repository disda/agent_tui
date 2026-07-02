cmake_minimum_required(VERSION 3.19)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(agent_tui VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(WIN32)
  set(CURL_USE_STATIC_LIBS ON)
endif()

find_package(CURL REQUIRED)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_MakeAvailable(nlohmann_json)

add_library(agent_tui_core INTERFACE)
target_include_directories(agent_tui_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(agent_tui_core INTERFACE CURL::libcurl nlohmann_json::nlohmann_json)
target_compile_definitions(agent_tui_core INTERFACE AGENT_TUI_USE_LIBCURL)

if(WIN32)
  target_compile_definitions(agent_tui_core INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_executable(agent_tui src/main.cpp)
target_link_libraries(agent_tui PRIVATE agent_tui_core)

# tests
add_executable(agent_tui_tests tests/test_agent_runner.cpp)
target_link_libraries(agent_tui_tests PRIVATE agent_tui_core)

add_executable(agent_tui_trace_tests tests/test_trace_adapter.cpp)
target_link_libraries(agent_tui_trace_tests PRIVATE agent_tui_core)

add_executable(agent_tui_context_tests tests/test_context_manager.cpp)
target_link_libraries(agent_tui_context_tests PRIVATE agent_tui_core)

enable_testing()
add_test(NAME agent_tui_tests COMMAND agent_tui_tests)
add_test(NAME agent_tui_trace_tests COMMAND agent_tui_trace_tests)
add_test(NAME agent_tui_context_tests COMMAND agent_tui_context_tests)
