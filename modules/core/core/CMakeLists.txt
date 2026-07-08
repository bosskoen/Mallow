#core/core/CMakeLists.txt

add_library(core STATIC)
add_library(core::core ALIAS core)

if(WIN32)
    target_link_libraries(core PRIVATE synchronization)
endif()

mlw_set_warnings(core LEVEL W4)

target_sources(core
    PRIVATE
        public/core/io/terminal.h
        private/terminal.cpp
        public/core/io/handle.h
        public/core/typedef.h
        private/c_string.cpp
        public/core/c_string.h
        public/core/io/io.h
        private/io.cpp
        public/core/macro.h
        private/format.cpp
        public/core/traits.h
        public/core/memory/memory.h
        private/mem.cpp
        public/core/io/format.h
        public/core/libc/mem.h
        public/core/libc/process.h
        public/core/libc/str.h
        public/core/libc/math.h
        private/memory.cpp
        public/core/thread/atomic_op.h
        public/core/thread/atomic.h
        public/core/thread/spinlock.h
        public/core/thread/lock.h
        public/core/optional.h
        public/core/compilers.h
        private/mutex.cpp
        public/core/thread/mutex.h
        public/core/thread/candvar.h
        public/core/thread/rwlock.h
        public/core/memory/galloc.h
        private/galloc.cpp
        public/core/thread/thread.h
        public/core/variant.h
        public/core/error.h
        public/core/result.h
        public/core/bit.h
        private/freestanding.cpp
        private/math/log2.cpp
        private/math/pow.cpp
        private/new_imp.cpp
        private/crt_internals.h

)

if(MLW_ABI_MSVC)
    target_sources(core PRIVATE
        private/windows_crt/crt.cpp
        private/windows_crt/init_thread.cpp
    )
else()  # GCC / Clang → Itanium ABI
    target_sources(core PRIVATE
        private/itanium_crt/crt.cpp
    )
endif()

if(MSVC)   # cl.exe or clang-cl — slash flags
    set_source_files_properties(private/freestanding.cpp PROPERTIES
        COMPILE_OPTIONS "/Oi-")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(private/freestanding.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-builtin;-fno-tree-loop-distribute-patterns")
else()  # Clang in gnu-mode
    set_source_files_properties(private/freestanding.cpp PROPERTIES
        COMPILE_OPTIONS "-fno-builtin")
endif()

target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
        ${CMAKE_CURRENT_SOURCE_DIR}/public/core
)


