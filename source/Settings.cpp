#include "Settings.h"

#include <windows.h>

#include <charconv>
#include <cmath>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
    // The linker supplies the base address of this module (the ASI).
    extern "C" IMAGE_DOS_HEADER __ImageBase;

    template<typename T>
    T ReadShadowNumber(const std::wstring& path, const wchar_t* key, T fallback)
    {
        wchar_t buffer[128];
        const auto length = GetPrivateProfileStringW(L"DynamicShadowColor", key, L"", buffer,
                                                      std::size(buffer), path.c_str());
        std::wstring_view text(buffer, length);
        const auto comment = text.find(L'#');
        if (comment == text.npos && length == std::size(buffer) - 1)
            return fallback; // The numeric value may have been truncated.
        text = text.substr(0, comment);
        const auto first = text.find_first_not_of(L" \t");
        if (first == text.npos || text.find_first_not_of(L"0123456789.eE+- \t") != text.npos)
            return fallback;
        text = text.substr(first, text.find_last_not_of(L" \t") - first + 1);
        const std::string number(text.begin(), text.end());
        T value;
        const auto end = number.data() + number.size();
        const auto result = std::from_chars(number.data(), end, value);
        return result.ec == std::errc{} && result.ptr == end ? value : fallback;
    }

}

Settings LoadSettings()
{
    Settings settings;
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),
                                                path.data(), static_cast<DWORD>(path.size()));
        if (!length)
            return settings;
        if (length < path.size())
        {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }
    path.replace(path.find_last_of(L'.'), std::wstring::npos, L".ini");

    const auto divisor = ReadShadowNumber(path, L"Div", settings.shadowColorDivisor);
    if (std::isfinite(divisor) && divisor > 0.0f)
        settings.shadowColorDivisor = divisor;
    const auto opacity = ReadShadowNumber(path, L"Opacity", settings.shadowOpacity);
    if (opacity >= 0 && opacity <= 255)
        settings.shadowOpacity = opacity;
    wchar_t flag[4] = {};
    GetPrivateProfileStringW(L"Extras", L"ForceStaticVehicleShadows", L"", flag, std::size(flag), path.c_str());
    const std::wstring_view value(flag);
    if (value == L"0" || value == L"1")
        settings.forceStaticVehicleShadows = value == L"1";
    return settings;
}
