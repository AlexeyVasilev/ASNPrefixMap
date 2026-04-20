cmake_minimum_required(VERSION 3.16)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
project(ASNPrefixMap VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(nlohmann_json CONFIG QUIET)
find_package(Boost REQUIRED COMPONENTS container)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

if (NOT nlohmann_json_FOUND)
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)

    if (NLOHMANN_JSON_INCLUDE_DIR)
        add_library(nlohmann_json INTERFACE)
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
        target_include_directories(nlohmann_json INTERFACE ${NLOHMANN_JSON_INCLUDE_DIR})
    else()
        FetchContent_Declare(
            nlohmann_json
            URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        )
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
endif()

add_executable(ASNPrefixMap
    src/main.cpp
    src/config.cpp
    src/peer/peer_registry.cpp
    src/prefix/prefix.cpp
    src/routing_state.cpp
    src/parser/ris_live_parser.cpp
    src/source/file_jsonl_source.cpp
    src/source/ris_live_websocket_source.cpp
    src/snapshot/snapshot_io.cpp
    src/stats/growth_stats.cpp
)

target_include_directories(ASNPrefixMap PRIVATE src)
target_link_libraries(ASNPrefixMap PRIVATE
    nlohmann_json::nlohmann_json
    Boost::container
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

if (WIN32)
    target_link_libraries(ASNPrefixMap PRIVATE
        ws2_32
        wsock32
        bcrypt
    )
endif()

add_executable(ASNPrefixMapRisLiveDemo
    src/ris_live_demo.cpp
    src/peer/peer_registry.cpp
    src/parser/ris_live_parser.cpp
)

target_include_directories(ASNPrefixMapRisLiveDemo PRIVATE src)
target_link_libraries(ASNPrefixMapRisLiveDemo PRIVATE nlohmann_json::nlohmann_json)
