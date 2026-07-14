#root\CMakeLists.txt

cmake_minimum_required(VERSION 3.21)
project(Mallow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/definitions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/definitions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/warnings.cmake)

add_compile_definitions(FREE_MLW_BUILD)

mlw_set_default_warnings(W3)

# (removed the stray `MLW_NO_STDLIB` line at the top — that's a CMake parse error)
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")



if(MSVC)
    # strip /RTC (Debug-only, needs _RTC_* CRT symbols we don't have)
    string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    # stop CMake injecting /MD or /MDd
    set(CMAKE_MSVC_RUNTIME_LIBRARY "")

    add_link_options(/NODEFAULTLIB /ENTRY:mlwStart /SUBSYSTEM:CONSOLE)
    link_libraries(kernel32)
    add_compile_options(/GS- /GR- /EHs-c- /Zc:preprocessor)

    if(MLW_ARCH_X64 OR MLW_ARCH_X86)
        add_compile_options(/arch:AVX2 /fp:contract)
    elseif(MLW_ARCH_ARM64)
        add_compile_options(/fp:contract)          # FMA baseline, just allow fusion
    endif()

 #off becous msvc bugs
   #    add_compile_options($<$<NOT:$<CONFIG:Debug>>:/GL>)
   # add_link_options($<$<NOT:$<CONFIG:Debug>>:/LTCG>
   #                         $<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>)
else()
    add_compile_options(-fno-rtti -fno-exceptions -ffreestanding -fno-stack-protector -fno-math-errno)
    add_link_options(-nostdlib -static -fno-pie)

    link_libraries(-static-libgcc)


   if(MLW_ARCH_X64 OR MLW_ARCH_X86)
        add_compile_options(-mfma -mavx2 -ffp-contract=fast)
    elseif(MLW_ARCH_ARM64)
        add_compile_options(-ffp-contract=fast)    # FMA baseline
    elseif(MLW_ARCH_ARM32)
        add_compile_options(-mfpu=neon-vfpv4 -ffp-contract=fast)
    endif()
    #off becous msvc bugs
  #     add_compile_options($<$<NOT:$<CONFIG:Debug>>:-flto>)
#add_link_options($<$<NOT:$<CONFIG:Debug>>:-flto>)
endif()


# if(MLW_NO_STDLIB AND MSVC)
#     add_compile_options(/Zc:threadSafeInit-)
# endif()


add_subdirectory(modules/core/core)



add_subdirectory(app)
add_subdirectory(modules/core/entry)