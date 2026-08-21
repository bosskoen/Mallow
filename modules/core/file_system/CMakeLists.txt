# mudules/core/file_system/CMakeLists.txt
add_library(file_system STATIC)
add_library(core::file_system ALIAS file_system)

mlw_set_warnings(file_system LEVEL W4)

target_sources(file_system
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/public/fs/concepts.h
        ${CMAKE_CURRENT_SOURCE_DIR}/public/fs/file.h
        ${CMAKE_CURRENT_SOURCE_DIR}/private/file.cpp

    )

target_include_directories(file_system
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
        ${CMAKE_CURRENT_SOURCE_DIR}/public/fs
    )

target_link_libraries(file_system
    PRIVATE

    PUBLIC
        core::stl
        core::core
    )


if(MLW_BUILD_TESTS)
    add_subdirectory(tests)
endif()


