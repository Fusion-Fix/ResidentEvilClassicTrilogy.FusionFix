#pragma once
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <string_view>

namespace AssetPaths
{
    inline const char* Find(HMODULE module, std::string_view signature)
    {
        // Asset paths live in data sections, beyond the code pattern scanner.
        const auto base = reinterpret_cast<uintptr_t>(module);
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const auto sections = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
        {
            const auto first = reinterpret_cast<const char*>(base + sections[i].VirtualAddress);
            const auto last = first + sections[i].Misc.VirtualSize;
            const auto found = std::search(first, last, signature.begin(), signature.end(),
                [](char actual, char expected) { return expected == '?' || actual == expected; });
            if (found != last) return found;
        }
        return nullptr;
    }

    inline std::string_view Language(HMODULE module)
    {
        using namespace std::literals::string_view_literals;
        if (const auto path = Find(module, "common\\dat?\\gwarning.adt\0"sv))
        {
            switch (path[10])
            {
                case 'f': return "french";
                case 'g': return "german";
                case 'i': return "italian";
                case 's': return "spanish";
                case 'a': return "japanese";
            }
        }
        else if (const auto path = Find(module, ".\\???\\data\\item_mix.pix\0"sv))
        {
            const std::string_view code(path + 2, 3);
            if (code == "fra") return "french";
            if (code == "ger") return "german";
            if (code == "jpn") return "japanese";
        }
        else if (const auto path = Find(module, "bio19/data_?/etc2/Warn?.tim\0"sv))
        {
            switch (path[11])
            {
                case 'f': return "french";
                case 'g': return "german";
                case 'i': return "italian";
                case 's': return "spanish";
                case 'j': return "japanese";
            }
        }
        return "american";
    }
}
