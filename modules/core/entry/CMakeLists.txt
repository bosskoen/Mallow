#core/entry/CMakeLists.txt

add_library(entry STATIC private/main.cpp)

target_link_libraries(entry core::core)

if(MSVC)
    target_link_options(entry INTERFACE /INCLUDE:mlwStart /ignore:4210)
else()
    target_link_options(entry INTERFACE  -u _start_c)
endif()