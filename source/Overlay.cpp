#include <common.hxx>
#include <safetyhook.hpp>
#include <Xinput.h>
#include <overlay/OverlayD3D9.hpp>
#include "Overlay.hpp"

import FixSettings;

namespace
{
    asi_overlay::D3D9Overlay overlay;
    HMODULE host = nullptr;
    std::mutex hooksMutex;
    // Device methods are shared by every device of the runtime: hook each implementation once
    SafetyHookInline shCreate, shCreateDevice, shPresent, shReset, shSwapChain, shSwapPresent;
    std::vector<std::function<void(bool)>> listeners;
    bool blocking = false;
    // The runtime's device Present can call the swap chain's: draw once per frame
    thread_local bool presenting = false;

    using GetPad = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    GetPad getPad = nullptr;
    DWORD activePad = 0;

    void* Method(void* object, size_t slot)
    {
        return (*static_cast<void***>(object))[slot];
    }

    void PollPad()
    {
        if (!getPad)
        {
            for (auto name : { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" })
            {
                if (auto library = LoadLibraryW(name))
                {
                    getPad = reinterpret_cast<GetPad>(GetProcAddress(library, "XInputGetState"));
                    if (getPad)
                        break;
                }
            }
            if (!getPad)
                return;
        }
        XINPUT_STATE state{};
        if (getPad(activePad, &state) != ERROR_SUCCESS)
        {
            state = {};
            for (DWORD i = 0; i < 4; ++i)
            {
                if (getPad(i, &state) == ERROR_SUCCESS)
                {
                    activePad = i;
                    break;
                }
            }
        }
        const auto& pad = state.Gamepad;
        const WORD from[] = { XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_RIGHT,
            XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
            XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_X };
        const uint16_t to[] = { 1, 8, 0x10, 0x20, 0x40, 0x80, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000 };
        uint16_t mapped = 0;
        for (size_t i = 0; i < std::size(from); ++i)
        {
            if (pad.wButtons & from[i])
                mapped |= to[i];
        }
        overlay.Gamepad(mapped, int8_t(pad.sThumbLX / 256), int8_t(-int(pad.sThumbLY) / 258));
    }

    void Frame(IDirect3DDevice9* device)
    {
        const int hotkey = FixSettings::Plugin().Int("MAIN.OverlayHotkey");
        overlay.hotkey = hotkey ? hotkey : VK_F10;
        PollPad();
        overlay.Present(device, host);
        const bool now = overlay.BlocksInput();
        if (now != blocking)
        {
            blocking = now;
            for (const auto& listener : listeners)
                listener(now);
        }
    }

    HRESULT WINAPI Present(IDirect3DDevice9* device, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty)
    {
        Frame(device);
        presenting = true;
        const auto hr = shPresent.unsafe_stdcall<HRESULT>(device, source, destination, window, dirty);
        presenting = false;
        return hr;
    }

    HRESULT WINAPI SwapPresent(IDirect3DSwapChain9* swapchain, const RECT* source, const RECT* destination, HWND window,
        const RGNDATA* dirty, DWORD flags)
    {
        IDirect3DDevice9* device = nullptr;
        if (!presenting && SUCCEEDED(swapchain->GetDevice(&device)) && device)
        {
            Frame(device);
            device->Release();
        }
        return shSwapPresent.unsafe_stdcall<HRESULT>(swapchain, source, destination, window, dirty, flags);
    }

    void HookSwapChain(IDirect3DSwapChain9* swapchain)
    {
        if (swapchain && !shSwapPresent)
            shSwapPresent = safetyhook::create_inline(Method(swapchain, 3), SwapPresent);
    }

    HRESULT WINAPI CreateSwapChain(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters, IDirect3DSwapChain9** result)
    {
        const auto hr = shSwapChain.unsafe_stdcall<HRESULT>(device, parameters, result);
        if (SUCCEEDED(hr) && result)
        {
            const std::lock_guard lock(hooksMutex);
            HookSwapChain(*result);
        }
        return hr;
    }

    HRESULT WINAPI Reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters)
    {
        overlay.BeforeReset();
        return shReset.unsafe_stdcall<HRESULT>(device, parameters);
    }

    HRESULT WINAPI CreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
        D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** result)
    {
        const auto hr = shCreateDevice.unsafe_stdcall<HRESULT>(d3d, adapter, type, window, flags, parameters, result);
        if (SUCCEEDED(hr) && result && *result)
        {
            const std::lock_guard lock(hooksMutex);
            if (!shPresent)
                shPresent = safetyhook::create_inline(Method(*result, 17), Present);
            if (!shReset)
                shReset = safetyhook::create_inline(Method(*result, 16), Reset);
            // The wrapper may present through a swap chain instead of the device
            if (!shSwapChain)
                shSwapChain = safetyhook::create_inline(Method(*result, 13), CreateSwapChain);
            IDirect3DSwapChain9* swapchain = nullptr;
            if (SUCCEEDED((*result)->GetSwapChain(0, &swapchain)) && swapchain)
            {
                HookSwapChain(swapchain);
                swapchain->Release();
            }
        }
        return hr;
    }

    IDirect3D9* WINAPI Create(UINT version)
    {
        auto d3d = shCreate.unsafe_stdcall<IDirect3D9*>(version);
        if (d3d)
        {
            const std::lock_guard lock(hooksMutex);
            if (!shCreateDevice)
                shCreateDevice = safetyhook::create_inline(Method(d3d, 16), CreateDevice);
        }
        return d3d;
    }
}

namespace FixOverlay
{
    void Init()
    {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Init), &host);
        // The DirectDraw wrapper of the Steam and GOG releases draws with the Direct3D 9 runtime
        if (auto d3d9 = LoadLibraryW(L"d3d9.dll"))
        {
            if (auto create = GetProcAddress(d3d9, "Direct3DCreate9"))
                shCreate = safetyhook::create_inline(create, Create);
        }
    }

    bool Blocks()
    {
        return overlay.BlocksInput();
    }

    bool Message(HWND window, UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result)
    {
        if (message == WM_NCDESTROY)
            overlay.SetOpen(false);
        return overlay.Message(window, message, wparam, lparam, result);
    }

    void OnBlocking(std::function<void(bool)> callback)
    {
        listeners.push_back(std::move(callback));
    }
}
