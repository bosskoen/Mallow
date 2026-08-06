#core/core/test/CMakeLits.txt


set_property(GLOBAL APPEND PROPERTY MLW_TEST_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/test.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_io.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_bit.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_c_string.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_compilers.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_typedef.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_traits.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_error.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_optional.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_result.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_variant.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_proc_context.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/test_process.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/test_mem.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/libc/test_math.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/thread/test_spinlock.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/thread/test_thread.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/thread/test_rwlock.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/thread/test_mutex.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/thread/test_atomic.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/memory/test_memory.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/memory/test_galloc.cpp
            )
set_property(GLOBAL APPEND PROPERTY MLW_MODULES core::core)
