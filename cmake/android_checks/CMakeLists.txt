cmake_minimum_required(VERSION 3.25)
project(brawl_android_checks LANGUAGES CXX ASM)

if(NOT ANDROID OR NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "These standalone checks require Android arm64-v8a")
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(RUNTIME "${REPO_ROOT}/wiicompiled/runtime")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_compile_options(-O2 -fno-fast-math -ffp-contract=off -fno-slp-vectorize)

function(runtime_check name source)
    add_executable(${name} "${RUNTIME}/tests/${source}" ${ARGN})
    target_include_directories(${name} PRIVATE "${RUNTIME}/include" "${RUNTIME}/third_party/toml11")
endfunction()

runtime_check(host_context host_context_tests.cpp "${RUNTIME}/src/host_context.cpp"
              "${RUNTIME}/src/platform/android/co_switch.S")
runtime_check(context_abi macos_context_abi_tests.cpp "${RUNTIME}/src/platform/android/co_switch.S")
runtime_check(psq_helpers psq_helpers_tests.cpp "${RUNTIME}/src/ppc_quantized.cpp")
target_include_directories(psq_helpers BEFORE PRIVATE "${RUNTIME}/tests/psq_memory" "${RUNTIME}/include/isa")
runtime_check(nand_save nand_save_tests.cpp)
runtime_check(nand_settings nand_settings_tests.cpp)
runtime_check(sc_serial sc_serial_tests.cpp)
runtime_check(runtime_config runtime_config_tests.cpp)
target_compile_features(runtime_config PRIVATE cxx_std_20)
runtime_check(input_expr test_expr.cpp "${RUNTIME}/src/input_expr.cpp")
add_executable(save_state_jump "${CMAKE_CURRENT_LIST_DIR}/save_state_jump_tests.cpp"
               "${RUNTIME}/src/host_context.cpp" "${RUNTIME}/src/platform/android/co_switch.S")
target_include_directories(save_state_jump PRIVATE "${RUNTIME}/include")
