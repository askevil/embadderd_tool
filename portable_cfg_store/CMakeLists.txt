cmake_minimum_required(VERSION 3.16)
project(portable_cfg_store VERSION 0.1.0 LANGUAGES C CXX)

option(PCFG_BUILD_TESTS "Build host tests" ON)
option(PCFG_BUILD_EXAMPLES "Build examples" ON)
option(PCFG_BUILD_CJSON_ADAPTER "Build optional cJSON adapter" OFF)
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
endif()
