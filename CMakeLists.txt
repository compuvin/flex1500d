# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20)

project(flex1500d VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

option(FLEX1500_BUILD_SOAPYSDR
    "Build the receive-only SoapySDR adapter"
    ON
)
if(FLEX1500_BUILD_SOAPYSDR)
    find_package(SoapySDR 0.8 REQUIRED)
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(FLEX1500_BUILD_SOAPYSDR)
    SOAPY_SDR_MODULE_UTIL(
        TARGET flex1500Support
        SOURCES soapy/Flex1500Device.cpp
    )
    target_compile_options(flex1500Support PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

option(FLEX1500_BUILD_TX_RESEARCH
    "Build minimally tested, dummy-load-only standalone TX research probes"
    OFF
)
if(FLEX1500_BUILD_TX_RESEARCH)
    message(WARNING
        "Building minimally tested standalone TX research probes. "
        "Their arming strings are not security controls. Never execute a TX "
        "probe without a suitable dummy load and an independently reviewed "
        "test plan. The flex1500d daemon and API remain receive-only."
    )
endif()

add_library(flex1500_protocol STATIC
    src/api.c
    src/dsp.c
    src/iq.c
    src/network.c
    src/publisher.c
    src/protocol.c
    src/web_ui.c
)
target_link_libraries(flex1500_protocol PUBLIC m)
target_include_directories(flex1500_protocol PUBLIC include)
target_compile_options(flex1500_protocol PRIVATE -Wall -Wextra -Wpedantic)

add_library(flex1500_usb_rx STATIC src/usb_rx.c)
target_include_directories(flex1500_usb_rx PUBLIC include)
target_link_libraries(flex1500_usb_rx PUBLIC
    flex1500_protocol
    PkgConfig::LIBUSB
)
target_compile_options(flex1500_usb_rx PRIVATE -Wall -Wextra -Wpedantic)

add_executable(flex1500-info src/flex1500_info.c)
target_link_libraries(flex1500-info PRIVATE flex1500_protocol)
target_compile_options(flex1500-info PRIVATE -Wall -Wextra -Wpedantic)

add_executable(flex1500d src/flex1500d.c)
target_link_libraries(flex1500d PRIVATE flex1500_usb_rx)
target_compile_options(flex1500d PRIVATE -Wall -Wextra -Wpedantic)

add_executable(flex1500-status-probe src/flex1500_status_probe.c)
target_link_libraries(flex1500-status-probe PRIVATE
    flex1500_protocol
    PkgConfig::LIBUSB
)
target_compile_options(flex1500-status-probe PRIVATE -Wall -Wextra -Wpedantic)

add_executable(flex1500-sample-in-probe src/flex1500_sample_in_probe.c)
target_link_libraries(flex1500-sample-in-probe PRIVATE
    flex1500_protocol
    PkgConfig::LIBUSB
)
target_compile_options(flex1500-sample-in-probe PRIVATE -Wall -Wextra -Wpedantic)

add_executable(flex1500-firmware-read-probe
    src/flex1500_firmware_read_probe.c
)
target_link_libraries(flex1500-firmware-read-probe PRIVATE
    flex1500_protocol
    PkgConfig::LIBUSB
)
target_compile_options(flex1500-firmware-read-probe PRIVATE
    -Wall -Wextra -Wpedantic
)

add_executable(flex1500-min-rx-init-probe
    src/flex1500_min_rx_init_probe.c
)
target_link_libraries(flex1500-min-rx-init-probe PRIVATE
    flex1500_protocol
    PkgConfig::LIBUSB
)
target_compile_options(flex1500-min-rx-init-probe PRIVATE
    -Wall -Wextra -Wpedantic
)

add_executable(flex1500-settled-rx-capture
    src/flex1500_settled_rx_capture.c
)

add_executable(flex1500-rx-tune-probe src/flex1500_rx_tune_probe.c)
target_link_libraries(flex1500-rx-tune-probe PRIVATE
    flex1500_protocol
    PkgConfig::LIBUSB
)

add_executable(flex1500-rx-gain-probe src/flex1500_rx_gain_probe.c)
target_link_libraries(flex1500-rx-gain-probe PRIVATE
    flex1500_protocol
    PkgConfig::LIBUSB
)
target_compile_options(flex1500-rx-gain-probe PRIVATE
    -Wall -Wextra -Wpedantic
)

add_executable(flex1500-rx-filter-click-probe
    src/flex1500_rx_filter_click_probe.c
)

if(FLEX1500_BUILD_TX_RESEARCH)
    add_executable(flex1500-pa-filter-click-probe
        src/flex1500_pa_filter_click_probe.c
    )
    target_link_libraries(flex1500-pa-filter-click-probe PRIVATE
        flex1500_protocol
        PkgConfig::LIBUSB
    )
    target_compile_options(flex1500-pa-filter-click-probe PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_executable(flex1500-zero-iq-tx-probe
        src/flex1500_zero_iq_tx_probe.c
    )
    target_link_libraries(flex1500-zero-iq-tx-probe PRIVATE
        flex1500_protocol
        PkgConfig::LIBUSB
    )
    target_compile_options(flex1500-zero-iq-tx-probe PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_executable(flex1500-two-tone-tx-probe
        src/flex1500_zero_iq_tx_probe.c
    )
    target_compile_definitions(flex1500-two-tone-tx-probe PRIVATE
        FLEX1500_TWO_TONE_PROBE=1
    )
    target_link_libraries(flex1500-two-tone-tx-probe PRIVATE
        flex1500_protocol
        PkgConfig::LIBUSB
    )
    target_compile_options(flex1500-two-tone-tx-probe PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()
target_link_libraries(flex1500-rx-filter-click-probe PRIVATE
    flex1500_protocol
    PkgConfig::LIBUSB
)
target_compile_options(flex1500-rx-filter-click-probe PRIVATE
    -Wall -Wextra -Wpedantic
)
target_compile_options(flex1500-rx-tune-probe PRIVATE
    -Wall -Wextra -Wpedantic
)
target_link_libraries(flex1500-settled-rx-capture PRIVATE
    flex1500_protocol
    PkgConfig::LIBUSB
)
target_compile_options(flex1500-settled-rx-capture PRIVATE
    -Wall -Wextra -Wpedantic
)

if(BUILD_TESTING)
    add_executable(api-controller-test tests/api_controller_test.c)
    target_link_libraries(api-controller-test PRIVATE flex1500_protocol)
    target_compile_options(api-controller-test PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME api-controller COMMAND api-controller-test)

    if(TARGET flex1500Support)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_executable(soapy-adapter-test tests/soapy_adapter_test.cpp)
        target_link_libraries(soapy-adapter-test PRIVATE SoapySDR)
        target_compile_options(soapy-adapter-test PRIVATE
            -Wall -Wextra -Wpedantic
        )
        add_test(NAME soapy-adapter-offline
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_soapy_daemon.py
                $<TARGET_FILE:soapy-adapter-test>
                $<TARGET_FILE_DIR:flex1500Support>
        )
    endif()
    add_executable(protocol-policy-test tests/protocol_policy_test.c)
    target_link_libraries(protocol-policy-test PRIVATE flex1500_protocol)
    target_compile_options(protocol-policy-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME protocol-policy COMMAND protocol-policy-test)

    add_executable(iq-processing-test tests/iq_processing_test.c)
    target_link_libraries(iq-processing-test PRIVATE flex1500_protocol)
    target_compile_options(iq-processing-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME iq-processing COMMAND iq-processing-test)

    add_executable(dsp-test tests/dsp_test.c)
    target_link_libraries(dsp-test PRIVATE flex1500_protocol)
    target_compile_options(dsp-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME dsp COMMAND dsp-test)
    add_test(NAME daemon-offline-status COMMAND flex1500d --status)
    add_test(NAME daemon-live-rx-requires-arming-token
        COMMAND flex1500d --serve-live-rx 15000)
    set_tests_properties(daemon-live-rx-requires-arming-token PROPERTIES
        WILL_FAIL TRUE)
    add_test(NAME daemon-rx-tuning-requires-exact-arming-token
        COMMAND flex1500d --serve-live-rx 15000 --enable-rx-tuning)
    set_tests_properties(daemon-rx-tuning-requires-exact-arming-token PROPERTIES
        WILL_FAIL TRUE)

    add_executable(usb-rx-lifecycle-test tests/usb_rx_lifecycle_test.c)
    target_link_libraries(usb-rx-lifecycle-test PRIVATE flex1500_usb_rx)
    target_compile_options(usb-rx-lifecycle-test PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME usb-rx-lifecycle-offline COMMAND usb-rx-lifecycle-test)

    add_executable(network-protocol-test tests/network_protocol_test.c)
    target_link_libraries(network-protocol-test PRIVATE flex1500_protocol)
    target_compile_options(network-protocol-test PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME network-protocol COMMAND network-protocol-test)

    add_executable(publisher-test tests/publisher_test.c)
    target_link_libraries(publisher-test PRIVATE flex1500_protocol)
    target_compile_options(publisher-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME publisher COMMAND publisher-test)
    add_test(NAME status-probe-default-is-offline COMMAND flex1500-status-probe)
    add_test(NAME sample-in-probe-default-is-offline
        COMMAND flex1500-sample-in-probe)
    add_test(NAME firmware-read-probe-default-is-offline
        COMMAND flex1500-firmware-read-probe)
    add_test(NAME min-rx-init-probe-default-is-offline
        COMMAND flex1500-min-rx-init-probe)
    add_test(NAME settled-rx-capture-default-is-offline
        COMMAND flex1500-settled-rx-capture)
    add_test(NAME rx-tune-probe-default-is-offline COMMAND flex1500-rx-tune-probe)
    add_test(NAME rx-gain-probe-default-is-offline COMMAND flex1500-rx-gain-probe)
    add_test(NAME rx-tune-probe-plan-is-offline
        COMMAND flex1500-rx-tune-probe --plan 10000000)
    add_test(NAME rx-filter-click-probe-default-is-offline
        COMMAND flex1500-rx-filter-click-probe)
    if(FLEX1500_BUILD_TX_RESEARCH)
        add_test(NAME pa-filter-click-probe-default-is-offline
            COMMAND flex1500-pa-filter-click-probe)
        add_test(NAME zero-iq-tx-probe-default-is-offline
            COMMAND flex1500-zero-iq-tx-probe)
        add_test(NAME zero-iq-tx-probe-rejects-short-arming-token
            COMMAND flex1500-zero-iq-tx-probe --execute-approved-zero-iq-tx)
        set_tests_properties(
            zero-iq-tx-probe-rejects-short-arming-token PROPERTIES
            WILL_FAIL TRUE
        )
        add_test(NAME two-tone-tx-probe-default-is-offline
            COMMAND flex1500-two-tone-tx-probe)
        add_test(NAME two-tone-tx-probe-rejects-zero-iq-token
            COMMAND flex1500-two-tone-tx-probe
                --execute-approved-zero-iq-tx-28475000-50pct-3s)
        set_tests_properties(
            two-tone-tx-probe-rejects-zero-iq-token PROPERTIES
            WILL_FAIL TRUE
        )
    endif()
endif()
