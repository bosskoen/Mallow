#app/CMakeLists.txt

add_executable(Mallow)

target_sources(Mallow
    PRIVATE
        main.cpp
)

target_link_libraries(Mallow
    PRIVATE
        core::core
        entry
)

