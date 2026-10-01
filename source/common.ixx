module;

#include <common.hxx>
#include <Zydis.h>
#include "includes/gameref.hpp"
#include "includes/callbacks.h"

export module common;

export using ::GameRef;
export using ::ProtectedGameRef;
export using ::CallbackHandler;

import <stacktrace>;
import <optional>;

export class FusionFix
{
public:
    template<typename... Args>
    class Event : public std::function<void(Args...)>
    {
    public:
        using std::function<void(Args...)>::function;

    private:
        std::list<std::function<void(Args...)>> handlers;

    public:
        auto operator+=(std::function<void(Args...)>&& handler) -> std::function<void()>
        {
            auto it = handlers.insert(handlers.end(), std::move(handler));
            return [this, it]() { handlers.erase(it); };
        }

        void executeAll(Args... args) const
        {
            if (!handlers.empty())
            {
                for (auto& handler : handlers)
                {
                    handler(args...);
                }
            }
        }

        std::reference_wrapper<std::vector<std::future<void>>> executeAllAsync(Args... args) const
        {
            static std::vector<std::future<void>> pendingFutures;
            if (!handlers.empty())
            {
                for (auto& handler : handlers)
                {
                    pendingFutures.emplace_back(std::async(std::launch::async, std::cref(handler), args...));
                }
            }
            return std::ref(pendingFutures);
        }
    };

public:
    static Event<>& onInitEvent()
    {
        static Event<> InitEvent;
        return InitEvent;
    }
    static Event<>& onInitEventAsync()
    {
        static Event<> InitEventAsync;
        return InitEventAsync;
    }
    static Event<>& onShutdownEvent()
    {
        static Event<> ShutdownEvent;
        return ShutdownEvent;
    }
    static Event<>& onGameInitEvent()
    {
        static Event<> GameInitEvent;
        return GameInitEvent;
    }
    static Event<>& onGameProcessEvent()
    {
        static Event<> GameProcessEvent;
        return GameProcessEvent;
    }
    static Event<>& onMenuDrawingEvent()
    {
        static Event<> MenuDrawingEvent;
        return MenuDrawingEvent;
    }
    static Event<>& onMenuEnterEvent()
    {
        static Event<> MenuEnterEvent;
        return MenuEnterEvent;
    }
    static Event<>& onMenuExitEvent()
    {
        static Event<> MenuExitEvent;
        return MenuExitEvent;
    }
    static Event<bool>& onActivateApp()
    {
        static Event<bool> ActivateApp;
        return ActivateApp;
    }
    static Event<>& onBeforeReset()
    {
        static Event<> BeforeReset;
        return BeforeReset;
    }
    static Event<>& onEndScene()
    {
        static Event<> EndScene;
        return EndScene;
    }
    static Event<>& onReadGameConfig()
    {
        static Event<> ReadGameConfig;
        return ReadGameConfig;
    }
};

export template<class T = std::filesystem::path>
T GetModulePath(HMODULE hModule)
{
    static constexpr auto INITIAL_BUFFER_SIZE = MAX_PATH;
    static constexpr auto MAX_ITERATIONS = 7;

    if constexpr (std::is_same_v<T, std::filesystem::path>)
    {
        std::u16string ret;
        std::filesystem::path pathret;
        auto bufferSize = INITIAL_BUFFER_SIZE;
        for (size_t iterations = 0; iterations < MAX_ITERATIONS; ++iterations)
        {
            ret.resize(bufferSize);
            size_t charsReturned = 0;
            charsReturned = GetModuleFileNameW(hModule, (LPWSTR)&ret[0], bufferSize);
            if (charsReturned < ret.length())
            {
                ret.resize(charsReturned);
                pathret = ret;
                return pathret;
            }
            else
            {
                bufferSize *= 2;
            }
        }
    }
    else
    {
        T ret;
        auto bufferSize = INITIAL_BUFFER_SIZE;
        for (size_t iterations = 0; iterations < MAX_ITERATIONS; ++iterations)
        {
            ret.resize(bufferSize);
            size_t charsReturned = 0;
            if constexpr (std::is_same_v<T, std::string>)
                charsReturned = GetModuleFileNameA(hModule, &ret[0], bufferSize);
            else
                charsReturned = GetModuleFileNameW(hModule, &ret[0], bufferSize);
            if (charsReturned < ret.length())
            {
                ret.resize(charsReturned);
                return ret;
            }
            else
            {
                bufferSize *= 2;
            }
        }
    }
    return T();
}

export template<class T = std::filesystem::path>
T GetThisModulePath()
{
    HMODULE hm = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&FusionFix::onInitEvent, &hm);
    T r = GetModulePath<T>(hm);
    if constexpr (std::is_same_v<T, std::filesystem::path>)
        return r.parent_path();
    else if constexpr (std::is_same_v<T, std::string>)
        r = r.substr(0, r.find_last_of("/\\") + 1);
    else
        r = r.substr(0, r.find_last_of(L"/\\") + 1);
    return r;
}

export template<class T = std::filesystem::path>
T GetThisModuleName()
{
    HMODULE hm = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&FusionFix::onInitEvent, &hm);
    const T moduleFileName = GetModulePath<T>(hm);

    if constexpr (std::is_same_v<T, std::filesystem::path>)
        return moduleFileName.filename();
    else if constexpr (std::is_same_v<T, std::string>)
        return moduleFileName.substr(moduleFileName.find_last_of("/\\") + 1);
    else
        return moduleFileName.substr(moduleFileName.find_last_of(L"/\\") + 1);
}

export template<class T = std::filesystem::path>
T GetExeModulePath()
{
    T r = GetModulePath<T>(NULL);

    if constexpr (std::is_same_v<T, std::filesystem::path>)
        return r.parent_path();
    else if constexpr (std::is_same_v<T, std::string>)
        r = r.substr(0, r.find_last_of("/\\") + 1);
    else
        r = r.substr(0, r.find_last_of(L"/\\") + 1);
    return r;
}

export template<class T = std::filesystem::path>
T GetExeModuleName()
{
    const T moduleFileName = GetModulePath<T>(NULL);
    if constexpr (std::is_same_v<T, std::filesystem::path>)
        return moduleFileName.filename();
    else if constexpr (std::is_same_v<T, std::string>)
        return moduleFileName.substr(moduleFileName.find_last_of("/\\") + 1);
    else
        return moduleFileName.substr(moduleFileName.find_last_of(L"/\\") + 1);
}

export bool iequals(std::string_view s1, std::string_view s2)
{
    if (s1.size() != s2.size()) return false;
    return std::equal(s1.begin(), s1.end(), s2.begin(), s2.end(),
        [](char a, char b) { return ::tolower(a) == ::tolower(b); });
}

export bool iequals(std::wstring_view s1, std::wstring_view s2)
{
    if (s1.size() != s2.size()) return false;
    return std::equal(s1.begin(), s1.end(), s2.begin(), s2.end(),
        [](wchar_t a, wchar_t b) { return ::towlower(a) == ::towlower(b); });
}

export std::filesystem::path lexicallyRelativeCaseIns(const std::filesystem::path& path, const std::filesystem::path& base)
{
    class input_iterator_range
    {
    public:
        input_iterator_range(const std::filesystem::path::const_iterator& first, const std::filesystem::path::const_iterator& last)
            : _first(first)
            , _last(last)
        {
        }
        std::filesystem::path::const_iterator begin() const
        {
            return _first;
        }
        std::filesystem::path::const_iterator end() const
        {
            return _last;
        }
    private:
        std::filesystem::path::const_iterator _first;
        std::filesystem::path::const_iterator _last;
    };

    if (!iequals(path.root_name().wstring(), base.root_name().wstring()) || path.is_absolute() != base.is_absolute() || (!path.has_root_directory() && base.has_root_directory()))
    {
        return std::filesystem::path();
    }

    std::filesystem::path::const_iterator a = path.begin(), b = base.begin();

    while (a != path.end() && b != base.end() && iequals(a->wstring(), b->wstring()))
    {
        ++a;
        ++b;
    }

    if (a == path.end() && b == base.end())
    {
        return std::filesystem::path(".");
    }

    int count = 0;

    for (const auto& element : input_iterator_range(b, base.end()))
    {
        if (element != "." && element != "" && element != "..")
        {
            ++count;
        }
        else if (element == "..")
        {
            --count;
        }
    }

    if (count < 0)
    {
        return std::filesystem::path();
    }

    std::filesystem::path result;
    for (int i = 0; i < count; ++i)
    {
        result /= "..";
    }

    for (const auto& element : input_iterator_range(a, path.end()))
    {
        result /= element;
    }

    return result;
};

export inline void CreateThreadAutoClose(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId)
{
    CloseHandle(CreateThread(lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags, lpThreadId));
}

export inline bool IsModuleUAL(HMODULE mod)
{
    if (GetProcAddress(mod, "IsUltimateASILoader") != NULL)
        return true;
    return false;
}

export bool IsUALPresent()
{
    for (const auto& entry : std::stacktrace::current())
    {
        HMODULE hModule = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)entry.native_handle(), &hModule))
        {
            if (IsModuleUAL(hModule))
                return true;
        }
    }
    return false;
}

export template <size_t count = 1, typename... Args>
hook::pattern find_pattern(Args... args)
{
    hook::pattern pattern;
    ((pattern = hook::pattern(args), !pattern.count_hint(count).empty()) || ...);
    return pattern;
}

std::string format(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vector<char> v(1024);
    while (true)
    {
        va_list args2;
        va_copy(args2, args);
        int res = vsnprintf(v.data(), v.size(), fmt, args2);
        if ((res >= 0) && (res < static_cast<int>(v.size())))
        {
            va_end(args);
            va_end(args2);
            return std::string(v.data());
        }
        size_t size;
        if (res < 0)
            size = v.size() * 2;
        else
            size = static_cast<size_t>(res) + 1;
        v.clear();
        v.resize(size);
        va_end(args2);
    }
}

export template<typename T>
std::array<uint8_t, sizeof(T)> to_bytes(const T& object)
{
    std::array<uint8_t, sizeof(T)> bytes;
    const uint8_t* begin = reinterpret_cast<const uint8_t*>(std::addressof(object));
    const uint8_t* end = begin + sizeof(T);
    std::copy(begin, end, std::begin(bytes));
    return bytes;
}

export template<typename T>
T& from_bytes(const std::array<uint8_t, sizeof(T)>& bytes, T& object)
{
    static_assert(std::is_trivially_copyable<T>::value, "not a TriviallyCopyable type");
    uint8_t* begin_object = reinterpret_cast<uint8_t*>(std::addressof(object));
    std::copy(std::begin(bytes), std::end(bytes), begin_object);
    return object;
}

export template<class T, class T1>
T from_bytes(const T1& bytes)
{
    static_assert(std::is_trivially_copyable<T>::value, "not a TriviallyCopyable type");
    T object;
    uint8_t* begin_object = reinterpret_cast<uint8_t*>(std::addressof(object));
    std::copy(std::begin(bytes), std::end(bytes) - (sizeof(T1) - sizeof(T)), begin_object);
    return object;
}

export template <size_t n>
std::string pattern_str(const std::array<uint8_t, n> bytes)
{
    std::string result;
    for (size_t i = 0; i < n; i++)
    {
        result += format("%02X ", bytes[i]);
    }
    return result;
}

export template <typename T>
std::string pattern_str(T t)
{
    return std::string((std::is_same<T, char>::value ? format("%c ", t) : format("%02X ", t)));
}

export template <typename T, typename... Rest>
std::string pattern_str(T t, Rest... rest)
{
    return std::string((std::is_same<T, char>::value ? format("%c ", t) : format("%02X ", t)) + pattern_str(rest...));
}

export std::string pattern_str(std::string_view str)
{
    std::stringstream str_stream;
    for (const auto& item : str)
    {
        str_stream << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << +uint8_t(item) << " ";
    }
    return str_stream.str();
}

export class IATHook
{
public:
    template <class... Ts>
    static auto Replace(HMODULE target_module, std::string_view dll_name, Ts&& ... inputs)
    {
        std::map<std::string, std::future<void*>> originalPtrs;

        const DWORD_PTR instance = reinterpret_cast<DWORD_PTR>(target_module);
        const PIMAGE_NT_HEADERS ntHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(instance + reinterpret_cast<PIMAGE_DOS_HEADER>(instance)->e_lfanew);
        PIMAGE_IMPORT_DESCRIPTOR pImports = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(instance + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        DWORD dwProtect[2];

        // Regular imports
        for (; pImports->Name != 0; pImports++)
        {
            if (_stricmp(reinterpret_cast<const char*>(instance + pImports->Name), dll_name.data()) == 0)
            {
                if (pImports->OriginalFirstThunk != 0)
                {
                    const PIMAGE_THUNK_DATA pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(instance + pImports->OriginalFirstThunk);

                    for (ptrdiff_t j = 0; pThunk[j].u1.AddressOfData != 0; j++)
                    {
                        auto pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
                        if (!pAddress) continue;
                        VirtualProtect(pAddress, sizeof(void*), PAGE_EXECUTE_READWRITE, &dwProtect[0]);
                        ([&]
                        {
                            auto name = std::string_view(std::get<0>(inputs));
                            auto num = std::string("-1");
                            if (name.contains("@"))
                            {
                                num = name.substr(name.find_last_of("@") + 1);
                                name = name.substr(0, name.find_last_of("@"));
                            }

                            if (pThunk[j].u1.Ordinal & IMAGE_ORDINAL_FLAG)
                            {
                                try
                                {
                                    if (IMAGE_ORDINAL(pThunk[j].u1.Ordinal) == std::stoi(num.data()))
                                    {
                                        originalPtrs[std::get<0>(inputs)] = std::async(std::launch::deferred, [&]() -> void* { return *pAddress; });
                                        originalPtrs[std::get<0>(inputs)].wait();
                                        *pAddress = std::get<1>(inputs);
                                    }
                                } catch (...) {}
                            }
                            else if ((*pAddress && *pAddress == (void*)GetProcAddress(GetModuleHandleA(dll_name.data()), name.data())) ||
                            (strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pThunk[j].u1.AddressOfData)->Name, name.data()) == 0))
                            {
                                originalPtrs[std::get<0>(inputs)] = std::async(std::launch::deferred, [&]() -> void* { return *pAddress; });
                                originalPtrs[std::get<0>(inputs)].wait();
                                *pAddress = std::get<1>(inputs);
                            }
                        } (), ...);
                        VirtualProtect(pAddress, sizeof(void*), dwProtect[0], &dwProtect[1]);
                    }
                }
                else
                {
                    auto pFunctions = reinterpret_cast<void**>(instance + pImports->FirstThunk);

                    for (ptrdiff_t j = 0; pFunctions[j] != nullptr; j++)
                    {
                        auto pAddress = &pFunctions[j];
                        VirtualProtect(pAddress, sizeof(void*), PAGE_EXECUTE_READWRITE, &dwProtect[0]);
                        ([&]
                        {
                            if (*pAddress && *pAddress == (void*)GetProcAddress(GetModuleHandleA(dll_name.data()), std::get<0>(inputs)))
                            {
                                originalPtrs[std::get<0>(inputs)] = std::async(std::launch::deferred, [&]() -> void* { return *pAddress; });
                                originalPtrs[std::get<0>(inputs)].wait();
                                *pAddress = std::get<1>(inputs);
                            }
                        } (), ...);
                        VirtualProtect(pAddress, sizeof(void*), dwProtect[0], &dwProtect[1]);
                    }
                }
            }
        }

        // Delay imports
        PIMAGE_DELAYLOAD_DESCRIPTOR pDelayed = reinterpret_cast<PIMAGE_DELAYLOAD_DESCRIPTOR>(instance + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress);
        if (pDelayed && ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress != 0 && ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size != 0)
        {
            for (; pDelayed->DllNameRVA != 0; pDelayed++)
            {
                if (_stricmp(reinterpret_cast<const char*>(instance + pDelayed->DllNameRVA), dll_name.data()) == 0)
                {
                    if (pDelayed->ImportAddressTableRVA != 0)
                    {
                        const PIMAGE_THUNK_DATA pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(instance + pDelayed->ImportNameTableRVA);
                        const PIMAGE_THUNK_DATA pFThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(instance + pDelayed->ImportAddressTableRVA);

                        for (ptrdiff_t j = 0; pThunk[j].u1.AddressOfData != 0; j++)
                        {
                            auto pAddress = reinterpret_cast<void**>(&pFThunk[j].u1.Function);
                            if (!pAddress) continue;
                            VirtualProtect(pAddress, sizeof(void*), PAGE_EXECUTE_READWRITE, &dwProtect[0]);
                            ([&]
                            {
                                auto name = std::string_view(std::get<0>(inputs));
                                auto num = std::string("-1");
                                if (name.contains("@"))
                                {
                                    num = name.substr(name.find_last_of("@") + 1);
                                    name = name.substr(0, name.find_last_of("@"));
                                }

                                if (pThunk[j].u1.Ordinal & IMAGE_ORDINAL_FLAG)
                                {
                                    try
                                    {
                                        if (IMAGE_ORDINAL(pThunk[j].u1.Ordinal) == std::stoi(num.data()))
                                        {
                                            originalPtrs[std::get<0>(inputs)] = std::async(std::launch::async,
                                            [](void** pAddress, void* value, PVOID instance) -> void*
                                            {
                                                DWORD dwProtect[2];
                                                VirtualProtect(pAddress, sizeof(void*), PAGE_EXECUTE_READWRITE, &dwProtect[0]);
                                                MEMORY_BASIC_INFORMATION mbi;
                                                mbi.AllocationBase = instance;
                                                do
                                                {
                                                    VirtualQuery(*pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
                                                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                                } while (mbi.AllocationBase == instance);
                                                auto r = *pAddress;
                                                *pAddress = value;
                                                VirtualProtect(pAddress, sizeof(void*), dwProtect[0], &dwProtect[1]);
                                                return r;
                                            }, pAddress, std::get<1>(inputs), (PVOID)instance);
                                        }
                                    } catch (...) {}
                                }
                                else if (strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pThunk[j].u1.AddressOfData)->Name, name.data()) == 0)
                                {
                                    originalPtrs[std::get<0>(inputs)] = std::async(std::launch::async,
                                    [](void** pAddress, void* value, PVOID instance) -> void*
                                    {
                                        DWORD dwProtect[2];
                                        VirtualProtect(pAddress, sizeof(void*), PAGE_EXECUTE_READWRITE, &dwProtect[0]);
                                        MEMORY_BASIC_INFORMATION mbi;
                                        mbi.AllocationBase = instance;
                                        do
                                        {
                                            VirtualQuery(*pAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
                                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                        } while (mbi.AllocationBase == instance);
                                        auto r = *pAddress;
                                        *pAddress = value;
                                        VirtualProtect(pAddress, sizeof(void*), dwProtect[0], &dwProtect[1]);
                                        return r;
                                    }, pAddress, std::get<1>(inputs), (PVOID)instance);
                                }
                            } (), ...);
                            VirtualProtect(pAddress, sizeof(void*), dwProtect[0], &dwProtect[1]);
                        }
                    }
                }
            }
        }

        // Fallback section scan (e.g. re5dx9.exe steam)
        if (originalPtrs.empty())
        {
            static auto getSection = [](const PIMAGE_NT_HEADERS nt_headers, unsigned section) -> PIMAGE_SECTION_HEADER
            {
                return reinterpret_cast<PIMAGE_SECTION_HEADER>(
                    (UCHAR*)nt_headers->OptionalHeader.DataDirectory +
                    nt_headers->OptionalHeader.NumberOfRvaAndSizes * sizeof(IMAGE_DATA_DIRECTORY) +
                    section * sizeof(IMAGE_SECTION_HEADER));
            };

            for (auto i = 0; i < ntHeader->FileHeader.NumberOfSections; i++)
            {
                auto sec = getSection(ntHeader, i);
                auto pFunctions = reinterpret_cast<void**>(instance + std::max(sec->PointerToRawData, sec->VirtualAddress));

                for (ptrdiff_t j = 0; j < 300; j++)
                {
                    auto pAddress = &pFunctions[j];
                    VirtualProtect(pAddress, sizeof(void*), PAGE_EXECUTE_READWRITE, &dwProtect[0]);
                    ([&]
                    {
                        auto name = std::string_view(std::get<0>(inputs));
                        auto num = std::string("-1");
                        if (name.contains("@"))
                        {
                            num = name.substr(name.find_last_of("@") + 1);
                            name = name.substr(0, name.find_last_of("@"));
                        }

                        if (*pAddress && *pAddress == (void*)GetProcAddress(GetModuleHandleA(dll_name.data()), name.data()))
                        {
                            originalPtrs[std::get<0>(inputs)] = std::async(std::launch::deferred, [&]() -> void* { return *pAddress; });
                            originalPtrs[std::get<0>(inputs)].wait();
                            *pAddress = std::get<1>(inputs);
                        }
                    } (), ...);
                    VirtualProtect(pAddress, sizeof(void*), dwProtect[0], &dwProtect[1]);
                }

                if (!originalPtrs.empty())
                    return originalPtrs;
            }
        }

        return originalPtrs;
    }
};

export class raw_mem
{
public:
    raw_mem(injector::memory_pointer_tr addr, std::initializer_list<uint8_t> bytes, bool offset_back = false)
    {
        ptr = addr.as_int() - (offset_back ? bytes.size() : 0);
        new_code.assign(std::move(bytes));
        old_code.resize(new_code.size());
        ReadMemoryRaw(ptr, old_code.data(), old_code.size(), true);
    }

    void Write()
    {
        WriteMemoryRaw(ptr, new_code.data(), new_code.size(), true);
    }

    void Restore()
    {
        WriteMemoryRaw(ptr, old_code.data(), old_code.size(), true);
    }

    size_t Size()
    {
        return old_code.size();
    }

private:
    injector::memory_pointer ptr;
    std::vector<uint8_t> old_code;
    std::vector<uint8_t> new_code;
};

export std::optional<uintptr_t> resolve_displacement(auto ip)
{
    ZydisDecoder decoder;
    #if defined(_M_X64) || defined(__x86_64__)
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    #else
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
    #endif

    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

    ZyanStatus status = ZydisDecoderDecodeFull(
        &decoder,
        (void*)ip,
        ZYDIS_MAX_INSTRUCTION_LENGTH,
        &instruction,
        operands
    );

    if (!ZYAN_SUCCESS(status))
    {
        return std::nullopt;
    }

    for (uint32_t i = 0; i < instruction.operand_count_visible; ++i)
    {
        const auto& operand = operands[i];

        if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY)
        {
            if (operand.mem.disp.has_displacement)
            {
                #if defined(_M_X64) || defined(__x86_64__)
                ZyanU64 absolute_address = 0;
                if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &operand, static_cast<ZyanU64>((uintptr_t)ip), &absolute_address)))
                {
                    return static_cast<uintptr_t>(absolute_address);
                }
                #else
                return static_cast<uintptr_t>(operand.mem.disp.value);
                #endif
            }
        }
        else if (operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
        {
            if (operand.imm.is_relative)
            {
                return (uintptr_t)ip + instruction.length + ZyanISize(operand.imm.value.s);
            }
        }
    }

    if (instruction.attributes & ZYDIS_ATTRIB_IS_RELATIVE && instruction.raw.disp.size > 0)
    {
        return (uintptr_t)ip + instruction.length + ZyanISize(instruction.raw.disp.value);
    }

    return std::nullopt;
}

export std::optional<uintptr_t> resolve_next_displacement(auto ip)
{
    ZydisDecoder decoder;
    #if defined(_M_X64) || defined(__x86_64__)
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    #else
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
    #endif

    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

    uintptr_t current_ip = (uintptr_t)ip;
    size_t instruction_count = 0;

    while (true)
    {
        ZyanStatus status = ZydisDecoderDecodeFull(
            &decoder,
            (void*)current_ip,
            ZYDIS_MAX_INSTRUCTION_LENGTH,
            &instruction,
            operands
        );

        if (!ZYAN_SUCCESS(status))
        {
            return std::nullopt;
        }

        if (instruction.meta.category == ZYDIS_CATEGORY_COND_BR)
        {
            for (uint32_t i = 0; i < instruction.operand_count_visible; ++i)
            {
                const auto& operand = operands[i];

                if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY)
                {
                    if (operand.mem.disp.has_displacement)
                    {
                        #if defined(_M_X64) || defined(__x86_64__)
                        ZyanU64 absolute_address = 0;
                        if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &operand, static_cast<ZyanU64>(current_ip), &absolute_address)))
                        {
                            return static_cast<uintptr_t>(absolute_address);
                        }
                        #else
                        return static_cast<uintptr_t>(operand.mem.disp.value);
                        #endif
                    }
                }
                else if (operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE)
                {
                    if (operand.imm.is_relative)
                    {
                        return current_ip + instruction.length + ZyanISize(operand.imm.value.s);
                    }
                }
            }

            if (instruction.attributes & ZYDIS_ATTRIB_IS_RELATIVE && instruction.raw.disp.size > 0)
            {
                return current_ip + instruction.length + ZyanISize(instruction.raw.disp.value);
            }

            return std::nullopt;
        }

        current_ip += instruction.length;

        constexpr size_t MAX_INSTRUCTIONS = 20;
        if (++instruction_count >= MAX_INSTRUCTIONS)
        {
            return std::nullopt;
        }
    }

    return std::nullopt;
}
