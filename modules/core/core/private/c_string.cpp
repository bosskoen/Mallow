#include "c_string.h"

static index_t c_strLen(const char* ptr){
    index_t len = 0;
    while (ptr[len] != '\0') len++;
    return len;
}

namespace core
{
    constexpr CStr CStr::forPtr(const char *ptr)
    {
        return CStr(ptr, c_strLen(ptr));
    }
} // namespace core

