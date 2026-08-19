#core/stl/test/CMakeLits.txt

set_property(GLOBAL APPEND PROPERTY MLW_TEST_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/test_vector.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_list.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_map.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_set.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_string.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/test_hashtype_format.cpp

            )

set_property(GLOBAL APPEND PROPERTY MLW_MODULES core::stl)