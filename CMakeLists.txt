cmake_minimum_required(VERSION 3.24)

include(cmake/svm_version.cmake)

project(SerialValueMatcherNative
    VERSION "${SVM_VERSION}"
    DESCRIPTION "Chinese-native Windows desktop serial debugging and value analysis tool"
    LANGUAGES CXX
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/svm_version_resource.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/svm_version_resource.h"
    @ONLY
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SVM_BUILD_WIN32_APP "Build the lightweight Win32 native application target" OFF)

if(SVM_BUILD_WIN32_APP AND NOT WIN32)
    message(FATAL_ERROR "SVM_BUILD_WIN32_APP can only be enabled on Windows.")
endif()

if(SVM_BUILD_WIN32_APP)
    enable_language(RC)
endif()

if(MSVC AND SVM_BUILD_WIN32_APP)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(CMAKE_CROSSCOMPILING AND WIN32 AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    find_program(SVM_WINE_EXECUTABLE NAMES wine)
    if(SVM_WINE_EXECUTABLE)
        set(CMAKE_CROSSCOMPILING_EMULATOR "${SVM_WINE_EXECUTABLE}")
    endif()
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(svm_slim_core
    src/command_sequence/command_sequence.cpp
    src/core/dangerous_operation_policy.cpp
    src/core/protocol_core.cpp
    src/core/modbus_core.cpp
    src/core/modbus_scan_executor_core.cpp
    src/core/analysis_core.cpp
    src/core/report_core.cpp
    src/report/evidence_bundle_writer.cpp
    src/transport/serial_rtu_transport.cpp
    src/transport/serial_write_queue.cpp
)

target_include_directories(svm_slim_core PUBLIC src)

add_library(svm_win32_serial
    src/win32/win32_serial_types.cpp
)

if(WIN32)
    target_sources(svm_win32_serial
        PRIVATE
            src/win32/win32_serial_port.cpp
            src/win32/win32_serial_enumerator.cpp
    )
    target_compile_definitions(svm_win32_serial
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE
    )
    target_link_libraries(svm_win32_serial
        PUBLIC
            setupapi
    )
endif()

target_include_directories(svm_win32_serial PUBLIC src)
target_link_libraries(svm_win32_serial PUBLIC svm_slim_core)

add_library(svm_native_storage
    src/native_storage/native_store_file_ops.cpp
    src/native_storage/native_store_files.cpp
    src/native_storage/native_store_record_io.cpp
    src/native_storage/native_store_record_codec.cpp
    src/native_storage/native_session_store.cpp
    src/native_storage/native_session_store_cache.cpp
)

target_include_directories(svm_native_storage PUBLIC src)

if(SVM_BUILD_WIN32_APP)
    add_executable(svm-native-win32
        WIN32
        src/win32/main.cpp
        src/win32/main_window.cpp
        src/win32/main_window_analysis.cpp
        src/win32/main_window_commands.cpp
        src/win32/main_window_controls.cpp
        src/win32/main_window_layout.cpp
        src/win32/main_window_lifecycle.cpp
        src/win32/main_window_log.cpp
        src/win32/main_window_menu.cpp
        src/win32/main_window_messages.cpp
        src/win32/main_window_modbus.cpp
        src/win32/main_window_preferences.cpp
        src/win32/main_window_send.cpp
        src/win32/main_window_self_test.cpp
        src/win32/main_window_serial.cpp
        src/win32/main_window_serial_io.cpp
        src/win32/main_window_status.cpp
        src/win32/main_window_storage.cpp
        src/win32/main_window_workbench.cpp
        src/win32/native_analysis_workflow.cpp
        src/win32/native_candidate_cache_state.cpp
        src/win32/native_connection_ui_state.cpp
        src/win32/native_control_utils.cpp
        src/win32/native_file_send_state.cpp
        src/win32/native_frame_scheduler.cpp
        src/win32/native_layout_model.cpp
        src/win32/native_layout_metrics.cpp
        src/win32/native_layout_transaction.cpp
        src/win32/native_log_model.cpp
        src/win32/native_log_scroll_state.cpp
        src/win32/native_log_view.cpp
        src/win32/native_modbus_analysis_controller.cpp
        src/win32/native_modbus_scan_request.cpp
        src/win32/native_paint_policy.cpp
        src/win32/native_modbus_scan_ui_state.cpp
        src/win32/native_modbus_scan_worker.cpp
        src/win32/native_progress_control.cpp
        src/win32/native_reconnect_state.cpp
        src/win32/native_serial_io_state.cpp
        src/win32/native_serial_send_controller.cpp
        src/win32/native_serial_profile_codec.cpp
        src/win32/native_send_codec.cpp
        src/win32/native_send_control_state.cpp
        src/win32/native_send_history_state.cpp
        src/win32/native_status_counters_state.cpp
        src/win32/native_time_utils.cpp
        src/win32/native_ui_preferences.cpp
        src/win32/native_workbench_tab_state.cpp
        src/win32/ui_text.cpp
        src/win32/utf8_win32.cpp
        src/win32/app.rc
    )

    target_link_libraries(svm-native-win32
        PRIVATE
            svm_slim_core
            svm_win32_serial
            svm_native_storage
            user32
            gdi32
            comctl32
            comdlg32
            shell32
            ole32
    )

    target_include_directories(svm-native-win32
        PRIVATE
            src
            src/win32
            ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_compile_definitions(svm-native-win32
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE
    )

    if(MSVC)
        set_property(TARGET svm-native-win32 PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        target_compile_options(svm-native-win32 PRIVATE /O1 /GL)
        target_link_options(svm-native-win32 PRIVATE /OPT:REF /OPT:ICF /LTCG)
    elseif(MINGW)
        target_compile_options(svm-native-win32 PRIVATE
            -Os
            -ffunction-sections
            -fdata-sections
            -finput-charset=UTF-8
            -fexec-charset=UTF-8
        )
        target_link_options(svm-native-win32 PRIVATE
            -municode
            -static
            -static-libgcc
            -static-libstdc++
            -Wl,--gc-sections
            -Wl,-s
        )
    endif()

    install(TARGETS svm-native-win32
        RUNTIME DESTINATION .
    )
endif()

enable_testing()

add_executable(native_protocol_modbus_tests
    tests/native_protocol_modbus_tests.cpp
)
set_target_properties(native_protocol_modbus_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(native_protocol_modbus_tests PRIVATE svm_slim_core)
add_test(NAME native_protocol_modbus_tests COMMAND native_protocol_modbus_tests)

add_executable(native_analysis_report_tests
    tests/native_analysis_report_tests.cpp
)
set_target_properties(native_analysis_report_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(native_analysis_report_tests PRIVATE svm_slim_core)
add_test(NAME native_analysis_report_tests COMMAND native_analysis_report_tests)

add_executable(evidence_bundle_writer_tests
    tests/evidence_bundle_writer_tests.cpp
)
set_target_properties(evidence_bundle_writer_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(evidence_bundle_writer_tests PRIVATE svm_slim_core)
add_test(NAME evidence_bundle_writer_tests COMMAND evidence_bundle_writer_tests)

add_executable(command_sequence_tests
    tests/command_sequence_tests.cpp
)
set_target_properties(command_sequence_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(command_sequence_tests PRIVATE svm_slim_core)
target_include_directories(command_sequence_tests PRIVATE src)
add_test(NAME command_sequence_tests COMMAND command_sequence_tests)

add_executable(dangerous_operation_policy_tests
    tests/dangerous_operation_policy_tests.cpp
)
set_target_properties(dangerous_operation_policy_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(dangerous_operation_policy_tests PRIVATE svm_slim_core)
target_include_directories(dangerous_operation_policy_tests PRIVATE src)
add_test(NAME dangerous_operation_policy_tests COMMAND dangerous_operation_policy_tests)

add_executable(version_metadata_tests
    tests/version_metadata_tests.cpp
)
set_target_properties(version_metadata_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_compile_definitions(version_metadata_tests
    PRIVATE
        SVM_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
        SVM_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME version_metadata_tests COMMAND version_metadata_tests)

add_executable(native_candidate_cache_state_tests
    tests/native_candidate_cache_state_tests.cpp
    src/win32/native_candidate_cache_state.cpp
)
set_target_properties(native_candidate_cache_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_candidate_cache_state_tests PRIVATE src)
add_test(NAME native_candidate_cache_state_tests COMMAND native_candidate_cache_state_tests)

add_executable(native_connection_ui_state_tests
    tests/native_connection_ui_state_tests.cpp
    src/win32/native_connection_ui_state.cpp
)
set_target_properties(native_connection_ui_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_connection_ui_state_tests PRIVATE src)
add_test(NAME native_connection_ui_state_tests COMMAND native_connection_ui_state_tests)

add_executable(native_modbus_scan_ui_state_tests
    tests/native_modbus_scan_ui_state_tests.cpp
    src/win32/native_modbus_analysis_controller.cpp
    src/win32/native_modbus_scan_ui_state.cpp
    src/win32/native_serial_io_state.cpp
    src/win32/native_ui_preferences.cpp
)
set_target_properties(native_modbus_scan_ui_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_modbus_scan_ui_state_tests PRIVATE src)
add_test(NAME native_modbus_scan_ui_state_tests COMMAND native_modbus_scan_ui_state_tests)

add_executable(native_modbus_scan_request_tests
    tests/native_modbus_scan_request_tests.cpp
    src/win32/native_modbus_scan_request.cpp
)
set_target_properties(native_modbus_scan_request_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(native_modbus_scan_request_tests PRIVATE svm_slim_core)
target_include_directories(native_modbus_scan_request_tests PRIVATE src)
add_test(NAME native_modbus_scan_request_tests COMMAND native_modbus_scan_request_tests)

add_executable(serial_write_queue_tests
    tests/serial_write_queue_tests.cpp
)
set_target_properties(serial_write_queue_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(serial_write_queue_tests PRIVATE svm_slim_core)
target_include_directories(serial_write_queue_tests PRIVATE src)
add_test(NAME serial_write_queue_tests COMMAND serial_write_queue_tests)

add_executable(transport_contract_tests
    tests/transport_contract_tests.cpp
)
set_target_properties(transport_contract_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(transport_contract_tests PRIVATE svm_slim_core)
target_include_directories(transport_contract_tests PRIVATE src)
add_test(NAME transport_contract_tests COMMAND transport_contract_tests)

add_executable(native_modbus_transport_adapter_tests
    tests/native_modbus_transport_adapter_tests.cpp
)
set_target_properties(native_modbus_transport_adapter_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(native_modbus_transport_adapter_tests PRIVATE svm_slim_core)
target_include_directories(native_modbus_transport_adapter_tests PRIVATE src)
add_test(NAME native_modbus_transport_adapter_tests COMMAND native_modbus_transport_adapter_tests)

add_executable(native_serial_io_state_tests
    tests/native_serial_io_state_tests.cpp
    src/win32/native_serial_io_state.cpp
)
set_target_properties(native_serial_io_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_serial_io_state_tests PRIVATE src)
add_test(NAME native_serial_io_state_tests COMMAND native_serial_io_state_tests)

add_executable(native_file_send_state_tests
    tests/native_file_send_state_tests.cpp
    src/win32/native_file_send_state.cpp
)
set_target_properties(native_file_send_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(native_file_send_state_tests PRIVATE svm_slim_core)
target_include_directories(native_file_send_state_tests PRIVATE src)
add_test(NAME native_file_send_state_tests COMMAND native_file_send_state_tests)

add_executable(native_serial_profile_codec_tests
    tests/native_serial_profile_codec_tests.cpp
    src/win32/native_serial_profile_codec.cpp
)
set_target_properties(native_serial_profile_codec_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_serial_profile_codec_tests PRIVATE src)
add_test(NAME native_serial_profile_codec_tests COMMAND native_serial_profile_codec_tests)

add_executable(native_ui_preferences_tests
    tests/native_ui_preferences_tests.cpp
    src/win32/native_modbus_analysis_controller.cpp
    src/win32/native_modbus_scan_ui_state.cpp
    src/win32/native_serial_io_state.cpp
    src/win32/native_ui_preferences.cpp
)
set_target_properties(native_ui_preferences_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_ui_preferences_tests PRIVATE src)
add_test(NAME native_ui_preferences_tests COMMAND native_ui_preferences_tests)

add_executable(native_workbench_tab_state_tests
    tests/native_workbench_tab_state_tests.cpp
    src/win32/native_workbench_tab_state.cpp
)
set_target_properties(native_workbench_tab_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_workbench_tab_state_tests PRIVATE src)
add_test(NAME native_workbench_tab_state_tests COMMAND native_workbench_tab_state_tests)

add_executable(native_log_filter_state_tests
    tests/native_log_filter_state_tests.cpp
    src/win32/native_log_model.cpp
)
set_target_properties(native_log_filter_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_log_filter_state_tests PRIVATE src)
add_test(NAME native_log_filter_state_tests COMMAND native_log_filter_state_tests)

add_executable(native_log_scroll_state_tests
    tests/native_log_scroll_state_tests.cpp
    src/win32/native_log_scroll_state.cpp
)
set_target_properties(native_log_scroll_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_log_scroll_state_tests PRIVATE src)
add_test(NAME native_log_scroll_state_tests COMMAND native_log_scroll_state_tests)

add_executable(native_reconnect_state_tests
    tests/native_reconnect_state_tests.cpp
    src/win32/native_reconnect_state.cpp
)
set_target_properties(native_reconnect_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_reconnect_state_tests PRIVATE src)
add_test(NAME native_reconnect_state_tests COMMAND native_reconnect_state_tests)

add_executable(native_send_codec_tests
    tests/native_send_codec_tests.cpp
    src/win32/native_send_codec.cpp
)
set_target_properties(native_send_codec_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_send_codec_tests PRIVATE src)
add_test(NAME native_send_codec_tests COMMAND native_send_codec_tests)

add_executable(native_send_control_state_tests
    tests/native_send_control_state_tests.cpp
    src/win32/native_serial_io_state.cpp
    src/win32/native_serial_send_controller.cpp
    src/win32/native_send_control_state.cpp
    src/win32/native_ui_preferences.cpp
)
set_target_properties(native_send_control_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_send_control_state_tests PRIVATE src)
add_test(NAME native_send_control_state_tests COMMAND native_send_control_state_tests)

add_executable(native_send_history_state_tests
    tests/native_send_history_state_tests.cpp
    src/win32/native_send_history_state.cpp
)
set_target_properties(native_send_history_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_send_history_state_tests PRIVATE src)
add_test(NAME native_send_history_state_tests COMMAND native_send_history_state_tests)

add_executable(native_status_counters_state_tests
    tests/native_status_counters_state_tests.cpp
    src/win32/native_status_counters_state.cpp
)
set_target_properties(native_status_counters_state_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_include_directories(native_status_counters_state_tests PRIVATE src)
add_test(NAME native_status_counters_state_tests COMMAND native_status_counters_state_tests)

add_executable(native_win32_serial_tests
    tests/native_win32_serial_tests.cpp
)
set_target_properties(native_win32_serial_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(native_win32_serial_tests PRIVATE svm_win32_serial)
add_test(NAME native_win32_serial_tests COMMAND native_win32_serial_tests)

if(WIN32)
    add_executable(native_win32_serial_loopback_tests
        tests/native_win32_serial_loopback_tests.cpp
    )
    set_target_properties(native_win32_serial_loopback_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_link_libraries(native_win32_serial_loopback_tests PRIVATE svm_win32_serial)
    add_test(NAME native_win32_serial_loopback_tests COMMAND native_win32_serial_loopback_tests)
endif()

add_executable(native_storage_tests
    tests/native_storage_tests.cpp
)
set_target_properties(native_storage_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(native_storage_tests PRIVATE svm_native_storage)
add_test(NAME native_storage_tests COMMAND native_storage_tests)

if(SVM_BUILD_WIN32_APP)
    add_executable(native_layout_metrics_tests
        tests/native_layout_metrics_tests.cpp
        src/win32/native_layout_metrics.cpp
    )
    set_target_properties(native_layout_metrics_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(native_layout_metrics_tests PRIVATE src)
    add_test(NAME native_layout_metrics_tests COMMAND native_layout_metrics_tests)

    add_executable(native_ui_layout_model_tests
        tests/native_ui_layout_model_tests.cpp
        src/win32/native_layout_model.cpp
        src/win32/native_layout_metrics.cpp
    )
    set_target_properties(native_ui_layout_model_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(native_ui_layout_model_tests PRIVATE src)
    add_test(NAME native_ui_layout_model_tests COMMAND native_ui_layout_model_tests)

    add_executable(native_ui_layout_transaction_tests
        tests/native_ui_layout_transaction_tests.cpp
        src/win32/native_layout_transaction.cpp
        src/win32/native_control_utils.cpp
    )
    set_target_properties(native_ui_layout_transaction_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(native_ui_layout_transaction_tests PRIVATE src)
    target_link_libraries(native_ui_layout_transaction_tests PRIVATE user32 gdi32 comctl32)
    add_test(NAME native_ui_layout_transaction_tests COMMAND native_ui_layout_transaction_tests)

    add_executable(native_frame_scheduler_tests
        tests/native_frame_scheduler_tests.cpp
        src/win32/native_frame_scheduler.cpp
    )
    set_target_properties(native_frame_scheduler_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(native_frame_scheduler_tests PRIVATE src)
    add_test(NAME native_frame_scheduler_tests COMMAND native_frame_scheduler_tests)

    add_executable(native_paint_policy_tests
        tests/native_paint_policy_tests.cpp
        src/win32/native_paint_policy.cpp
    )
    set_target_properties(native_paint_policy_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(native_paint_policy_tests PRIVATE src)
    target_link_libraries(native_paint_policy_tests PRIVATE user32)
    add_test(NAME native_paint_policy_tests COMMAND native_paint_policy_tests)

    add_executable(native_win32_analysis_workflow_tests
        tests/native_win32_analysis_workflow_tests.cpp
        src/win32/native_analysis_workflow.cpp
        src/win32/utf8_win32.cpp
    )
    set_target_properties(native_win32_analysis_workflow_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_link_libraries(native_win32_analysis_workflow_tests
        PRIVATE
            svm_slim_core
            svm_native_storage
    )
    target_compile_definitions(native_win32_analysis_workflow_tests
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE
    )
    add_test(NAME native_win32_analysis_workflow_tests COMMAND native_win32_analysis_workflow_tests)
endif()
