#core/stl/CMakeLists.txt

add_library(stl INTERFACE)
add_library(core::stl ALIAS stl)

mlw_set_warnings(core LEVEL W4)

target_link_libraries(stl
    INTERFACE
        core::core
)

target_sources(stl
    PRIVATE
        public/stl/vector.h
        public/stl/map.h
        public/stl/list.h
)

target_include_directories(stl
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/public
 #   PRIVATE
  #      ${CMAKE_CURRENT_SOURCE_DIR}/private
   #     ${CMAKE_CURRENT_SOURCE_DIR}/public/stl
)

if(MLW_BUILD_TESTS)
    add_subdirectory(tests)
endif()