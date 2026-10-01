module;
#include <common.hxx>
#include <safetyhook.hpp>
#include <fstream>
#include <sstream>
#include <vector>
#include <cwctype>

export module PortableSettings;
import common;

namespace NativeSettings
{
    struct Value { DWORD type; std::vector<BYTE> bytes; };
    struct Store
    {
        std::recursive_mutex mutex;
        std::filesystem::path root, file;
        std::map<std::wstring, Value> defaults;
        std::map<HKEY, std::wstring> handles;
        std::wstring key;
        uintptr_t nextHandle = 0x7F000000;
        int game = 0;
        bool initialized = false;
        SafetyHookInline openA, openW, createA, createW, queryA, queryW, setA, setW, close, openLegacyA, openLegacyW;
        SafetyHookInline assets;
    };

    Store& State()
    {
        // Registry callbacks can run from other DLLs during process shutdown.
        // Keep their storage and trampolines alive until the process terminates.
        static auto* state = new Store;
        return *state;
    }

    std::wstring Lower(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(), towlower);
        while (!text.empty() && text.back() == L'\\') text.pop_back();
        return text;
    }

    std::wstring Wide(const char* text)
    {
        if (!text) return {};
        const int size = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), size);
        if (!result.empty()) result.pop_back();
        return result;
    }

    std::string Narrow(const std::wstring& text)
    {
        const int size = WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
        if (!result.empty()) result.pop_back();
        return result;
    }

    Value Decode(DWORD type, const std::wstring& text)
    {
        Value value{ type, {} };
        if (type == REG_DWORD)
        {
            wchar_t* end = nullptr;
            const auto number = wcstoull(text.c_str(), &end, 0);
            if (end == text.c_str() || *end || number > UINT32_MAX) return { REG_NONE, {} };
            const DWORD dword = DWORD(number);
            value.bytes.resize(4);
            memcpy(value.bytes.data(), &dword, 4);
        }
        else if (type == REG_SZ)
        {
            value.bytes.resize((text.size() + 1) * sizeof(wchar_t));
            memcpy(value.bytes.data(), text.c_str(), value.bytes.size());
        }
        else
        {
            std::wistringstream stream(text);
            std::wstring byte;
            while (stream >> byte)
            {
                wchar_t* end = nullptr;
                const auto number = wcstoul(byte.c_str(), &end, 16);
                if (byte.size() != 2 || *end || number > 255) return { REG_NONE, {} };
                value.bytes.push_back(BYTE(number));
            }
        }
        return value;
    }

    constexpr std::array<const wchar_t*, 4> sections = { L"NONE", L"STRING", L"BINARY", L"DWORD" };
    constexpr std::array<DWORD, 4> types = { REG_NONE, REG_SZ, REG_BINARY, REG_DWORD };

    std::wstring Read(const wchar_t* section, const wchar_t* name, const wchar_t* fallback)
    {
        std::array<wchar_t, 8192> text{};
        GetPrivateProfileStringW(section, name, fallback, text.data(), DWORD(text.size()), State().file.c_str());
        return text.data();
    }

    bool Get(const wchar_t* name, Value& value)
    {
        auto& state = State();
        const auto normalized = Lower(name ? name : L"");
        if (normalized == L"install path" || normalized == L"install path2" || normalized == L"save path")
        {
            const bool save = normalized == L"save path";
            const auto fallback = save ? (state.game == 1 ? L"SAVE" : L"Saves") : L".";
            const auto relative = std::filesystem::path(Read(L"PATHS", save ? L"Save Path" : L"Install Path", fallback));
            // Never retain a machine-specific registry path in a portable profile.
            const auto path = (state.root / (relative.is_relative() ? relative : std::filesystem::path(fallback))).lexically_normal();
            if (save) { std::error_code error; std::filesystem::create_directories(path, error); }
            value = Decode(REG_SZ, path.wstring() + L"\\");
            return true;
        }
        for (size_t i = 0; i < sections.size(); ++i)
        {
            const auto text = Read(sections[i], name, L"\x1");
            if (text != L"\x1")
            {
                value = Decode(types[i], text);
                return value.type == types[i] && (types[i] == REG_SZ || !value.bytes.empty()
                    || (text.empty() && (types[i] == REG_NONE || types[i] == REG_BINARY)));
            }
        }
        const auto found = state.defaults.find(normalized);
        if (found == state.defaults.end()) return false;
        value = found->second;
        return true;
    }

    std::wstring Path(HKEY key, const wchar_t* subkey)
    {
        auto& state = State();
        std::wstring path;
        if (key != HKEY_CURRENT_USER)
        {
            const auto found = state.handles.find(key);
            if (found == state.handles.end()) return {};
            path = found->second + L"\\";
        }
        return Lower(path + (subkey ? subkey : L""));
    }

    bool Open(HKEY key, const wchar_t* subkey, HKEY* result)
    {
        auto& state = State();
        const auto path = Path(key, subkey);
        if (path != state.key) return false;
        if (path == state.key) SetCurrentDirectoryW(state.root.c_str());
        if (result)
        {
            *result = reinterpret_cast<HKEY>(++state.nextHandle);
            state.handles.emplace(*result, path);
        }
        return true;
    }

    LSTATUS WINAPI OpenA(HKEY key, LPCSTR subkey, DWORD options, REGSAM access, PHKEY result)
    {
        const std::lock_guard lock(State().mutex);
        if (Open(key, Wide(subkey).c_str(), result)) return result ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
        return State().openA.unsafe_stdcall<LSTATUS>(key, subkey, options, access, result);
    }
    LSTATUS WINAPI OpenW(HKEY key, LPCWSTR subkey, DWORD options, REGSAM access, PHKEY result)
    {
        const std::lock_guard lock(State().mutex);
        if (Open(key, subkey, result)) return result ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
        return State().openW.unsafe_stdcall<LSTATUS>(key, subkey, options, access, result);
    }
    LSTATUS WINAPI CreateA(HKEY key, LPCSTR subkey, DWORD reserved, LPSTR cls, DWORD options, REGSAM access, LPSECURITY_ATTRIBUTES security, PHKEY result, LPDWORD disposition)
    {
        const std::lock_guard lock(State().mutex);
        if (Open(key, Wide(subkey).c_str(), result))
        {
            if (disposition) *disposition = REG_OPENED_EXISTING_KEY;
            return result ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
        }
        return State().createA.unsafe_stdcall<LSTATUS>(key, subkey, reserved, cls, options, access, security, result, disposition);
    }
    LSTATUS WINAPI CreateW(HKEY key, LPCWSTR subkey, DWORD reserved, LPWSTR cls, DWORD options, REGSAM access, LPSECURITY_ATTRIBUTES security, PHKEY result, LPDWORD disposition)
    {
        const std::lock_guard lock(State().mutex);
        if (Open(key, subkey, result))
        {
            if (disposition) *disposition = REG_OPENED_EXISTING_KEY;
            return result ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
        }
        return State().createW.unsafe_stdcall<LSTATUS>(key, subkey, reserved, cls, options, access, security, result, disposition);
    }
    LSTATUS WINAPI OpenLegacyA(HKEY key, LPCSTR subkey, PHKEY result)
    {
        const std::lock_guard lock(State().mutex);
        if (Open(key, Wide(subkey).c_str(), result)) return result ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
        return State().openLegacyA.unsafe_stdcall<LSTATUS>(key, subkey, result);
    }
    LSTATUS WINAPI OpenLegacyW(HKEY key, LPCWSTR subkey, PHKEY result)
    {
        const std::lock_guard lock(State().mutex);
        if (Open(key, subkey, result)) return result ? ERROR_SUCCESS : ERROR_INVALID_PARAMETER;
        return State().openLegacyW.unsafe_stdcall<LSTATUS>(key, subkey, result);
    }

    LSTATUS Query(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size, bool unicode)
    {
        if (!size || reserved) return ERROR_INVALID_PARAMETER;
        Value value{};
        if (State().handles.at(key) != State().key || !Get(name, value)) return ERROR_FILE_NOT_FOUND;
        if (!unicode && value.type == REG_SZ)
        {
            // Keep the INI's Unicode text intact for wide-character callers.
            // Only ANSI callers require conversion to the system code page.
            const auto string = Narrow(reinterpret_cast<const wchar_t*>(value.bytes.data()));
            value.bytes.assign(string.begin(), string.end());
            value.bytes.push_back(0);
        }
        if (type) *type = value.type;
        const DWORD capacity = *size;
        *size = DWORD(value.bytes.size());
        if (!data) return ERROR_SUCCESS;
        if (capacity < value.bytes.size()) return ERROR_MORE_DATA;
        memcpy(data, value.bytes.data(), value.bytes.size());
        return ERROR_SUCCESS;
    }
    LSTATUS WINAPI QueryA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
    {
        const std::lock_guard lock(State().mutex);
        if (State().handles.contains(key)) return Query(key, Wide(name).c_str(), reserved, type, data, size, false);
        return State().queryA.unsafe_stdcall<LSTATUS>(key, name, reserved, type, data, size);
    }
    LSTATUS WINAPI QueryW(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size)
    {
        const std::lock_guard lock(State().mutex);
        if (State().handles.contains(key)) return Query(key, name, reserved, type, data, size, true);
        return State().queryW.unsafe_stdcall<LSTATUS>(key, name, reserved, type, data, size);
    }

    LSTATUS Set(LPCWSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size, bool unicode)
    {
        if (reserved || (!data && size) || !name) return ERROR_INVALID_PARAMETER;
        if (type != REG_SZ && type != REG_DWORD && type != REG_BINARY && type != REG_NONE) return ERROR_UNSUPPORTED_TYPE;
        const auto normalized = Lower(name);
        if (normalized == L"install path" || normalized == L"install path2" || normalized == L"save path") return ERROR_SUCCESS;
        std::wstring text;
        if (type == REG_DWORD)
        {
            if (size != 4) return ERROR_INVALID_DATA;
            DWORD number; memcpy(&number, data, 4);
            std::wostringstream stream; stream << L"0x" << std::hex << std::setw(8) << std::setfill(L'0') << number;
            text = stream.str();
        }
        else if (type == REG_SZ)
        {
            if (!size) text.clear();
            else if (unicode)
            {
                if (size % sizeof(wchar_t)) return ERROR_INVALID_DATA;
                text.assign(reinterpret_cast<const wchar_t*>(data), size / sizeof(wchar_t));
            }
            else
            {
                std::string string(reinterpret_cast<const char*>(data), size);
                string.push_back(0); text = Wide(string.c_str());
            }
            while (!text.empty() && !text.back()) text.pop_back();
        }
        else
        {
            std::wostringstream stream;
            for (DWORD i = 0; i < size; ++i) stream << (i ? L" " : L"") << std::hex << std::setw(2) << std::setfill(L'0') << unsigned(data[i]);
            text = stream.str();
        }
        size_t section = 0;
        for (size_t i = 0; i < types.size(); ++i) if (types[i] == type) section = i;
        if (!WritePrivateProfileStringW(sections[section], name, text.c_str(), State().file.c_str())) return ERROR_WRITE_FAULT;
        for (size_t i = 0; i < types.size(); ++i)
            if (i != section) WritePrivateProfileStringW(sections[i], name, nullptr, State().file.c_str());
        return ERROR_SUCCESS;
    }
    LSTATUS WINAPI SetA(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size)
    {
        const std::lock_guard lock(State().mutex);
        if (State().handles.contains(key)) return Set(Wide(name).c_str(), reserved, type, data, size, false);
        return State().setA.unsafe_stdcall<LSTATUS>(key, name, reserved, type, data, size);
    }
    LSTATUS WINAPI SetW(HKEY key, LPCWSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size)
    {
        const std::lock_guard lock(State().mutex);
        if (State().handles.contains(key)) return Set(name, reserved, type, data, size, true);
        return State().setW.unsafe_stdcall<LSTATUS>(key, name, reserved, type, data, size);
    }
    LSTATUS WINAPI Close(HKEY key)
    {
        const std::lock_guard lock(State().mutex);
        if (State().handles.erase(key)) return ERROR_SUCCESS;
        return State().close.unsafe_stdcall<LSTATUS>(key);
    }

    void Migrate()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, State().key.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
        for (DWORD index = 0;; ++index)
        {
            std::array<wchar_t, 256> name{};
            std::array<BYTE, 8192> bytes{};
            DWORD length = DWORD(name.size()), size = DWORD(bytes.size()), type = 0;
            const auto result = RegEnumValueW(key, index, name.data(), &length, nullptr, &type, bytes.data(), &size);
            if (result == ERROR_NO_MORE_ITEMS) break;
            if (result == ERROR_SUCCESS) Set(name.data(), 0, type, bytes.data(), size, true);
        }
        RegCloseKey(key);
    }

    bool UnicodeProfile()
    {
        // The Windows INI API writes ANSI unless the file starts with a UTF-16
        // BOM. Preserve existing settings before enabling wide-character writes.
        std::ifstream input(State().file, std::ios::binary);
        if (!input) return false;
        const std::string bytes((std::istreambuf_iterator<char>(input)), {});
        input.close();
        if (bytes.starts_with("\xFF\xFE")) return true;
        const auto text = std::string_view(bytes).substr(bytes.starts_with("\xEF\xBB\xBF") ? 3 : 0);
        UINT codePage = CP_UTF8;
        int length = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
        if (!length && !text.empty())
        {
            codePage = CP_ACP;
            length = MultiByteToWideChar(codePage, 0, text.data(), int(text.size()), nullptr, 0);
            if (!length) return false;
        }
        std::wstring wide(length, L'\0');
        if (length) MultiByteToWideChar(codePage, 0, text.data(), int(text.size()), wide.data(), length);
        auto temporary = State().file;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write("\xFF\xFE", 2);
        output.write(reinterpret_cast<const char*>(wide.data()), std::streamsize(wide.size() * sizeof(wchar_t)));
        output.close();
        if (!output || !MoveFileExW(temporary.c_str(), State().file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporary.c_str());
            return false;
        }
        return true;
    }

    BOOL __cdecl Assets(void* drives)
    {
        // RE2 probes its local data before reading the native settings.
        SetCurrentDirectoryW(State().root.c_str());
        return State().assets.unsafe_ccall<BOOL>(drives);
    }
}

export namespace PortableSettings
{
    std::filesystem::path SaveDirectory()
    {
        auto& state = NativeSettings::State();
        const std::lock_guard lock(state.mutex);
        if (state.initialized)
        {
            NativeSettings::Value value;
            if (NativeSettings::Get(L"Save Path", value))
                return std::filesystem::path(reinterpret_cast<const wchar_t*>(value.bytes.data()));
        }
        HKEY key = nullptr;
        std::array<wchar_t, 32768> path{};
        DWORD size = DWORD(sizeof(path)), type = 0;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\CAPCOM\\RESIDENT EVIL2", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
        {
            const auto result = RegQueryValueExW(key, L"Save Path", nullptr, &type, reinterpret_cast<BYTE*>(path.data()), &size);
            RegCloseKey(key);
            if (result == ERROR_SUCCESS && type == REG_SZ) return path.data();
        }
        GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        return std::filesystem::path(path.data()).parent_path() / L"Saves";
    }

    bool Init(int game)
    {
        CIniReader reader("");
        if (!reader.ReadInteger("MAIN", "PortableMode", 1)) return true;
        auto& state = NativeSettings::State();
        const std::lock_guard lock(state.mutex);
        if (state.initialized) return true;
        std::array<wchar_t, 32768> executable{};
        GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
        state.root = std::filesystem::path(executable.data()).parent_path();
        state.game = game;
        SetCurrentDirectoryW(state.root.c_str());
        state.file = state.root / (game == 1 ? L"RE1Classic.Game.ini" : L"RE2Classic.Game.ini");
        state.key = game == 1 ? L"software\\capcom\\resident evil" : L"software\\capcom\\resident evil2";
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Init), &module);
        const auto resource = FindResourceW(module, MAKEINTRESOURCEW(200 + game), RT_RCDATA);
        if (!resource) return false;
        const auto data = LockResource(LoadResource(module, resource));
        const auto size = SizeofResource(module, resource);
        std::string defaults(static_cast<const char*>(data), size);
        const bool createdProfile = !std::filesystem::exists(state.file);
        if (createdProfile)
        {
            std::ofstream file(state.file, std::ios::binary);
            file.write(defaults.data(), defaults.size());
            if (!file) return false;
            file.close();
        }
        if (!NativeSettings::UnicodeProfile()) return false;
        if (createdProfile) NativeSettings::Migrate();
        std::istringstream stream(defaults);
        std::string line, section;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with('[')) { section = line; continue; }
            const auto equals = line.find('=');
            if (equals == std::string::npos) continue;
            for (size_t i = 0; i < NativeSettings::sections.size(); ++i)
                if (section == "[" + NativeSettings::Narrow(NativeSettings::sections[i]) + "]")
                    state.defaults.emplace(NativeSettings::Lower(NativeSettings::Wide(line.substr(0, equals).c_str())),
                        NativeSettings::Decode(NativeSettings::types[i], NativeSettings::Wide(line.substr(equals + 1).c_str())));
        }
        const auto advapi = LoadLibraryW(L"advapi32.dll");
        const auto hook = [&](SafetyHookInline& target, const char* name, auto replacement)
        {
            target = safetyhook::create_inline(GetProcAddress(advapi, name), replacement, SafetyHookInline::StartDisabled);
            return bool(target);
        };
        // Hook the API, so native game code and GOG DLLs share the same profile.
        // Unrelated registry keys retain the original Windows behavior.
        const bool created = hook(state.openA, "RegOpenKeyExA", NativeSettings::OpenA)
            && hook(state.openW, "RegOpenKeyExW", NativeSettings::OpenW)
            && hook(state.createA, "RegCreateKeyExA", NativeSettings::CreateA)
            && hook(state.createW, "RegCreateKeyExW", NativeSettings::CreateW)
            && hook(state.queryA, "RegQueryValueExA", NativeSettings::QueryA)
            && hook(state.queryW, "RegQueryValueExW", NativeSettings::QueryW)
            && hook(state.setA, "RegSetValueExA", NativeSettings::SetA)
            && hook(state.setW, "RegSetValueExW", NativeSettings::SetW)
            && hook(state.close, "RegCloseKey", NativeSettings::Close)
            && hook(state.openLegacyA, "RegOpenKeyA", NativeSettings::OpenLegacyA)
            && hook(state.openLegacyW, "RegOpenKeyW", NativeSettings::OpenLegacyW);
        std::array hooks = { &state.openA, &state.openW, &state.createA, &state.createW, &state.queryA,
            &state.queryW, &state.setA, &state.setW, &state.close, &state.openLegacyA, &state.openLegacyW };
        if (!created)
        {
            for (auto* item : hooks) item->reset();
            return false;
        }
        for (auto* item : hooks)
            if (!item->enable())
            {
                for (auto* installed : hooks) installed->reset();
                return false;
            }
        if (game == 2)
        {
            auto pattern = hook::pattern("81 EC 08 02 00 00 8D 84 24 04 01 00 00 53 55 56 57 50 68 00 01 00 00");
            if (pattern.size() == 1)
                state.assets = safetyhook::create_inline(pattern.get_first(), NativeSettings::Assets);
        }
        state.initialized = true;
        return true;
    }
}
