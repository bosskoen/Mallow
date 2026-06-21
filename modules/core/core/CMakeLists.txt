#core/core/CMakeLists.txt

add_library(core STATIC)
add_library(core::core ALIAS core)

if(WIN32)
    target_link_libraries(core PRIVATE synchronization)
endif()

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
)

target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
        ${CMAKE_CURRENT_SOURCE_DIR}/public/core
)


