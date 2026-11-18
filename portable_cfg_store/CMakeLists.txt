cmake_minimum_required(VERSION 3.16)
project(portable_cfg_store VERSION 0.1.0 LANGUAGES C CXX)

option(PCFG_BUILD_TESTS "Build host tests" ON)
option(PCFG_BUILD_EXAMPLES "Build examples" ON)
option(PCFG_BUILD_CJSON_ADAPTER "Build optional cJSON adapter" OFF)
set(PCFG_CJSON_TARGET "cjson" CACHE STRING "Existing CMake target that supplies cJSON")
option(PCFG_BUILD_RTTHREAD_ADAPTER "Build RT-Thread adapter contract" OFF)
option(PCFG_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)

add_library(pcfg_store STATIC
    src/pcfg_crc32.c
    src/pcfg_store.c
)
target_include_directories(pcfg_store PUBLIC include PRIVATE src)
target_compile_features(pcfg_store PUBLIC c_std_99)
target_compile_options(pcfg_store PRIVATE -Wall -Wextra -Werror -Wpedantic)

if(PCFG_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pcfg_store PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pcfg_store PUBLIC -fsanitize=address,undefined)
endif()

if(PCFG_BUILD_CJSON_ADAPTER)
    if(NOT TARGET ${PCFG_CJSON_TARGET})
        message(FATAL_ERROR "PCFG_CJSON_TARGET='${PCFG_CJSON_TARGET}' must name an existing cJSON target")
    endif()
    add_library(pcfg_cjson STATIC adapters/cjson/pcfg_cjson.c)
    target_include_directories(pcfg_cjson PUBLIC adapters/cjson)
    target_link_libraries(pcfg_cjson PUBLIC pcfg_store ${PCFG_CJSON_TARGET})
    if(NOT MSVC)
        target_link_libraries(pcfg_cjson PUBLIC m)
    endif()
    target_compile_features(pcfg_cjson PUBLIC c_std_99)
    target_compile_options(pcfg_cjson PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

if(PCFG_BUILD_TESTS)
    enable_testing()
    add_executable(test_crc_media tests/test_crc_media.c)
    target_include_directories(test_crc_media PRIVATE tests src)
    target_link_libraries(test_crc_media PRIVATE pcfg_store)
    target_compile_options(test_crc_media PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME pcfg_crc_media COMMAND test_crc_media)

    add_executable(test_store
        tests/test_store.c
        tests/flash_emulator.c
    )
    target_include_directories(test_store PRIVATE tests src)
    target_link_libraries(test_store PRIVATE pcfg_store)
    target_compile_options(test_store PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME pcfg_store COMMAND test_store)

    add_executable(test_load
        tests/test_load.c
        tests/flash_emulator.c
    )
    target_include_directories(test_load PRIVATE tests src)
    target_link_libraries(test_load PRIVATE pcfg_store)
    target_compile_options(test_load PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME pcfg_load COMMAND test_load)

    add_executable(test_save
        tests/test_save.c
        tests/flash_emulator.c
    )
    target_include_directories(test_save PRIVATE tests src)
    target_link_libraries(test_save PRIVATE pcfg_store)
    target_compile_options(test_save PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME pcfg_save COMMAND test_save)

    add_executable(test_fault_injection
        tests/test_fault_injection.c
        tests/flash_emulator.c
    )
    target_include_directories(test_fault_injection PRIVATE tests src)
    target_link_libraries(test_fault_injection PRIVATE pcfg_store)
    target_compile_options(test_fault_injection PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME pcfg_fault_injection COMMAND test_fault_injection)

    add_executable(pcfg_posix_memory_example examples/posix_memory/main.c)
    target_link_libraries(pcfg_posix_memory_example PRIVATE pcfg_store)
    target_compile_options(pcfg_posix_memory_example PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME pcfg_posix_example COMMAND pcfg_posix_memory_example)
    set_tests_properties(pcfg_posix_example PROPERTIES
        PASS_REGULAR_EXPRESSION "loaded: brightness=42;sequence: 1;loaded default: brightness=10")

    if(PCFG_BUILD_CJSON_ADAPTER)
        add_executable(test_pcfg_cjson adapters/cjson/test_pcfg_cjson.c tests/flash_emulator.c)
        target_include_directories(test_pcfg_cjson PRIVATE tests)
        target_link_libraries(test_pcfg_cjson PRIVATE pcfg_cjson)
        target_compile_options(test_pcfg_cjson PRIVATE -Wall -Wextra -Werror -Wpedantic)
        add_test(NAME pcfg_cjson COMMAND test_pcfg_cjson)
    endif()
endif()

if(PCFG_BUILD_RTTHREAD_ADAPTER)
    set(PCFG_RTTHREAD_INCLUDE_DIR "" CACHE PATH "RT-Thread headers (empty uses host test stubs)")
    add_library(pcfg_rtthread_device STATIC adapters/rtthread_device/pcfg_rtthread_device.c)
    target_include_directories(pcfg_rtthread_device PUBLIC adapters/rtthread_device)
    target_link_libraries(pcfg_rtthread_device PUBLIC pcfg_store)
    if(PCFG_RTTHREAD_INCLUDE_DIR)
        target_include_directories(pcfg_rtthread_device PUBLIC ${PCFG_RTTHREAD_INCLUDE_DIR})
    elseif(PCFG_BUILD_TESTS)
        target_include_directories(pcfg_rtthread_device PUBLIC adapters/rtthread_device/test_stubs)
        add_executable(test_rtthread_device adapters/rtthread_device/test_compile.c)
        target_include_directories(test_rtthread_device PRIVATE tests)
        target_link_libraries(test_rtthread_device PRIVATE pcfg_rtthread_device)
        add_test(NAME pcfg_rtthread_device COMMAND test_rtthread_device)
    else()
        message(FATAL_ERROR "Supply PCFG_RTTHREAD_INCLUDE_DIR for an embedded build")
    endif()
    target_compile_options(pcfg_rtthread_device PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

if(PCFG_BUILD_TESTS)
    add_executable(test_headers tests/test_headers.cpp)
    target_link_libraries(test_headers PRIVATE pcfg_store)
    target_compile_features(test_headers PRIVATE cxx_std_11)
    if(PCFG_BUILD_CJSON_ADAPTER)
        target_compile_definitions(test_headers PRIVATE PCFG_TEST_CJSON)
        target_link_libraries(test_headers PRIVATE pcfg_cjson)
    endif()
    if(PCFG_BUILD_RTTHREAD_ADAPTER)
        target_compile_definitions(test_headers PRIVATE PCFG_TEST_RTTHREAD)
        target_link_libraries(test_headers PRIVATE pcfg_rtthread_device)
    endif()
    add_test(NAME pcfg_cpp_headers COMMAND test_headers)
endif()
