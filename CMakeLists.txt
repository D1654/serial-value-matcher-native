cmake_minimum_required(VERSION 3.24)

project(SerialValueMatcherNative
    VERSION 0.1.0
    DESCRIPTION "Chinese-native Windows desktop serial debugging and value analysis tool"
    LANGUAGES CXX
)

set(SVM_PRODUCT_DISPLAY_NAME "串口值匹配器")
set(SVM_PRODUCT_REPOSITORY_NAME "SerialValueMatcher Native")
set(SVM_PRODUCT_AUTHOR "w")
set(SVM_PRODUCT_DESCRIPTION "面向中文用户的 Windows 原生串口调试、Modbus RTU 扫描和值候选分析工具")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SVM_BUILD_QT_APP "Build the current Qt Widgets application target" ON)
option(SVM_BUILD_WIN32_APP "Build the future lightweight Win32 native application target" OFF)

if(SVM_BUILD_WIN32_APP)
    message(FATAL_ERROR "SVM_BUILD_WIN32_APP is reserved for the Change #4 native slimming implementation and is not available yet.")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets SerialPort Sql Test)

qt_standard_project_setup()

add_library(svm_native_core
    src/analysis/stability_analysis_workflow.cpp
    src/capture/capture_bus.cpp
    src/session/console_model.cpp
    src/transport/serial_error_translator.cpp
    src/transport/serial_port_enumerator.cpp
    src/transport/serial_port_service.cpp
    src/transport/serial_port_selection.cpp
    src/transport/serial_reconnect_policy.cpp
    src/storage/session_store.cpp
    src/storage/session_store_schema.cpp
    src/storage/session_store_scan.cpp
    src/storage/session_store_matching.cpp
    src/storage/session_store_rules.cpp
    src/report/rule_verification_report.cpp
    src/report/text_file_writer.cpp
    src/protocol/checksum.cpp
    src/protocol/payload_codec.cpp
    src/modbus/modbus_rtu_codec.cpp
    src/modbus/modbus_read_request.cpp
    src/modbus/modbus_read_response.cpp
    src/modbus/modbus_scan_plan.cpp
    src/modbus/modbus_scan_executor.cpp
    src/modbus/modbus_rtu_serial_transport.cpp
    src/matching/value_candidate_generator.cpp
    src/matching/numeric_decoder.cpp
    src/matching/scan_observation_adapter.cpp
    src/matching/candidate_stability_analyzer.cpp
    src/matching/protocol_rule_interpretation.cpp
    src/matching/protocol_rule_metadata.cpp
    src/matching/protocol_rule_verifier.cpp
)

target_include_directories(svm_native_core PUBLIC src)

target_link_libraries(svm_native_core
    PUBLIC
        Qt6::Core
        Qt6::SerialPort
        Qt6::Sql
)

if(SVM_BUILD_QT_APP)
    qt_add_executable(svm-native
        WIN32
        src/app/main.cpp
        src/app/main_window.cpp
        src/app/modbus_scan_result_adapter.cpp
        src/app/modbus_scan_worker.cpp
        src/app/qt_serial_byte_channel.cpp
    )

    target_link_libraries(svm-native
        PRIVATE
            svm_native_core
            Qt6::Widgets
    )

    install(TARGETS svm-native
        RUNTIME DESTINATION .
    )
endif()

if(WIN32 AND SVM_BUILD_QT_APP)
    find_program(POWERSHELL_EXE powershell pwsh)
    if(POWERSHELL_EXE)
        add_custom_target(package-windows
            COMMAND ${POWERSHELL_EXE} -ExecutionPolicy Bypass -File ${CMAKE_SOURCE_DIR}/scripts/package-windows.ps1 -BuildDir ${CMAKE_BINARY_DIR} -Config "$<CONFIG>" -SkipBuild
            DEPENDS svm-native
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Package SerialValueMatcher Native for Windows"
        )
    endif()
endif()

enable_testing()

option(SVM_ENABLE_STRESS_TESTS "Build long-running non-hardware stress tests" OFF)

function(add_svm_qt_test target source)
    qt_add_executable(${target}
        ${source}
    )
    target_link_libraries(${target}
        PRIVATE
            svm_native_core
            Qt6::Test
    )
    add_test(NAME ${target} COMMAND ${target})
    if(ARGN)
        set_tests_properties(${target} PROPERTIES LABELS "${ARGN}")
    endif()
endfunction()

add_svm_qt_test(modbus_rtu_codec_tests tests/modbus_rtu_codec_tests.cpp)
add_svm_qt_test(modbus_read_request_tests tests/modbus_read_request_tests.cpp)
add_svm_qt_test(modbus_read_response_tests tests/modbus_read_response_tests.cpp)
add_svm_qt_test(modbus_scan_plan_tests tests/modbus_scan_plan_tests.cpp)
add_svm_qt_test(modbus_scan_executor_tests tests/modbus_scan_executor_tests.cpp)
add_svm_qt_test(modbus_scan_persistence_tests tests/modbus_scan_persistence_tests.cpp)
add_svm_qt_test(modbus_rtu_serial_transport_tests tests/modbus_rtu_serial_transport_tests.cpp)
add_svm_qt_test(numeric_decoder_tests tests/numeric_decoder_tests.cpp)
add_svm_qt_test(value_candidate_generator_tests tests/value_candidate_generator_tests.cpp)
add_svm_qt_test(match_persistence_tests tests/match_persistence_tests.cpp)
add_svm_qt_test(candidate_stability_analyzer_tests tests/candidate_stability_analyzer_tests.cpp)
add_svm_qt_test(stability_persistence_tests tests/stability_persistence_tests.cpp)
add_svm_qt_test(stability_analysis_workflow_tests tests/stability_analysis_workflow_tests.cpp)
add_svm_qt_test(protocol_rule_persistence_tests tests/protocol_rule_persistence_tests.cpp)
add_svm_qt_test(protocol_rule_verifier_tests tests/protocol_rule_verifier_tests.cpp)
add_svm_qt_test(protocol_rule_interpretation_tests tests/protocol_rule_interpretation_tests.cpp)
add_svm_qt_test(protocol_rule_metadata_tests tests/protocol_rule_metadata_tests.cpp)
add_svm_qt_test(rule_verification_persistence_tests tests/rule_verification_persistence_tests.cpp)
add_svm_qt_test(rule_verification_report_tests tests/rule_verification_report_tests.cpp)
add_svm_qt_test(checksum_tests tests/checksum_tests.cpp)
add_svm_qt_test(session_store_tests tests/session_store_tests.cpp)
add_svm_qt_test(console_model_tests tests/console_model_tests.cpp)
add_svm_qt_test(payload_codec_tests tests/payload_codec_tests.cpp)
add_svm_qt_test(send_history_tests tests/send_history_tests.cpp)
add_svm_qt_test(serial_profile_tests tests/serial_profile_tests.cpp)
add_svm_qt_test(serial_port_selection_tests tests/serial_port_selection_tests.cpp)
add_svm_qt_test(serial_reconnect_policy_tests tests/serial_reconnect_policy_tests.cpp)
add_svm_qt_test(serial_error_translator_tests tests/serial_error_translator_tests.cpp)
add_svm_qt_test(quality_regression_tests tests/quality_regression_tests.cpp quality)

if(SVM_ENABLE_STRESS_TESTS)
    add_svm_qt_test(quality_stress_tests tests/quality_stress_tests.cpp stress)
endif()
