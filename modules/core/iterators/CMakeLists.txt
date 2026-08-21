# core/iterators/CMakeLists.txt

add_library(iterators INTERFACE)
add_library(core::iterators ALIAS iterators)

target_link_libraries(iterators INTERFACE core::core)

target_sources(iterators
    PRIVATE
        public/iter/iterator.h
)

target_include_directories(iterators INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/public
)

if(MLW_BUILD_TESTS)
    add_subdirectory(tests)
endif()