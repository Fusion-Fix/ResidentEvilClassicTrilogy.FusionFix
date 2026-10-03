module;
#include <common.hxx>
#include "ClassicDisplay.hxx"
#include <safetyhook.hpp>
#include <d3d.h>
#include <mmsystem.h>
#include <digitalv.h>
#include <chrono>
#include "ClassicPresentation.hxx"

export module RE1Widescreen;
import common;
import ClassicGame;
import ClassicInput;
import ClassicMemory;
import ClassicMenu;

namespace RE1Presentation
{
    using ClassicMemory::Read;
    using ClassicMemory::Write;
    SafetyHookMid shMode, shMovieInput, shMovieBlit;
    SafetyHookInline shRender, shMatrix, shSprite, shBackground, shMovie, shMovieFrame, shMoviePlay;
    HWND* mainWindow = nullptr;
    HWND* movieWindow = nullptr;
    MCIDEVICEID* movieDevice = nullptr;
    decltype(&mciSendCommandA) sendMovieCommand = nullptr;
    MCIDEVICEID pausedMovie = 0;
    int* movieFinished = nullptr;
    int pausedFinished = 0;
    bool consumeMovieInput = false;
    int movieEnd = 0;
    int(__cdecl* present)() = nullptr;
    int outputWidth = 0, outputHeight = 0;
    uint8_t* player = nullptr;
    void* view = nullptr;
    uint8_t* fontTextures = nullptr;
    void** tasks = nullptr;
    void* titleTask = nullptr;
    void* inventoryTask = nullptr;
    uint8_t* camera = nullptr;
    bool roomDrawn = false;
    // Japanese MarniSystem uses a different class and packet layout.
    struct Layout
    {
        int type = 780, width = 16, height = 20, depth = 24, lens = 68;
        int selected = 788, modeCount = 732, modes = 220;
        int draw = 7556, surface = 8376, device = 7892;
        int worldHandle = 5460, spriteHandle = 5480, worldMatrix = 7904, spriteMatrix = 8224;
        int fontStride = 892;
        bool japanese = false;
    } layout;
    Presentation::Pan pan;
    auto lastFrame = std::chrono::steady_clock::now();

    struct Context
    {
        void* renderer = nullptr;
        bool crop = false;
        bool full = false;
        float pan = 30.0f;
    };
    thread_local Context context;
    BOOL __cdecl Movie();

    void* __cdecl Background()
    {
        roomDrawn = true;
        return shBackground.unsafe_ccall<void*>();
    }

    LRESULT __cdecl MovieFrame()
    {
        if (ClassicMenu::Update()) { present(); return 0; }
        return shMovieFrame.unsafe_ccall<LRESULT>();
    }

    void MovieInput(SafetyHookContext& registers)
    {
        if (!consumeMovieInput) return;
        // Native movie skipping polls the device directly, independently of
        // the menu's window messages. Require release before accepting a new
        // skip press, including a held gamepad confirm/Start button.
        if (uint16_t(registers.eax) == 0) consumeMovieInput = false;
        registers.eax &= 0xFFFF0000u;
    }

    void __cdecl PlayMovie(int from, int to)
    {
        movieEnd = to;
        shMoviePlay.unsafe_ccall<void>(from, to);
        Movie();
    }

    void PauseMovie(bool active)
    {
        if (!sendMovieCommand) return;
        MCI_GENERIC_PARMS parameters{};
        if (active && *movieWindow && IsWindow(*movieWindow)
            && !sendMovieCommand(*movieDevice, MCI_PAUSE, MCI_WAIT, reinterpret_cast<DWORD_PTR>(&parameters)))
        {
            pausedMovie = *movieDevice;
            pausedFinished = *movieFinished;
            ShowWindow(*movieWindow, SW_HIDE);
        }
        else if (!active && pausedMovie)
        {
            // Reissue native Play from the current position, retaining its end
            // frame and completion notification. Resume alone can leave the
            // game waiting for an aborted MCI_PLAY notification.
            shMoviePlay.unsafe_ccall<void>(0, movieEnd);
            Movie();
            pausedMovie = 0;
            if (*movieWindow && IsWindow(*movieWindow)) ShowWindow(*movieWindow, SW_SHOWNOACTIVATE);
            // The native WM_ACTIVATE handler treats deactivation as movie
            // completion. Hiding/showing the movie for our menu must not turn
            // that synthetic notification into a skip on the next frame.
            *movieFinished = pausedFinished;
            consumeMovieInput = true;
        }
    }

    BOOL __cdecl Movie()
    {
        const auto result = shMovie.unsafe_ccall<BOOL>();
        if (!*movieWindow || !IsWindow(*movieWindow) || !sendMovieCommand) return result;
        MCI_DGV_RECT_PARMS source{};
        if (sendMovieCommand(*movieDevice, MCI_WHERE, MCI_DGV_WHERE_SOURCE, reinterpret_cast<DWORD_PTR>(&source))) return result;
        RECT client{};
        // MCI rectangles store width/height in right/bottom, unlike Win32 RECT.
        if (!GetClientRect(*mainWindow, &client) || client.right <= 0 || client.bottom <= 0
            || source.rc.right <= 0 || source.rc.bottom <= 0) return result;
        const auto viewport = Presentation::Viewport::Scene(float(client.right), float(client.bottom), ClassicGame::GetSettings().maxAspectRatio);
        const float width = float(source.rc.right), height = float(source.rc.bottom);
        const float scale = ClassicGame::Enabled(ClassicGame::Option::PanAndScan)
            ? std::max(viewport.width / width, viewport.height / height)
            : std::min(viewport.width / width, viewport.height / height);
        const int x = int(std::lround(viewport.x + (viewport.width - width * scale) * 0.5f));
        const int y = int(std::lround(viewport.y + (viewport.height - height * scale) * 0.5f));
        const int scaledWidth = int(std::lround(width * scale));
        const int scaledHeight = int(std::lround(height * scale));
        const auto moved = MoveWindow(*movieWindow, x, y, scaledWidth, scaledHeight, TRUE);
        MCI_DGV_PUT_PARMS destination{};
        destination.rc = { 0, 0, scaledWidth, scaledHeight };
        sendMovieCommand(*movieDevice, MCI_PUT, MCI_DGV_PUT_DESTINATION | MCI_DGV_RECT,
            reinterpret_cast<DWORD_PTR>(&destination));
        // Clip the enlarged image to the configured viewport, including the
        // maximum aspect-ratio bars on wider monitors.
        if (const auto region = CreateRectRgn(std::max(0, int(std::lround(viewport.x)) - x),
            std::max(0, int(std::lround(viewport.y)) - y),
            std::min(scaledWidth, int(std::lround(viewport.x + viewport.width)) - x),
            std::min(scaledHeight, int(std::lround(viewport.y + viewport.height)) - y)))
            if (!SetWindowRgn(*movieWindow, region, TRUE)) DeleteObject(region);
        return moved;
    }

    void MovieBlit(SafetyHookContext& registers)
    {
        // GOG redirects the movie's StretchBlt to a DirectDraw surface and
        // replaces its destination with the entire surface. MCI window sizing
        // cannot control this path; correct the final surface copy instead.
        if (!movieWindow || !movieDevice || !*movieDevice || !IsWindow(*movieWindow)) return;
        auto* args = reinterpret_cast<uint32_t*>(registers.esp);
        const int width = int(args[3]), height = int(args[4]);
        const int sourceWidth = int(args[8]), sourceHeight = int(args[9]);
        if (width <= 0 || height <= 0 || sourceWidth <= 0 || sourceHeight <= 0) return;
        const auto viewport = Presentation::Viewport::Scene(float(width), float(height), ClassicGame::GetSettings().maxAspectRatio);
        const float scale = ClassicGame::Enabled(ClassicGame::Option::PanAndScan)
            ? std::max(viewport.width / sourceWidth, viewport.height / sourceHeight)
            : std::min(viewport.width / sourceWidth, viewport.height / sourceHeight);
        const float x = viewport.x + (viewport.width - sourceWidth * scale) * 0.5f;
        const float y = viewport.y + (viewport.height - sourceHeight * scale) * 0.5f;
        const float left = std::max(x, viewport.x), top = std::max(y, viewport.y);
        const float right = std::min(x + sourceWidth * scale, viewport.x + viewport.width);
        const float bottom = std::min(y + sourceHeight * scale, viewport.y + viewport.height);
        const int sourceLeft = int(std::lround((left - x) / scale));
        const int sourceTop = int(std::lround((top - y) / scale));
        const int sourceRight = int(std::lround((right - x) / scale));
        const int sourceBottom = int(std::lround((bottom - y) / scale));
        if (sourceRight <= sourceLeft || sourceBottom <= sourceTop) return;
        PatBlt(reinterpret_cast<HDC>(args[0]), 0, 0, width, height, BLACKNESS);
        args[1] = uint32_t(std::lround(left));
        args[2] = uint32_t(std::lround(top));
        args[3] = uint32_t(std::lround(right)) - args[1];
        args[4] = uint32_t(std::lround(bottom)) - args[2];
        args[6] += sourceLeft;
        args[7] += sourceTop;
        args[8] = sourceRight - sourceLeft;
        args[9] = sourceBottom - sourceTop;
    }

    void Mode(SafetyHookContext& registers)
    {
        auto* renderer = reinterpret_cast<void*>(registers.esi);
        if (Read<int>(renderer, layout.type) == 5) return;
        Write(renderer, layout.width, outputWidth);
        Write(renderer, layout.height, outputHeight);
        Write(renderer, layout.depth, 16);
        const auto selected = Read<int>(renderer, layout.selected);
        if (selected >= 0 && selected < Read<int>(renderer, layout.modeCount))
        {
            auto* mode = static_cast<uint8_t*>(renderer) + layout.modes + 16 * selected;
            Write(mode, 0, outputWidth);
            Write(mode, 4, outputHeight);
            Write(mode, 8, 16);
        }
        if (layout.japanese)
        {
            Write(renderer, 5712, float(outputWidth) / Read<int>(renderer, 5668));
            Write(renderer, 5716, float(outputHeight) / Read<int>(renderer, 5672));
        }
    }

    float PlayerY(void* renderer)
    {
        // Native projection reverses world Y before rotation and camera Y after
        // rotation. The translation already uses the resulting screen basis.
        const double point[] = { double(Read<int32_t>(player, 52)), 800.0 - double(Read<int32_t>(player, 56)),
            double(Read<int32_t>(player, 60)) };
        double projected[3]{};
        for (size_t row = 0; row < 3; ++row)
        {
            projected[row] = Read<int32_t>(view, 20 + row * 4);
            for (size_t column = 0; column < 3; ++column)
                projected[row] += Read<int16_t>(view, (row * 3 + column) * 2) * point[column] / 4096.0 * (row == 1 ? -1.0 : 1.0);
        }
        return projected[2] > 1.0 ? float(120.0 + projected[1] * Read<int>(renderer, layout.lens) / projected[2]) : 120.0f;
    }

    HRESULT WINAPI Matrix(IDirect3DDevice* device, D3DMATRIXHANDLE handle, D3DMATRIX* matrix)
    {
        if (!context.renderer || !matrix || (handle != Read<D3DMATRIXHANDLE>(context.renderer, layout.worldHandle)
            && handle != Read<D3DMATRIXHANDLE>(context.renderer, layout.spriteHandle)))
            return shMatrix.unsafe_stdcall<HRESULT>(device, handle, matrix);
        const float width = float(Read<int>(context.renderer, layout.width)), height = float(Read<int>(context.renderer, layout.height));
        const auto viewport = Presentation::Viewport::Scene(width, height, ClassicGame::GetSettings().maxAspectRatio);
        const auto transform = context.full ? Presentation::Transform{ viewport.width / width, viewport.height / height, viewport.x, viewport.y }
            : context.crop ? Presentation::Transform::Crop(width, height, viewport, context.pan)
            : Presentation::Transform::Fit(width, height, viewport);
        const float x = (transform.scaleX - 1.0f) * width * 0.5f + transform.offsetX;
        const float y = -((transform.scaleY - 1.0f) * height * 0.5f + transform.offsetY);
        float copy[16];
        memcpy(copy, matrix, sizeof(copy));
        for (size_t row = 0; row < 4; ++row)
        {
            copy[row * 4] = copy[row * 4] * transform.scaleX + copy[row * 4 + 3] * x;
            copy[row * 4 + 1] = copy[row * 4 + 1] * transform.scaleY + copy[row * 4 + 3] * y;
        }
        return shMatrix.unsafe_stdcall<HRESULT>(device, handle, reinterpret_cast<D3DMATRIX*>(copy));
    }

    int __fastcall Sprite(void* renderer, void*, void* packet)
    {
        const auto texture = Read<uint32_t>(packet, 48);
        const int left = Read<int16_t>(packet, 8), top = Read<int16_t>(packet, 10);
        const int right = Read<int16_t>(packet, 12), bottom = Read<int16_t>(packet, 14);
        bool text = false;
        if (texture)
            for (const int group : { 12, 13, 14 })
            {
                if (group == 13 && !layout.japanese) continue;
                for (int palette = 0; palette < 8; ++palette)
                    text |= texture == Read<uint32_t>(fontTextures, layout.fontStride * group + 4 * palette);
            }
        const auto previous = context;
        const bool fade = !texture && left <= 0 && top <= 0 && right >= 319 && bottom >= 239;
        const bool change = fade || (text && context.crop);
        auto* device = Read<IDirect3DDevice*>(renderer, layout.device);
        const auto projection = Read<D3DMATRIXHANDLE>(renderer, layout.spriteHandle);
        auto* matrix = reinterpret_cast<D3DMATRIX*>(static_cast<uint8_t*>(renderer) + layout.spriteMatrix);
        if (change && device)
        {
            context.crop = fade ? context.crop : false;
            context.full = fade;
            device->SetMatrix(projection, matrix);
        }
        const auto result = shSprite.unsafe_thiscall<int>(renderer, packet);
        if (change && device)
        {
            context = previous;
            device->SetMatrix(projection, matrix);
        }
        return result;
    }

    bool Cinematic(void* queue)
    {
        if (!queue) return false;
        const auto count = Read<uint32_t>(queue, 12);
        if (count > 4096) return false;
        auto* buckets = Read<uint8_t*>(queue, 16);
        bool top = false, bottom = false;
        for (uint32_t bucket = 0; buckets && bucket < count; ++bucket)
        {
            auto* list = buckets + 12 * bucket;
            const auto packets = std::clamp<int>(Read<int16_t>(list, layout.japanese ? 4 : 0), 0, 8192);
            auto* packet = Read<uint8_t*>(list, layout.japanese ? 0 : 4);
            for (int i = 0; packet && i < packets; ++i, packet = Read<uint8_t*>(packet, layout.japanese ? 0 : 4))
            {
                if ((layout.japanese ? Read<uint8_t>(packet, 4) != 0xA0 : Read<uint32_t>(packet) != 10) || Read<uint32_t>(packet, 48)
                    || Read<int16_t>(packet, 8) > 0 || Read<int16_t>(packet, 12) < 319) continue;
                if (Read<float>(packet, 32) > 1.0f / 256.0f || Read<float>(packet, 36) > 1.0f / 256.0f
                    || Read<float>(packet, 40) > 1.0f / 256.0f) continue;
                const int y0 = Read<int16_t>(packet, 10), y1 = Read<int16_t>(packet, 14);
                top |= y0 <= 0 && y1 > 0 && y1 < 60;
                bottom |= y0 > 180 && y0 < 239 && y1 >= 239;
            }
        }
        return top && bottom;
    }

    int __fastcall Render(void* renderer, void*, void* queue)
    {
        auto* draw = Read<IDirectDraw*>(renderer, layout.draw);
        auto* surface = Read<IDirectDrawSurface*>(renderer, layout.surface);
        if (ClassicMenu::opened && ClassicMenu::Draw(draw, surface)) return 1;
        const auto previous = context;
        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        // Follow the active native task: the inventory request flag is cleared
        // when its UI opens and cannot identify the displayed screen.
        context = { renderer, ClassicGame::Enabled(ClassicGame::Option::PanAndScan)
            && roomDrawn && tasks[0] != titleTask && tasks[1] != inventoryTask };
        const auto viewport = Presentation::Viewport::Scene(float(Read<int>(renderer, layout.width)), float(Read<int>(renderer, layout.height)),
            ClassicGame::GetSettings().maxAspectRatio);
        const auto cameraKey = Read<uint32_t>(camera) & 0xFFFFFF;
        if (ClassicGame::Enabled(ClassicGame::Option::PanAndScan) && Cinematic(queue))
        {
            context.crop = true;
            context.pan = pan.Center(cameraKey, viewport.SourceHeight());
        }
        else if (context.crop)
            context.pan = pan.Update(PlayerY(renderer), cameraKey, elapsed, ClassicInput::pad.right.y, viewport.SourceHeight());
        else pan.camera = UINT32_MAX;
        roomDrawn = false;
        auto* device = Read<IDirect3DDevice*>(renderer, layout.device);
        if (device && !shMatrix)
            shMatrix = safetyhook::create_inline((*reinterpret_cast<void***>(device))[16], Matrix);
        // Projection matrices survive between frames. Room changes and text
        // overlays need the current presentation even without a lens packet.
        if (device && shMatrix)
        {
            device->SetMatrix(Read<D3DMATRIXHANDLE>(renderer, layout.worldHandle),
                reinterpret_cast<D3DMATRIX*>(static_cast<uint8_t*>(renderer) + layout.worldMatrix));
            device->SetMatrix(Read<D3DMATRIXHANDLE>(renderer, layout.spriteHandle),
                reinterpret_cast<D3DMATRIX*>(static_cast<uint8_t*>(renderer) + layout.spriteMatrix));
        }
        if (surface)
        {
            DDBLTFX fill{ .dwSize = sizeof(DDBLTFX) };
            surface->Blt(nullptr, nullptr, nullptr, DDBLT_COLORFILL | DDBLT_WAIT, &fill);
        }
        const auto result = shRender.unsafe_thiscall<int>(renderer, queue);
        ClassicMenu::Draw(draw, surface);
        context = previous;
        return result;
    }

    void Init()
    {
        auto mode = hook::pattern("8B 86 84 1D 00 00 8B 4E 68 50 8B 56 04 C7 46 70 01 00 00 00 51 52 E8");
        auto render = hook::pattern("83 EC 20 53 56 57 55 8B 81 0C 03 00 00 8B F9 83 F8 05 74 37 83 BF D4 1E 00 00 00");
        auto actor = hook::pattern("C7 05 ? ? ? ? ? ? ? ? F6 05 ? ? ? ? 01 74 ? 33 C0 A0 ? ? ? ? FF 14 85");
        auto background = hook::pattern("53 33 C9 56 8B 15 ? ? ? ? 57 55 8D 1C 92 8D 14 5A C1 E2 04 8D BA ? ? ? ? 8B C7 83 38 00");
        auto task = hook::pattern("8B 0D ? ? ? ? 8B 44 24 04 8B 15 ? ? ? ? 89 04 8D ? ? ? ? 66 C7 02 02 00 E9");
        auto title = hook::pattern("83 EC 08 81 25 ? ? ? ? FF FF FE FF 53 56 33 DB 53 89 1D ? ? ? ? 53 89 1D");
        auto inventoryScreen = hook::pattern("81 EC B0 01 00 00 53 56 57 33 DB 89 5C 24 1C 55 89 5C 24 24 89 5C 24 2C");
        auto matrix = hook::pattern("68 ? ? ? ? 03 C2 68 ? ? ? ? C1 F8 02 8D 4C 24 ? 66 89 44 24 ? 51 E8");
        auto sprite = hook::pattern("81 EC F4 00 00 00 53 56 57 55 83 79 3C 00 8B E9 0F 84 ? ? ? ? 8B 8C 24 08 01 00 00 8B 41 30 85 C0 75");
        auto fonts = hook::pattern("8D B9 ? ? ? ? 8B 07 85 C0 74 0F 50 E8 ? ? ? ? 83 C4 04 C7 07 00 00 00 00 8B 4D F0 83 C7 04 46 39 B1 ? ? ? ? 77 DC 8B 4D F0 81 C1 ? ? ? ? E8 ? ? ? ? 6A 01");
        auto room = hook::pattern("80 3D ? ? ? ? 03 75 37 80 3D ? ? ? ? 11 75 2E");
        auto movie = hook::pattern("83 EC 10 53 56 57 33 F6 55 39 35 ? ? ? ? 74 ? 39 35 ? ? ? ? 74 ? 8D 44 24 10 8B 0D");
        auto movieId = hook::pattern("8B 0D ? ? ? ? 50 6A 02 68 04 08 00 00 51 FF 15");
        auto movieFrame = hook::pattern("56 A1 ? ? ? ? 83 3D ? ? ? ? 00 57 0F 84 ? ? ? ? 85 C0 74 15 83 F8 01");
        auto moviePlay = hook::pattern("83 EC 0C 83 3D ? ? ? ? 00 74 6B 83 3D ? ? ? ? 00 74 62 A1 ? ? ? ? BA 01 00 00 01 8B 4C 24 14");
        auto movieInput = hook::pattern("66 8B 0D ? ? ? ? 66 F7 D1 66 23 C8 66 A3 ? ? ? ? A1 ? ? ? ? 66 85 0C C5 ? ? ? ? 74 13 83 3D ? ? ? ? 00 75 0A C7 05 ? ? ? ? 00 00 00 00 83 3D");
        auto flip = hook::pattern("56 FF 05 ? ? ? ? E8 ? ? ? ? FF 15 ? ? ? ? 83 3D ? ? ? ? 00 8B F0 E9");
        if (mode.size() == 0)
        {
            mode = hook::pattern("8B 86 3C 13 00 00 8B 8E 84 16 00 00 50 8B 96 20 16 00 00 C7 86 8C 16 00 00 01 00 00 00 51 52 E8");
            render = hook::pattern("83 EC 18 53 56 57 55 8B 01 8B E9 83 F8 05 74 37 83 BD 8C 14 00 00 00");
            background = hook::pattern("53 A1 ? ? ? ? 56 57 55 33 F6 8D 14 80 8D 04 50 8D 14 C5 ? ? ? ? 8D 3C C5 00 00 00 00 8B C2 83 38 00");
            sprite = hook::pattern("81 EC F4 00 00 00 53 56 57 55 83 B9 58 16 00 00 00 8B E9 0F 84 ? ? ? ? 8B 8C 24 08 01 00 00 8B 41 30");
            fonts = hook::pattern("8D 9E ? ? ? ? 8B 03 85 C0 74 0F 50 E8 ? ? ? ? 83 C4 04 C7 03 00 00 00 00 83 C3 04 47 39 BE ? ? ? ? 77 DF 8D 8E ? ? ? ? E8 ? ? ? ? 6A 01 8B 45 08");
            layout = { .type = 0, .width = 5676, .height = 5680, .depth = 5684, .lens = 5728,
                .selected = 8, .modeCount = 6860, .modes = 5836,
                .draw = 4924, .surface = 5508, .device = 5260,
                .worldHandle = 4892, .spriteHandle = 4896, .worldMatrix = 5272, .spriteMatrix = 5336,
                .fontStride = 540, .japanese = true };
        }
        if (movieFrame.size() == 0)
            movieFrame = hook::pattern("81 EC 00 01 00 00 A1 ? ? ? ? 83 3D ? ? ? ? 00 56 57 0F 84 ? ? ? ? 85 C0 74 1B 83 F8 01");
        if (mode.size() != 1 || render.size() != 1 || actor.size() != 1 || matrix.size() != 1
            || sprite.size() != 1 || fonts.size() != 1 || room.size() != 1
            || background.size() != 1 || task.size() != 1 || title.size() != 1 || inventoryScreen.size() != 1) return;
        const auto display = Presentation::OutputDisplaySize();
        if (!display.width || !display.height) return;
        outputWidth = display.width; outputHeight = display.height;
        player = *actor.get_first<uint8_t*>(6);
        view = *matrix.get_first<void*>(1);
        fontTextures = *fonts.get_first<uint8_t*>(2);
        tasks = *task.get_first<void**>(19);
        titleTask = title.get_first();
        inventoryTask = inventoryScreen.get_first();
        ClassicInput::nativeMenu = []() { return tasks[1] == inventoryTask; };
        camera = *room.get_first<uint8_t*>(2);
        shMode = safetyhook::create_mid(mode.get_first(), Mode);
        shBackground = safetyhook::create_inline(background.get_first(), Background);
        shRender = safetyhook::create_inline(render.get_first(), Render);
        shSprite = safetyhook::create_inline(sprite.get_first(), Sprite);
        if (movie.size() == 1 && movieId.size() == 1)
        {
            mainWindow = *movie.get_first<HWND*>(31);
            movieWindow = *movie.get_first<HWND*>(layout.japanese ? 111 : 105);
            movieDevice = *movieId.get_first<MCIDEVICEID*>(2);
            if (const auto library = GetModuleHandleW(L"winmm.dll"))
                sendMovieCommand = reinterpret_cast<decltype(sendMovieCommand)>(GetProcAddress(library, "mciSendCommandA"));
            shMovie = safetyhook::create_inline(movie.get_first(), Movie);
            if (const auto wrapper = GetModuleHandleW(L"ddraw.dll"))
            {
                auto blit = hook::module_pattern(wrapper, "8B 15 ? ? ? ? 50 A1 ? ? ? ? 51 8B 4C 24 34 D1 EA D1 E8 52 50 6A 00 6A 00 51 FF 15 ? ? ? ? 8B 16 8B F8 8B 44 24 1C 50 56 FF 52 68");
                if (blit.size() == 1) shMovieBlit = safetyhook::create_mid(blit.get_first(28), MovieBlit);
            }
            ClassicGame::onSettingsChanged() += []() { Movie(); };
            if (movieFrame.size() == 1 && flip.size() == 1 && moviePlay.size() == 1 && movieInput.size() == 1)
            {
                present = reinterpret_cast<decltype(present)>(flip.get_first());
                movieFinished = *movieInput.get_first<int*>(55);
                shMovieInput = safetyhook::create_mid(movieInput.get_first(), MovieInput);
                shMovieFrame = safetyhook::create_inline(movieFrame.get_first(), MovieFrame);
                shMoviePlay = safetyhook::create_inline(moviePlay.get_first(), PlayMovie);
                ClassicMenu::pause.emplace_back(PauseMovie);
            }
        }
    }
}

class Widescreen
{
public:
    Widescreen() { FusionFix::onInitEvent() += []() { RE1Presentation::Init(); }; }
} Widescreen;
