#pragma once

#include <Windows.h>
#include <charconv>
#include <filesystem>
#include <string_view>
#include "IniReader.h"

namespace Presentation
{
    struct DisplaySize
    {
        int width = 0, height = 0;
    };

    inline DisplaySize ConfiguredDisplaySize(std::string_view value, DisplaySize fallback)
    {
        DisplaySize size;
        const auto end = value.data() + value.size();
        const auto width = std::from_chars(value.data(), end, size.width);
        if (width.ec != std::errc{} || width.ptr == end || *width.ptr != 'x') return fallback;
        const auto height = std::from_chars(width.ptr + 1, end, size.height);
        if (height.ec != std::errc{} || size.width < 320 || size.height < 200
            || size.width > 32767 || size.height > 32767) return fallback;
        if (height.ptr != end)
        {
            if (*height.ptr != '@') return fallback;
            float refresh = 0;
            const auto rate = std::from_chars(height.ptr + 1, end, refresh);
            if (rate.ec != std::errc{} || rate.ptr != end || !(refresh > 0 && refresh <= 1000)) return fallback;
        }
        return size;
    }

    inline DisplaySize OutputDisplaySize()
    {
        DEVMODEW desktop{ .dmSize = sizeof(DEVMODEW) };
        if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &desktop)) return {};
        const DisplaySize fallback{ int(desktop.dmPelsWidth), int(desktop.dmPelsHeight) };
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)));
        if (!length || length >= std::size(executable)) return fallback;
        // GOG's wrapper can present at a different resolution from the desktop.
        // Render at that size too, otherwise its aspect-preserving fit adds bars
        // before the configured widescreen limit can use the complete output.
        CIniReader config(std::filesystem::path(executable).parent_path() / L"dxcfg.ini");
        return ConfiguredDisplaySize(config.ReadString("dxcfg", "display", "desktop"), fallback);
    }
}
