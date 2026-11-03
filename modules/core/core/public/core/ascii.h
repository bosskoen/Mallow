#pragma once
#include "core/compilers.h"

namespace core
{
    MLW_FORCE_INLINE constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }
    MLW_FORCE_INLINE constexpr bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
    MLW_FORCE_INLINE constexpr bool isLower(char c) { return c >= 'a' && c <= 'z'; }
    MLW_FORCE_INLINE constexpr bool isAlpha(char c) { return isUpper(c) || isLower(c); }
    MLW_FORCE_INLINE constexpr bool isAlnum(char c) { return isAlpha(c) || isDigit(c); }
    MLW_FORCE_INLINE constexpr bool isSpace(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
    MLW_FORCE_INLINE constexpr bool isHex(char c)   { return isDigit(c) || (c>='a'&&c<='f') || (c>='A'&&c<='F'); }
    MLW_FORCE_INLINE constexpr char toUpper(char c) { return isLower(c) ? char(c - 32) : c; }
    MLW_FORCE_INLINE constexpr char toLower(char c) { return isUpper(c) ? char(c + 32) : c; }
    MLW_FORCE_INLINE constexpr int  hexVal(char c)  { // -1 if not hex; handy for parsing
        if (isDigit(c)) return c - '0';
        if (c>='a'&&c<='f') return c - 'a' + 10;
        if (c>='A'&&c<='F') return c - 'A' + 10;
        return -1;
    }
}