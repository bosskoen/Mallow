#core/core/test/CMakeLits.txt
add_library(core_test STATIC test.cpp)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. core)

target_link_libraries(core_test core::core)

