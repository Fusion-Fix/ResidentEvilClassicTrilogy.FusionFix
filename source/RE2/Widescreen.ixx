module;
#include <common.hxx>
#include "ClassicDisplay.hxx"
#include <safetyhook.hpp>
#include <d3d.h>
#include <dshow.h>
#include <amstream.h>
#include <chrono>
#include <vector>
#include "ClassicPresentation.hxx"

export module RE2Widescreen;
import common;
import ClassicGame;
import ClassicInput;
import ClassicMemory;
import ClassicMenu;

namespace RE2Presentation
{
    using ClassicMemory::Read;
    using ClassicMemory::Write;
    SafetyHookInline shMode, shRender, shPacket, shDraw, shBackground, shBlit, shState, shFrame, shMovie, shMovieClose;
    void* liveMovie = nullptr;
    HWND liveMovieWindow = nullptr;
    IUnknown* pausedMovie = nullptr;
    IVideoWindow* pausedWindow = nullptr;
    DWORD movieState = 0;
    LONG movieVisible = OAFALSE;
    bool streamedMovie = false;
    int outputWidth = 0, outputHeight = 0;
    void** game = nullptr;
    void* view = nullptr;
    uint32_t* fonts = nullptr;
    uint8_t* camera = nullptr;
    Presentation::Pan pan;
    bool roomDrawn = false;
    bool frameCrop = false;
    float framePan = 30.0f;
    auto lastFrame = std::chrono::steady_clock::now();

    struct Context
    {
        void* renderer = nullptr;
        bool crop = false, ui = false, fade = false, movie = false;
        float movieAspect = 4.0f / 3.0f;
        float pan = 30.0f;
        float width = 320.0f, height = 240.0f;
    };
    thread_local Context context;
    thread_local std::vector<D3DTLVERTEX> vertices;
    SafetyHookInline shText, shGetDC, shReleaseDC;
    void** textRenderer = nullptr;
    uint8_t* textResolution = nullptr;
    thread_local bool drawingText = false;
    thread_local HDC textDC = nullptr;
    thread_local int savedTextDC = 0;

    void RestoreTextDC()
    {
        if (textDC && savedTextDC) RestoreDC(textDC, savedTextDC);
        textDC = nullptr;
        savedTextDC = 0;
    }

    HRESULT WINAPI GetDC(IDirectDrawSurface* surface, HDC* dc)
    {
        const auto result = shGetDC.unsafe_stdcall<HRESULT>(surface, dc);
        if (FAILED(result) || !drawingText || !dc || !*dc) return result;
        DDSURFACEDESC description{ .dwSize = sizeof(DDSURFACEDESC) };
        if (FAILED(surface->GetSurfaceDesc(&description))) return result;
        const auto viewport = Presentation::Viewport::Fit(float(description.dwWidth), float(description.dwHeight), 4.0f / 3.0f);
        const float scale = viewport.height / (*textResolution ? 480.0f : 240.0f);
        RestoreTextDC();
        textDC = *dc;
        savedTextDC = SaveDC(textDC);
        const XFORM transform{ scale, 0.0f, 0.0f, scale, viewport.x, viewport.y };
        if (!savedTextDC || !SetGraphicsMode(textDC, GM_ADVANCED) || !SetWorldTransform(textDC, &transform)) RestoreTextDC();
        return result;
    }

    HRESULT WINAPI ReleaseDC(IDirectDrawSurface* surface, HDC dc)
    {
        if (dc == textDC) RestoreTextDC();
        return shReleaseDC.unsafe_stdcall<HRESULT>(surface, dc);
    }

    int __cdecl Text()
    {
        // Native save/load text is drawn through GDI, outside the render queues.
        // Keep measurement and clipping in its logical canvas, then scale the DC.
        auto* surface = *textRenderer ? Read<IDirectDrawSurface*>(*textRenderer, 24400) : nullptr;
        if (surface && !shGetDC && !shReleaseDC)
        {
            auto** table = *reinterpret_cast<void***>(surface);
            shGetDC = safetyhook::create_inline(table[17], GetDC);
            shReleaseDC = safetyhook::create_inline(table[26], ReleaseDC);
        }
        drawingText = bool(shGetDC) && bool(shReleaseDC);
        const auto result = shText.unsafe_ccall<int>();
        RestoreTextDC();
        drawingText = false;
        return result;
    }

    void PauseMovie(bool active)
    {
        if (active && liveMovie && (Read<uint32_t>(liveMovie, 148) & 1))
        {
            streamedMovie = (Read<uint32_t>(liveMovie, 148) & 8) != 0;
            auto* movie = Read<IUnknown*>(liveMovie, streamedMovie ? 132 : 116);
            if (!movie) return;
            HRESULT result;
            if (streamedMovie)
            {
                STREAM_STATE state{};
                auto* stream = static_cast<IAMMultiMediaStream*>(movie);
                result = stream->GetState(&state);
                if (SUCCEEDED(result)) { movieState = DWORD(state); result = stream->SetState(STREAMSTATE_STOP); }
            }
            else
            {
                OAFilterState state{};
                auto* control = static_cast<IMediaControl*>(movie);
                result = control->GetState(0, &state);
                if (SUCCEEDED(result)) { movieState = DWORD(state); result = control->Pause(); }
            }
            if (FAILED(result)) return;
            movie->AddRef(); pausedMovie = movie;
            if (!streamedMovie)
            {
                pausedWindow = Read<IVideoWindow*>(liveMovie, 124);
                if (pausedWindow)
                {
                    pausedWindow->AddRef(); pausedWindow->get_Visible(&movieVisible);
                    pausedWindow->put_Visible(OAFALSE);
                }
            }
        }
        else if (!active && pausedMovie)
        {
            if (streamedMovie) static_cast<IAMMultiMediaStream*>(pausedMovie)->SetState(STREAM_STATE(movieState));
            else
            {
                auto* control = static_cast<IMediaControl*>(pausedMovie);
                if (movieState == State_Running) control->Run();
                else if (movieState == State_Stopped) control->Stop();
            }
            if (pausedWindow) { pausedWindow->put_Visible(movieVisible); pausedWindow->Release(); pausedWindow = nullptr; }
            pausedMovie->Release(); pausedMovie = nullptr;
        }
    }
    int __fastcall CloseMovie(void* movie, void*)
    {
        if (liveMovie == movie) { PauseMovie(false); liveMovie = nullptr; liveMovieWindow = nullptr; }
        return shMovieClose.unsafe_thiscall<int>(movie);
    }

    Presentation::Transform Transform()
    {
        const auto width = context.width;
        const auto height = context.height;
        const auto viewport = Presentation::Viewport::Scene(width, height, ClassicGame::GetSettings().maxAspectRatio);
        if (context.fade) return { viewport.width / width, viewport.height / height, viewport.x, viewport.y };
        return context.crop && !context.ui ? Presentation::Transform::Crop(width, height, viewport, context.pan)
            : Presentation::Transform::Fit(width, height, viewport);
    }

    float PlayerY(void* renderer)
    {
        if (!*game) return 120.0f;
        const auto* player = static_cast<uint8_t*>(*game) + 14864;
        double point[] = { double(Read<int32_t>(player, 56)), double(Read<int32_t>(player, 60)) - 800.0,
            double(Read<int32_t>(player, 64)) };
        double projected[3]{};
        for (size_t row = 0; row < 3; ++row)
        {
            projected[row] = Read<int32_t>(view, 20 + row * 4);
            for (size_t column = 0; column < 3; ++column)
                projected[row] += Read<int16_t>(view, (row * 3 + column) * 2) * point[column] / 4096.0;
        }
        return projected[2] > 1.0 ? float(120.0 + projected[1] * Read<int>(renderer, 23232) / projected[2]) : 120.0f;
    }

    int __fastcall Mode(void* renderer, void*)
    {
        // Use the desktop for the native primary/back surfaces. Presentation
        // then fits or crops the same image without changing display modes.
        if (!(Read<uint32_t>(renderer, 24524) & 0x2000))
        {
            const int selection = Read<int>(renderer, 19472);
            const int count = Read<int>(renderer, 24284);
            if (selection >= 0 && selection < count)
            {
                auto* mode = static_cast<uint8_t*>(renderer) + 23260 + selection * 16;
                Write(mode, 0, outputWidth);
                Write(mode, 4, outputHeight);
                Write(mode, 8, 16);
                Write(mode, 12, 1);
            }
        }
        return shMode.unsafe_thiscall<int>(renderer);
    }

    int __cdecl Background()
    {
        roomDrawn = true;
        return shBackground.unsafe_ccall<int>();
    }

    int __fastcall State(void* renderer, void*, void* packet)
    {
        // The game's state packets restore a logical 320x240 scale during
        // character rendering. Keep those packets on the physical surfaces.
        uint32_t copy[13];
        memcpy(copy, packet, sizeof(copy));
        if (context.renderer && (copy[1] & 0x2000))
        {
            const float x = context.width / Read<int>(renderer, 23184);
            const float y = context.height / Read<int>(renderer, 23188);
            memcpy(copy + 7, &x, sizeof(x));
            memcpy(copy + 8, &y, sizeof(y));
        }
        return shState.unsafe_thiscall<int>(renderer, copy);
    }

    HRESULT WINAPI Draw(IDirect3DDevice2* device, D3DPRIMITIVETYPE primitive, D3DVERTEXTYPE type,
        void* data, DWORD count, DWORD flags)
    {
        if (!context.renderer || type != D3DVT_TLVERTEX || !data || !count || count > 8192)
            return shDraw.unsafe_stdcall<HRESULT>(device, primitive, type, data, count, flags);
        vertices.resize(count);
        memcpy(vertices.data(), data, count * sizeof(D3DTLVERTEX));
        const auto transform = Transform();
        for (auto& vertex : vertices)
        {
            vertex.sx = vertex.sx * transform.scaleX + transform.offsetX;
            vertex.sy = vertex.sy * transform.scaleY + transform.offsetY;
        }
        return shDraw.unsafe_stdcall<HRESULT>(device, primitive, type, vertices.data(), count, flags);
    }

    int __fastcall Packet(void* renderer, void*, void* packet)
    {
        const auto previous = context;
        const auto kind = Read<uint32_t>(packet, 4) & 0xFFFFF;
        const auto texture = Read<uint32_t>(packet, 8);
        context.ui = (kind & 4) && (texture == fonts[8 * 3] || texture == fonts[9 * 3]);
        // Streaming movies use a normal textured sprite whose texture is marked
        // as a movie surface. They do not use the DirectShow window placement.
        context.movie = kind == 36 && texture < 256 && (Read<uint32_t>(renderer, 6144 + 52 * texture) & 0x4000);
        if (context.movie)
        {
            const int width = Read<int16_t>(packet, 20) + 1 - Read<int16_t>(packet, 16);
            const int height = Read<int16_t>(packet, 22) + 1 - Read<int16_t>(packet, 18);
            context.movie = width > 0 && height > 0;
            if (context.movie) context.movieAspect = float(width) / height;
        }
        if (kind == 33)
        {
            const auto left = Read<int16_t>(packet, 8), top = Read<int16_t>(packet, 10);
            const auto right = Read<int16_t>(packet, 12), bottom = Read<int16_t>(packet, 14);
            context.fade = left == 0 && top == 0 && right == 319 && bottom == 239;
        }
        const auto result = shPacket.unsafe_thiscall<int>(renderer, packet);
        context = previous;
        return result;
    }

    int __cdecl Blit(void* destination, void* source, int left, int top, int right, int bottom,
        int sourceLeft, int sourceTop, int sourceRight, int sourceBottom,
        int clipLeft, int clipTop, int clipRight, int clipBottom, int color, unsigned flags)
    {
        if (context.renderer && (destination == static_cast<uint8_t*>(context.renderer) + 24356
            || destination == static_cast<uint8_t*>(context.renderer) + 24468
            || destination == static_cast<uint8_t*>(context.renderer) + 24588))
        {
            auto transform = Transform();
            if (context.movie && right > left && bottom > top)
            {
                const bool widescreen = ClassicGame::Enabled(ClassicGame::Option::PanAndScan);
                const auto viewport = widescreen
                    ? Presentation::Viewport::Scene(context.width, context.height, ClassicGame::GetSettings().maxAspectRatio)
                    : Presentation::Viewport::Fit(context.width, context.height, 4.0f / 3.0f);
                // The native movie path multiplies the display and renderer
                // scales. Use the authored quad's aspect rather than inheriting
                // that nonuniform scaling at desktop resolutions.
                const float width = widescreen ? std::max(viewport.width, viewport.height * context.movieAspect)
                    : std::min(viewport.width, viewport.height * context.movieAspect);
                const float height = width / context.movieAspect;
                const float x = width / (right - left), y = height / (bottom - top);
                transform = { x, y, viewport.x + (viewport.width - width) * 0.5f - left * x,
                    viewport.y + (viewport.height - height) * 0.5f - top * y };
            }
            left = int(std::lround(left * transform.scaleX + transform.offsetX));
            right = int(std::lround(right * transform.scaleX + transform.offsetX));
            top = int(std::lround(top * transform.scaleY + transform.offsetY));
            bottom = int(std::lround(bottom * transform.scaleY + transform.offsetY));
        }
        return shBlit.unsafe_ccall<int>(destination, source, left, top, right, bottom,
            sourceLeft, sourceTop, sourceRight, sourceBottom, clipLeft, clipTop, clipRight, clipBottom, color, flags);
    }

    int __fastcall Frame(void* renderer, void*)
    {
        auto* draw = Read<IDirectDraw*>(renderer, 24328);
        auto* surface = Read<IDirectDrawSurface*>(renderer, 24400);
        if (ClassicMenu::opened && ClassicMenu::Draw(draw, surface)) return 1;
        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        frameCrop = ClassicGame::Enabled(ClassicGame::Option::PanAndScan) && roomDrawn;
        const auto viewport = Presentation::Viewport::Scene(float(Read<uint16_t>(renderer, 24382)), float(Read<uint16_t>(renderer, 24384)),
            ClassicGame::GetSettings().maxAspectRatio);
        bool topBar = false, bottomBar = false;
        for (const int offset : { 19532, 19500, 19484 })
        {
            auto* packet = Read<uint8_t*>(renderer, offset + 12);
            for (int i = 0; packet && i < 8192; ++i, packet = Read<uint8_t*>(packet))
            {
                if ((Read<uint32_t>(packet, 4) & 0xFFFFF) != 33 || (Read<uint32_t>(packet, 16) & 0xFFFFFF)
                    || Read<int16_t>(packet, 8) != 0 || Read<int16_t>(packet, 12) != 319) continue;
                topBar |= Read<int16_t>(packet, 10) == 0 && Read<int16_t>(packet, 14) == 23;
                bottomBar |= Read<int16_t>(packet, 10) == 216 && Read<int16_t>(packet, 14) == 239;
            }
        }
        const auto cameraKey = Read<uint32_t>(camera - 4) ^ (uint32_t(*camera) << 24);
        if (ClassicGame::Enabled(ClassicGame::Option::PanAndScan) && topBar && bottomBar)
        {
            frameCrop = true;
            framePan = pan.Center(cameraKey, viewport.SourceHeight());
        }
        else if (frameCrop)
            framePan = pan.Update(PlayerY(renderer), cameraKey, elapsed, ClassicInput::pad.right.y, viewport.SourceHeight());
        else pan.camera = UINT32_MAX;
        roomDrawn = false;
        // All three queues belong to one image. Clear and select its crop once,
        // before submitting the background, characters and interface queues.
        if (surface)
        {
            DDBLTFX fill{};
            fill.dwSize = sizeof(fill);
            surface->Blt(nullptr, nullptr, nullptr, DDBLT_COLORFILL | DDBLT_WAIT, &fill);
        }
        const auto result = shFrame.unsafe_thiscall<int>(renderer);
        ClassicMenu::Draw(draw, surface);
        return result;
    }

    int __fastcall Render(void* renderer, void*, void* queue)
    {
        const auto previous = context;
        context = { renderer, frameCrop };
        context.pan = framePan;
        context.width = float(Read<uint16_t>(renderer, 24382));
        context.height = float(Read<uint16_t>(renderer, 24384));
        Write(renderer, 23224, context.width / Read<int>(renderer, 23184));
        Write(renderer, 23228, context.height / Read<int>(renderer, 23188));
        auto* device = Read<IDirect3DDevice2*>(renderer, 23172);
        if (device && !shDraw)
            shDraw = safetyhook::create_inline((*reinterpret_cast<void***>(device))[29], Draw);
        const auto result = shRender.unsafe_thiscall<int>(renderer, queue);
        context = previous;
        return result;
    }

    void PositionMovie(void* movie, HWND window)
    {
        if (Read<uint32_t>(movie, 148) & 8) return;
        // Offset 128 contains IMediaEvent, not IBasicVideo. Obtain the video
        // interface from the graph rather than calling a different COM vtable.
        auto* graph = Read<IUnknown*>(movie, 112);
        auto* videoWindow = Read<IVideoWindow*>(movie, 124);
        if (!graph || !videoWindow) return;
        IBasicVideo* video = nullptr;
        if (FAILED(graph->QueryInterface(__uuidof(IBasicVideo), reinterpret_cast<void**>(&video)))) return;
        LONG width = 0, height = 0;
        const auto widthResult = video->get_VideoWidth(&width);
        const auto heightResult = video->get_VideoHeight(&height);
        video->Release();
        if (FAILED(widthResult) || FAILED(heightResult) || width <= 0 || height <= 0) return;
        RECT client{};
        if (!GetClientRect(window, &client)) return;
        const bool widescreen = ClassicGame::Enabled(ClassicGame::Option::PanAndScan);
        const auto viewport = widescreen
            ? Presentation::Viewport::Scene(float(client.right), float(client.bottom), ClassicGame::GetSettings().maxAspectRatio)
            : Presentation::Viewport::Fit(float(client.right), float(client.bottom), 4.0f / 3.0f);
        const float scale = widescreen ? std::max(viewport.width / width, viewport.height / height)
            : std::min(viewport.width / width, viewport.height / height);
        videoWindow->SetWindowPosition(
            LONG(std::lround(viewport.x + (viewport.width - width * scale) * 0.5f)),
            LONG(std::lround(viewport.y + (viewport.height - height * scale) * 0.5f)),
            LONG(std::lround(width * scale)), LONG(std::lround(height * scale)));
        return;
    }
    int __fastcall Movie(void* movie, void*, const char* file, HWND window, RECT* placement, void* draw, void* surface)
    {
        const auto result = shMovie.unsafe_thiscall<int>(movie, file, window, placement, draw, surface);
        if (result) { liveMovie = movie; liveMovieWindow = window; PositionMovie(movie, window); }
        return result;
    }
    void Init()
    {
        auto mode = hook::pattern("81 EC 68 01 00 00 53 55 56 8B F1 33 DB 57 8B 86 10 4C 00 00 C1 E0 04");
        auto render = hook::pattern("83 EC 18 56 8B F1 57 8B 86 CC 5F 00 00 F6 C4 20 0F 85 ? ? ? ? 8B 86 84 5A 00 00");
        auto packet = hook::pattern("81 EC C8 00 00 00 53 55 56 57 8B BC 24 DC 00 00 00 8D 44 24 58 89 44 24 30");
        auto background = hook::pattern("A1 ? ? ? ? 83 EC 08 83 F8 01 53 55 56 57 0F 84 ? ? ? ? 6A 78 68 A0 00 00 00");
        auto blit = hook::pattern("8B 44 24 04 83 EC 10 8A 48 26 53 55 56 84 C9 57 0F 84 ? ? ? ? 8B 4C 24 28");
        auto actor = hook::pattern("A1 ? ? ? ? 83 C7 04 3B B8 4C 21 00 00 75 ? 8D B0 10 3A 00 00");
        auto matrix = hook::pattern("BF ? ? ? ? F3 A5 8B 4C 24 4C 8B 70 04 8B 18 8B 69 04");
        auto font = hook::pattern("8B 44 24 08 8B 0D ? ? ? ? 53 56 83 F8 29 57 8B F1 72 06 33 C0 5F 5E 5B C3 8D 3C 40 C1 E7 02 8B 87 ? ? ? ?");
        if (font.size() != 1)
            font = hook::pattern("8B 44 24 08 8B 0D ? ? ? ? 53 56 83 F8 28 57 8B F1 72 06 33 C0 5F 5E 5B C3 8D 3C 40 C1 E7 02 8B 87 ? ? ? ?");
        auto state = hook::pattern("83 EC 58 56 57 8B 7C 24 64 8B F1 8B 47 04 F6 C4 20 74 12 8B 47 1C 89 86 B8 5A 00 00");
        auto frame = hook::pattern("56 8B F1 8B 86 C4 5A 00 00 85 C0 0F 85 ? ? ? ? 8B 86 CC 5F 00 00 F6 C4 02 0F 84");
        auto movie = hook::pattern("81 EC 24 04 00 00 53 55 56 57 8B F1 E8 ? ? ? ? 8B 9C 24 38 04 00 00 8B 2D");
        // The German game wrapper replaces the first native call with a jump.
        if (movie.size() != 1)
            movie = hook::pattern("81 EC 24 04 00 00 53 55 56 57 8B F1 E9 ? ? ? ? 8B 9C 24 38 04 00 00 8B 2D");
        auto movieClose = hook::pattern("56 57 8B F1 E8 ? ? ? ? 8B 86 94 00 00 00 BF 00 00 00 00 83 E0 08 89 BE 98 00 00 00");
        auto text = hook::pattern("A1 ? ? ? ? 83 EC 30 85 C0 53 55 56 57 0F 84 ? ? ? ? A1 ? ? ? ? 8D 54 24 10 52 8B 80 50 5F 00 00 50 8B 08 FF 51 44");
        auto textScale = hook::pattern("A0 ? ? ? ? 8B 4C 24 28 84 C0 74 ? 81 F9 40 02 00 00");
        if (mode.size() != 1 || render.size() != 1 || packet.size() != 1 || background.size() != 1
            || blit.size() != 1 || actor.size() != 1 || matrix.size() != 1 || font.size() != 1
            || state.size() != 1 || frame.size() != 1) return;
        const auto display = Presentation::OutputDisplaySize();
        if (!display.width || !display.height) return;
        outputWidth = display.width; outputHeight = display.height;
        game = *actor.get_first<void**>(1);
        view = *matrix.get_first<void*>(1);
        fonts = *font.get_first<uint32_t*>(34);
        camera = *background.get_first<uint8_t*>(36);
        shMode = safetyhook::create_inline(mode.get_first(), Mode);
        shPacket = safetyhook::create_inline(packet.get_first(), Packet);
        shBackground = safetyhook::create_inline(background.get_first(), Background);
        shBlit = safetyhook::create_inline(blit.get_first(), Blit);
        shState = safetyhook::create_inline(state.get_first(), State);
        shRender = safetyhook::create_inline(render.get_first(), Render);
        shFrame = safetyhook::create_inline(frame.get_first(), Frame);
        if (text.size() == 1 && textScale.size() == 1)
        {
            textRenderer = *text.get_first<void**>(21);
            textResolution = *textScale.get_first<uint8_t*>(1);
            shText = safetyhook::create_inline(text.get_first(), Text);
        }
        if (movie.size() == 1 && movieClose.size() == 1)
        {
            shMovieClose = safetyhook::create_inline(movieClose.get_first(), CloseMovie);
            shMovie = safetyhook::create_inline(movie.get_first(), Movie);
            ClassicMenu::pause.emplace_back(PauseMovie);
            ClassicGame::onSettingsChanged() += []() { if (liveMovie) PositionMovie(liveMovie, liveMovieWindow); };
        }
    }
}

class Widescreen
{
public:
    Widescreen() { FusionFix::onInitEvent() += []() { RE2Presentation::Init(); }; }
} Widescreen;
