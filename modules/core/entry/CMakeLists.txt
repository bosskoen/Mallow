#core/entry/CMakeLists.txt

add_executable(entry private/main.cpp)

if(MSVC)
    target_link_options(entry PRIVATE /ignore:4210)
    #target_link_options(entry PRIVATE /VERBOSE)
endif()

target_link_libraries(entry PRIVATE core::core my_app)

set_target_properties(entry PROPERTIES
    OUTPUT_NAME ${MALLOW_APP_NAME}
)


