# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20)

project(flex1500d VERSION 0.2.1 LANGUAGES C CXX)

execute_process(
    COMMAND git rev-parse --short=8 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE FLEX1500_GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT FLEX1500_GIT_REVISION)
    set(FLEX1500_GIT_REVISION "unknown")
endif()
execute_process(
    COMMAND git diff --quiet --ignore-submodules HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE FLEX1500_GIT_DIRTY
    ERROR_QUIET
)
if(NOT FLEX1500_GIT_DIRTY EQUAL 0)
    string(APPEND FLEX1500_GIT_REVISION "-dirty")
endif()

include(CTest)
include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

option(FLEX1500_BUILD_SOAPYSDR
    "Build the SoapySDR RX/TX adapter"
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
    target_compile_definitions(flex1500Support PRIVATE
        FLEX1500_SOFTWARE_VERSION="${PROJECT_VERSION}"
        FLEX1500_GIT_REVISION="${FLEX1500_GIT_REVISION}"
    )
endif()

install(FILES 70-flex1500.rules
    DESTINATION lib/udev/rules.d
)
install(FILES README.md RELEASE_NOTES.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(FILES docs/CONFIGURATION.md docs/SYSTEMD_SERVICE.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME copyright
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "flex1500d")
set(CPACK_PACKAGE_VENDOR "KB1JDX")
set(CPACK_PACKAGE_CONTACT "compuvin@users.noreply.github.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Experimental Linux network SDR daemon for the FLEX-1500")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER
    "KB1JDX <compuvin@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/conffiles"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/prerm"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm"
)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
execute_process(
    COMMAND dpkg --print-architecture
    OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
endif()
set(CPACK_PACKAGE_FILE_NAME
    "flex1500d_${PROJECT_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}")
include(CPack)

option(FLEX1500_BUILD_TX_RESEARCH
    "Build standalone TX research probes (execution remains separately armed)"
    ON
)
if(FLEX1500_BUILD_TX_RESEARCH)
    message(STATUS
        "Building standalone TX research probes. Their execution arming "
        "strings are safety interlocks, not security controls."
    )
endif()

add_library(flex1500_protocol STATIC
    src/api.c
    src/config.c
    src/dsp.c
    src/iq.c
    src/network.c
    src/publisher.c
    src/protocol.c
    src/rtl_tcp.c
    src/web_ui.c
)

add_library(flex1500_tx_dsp STATIC
    src/tx_dsp.c
    src/tx_audio_stream.c
)
target_include_directories(flex1500_tx_dsp PUBLIC include)
target_link_libraries(flex1500_tx_dsp PUBLIC m)
target_compile_options(flex1500_tx_dsp PRIVATE -Wall -Wextra -Wpedantic)

add_library(flex1500_tune_control STATIC
    src/network_tx.c
    src/station_owner.c
    src/tune_control.c
    src/tx_control.c
)
target_include_directories(flex1500_tune_control PUBLIC include)
target_compile_options(flex1500_tune_control PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(flex1500_protocol PUBLIC flex1500_tune_control)

add_executable(flex1500-mic-iq-offline src/flex1500_mic_iq_offline.c)
target_link_libraries(flex1500-mic-iq-offline PRIVATE flex1500_tx_dsp m)
target_compile_options(flex1500-mic-iq-offline PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(flex1500_protocol PUBLIC m)
target_include_directories(flex1500_protocol PUBLIC include)
target_compile_options(flex1500_protocol PRIVATE -Wall -Wextra -Wpedantic)

add_library(flex1500_usb_rx STATIC
    src/usb_rx.c
    src/usb_io.c
    src/tx_lifecycle.c
)
target_include_directories(flex1500_usb_rx PUBLIC include)
target_link_libraries(flex1500_usb_rx PUBLIC
    flex1500_protocol
    flex1500_tx_dsp
    PkgConfig::LIBUSB
)
target_compile_options(flex1500_usb_rx PRIVATE -Wall -Wextra -Wpedantic)

add_executable(flex1500-info src/flex1500_info.c)
target_link_libraries(flex1500-info PRIVATE flex1500_protocol)
target_compile_options(flex1500-info PRIVATE -Wall -Wextra -Wpedantic)

add_executable(flex1500d src/flex1500d.c)
target_link_libraries(flex1500d PRIVATE flex1500_usb_rx)
target_compile_options(flex1500d PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(flex1500d PRIVATE
    FLEX1500_SOFTWARE_VERSION="${PROJECT_VERSION}"
    FLEX1500_GIT_REVISION="${FLEX1500_GIT_REVISION}"
)
install(TARGETS flex1500d
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES flex1500d.conf
    DESTINATION /etc/flex1500d
)
install(FILES packaging/flex1500d.service
    DESTINATION lib/systemd/system
)

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

    add_executable(flex1500-two-tone-100-tx-probe
        src/flex1500_zero_iq_tx_probe.c
    )
    target_compile_definitions(flex1500-two-tone-100-tx-probe PRIVATE
        FLEX1500_TWO_TONE_PROBE=1
        FLEX1500_TWO_TONE_100_PROBE=1
    )
    target_link_libraries(flex1500-two-tone-100-tx-probe PRIVATE
        flex1500_protocol
        PkgConfig::LIBUSB
    )
    target_compile_options(flex1500-two-tone-100-tx-probe PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_executable(flex1500-single-tone-tx-probe
        src/flex1500_zero_iq_tx_probe.c
    )
    target_compile_definitions(flex1500-single-tone-tx-probe PRIVATE
        FLEX1500_TWO_TONE_PROBE=1
        FLEX1500_SINGLE_TONE_PROBE=1
    )
    target_link_libraries(flex1500-single-tone-tx-probe PRIVATE
        flex1500_protocol
        PkgConfig::LIBUSB
    )
    target_compile_options(flex1500-single-tone-tx-probe PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_executable(flex1500-tune-5w-tx-probe
        src/flex1500_zero_iq_tx_probe.c
    )
    target_compile_definitions(flex1500-tune-5w-tx-probe PRIVATE
        FLEX1500_TWO_TONE_PROBE=1
        FLEX1500_TUNE_PROBE=1
    )
    target_link_libraries(flex1500-tune-5w-tx-probe PRIVATE
        flex1500_protocol
        PkgConfig::LIBUSB
    )
    target_compile_options(flex1500-tune-5w-tx-probe PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_executable(flex1500-mic-capture-tx-probe
        src/flex1500_zero_iq_tx_probe.c
    )
    target_compile_definitions(flex1500-mic-capture-tx-probe PRIVATE
        FLEX1500_MIC_CAPTURE_PROBE=1
    )
    target_link_libraries(flex1500-mic-capture-tx-probe PRIVATE
        flex1500_protocol
        PkgConfig::LIBUSB
    )
    target_compile_options(flex1500-mic-capture-tx-probe PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_executable(flex1500-recorded-speech-tx-probe
        src/flex1500_zero_iq_tx_probe.c
    )
    target_compile_definitions(flex1500-recorded-speech-tx-probe PRIVATE
        FLEX1500_SPEECH_PLAYBACK_PROBE=1
    )
    target_link_libraries(flex1500-recorded-speech-tx-probe PRIVATE
        flex1500_protocol
        PkgConfig::LIBUSB
    )
    target_compile_options(flex1500-recorded-speech-tx-probe PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_executable(flex1500-tx-cleanup-fault-probe
        src/flex1500_tx_cleanup_fault_probe.c
    )
    target_link_libraries(flex1500-tx-cleanup-fault-probe PRIVATE
        flex1500_usb_rx
    )
    target_compile_options(flex1500-tx-cleanup-fault-probe PRIVATE
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
    add_executable(config-test tests/config_test.c)
    target_link_libraries(config-test PRIVATE flex1500_protocol)
    target_compile_options(config-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME config COMMAND config-test)
    add_test(NAME daemon-config-check
        COMMAND flex1500d --config ${CMAKE_CURRENT_SOURCE_DIR}/flex1500d.conf
                --check-config)
    add_test(NAME daemon-config-print-with-overrides
        COMMAND flex1500d --config ${CMAKE_CURRENT_SOURCE_DIR}/flex1500d.conf
                --no-daemon --http-port 16000 --disable-test-page
                --print-effective-config)
    add_test(NAME systemd-service-validation
        COMMAND ${CMAKE_COMMAND}
                -DSERVICE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/packaging/flex1500d.service
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/validate_systemd_service.cmake)
    add_executable(rtl-tcp-test tests/rtl_tcp_test.c)
    target_link_libraries(rtl-tcp-test PRIVATE flex1500_protocol)
    target_compile_options(rtl-tcp-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rtl-tcp COMMAND rtl-tcp-test)
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
    add_executable(tx-dsp-test tests/tx_dsp_test.c)
    target_link_libraries(tx-dsp-test PRIVATE flex1500_tx_dsp m)
    target_compile_options(tx-dsp-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME tx-dsp COMMAND tx-dsp-test)
    add_executable(tx-audio-stream-test tests/tx_audio_stream_test.c)
    target_link_libraries(tx-audio-stream-test PRIVATE flex1500_tx_dsp m)
    target_compile_options(tx-audio-stream-test PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME tx-audio-stream COMMAND tx-audio-stream-test)
    add_executable(tune-control-test tests/tune_control_test.c)
    target_link_libraries(tune-control-test PRIVATE flex1500_tune_control)
    target_compile_options(tune-control-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME tune-control COMMAND tune-control-test)
    add_executable(tx-control-test tests/tx_control_test.c)
    target_link_libraries(tx-control-test PRIVATE flex1500_tune_control)
    target_compile_options(tx-control-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME tx-control COMMAND tx-control-test)
    add_executable(network-tx-test tests/network_tx_test.c)
    target_link_libraries(network-tx-test PRIVATE flex1500_tune_control)
    target_compile_options(network-tx-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME network-tx COMMAND network-tx-test)

    add_executable(station-owner-test tests/station_owner_test.c)
    target_link_libraries(station-owner-test PRIVATE flex1500_tune_control)
    target_compile_options(station-owner-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME station-owner COMMAND station-owner-test)
    add_test(NAME mic-iq-offline-requires-explicit-drive
        COMMAND flex1500-mic-iq-offline input.pcm output.iq16le usb)
    set_tests_properties(mic-iq-offline-requires-explicit-drive PROPERTIES
        WILL_FAIL TRUE)
    add_test(NAME daemon-offline-status COMMAND flex1500d --status)
    add_test(NAME daemon-live-rx-requires-arming-token
        COMMAND flex1500d --serve-live-rx 15000)
    set_tests_properties(daemon-live-rx-requires-arming-token PROPERTIES
        WILL_FAIL TRUE)
    add_test(NAME daemon-rx-tuning-requires-exact-arming-token
        COMMAND flex1500d --serve-live-rx 15000 --enable-rx-tuning)
    set_tests_properties(daemon-rx-tuning-requires-exact-arming-token PROPERTIES
        WILL_FAIL TRUE)
    add_test(NAME daemon-transmit-requires-exact-mode-token
        COMMAND flex1500d --serve-live-rx 15000
        --initialize-radio-and-enable-tx)
    set_tests_properties(daemon-transmit-requires-exact-mode-token PROPERTIES
        WILL_FAIL TRUE)
    add_executable(usb-rx-lifecycle-test tests/usb_rx_lifecycle_test.c)
    target_link_libraries(usb-rx-lifecycle-test PRIVATE flex1500_usb_rx)
    target_compile_options(usb-rx-lifecycle-test PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME usb-rx-lifecycle-offline COMMAND usb-rx-lifecycle-test)
    add_executable(tx-lifecycle-test tests/tx_lifecycle_test.c)
    target_link_libraries(tx-lifecycle-test PRIVATE flex1500_usb_rx)
    target_compile_options(tx-lifecycle-test PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME tx-lifecycle-offline COMMAND tx-lifecycle-test)
    add_executable(usb-io-test tests/usb_io_test.c)
    target_link_libraries(usb-io-test PRIVATE flex1500_usb_rx)
    target_compile_options(usb-io-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME usb-io-offline COMMAND usb-io-test)
    add_executable(tx-failure-injection-test
        tests/tx_failure_injection_test.c)
    target_link_libraries(tx-failure-injection-test PRIVATE flex1500_usb_rx)
    target_compile_options(tx-failure-injection-test PRIVATE
        -Wall -Wextra -Wpedantic)
    add_test(NAME tx-failure-injection-offline
        COMMAND tx-failure-injection-test)

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
        add_test(NAME two-tone-100-tx-probe-default-is-offline
            COMMAND flex1500-two-tone-100-tx-probe)
        add_test(NAME two-tone-100-tx-probe-rejects-50pct-token
            COMMAND flex1500-two-tone-100-tx-probe
                --execute-approved-two-tone-tx-28475000-50pct-3s)
        set_tests_properties(
            two-tone-100-tx-probe-rejects-50pct-token PROPERTIES
            WILL_FAIL TRUE
        )
        add_test(NAME single-tone-tx-probe-default-is-offline
            COMMAND flex1500-single-tone-tx-probe)
        add_test(NAME single-tone-tx-probe-rejects-two-tone-token
            COMMAND flex1500-single-tone-tx-probe
                --execute-approved-two-tone-100pct-tx-28475000-3s)
        set_tests_properties(
            single-tone-tx-probe-rejects-two-tone-token PROPERTIES
            WILL_FAIL TRUE
        )
        add_test(NAME tune-5w-tx-probe-default-is-offline
            COMMAND flex1500-tune-5w-tx-probe)
        add_test(NAME tune-5w-tx-probe-rejects-single-tone-token
            COMMAND flex1500-tune-5w-tx-probe
                --execute-approved-single-tone-12444-tx-28475000-3s)
        set_tests_properties(
            tune-5w-tx-probe-rejects-single-tone-token PROPERTIES
            WILL_FAIL TRUE
        )
        add_test(NAME mic-capture-tx-probe-default-is-offline
            COMMAND flex1500-mic-capture-tx-probe)
        add_test(NAME mic-capture-tx-probe-rejects-two-tone-token
            COMMAND flex1500-mic-capture-tx-probe
                --execute-approved-two-tone-tx-28475000-50pct-3s)
        set_tests_properties(
            mic-capture-tx-probe-rejects-two-tone-token PROPERTIES
            WILL_FAIL TRUE
        )
        add_test(NAME recorded-speech-tx-probe-default-is-offline
            COMMAND flex1500-recorded-speech-tx-probe)
        add_test(NAME recorded-speech-tx-probe-rejects-mic-capture-token
            COMMAND flex1500-recorded-speech-tx-probe
                --execute-approved-mic-capture-tx-28475000-50pct-3s)
        set_tests_properties(
            recorded-speech-tx-probe-rejects-mic-capture-token PROPERTIES
            WILL_FAIL TRUE
        )
        add_test(NAME tx-cleanup-fault-probe-default-is-offline
            COMMAND flex1500-tx-cleanup-fault-probe)
        add_test(NAME tx-cleanup-fault-probe-rejects-short-token
            COMMAND flex1500-tx-cleanup-fault-probe --after-key
                I-CONFIRM-DUMMY-LOAD)
        set_tests_properties(
            tx-cleanup-fault-probe-rejects-short-token PROPERTIES
            WILL_FAIL TRUE
        )
    endif()
endif()
