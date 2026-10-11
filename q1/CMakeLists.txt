cmake_minimum_required(VERSION 3.25)
project(MyProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep assert() active unless the user picks a build type explicitly
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(json)

file(GLOB HARDWARE_SRC   CONFIGURE_DEPENDS hardware/*.cpp)
file(GLOB REQUESTS_SRC   CONFIGURE_DEPENDS requests/*.cpp)
file(GLOB SIMULATION_SRC CONFIGURE_DEPENDS simulation/*.cpp)

add_library(sim STATIC ${HARDWARE_SRC} ${REQUESTS_SRC} ${SIMULATION_SRC})
target_include_directories(sim PUBLIC hardware requests simulation)
target_link_libraries(sim PUBLIC Threads::Threads nlohmann_json::nlohmann_json)

add_executable(world_test test/world_test.cpp)
target_link_libraries(world_test PRIVATE sim)

add_executable(request_parser_test test/candidate_tests/request_parser_test.cpp)
target_link_libraries(request_parser_test PRIVATE sim)

add_executable(example1_test test/candidate_tests/example1_test.cpp)
target_link_libraries(example1_test PRIVATE sim)

enable_testing()
add_test(NAME world_test COMMAND world_test)
add_test(NAME request_parser_test COMMAND request_parser_test)
add_test(NAME example1_test COMMAND example1_test)