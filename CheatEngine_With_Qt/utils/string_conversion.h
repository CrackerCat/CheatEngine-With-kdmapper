#pragma once
#include <string>
#include <windows.h>

/// @brief 将宽字符串 (WCHAR*) 转换为 UTF-8 窄字符串 (std::string)
inline std::string wstring_to_utf8(const wchar_t* wide_str)
{
    if (!wide_str || wide_str[0] == L'\0')
        return {};

    int len = WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};

    std::string result(static_cast<size_t>(len) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide_str, -1, result.data(), len, nullptr, nullptr);
    return result;
}
