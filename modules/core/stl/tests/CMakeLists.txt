#core/stl/test/CMakeLits.txt

set_property(GLOBAL APPEND PROPERTY MLW_TEST_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/test_vector.cpp
            )

set_property(GLOBAL APPEND PROPERTY MLW_MODULES core::stl)