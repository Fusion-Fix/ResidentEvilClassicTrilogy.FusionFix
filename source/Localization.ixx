module;
#include <common.hxx>
#include <sstream>
#include <unordered_map>
#include "AssetPathSearch.hxx"
#include "resources/MenuText.h"

export module Localization;

export namespace Localization
{
    std::unordered_map<std::string, std::wstring> strings;
    std::string language = "american";

    std::wstring Decode(std::string_view text)
    {
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
        if (!length) return {};
        std::wstring result(length, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(), length);
        return result;
    }

    void ReadResource(int id)
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&strings), &module);
        const auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (!resource) return;
        const auto data = LockResource(LoadResource(module, resource));
        const auto size = SizeofResource(module, resource);
        if (!data || !size) return;
        std::istringstream input(std::string(static_cast<const char*>(data), size));
        std::string line, key;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
            if (line.size() > 2 && line.front() == '[' && line.back() == ']')
                key = line.substr(1, line.size() - 2);
            else if (!key.empty() && !line.empty() && line.front() != ';')
            {
                auto value = Decode(line);
                if (!value.empty()) strings[key] = std::move(value);
                key.clear();
            }
        }
    }

    void Init()
    {
        language = AssetPaths::Language(GetModuleHandleW(nullptr));
        strings.clear();
        ReadResource(IDR_MENU_AMERICAN);
        constexpr std::pair<std::string_view, int> translations[] = {
            { "french", IDR_MENU_FRENCH }, { "german", IDR_MENU_GERMAN }, { "italian", IDR_MENU_ITALIAN },
            { "japanese", IDR_MENU_JAPANESE }, { "spanish", IDR_MENU_SPANISH }
        };
        for (const auto& [name, resource] : translations)
            if (language == name) ReadResource(resource);
    }

    std::wstring Text(std::string_view key)
    {
        if (const auto found = strings.find(std::string(key)); found != strings.end())
            return found->second;
        return Decode(key);
    }
}
