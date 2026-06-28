include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    # GoogleTest v1.17.0, pinned for reproducible builds.
    URL https://github.com/google/googletest/archive/52eb8108c5bdec04579160ae17225d66034bd723.tar.gz
    URL_HASH SHA256=745c55415660044610f7fcd3af7a6420d5de16a7dbb9ebfe2e131275676232be
)
FetchContent_MakeAvailable(googletest)

# GoogleTest is part of the test executable and must see the same libstdc++
# overlay and gthread ABI as the code under test.
target_link_libraries(gtest PRIVATE runtime::runtime)
target_link_libraries(gtest_main PRIVATE runtime::runtime)

add_executable(runtime_tests)

target_sources(runtime_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libc_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/libstdcxx_test.cpp
)

target_link_libraries(runtime_tests
    PRIVATE
        GTest::gtest_main
)

target_compile_definitions(runtime_tests
    PRIVATE
        RUNTIME_APPLICATION_THREAD_STACK_SIZE=65536U
)

target_compile_options(runtime_tests
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

runtime_attach_threadx_startup(runtime_tests)

add_test(NAME runtime COMMAND runtime_tests --gtest_color=no)
set_tests_properties(runtime PROPERTIES TIMEOUT 30)
