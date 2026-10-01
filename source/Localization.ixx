module;
#include <common.hxx>
#include <sstream>
#include <unordered_map>
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
        language = "american";
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        auto folder = std::filesystem::path(executable).parent_path().filename().wstring();
        std::transform(folder.begin(), folder.end(), folder.begin(), towlower);
        for (const auto* name : { "french", "german", "italian", "spanish", "japanese" })
            if (folder == Decode(name)) language = name;
        auto filename = std::filesystem::path(executable).filename().wstring();
        std::transform(filename.begin(), filename.end(), filename.begin(), towlower);
        if (filename.starts_with(L"bio3_pc") || filename.starts_with(L"biohazard"))
            language = "japanese";
        // RE2's character executables carry the language in their suffix.
        if (filename.starts_with(L"leon") || filename.starts_with(L"claire"))
        {
            const auto stem = std::filesystem::path(filename).stem().wstring();
            switch (stem.back())
            {
                case L'f': language = "french"; break;
                case L'g': language = "german"; break;
                case L'i': language = "italian"; break;
                case L's': language = "spanish"; break;
                case L'j': language = "japanese"; break;
                case L'u': language = "american"; break;
            }
        }
        auto warning2 = hook::pattern("63 6F 6D 6D 6F 6E 5C 64 61 74 ? 5C 67 77 61 72 6E 69 6E 67 2E 61 64 74 00");
        if (warning2.size() == 1)
        {
            switch (*warning2.get_first<char>(10))
            {
                case 'u': language = "american"; break;
                case 'f': language = "french"; break;
                case 'g': language = "german"; break;
                case 'i': language = "italian"; break;
                case 's': language = "spanish"; break;
                case 'j': language = "japanese"; break;
            }
        }
        auto assets1 = hook::pattern("2E 5C ? ? ? 5C 64 61 74 61 5C 69 74 65 6D 5F 6D 69 78 2E 70 69 78 00");
        if (assets1.size() == 1)
        {
            const std::string_view code(assets1.get_first<char>(2), 3);
            if (code == "usa") language = "american";
            else if (code == "fra") language = "french";
            else if (code == "ger") language = "german";
            else if (code == "jpn") language = "japanese";
        }
        // The localized warning asset identifies the executable's language,
        // even when the user moves it out of its Steam language folder.
        auto warning = hook::pattern("62 69 6F 31 39 2F 64 61 74 61 5F ? 2F 65 74 63 32 2F 57 61 72 6E ? 2E 74 69 6D 00");
        if (warning.size() == 1)
        {
            switch (*warning.get_first<char>(11))
            {
                case 'u': language = "american"; break;
                case 'f': language = "french"; break;
                case 'g': language = "german"; break;
                case 'i': language = "italian"; break;
                case 's': language = "spanish"; break;
                case 'j': language = "japanese"; break;
            }
        }
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
