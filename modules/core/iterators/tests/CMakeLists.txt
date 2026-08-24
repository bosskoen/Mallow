# core/iterators/tests/CMakeLists.txt
set_property(GLOBAL APPEND PROPERTY MLW_TEST_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/test_compile.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_iterator.cpp

            )

set_property(GLOBAL APPEND PROPERTY MLW_MODULES core::iterators)