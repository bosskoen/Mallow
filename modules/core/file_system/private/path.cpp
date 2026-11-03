#include "path.h"
#include <core/ascii.h>
static isize lastSep(const fs::Path p)
{
    for (isize i = p.len - 1; i >= 0; --i)
        if (fs::Path::isSep(p.ptr[i]))
            return i;
    return -1;
}
core::CStr fs::Path::fileName() const
{
    isize i = lastSep(*this);
    return core::CStr{ptr + i + 1, len - i - 1};
};

fs::Path fs::Path::parent() const
{
    isize end = len;
    if (end > 1 && isSep(ptr[end - 1]))
        --end; // ignore a single trailing sep
    // find last sep before `end`
    isize s = -1;
    for (isize i = end - 1; i >= 0; --i)
    {
        if (isSep(ptr[i]))
        {
            s = i;
            break;
        }
    }
    if (s < 0)
        return fs::Path{ptr, 0};
    if (s == 0)
        return fs::Path{ptr, 1};
    return fs::Path{ptr, s};
}

core::CStr fs::Path::extension() const
{
    isize i = len - 1;
    for (; i >= 0; --i)
    {
        if (isSep(ptr[i]))
            return "";
        if (ptr[i] == '.')
            break;
    }

    if (i == -1 || i == 0 || isSep(ptr[i - 1]))
        return "";
    return core::CStr{ptr + i + 1, len - i - 1};
}

core::CStr fs::Path::stem() const
{
    core::CStr name = fileName();
    if (name.len != 0 && name[0] == '.')
        return name;
    for (isize i = name.len - 1; i > 0; --i)
    { // i > 0: leading dot stays in stem
        if (name.ptr[i] == '.')
            return core::CStr{name.ptr, i};
    }
    return name;
}

bool fs::Path::isRoot() const
{
    if (len == 0)
        return false;
#ifdef MLW_WINDOWS
    if (len > 2 && ptr[0] == '\\' && ptr[1] == '\\')
        return true;
#endif
    return isSep(*ptr);
}

bool fs::Path::isAbsolute() const
{
    if (len == 0)
        return false;
#ifdef MLW_WINDOWS
    if (len <= 2)
        return false; // C:\ is longer then 3 and \\name has to be longer then 3
    if (ptr[0] == '\\' && ptr[1] == '\\')
        return true;
    if (core::isAlpha(ptr[0]) && ptr[1] == ':' && isSep(ptr[2]))
        return true;

    return false;
#else

    return isSep(*ptr);

#endif // MLW_WINDOWS
}

void fs::Pathbuffer::push(Path tail)
{
    if (tail.len == 0)
        return;

    if (tail.isAbsolute())
    {
        buffer.clear();
        buffer.append(tail.toCStr());
        return;
    }

#ifdef MLW_WINDOWS
    if (tail.isRoot())
    {
        if (buffer.len() >= 2 && core::isAlpha(buffer.data()[0]) && buffer.data()[1] == ':')
            buffer.truncate(2);
        else
            buffer.clear();
        buffer.append(tail.toCStr());
        return;
    }
#endif

    if (buffer.len() != 0 && !Path::isSep(buffer.back()) && !Path::isSep(tail.ptr[0]))
        buffer.push('/');
    buffer.append(tail.toCStr());
}

void fs::Pathbuffer::pop()
{
    isize end = buffer.len();
    if (end == 0)
        return;
    if (end > 1 && Path::isSep(buffer.data()[end - 1]))
        --end; // ignore trailing sep

    // find last sep before `end`
    isize s = -1;
    for (isize i = end - 1; i >= 0; --i)
        if (Path::isSep(buffer.data()[i]))
        {
            s = i;
            break;
        }

#ifdef MLW_WINDOWS
    // don't pop past a drive root "C:\" down to bare "C:"
    if ((s == 2 || s == -1) && buffer.len() >= 3 && core::isAlpha(buffer.data()[0]) && buffer.data()[1] == ':')
    {
        buffer.truncate(3); // keep "C:\" (drive + sep), not "C:"
        return;
    }
#endif

    if (s < 0)
        buffer.truncate(0); // no sep -> empty  (matches your "a" -> "")
    else if (s == 0)
        buffer.truncate(1); // sep at root -> keep "/"  (matches "/a" -> "/")
    else
        buffer.truncate(s);
}


void fs::Pathbuffer::toPortable()
{
    #ifndef MLW_WINDOWS
    return;
    #else
    isize n = buffer.len();
    if (n == 0) return;

    isize start = 0;
    char* d = buffer.data();
    if (n >= 4 && d[0]=='\\' && d[1]=='\\' && d[2]=='?' && d[3]=='\\') {
        return;   // \\?\ extended-length: leave entirely alone
    }
    if (n >= 2 && d[0]=='\\' && d[1]=='\\') {
        start = 2;   // UNC: keep the leading '\\', convert the rest? -> see decision below
    }

    for (isize i = start; i < n; ++i)
        if (d[i] == '\\') d[i] = '/';

    #endif
}