# SPDX-License-Identifier: GPL-3.0-only

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

add_library(flex1500_client_http STATIC
    src/audio_ring.cpp
    src/http_client.cpp
    src/iq_stream.cpp
    src/pipewire_source.cpp
)
target_include_directories(flex1500_client_http PUBLIC include)
target_link_libraries(flex1500_client_http PUBLIC
    flex1500_protocol
    PkgConfig::PIPEWIRE
)
target_compile_options(flex1500_client_http PRIVATE
    -Wall -Wextra -Wpedantic
)

add_executable(flex1500-client src/main.cpp)
target_link_libraries(flex1500-client PRIVATE flex1500_client_http)
target_compile_options(flex1500-client PRIVATE
    -Wall -Wextra -Wpedantic
)
target_compile_definitions(flex1500-client PRIVATE
    FLEX1500_CLIENT_VERSION="${PROJECT_VERSION}"
    FLEX1500_CLIENT_GIT_REVISION="${FLEX1500_GIT_REVISION}"
)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME client-help COMMAND flex1500-client --help)
    add_test(NAME client-version COMMAND flex1500-client --version)
    add_test(NAME client-owner-lifecycle
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_client_daemon.py
            $<TARGET_FILE:flex1500-client>
    )
endif()
