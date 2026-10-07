module;

#include <common.hxx>
#include "ClassicDisplay.hxx"
#include <safetyhook.hpp>
#include <intrin.h>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <Zydis.h>
#include "Presentation.hxx"

export module Widescreen;

import common;
import Geometry;
import Game;
import Input;
import Menu;

namespace PanAndScan
{
    using WobbleFix::Read;
    using WobbleFix::Write;

    SafetyHookInline shDrawList, shFrame, shNextPacket, shFlat, shTextured, shEndScene, shMovie, shTile;
    SafetyHookInline shReadVideo, shAcceptMode, shScoreMode, shFloor;
    SafetyHookInline shMovieTick, shCopyPacket;
    uintptr_t storySprites = 0;
    int outputWidth = 0, outputHeight = 0;
    std::map<uintptr_t, double> rectangleEdges;
    Game::State game;
    Presentation::Pan pan;
    uintptr_t frameAddress = 0;
    auto lastUpdate = std::chrono::steady_clock::now();

    struct Scene
    {
        bool crop = false;
        float pan = 30.0f;
    } scene;

    struct Range
    {
        uintptr_t begin, end;
        Scene scene;
        bool interface;
        bool cinematic = false;
    };

    std::mutex queueMutex;
    std::unordered_map<void*, std::vector<Range>> queues;

    struct Context
    {
        void* renderer = nullptr;
        std::vector<Range> ranges;
        const Range* drawing = nullptr;
        bool movie = false, movieDrawn = false, cinematicBar = false, cinematic = false;
    };
    thread_local Context context;
    thread_local std::vector<uint8_t> vertexBuffer;
    std::vector<uint8_t> pausedFrame;
    std::vector<Range> pausedRanges;

    static Presentation::Viewport SceneViewport(void* renderer)
    {
        return Presentation::Viewport::Scene(float(Read<int>(renderer, 33996)), float(Read<int>(renderer, 34000)),
            Game::GetSettings().maxAspectRatio);
    }

    static bool OutputMode(void* selection)
    {
        auto* mode = Read<void*>(selection, 8);
        return mode && Read<int>(mode, 12) == outputWidth && Read<int>(mode, 8) == outputHeight
            && Read<int>(mode, 84) == 32;
    }

    static int __stdcall AcceptModeHook(void* selection)
    {
        // Keep native modes as fallbacks when an adapter cannot use the desktop.
        return OutputMode(selection) || shAcceptMode.unsafe_stdcall<int>(selection);
    }

    static int __stdcall ScoreModeHook(void* selection)
    {
        if (!OutputMode(selection))
            return shScoreMode.unsafe_stdcall<int>(selection);
        const auto* adapter = Read<void*>(selection, 0);
        const auto* device = Read<void*>(selection, 4);
        return 4096 + (Read<uintptr_t>(adapter, 0) ? 0 : 128) + (Read<int>(device, 332) ? 256 : 0);
    }

    static int __fastcall ReadVideoHook(void* application, void*)
    {
        const auto result = shReadVideo.unsafe_thiscall<int>(application);
        Write(application, 147252, outputWidth);
        Write(application, 147256, outputHeight);
        Write(application, 147260, 32);
        return result;
    }

    static double __cdecl FloorHook(double value)
    {
        if (context.drawing && context.drawing->scene.crop && !context.drawing->interface)
            if (const auto it = rectangleEdges.find(reinterpret_cast<uintptr_t>(_ReturnAddress())); it != rectangleEdges.end())
                return value - it->second;
        return shFloor.unsafe_ccall<double>(value);
    }

    static uintptr_t FindRectangleEdges(uintptr_t function, int returns, size_t expected, unsigned biasedEdges, uintptr_t floor = 0)
    {
        ZydisDecoder decoder;
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
        size_t count = 0;
        for (uintptr_t ip = function; ip < function + 4096 && returns;)
        {
            ZydisDecodedInstruction instruction;
            ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, reinterpret_cast<void*>(ip), 15, &instruction, operands)))
                return 0;
            if (Read<uint8_t>(reinterpret_cast<void*>(ip), 0) == 0xE8
                && Read<uint8_t>(reinterpret_cast<void*>(ip - 3), 0) == 0xDD
                && Read<uint16_t>(reinterpret_cast<void*>(ip - 2), 0) == 0x241C)
            {
                const auto target = injector::GetBranchDestination(ip).as_int();
                if (floor && target != floor)
                    return 0;
                floor = target;
                // Native sprites add half a pixel only to their right/bottom
                // edges; tiled backgrounds do not. Remove both forms of
                // snapping before applying the shared scrolling transform.
                if (count >= expected)
                    return 0;
                rectangleEdges.emplace(ip + instruction.length, biasedEdges & (1u << count) ? 0.5 : 0.0);
                ++count;
            }
            if (instruction.mnemonic == ZYDIS_MNEMONIC_RET)
                --returns;
            ip += instruction.length;
        }
        return !returns && count == expected ? floor : 0;
    }

    static bool CinematicBar(const void* packet)
    {
        return Read<int16_t>(packet, 8) == 0
            && (Read<int16_t>(packet, 10) == 0 || Read<int16_t>(packet, 10) == 216)
            && Read<int16_t>(packet, 12) == 320 && Read<int16_t>(packet, 14) == 24
            && Read<uint8_t>(packet, 7) == 0x62;
    }

    static int __fastcall CopyPacketHook(void* renderer, void*, const void* packet, int size)
    {
        auto* queue = static_cast<uint8_t*>(renderer) + 64;
        const auto begin = Read<uintptr_t>(queue, 8) - Read<uintptr_t>(queue, 0);
        const auto result = shCopyPacket.unsafe_thiscall<int>(renderer, packet, size);
        const auto address = reinterpret_cast<uintptr_t>(packet);
        if (size == 20 && address >= storySprites && address < storySprites + 400
            && Game::Enabled(Game::Option::PanAndScan))
        {
            const auto end = Read<uintptr_t>(queue, 8) - Read<uintptr_t>(queue, 0);
            if (end > begin)
            {
                const std::lock_guard lock(queueMutex);
                // The opening illustrations are scene sprites in the UI table.
                // Identify their source buffer, leaving subtitles and menus safe.
                queues[queue].push_back({ begin, end, { true, 30.0f }, false, true });
            }
        }
        return result;
    }

    static uintptr_t __fastcall DrawListHook(void* renderer, void*, uintptr_t list)
    {
        if ((*game.flags & 0x10000) || *game.backgroundMode != 2 || !(*game.flags & 0x08000000))
            scene.crop = false;
        if (list == *game.backgroundTable + 28)
        {
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(now - lastUpdate).count();
            lastUpdate = now;
            // The menu request stays nonzero throughout both room fades.
            // The native menu display flag changes only after fading out,
            // and clears before the room fades back in.
            const bool roomVisible = (*game.flags & 0x08000000) && !(*game.flags & 0x01000000);
            scene.crop = Game::Enabled(Game::Option::PanAndScan) && roomVisible && *game.backgroundMode == 2;
            scene.pan = scene.crop ? pan.Update(game.PlayerY(), *game.room, elapsed, Input::GetPad().right.y,
                SceneViewport(renderer).SourceHeight()) : 30.0f;
            if (!scene.crop)
            {
                pan.camera = UINT32_MAX;
                MouseInput::WheelPan(false);
            }
        }

        auto* queue = static_cast<uint8_t*>(renderer) + 64;
        const auto begin = Read<uintptr_t>(queue, 8) - Read<uintptr_t>(queue, 0);
        if (!begin)
        {
            const std::lock_guard lock(queueMutex);
            queues[queue].clear();
        }
        const auto result = shDrawList.unsafe_thiscall<uintptr_t>(renderer, list);
        const auto end = Read<uintptr_t>(queue, 8) - Read<uintptr_t>(queue, 0);
        if (end > begin)
        {
            const std::lock_guard lock(queueMutex);
            auto& ranges = queues[queue];
            ranges.push_back({ begin, end, scene, list == *game.interfaceTable + 28 });
        }
        return result;
    }

    static uintptr_t __fastcall FrameHook(void* renderer, void*)
    {
        Menu::UpdateMovie(renderer);
        Context previous = std::move(context);
        context = {};
        context.renderer = renderer;
        context.movieDrawn = Game::Enabled(Game::Option::PanAndScan) && (*game.flags & 0x10000);
        auto* queue = static_cast<uint8_t*>(renderer) + 64;
        {
            const std::lock_guard lock(queueMutex);
            if (const auto it = queues.find(queue); it != queues.end())
                context.ranges = it->second;
        }
        const auto data = Read<uintptr_t>(queue, 0);
        const auto size = Read<uintptr_t>(queue, 8) - data;
        if (Menu::Active() && !pausedFrame.empty() && pausedFrame.size() < Read<uintptr_t>(queue, 4) - data)
        {
            memcpy(reinterpret_cast<void*>(data), pausedFrame.data(), pausedFrame.size());
            Write(queue, 8, data + pausedFrame.size());
            context.ranges = pausedRanges;
        }
        else if (!Menu::Active())
        {
            pausedFrame.assign(reinterpret_cast<uint8_t*>(data), reinterpret_cast<uint8_t*>(data + size));
            pausedRanges = context.ranges;
        }
        if (Game::Enabled(Game::Option::PanAndScan) && !context.movieDrawn)
        {
            // Inspect the complete copied frame before any scene vertices draw.
            // Native bars define a 320x192 aperture; retain their authored
            // framing when centering it inside the selected output aspect.
            unsigned bars = 0;
            const auto end = Read<uintptr_t>(queue, 8);
            for (auto ip = data; ip + 4 <= end;)
            {
                const auto length = Read<uint32_t>(reinterpret_cast<void*>(ip), 0);
                if (length > end - ip - 4 || length < 8) break;
                const auto* packet = reinterpret_cast<void*>(ip + 4);
                if (length == 16 && CinematicBar(packet))
                    bars |= Read<int16_t>(packet, 10) == 0 ? 1 : 2;
                ip += length + 4;
            }
            context.cinematic = bars == 3 || std::ranges::any_of(context.ranges,
                [](const Range& range) { return range.cinematic; });
            if (context.cinematic)
            {
                const auto viewport = SceneViewport(renderer);
                // Cinematic framing is chosen after DrawListHook has updated
                // tracking. Retain the crop actually displayed, so the next
                // frame on this camera eases from it instead of jumping.
                // Opening illustrations and paused replays do not move tracking.
                if (bars == 3 && !Menu::Active())
                    scene.pan = pan.Center(*game.room, viewport.SourceHeight());
                for (auto& range : context.ranges)
                    if (!range.interface) range.scene = { true, viewport.CenterPan() };
            }
        }
        const auto result = shFrame.unsafe_thiscall<uintptr_t>(renderer);
        {
            const std::lock_guard lock(queueMutex);
            queues.erase(queue);
        }
        context = std::move(previous);
        return result;
    }

    static void* __fastcall NextPacketHook(void* queue, void*, void* size)
    {
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        auto* packet = shNextPacket.unsafe_thiscall<void*>(queue, size);
        // The texture preloader also walks this queue. Only the frame's draw
        // loop selects a transform; its clear quad stays in output coordinates.
        if (context.renderer && caller >= frameAddress && caller < frameAddress + 0x400)
        {
            context.drawing = nullptr;
            if (packet)
            {
                const auto offset = reinterpret_cast<uintptr_t>(packet) - Read<uintptr_t>(queue, 0);
                for (const auto& range : context.ranges)
                    if (offset >= range.begin && offset < range.end)
                    {
                        context.drawing = &range;
                        break;
                    }
            }
        }
        return packet;
    }

    static Presentation::Transform GetTransform(const Range& range, bool fullViewport)
    {
        const float width = float(Read<int>(context.renderer, 33996));
        const float height = float(Read<int>(context.renderer, 34000));
        const auto viewport = range.scene.crop || context.movieDrawn || context.cinematic
            ? SceneViewport(context.renderer) : Presentation::Viewport::Fit(width, height, 4.0f / 3.0f);
        if (fullViewport)
            return { viewport.width / width, viewport.height / height, viewport.x, viewport.y };
        return range.scene.crop && !range.interface
            ? Presentation::Transform::Crop(width, height, viewport,
                range.cinematic || context.cinematic ? viewport.CenterPan() : range.scene.pan)
            : Presentation::Transform::Fit(width, height, viewport);
    }

    static bool ScreenRectangle(void* vertices, int count)
    {
        if (!context.drawing || !context.drawing->interface || (!context.drawing->scene.crop && !context.movieDrawn && !context.cinematic) || count != 4)
            return false;
        const float width = float(Read<int>(context.renderer, 33996));
        const float height = float(Read<int>(context.renderer, 34000));
        unsigned corners = 0;
        for (int i = 0; i < 4; ++i)
        {
            const auto* vertex = static_cast<uint8_t*>(vertices) + i * 24;
            const float x = Read<float>(vertex, 0), y = Read<float>(vertex, 4);
            const bool right = std::abs(x - width) < 0.01f;
            const bool bottom = std::abs(y - height) < 0.01f;
            if ((!right && std::abs(x) >= 0.01f) || (!bottom && std::abs(y) >= 0.01f))
                return false;
            corners |= 1u << (unsigned(right) + 2u * unsigned(bottom));
        }
        return corners == 15;
    }

    template<size_t Stride> static void* TransformVertices(void* vertices, int count, bool fullViewport = false)
    {
        if (!context.drawing || count < 3 || count > 1024)
            return vertices;
        vertexBuffer.resize(count * Stride);
        memcpy(vertexBuffer.data(), vertices, vertexBuffer.size());
        const auto transform = GetTransform(*context.drawing, fullViewport);
        for (int i = 0; i < count; ++i)
        {
            auto* vertex = vertexBuffer.data() + i * Stride;
            Write(vertex, 0, Read<float>(vertex, 0) * transform.scaleX + transform.offsetX);
            Write(vertex, 4, Read<float>(vertex, 4) * transform.scaleY + transform.offsetY);
        }
        return vertexBuffer.data();
    }

    static int __fastcall FlatHook(void* device, void*, void* vertices, int count, unsigned flags)
    {
        // Full-screen fades are UI tiles, but must cover the entire scene.
        // Only a flat rectangle spanning all four output corners bypasses the
        // 4:3 text-safe layout; ordinary panels and textured UI keep that layout.
        if (context.cinematicBar && Game::Enabled(Game::Option::PanAndScan))
        {
            const float width = float(Read<int>(context.renderer, 33996));
            const float height = float(Read<int>(context.renderer, 34000));
            const auto viewport = SceneViewport(context.renderer);
            const auto transform = Presentation::Transform::Crop(width, height, viewport, viewport.CenterPan());
            vertexBuffer.resize(count * 24);
            memcpy(vertexBuffer.data(), vertices, vertexBuffer.size());
            for (int i = 0; i < count; ++i)
            {
                auto* vertex = vertexBuffer.data() + i * 24;
                Write(vertex, 0, Read<float>(vertex, 0) * transform.scaleX + transform.offsetX);
                Write(vertex, 4, Read<float>(vertex, 4) * transform.scaleY + transform.offsetY);
            }
            return shFlat.unsafe_thiscall<int>(device, vertexBuffer.data(), count, flags);
        }
        return shFlat.unsafe_thiscall<int>(device, TransformVertices<24>(vertices, count, ScreenRectangle(vertices, count)), count, flags);
    }

    static int __fastcall TileHook(void* renderer, void*, void* packet, int page)
    {
        const auto previous = context.cinematicBar;
        // These are the two native cinematic tiles, not ordinary menu panels.
        context.cinematicBar = context.cinematic && CinematicBar(packet);
        const auto result = shTile.unsafe_thiscall<int>(renderer, packet, page);
        context.cinematicBar = previous;
        return result;
    }

    static int __fastcall MovieHook(void* renderer, void*, void* packet)
    {
        const auto previous = context.movie;
        context.movie = Game::Enabled(Game::Option::PanAndScan);
        context.movieDrawn |= context.movie;
        const auto result = shMovie.unsafe_thiscall<int>(renderer, packet);
        context.movie = previous;
        return result;
    }

    static int __cdecl MovieTickHook()
    {
        if (Menu::Active()) return 0;
        return shMovieTick.unsafe_ccall<int>();
    }

    static int __fastcall TexturedHook(void* device, void*, void* vertices, int count, int texture, unsigned flags)
    {
        if (context.movie && count == 4)
        {
            const float width = float(Read<int>(context.renderer, 33996));
            const float height = float(Read<int>(context.renderer, 34000));
            const auto viewport = SceneViewport(context.renderer);
            float left = width, top = height, right = 0, bottom = 0;
            for (int i = 0; i < count; ++i)
            {
                const auto* vertex = static_cast<uint8_t*>(vertices) + i * 32;
                left = std::min(left, Read<float>(vertex, 0)); right = std::max(right, Read<float>(vertex, 0));
                top = std::min(top, Read<float>(vertex, 4)); bottom = std::max(bottom, Read<float>(vertex, 4));
            }
            if (right > left && bottom > top)
            {
                // Native 320x160 movie packets stretch with the output mode.
                // Restore their authored aspect, then cover the scene viewport.
                const float aspect = (right - left) / (bottom - top) * height / width * 4.0f / 3.0f;
                const float targetWidth = std::max(viewport.width, viewport.height * aspect);
                const float targetHeight = targetWidth / aspect;
                vertexBuffer.resize(count * 32);
                memcpy(vertexBuffer.data(), vertices, vertexBuffer.size());
                for (int i = 0; i < count; ++i)
                {
                    auto* vertex = vertexBuffer.data() + i * 32;
                    Write(vertex, 0, viewport.x + (viewport.width - targetWidth) * 0.5f + (Read<float>(vertex, 0) - left) * targetWidth / (right - left));
                    Write(vertex, 4, viewport.y + (viewport.height - targetHeight) * 0.5f + (Read<float>(vertex, 4) - top) * targetHeight / (bottom - top));
                }
                return shTextured.unsafe_thiscall<int>(device, vertexBuffer.data(), count, texture, flags);
            }
        }
        // Keep native sampling flags. Room masks and opening illustrations
        // share packed texture atlases without filtering gutters; forcing
        // linear filtering leaks neighbouring texels into every tile edge.
        // The precision fix and native UV correction run before this point.
        // Transform only final XY, keeping UVs, colour, depth and packet data.
        return shTextured.unsafe_thiscall<int>(device, TransformVertices<32>(vertices, count), count, texture, flags);
    }

    static int __fastcall EndSceneHook(void* device, void*)
    {
        if (context.renderer && !context.ranges.empty())
        {
            const float width = float(Read<int>(context.renderer, 33996));
            const float height = float(Read<int>(context.renderer, 34000));
            const auto viewport = context.movieDrawn || context.cinematic || context.ranges.front().scene.crop
                ? SceneViewport(context.renderer) : Presentation::Viewport::Fit(width, height, 4.0f / 3.0f);
            struct Vertex { float x, y, z, rhw; uint32_t color, specular; };
            const auto rectangle = [&](float left, float top, float right, float bottom)
            {
                if (right <= left || bottom <= top)
                    return;
                Vertex vertices[] = { { left, top, 0, 1, 0xFF000000, 0 }, { right, top, 0, 1, 0xFF000000, 0 },
                    { left, bottom, 0, 1, 0xFF000000, 0 }, { right, bottom, 0, 1, 0xFF000000, 0 } };
                shFlat.unsafe_thiscall<int>(device, vertices, 4, 0);
            };
            rectangle(0, 0, width, viewport.y);
            rectangle(0, viewport.y + viewport.height, width, height);
            rectangle(0, viewport.y, viewport.x, viewport.y + viewport.height);
            rectangle(viewport.x + viewport.width, viewport.y, width, viewport.y + viewport.height);
        }
        if (context.renderer)
            Menu::Draw(context.renderer, device, shFlat.original<void*>(), shTextured.original<void*>());
        return shEndScene.unsafe_thiscall<int>(device);
    }

    static void Init()
    {
        if (!Game::Resolve(game))
            return;

        auto rendererPattern = hook::pattern("C7 06 ? ? ? ? 89 BE ? CC 10 00 68 E0 01 00 00 68 80 02 00 00");
        auto nextPattern = hook::pattern("8B 51 0C 8B 41 08 3B D0 72 05 33 C0 C2 04 00 56 8B 32 8D 42 04 8D 54 32 04");
        auto flatPattern = hook::pattern("53 55 8B 5C 24 14 56 8B F1 57 8B 86 58 81 00 00 85 C0 74 07 83 CB 01");
        auto texturedPattern = hook::pattern("53 8B D9 55 56 8B 83 58 81 00 00 57 8B 7C 24 20 85 C0 74 03 83 CF 01");
        auto endPattern = hook::pattern("56 57 8B F1 BF 01 00 00 00 E8 ? ? ? ? 8B 06 50 8B 08 FF 51 28");
        auto readVideoPattern = hook::pattern("81 EC 00 04 00 00 8D 44 24 00 56 68 00 04 00 00 50 68 ? ? ? ? 68 ? ? ? ? 8B F1 68");
        auto acceptModePattern = hook::pattern("8B 44 24 04 56 8B 70 08 85 F6 75 09 B8 01 00 00 00 5E C2 04 00 83 7E 54 08");
        auto scoreModePattern = hook::pattern("53 55 8B 6C 24 0C 57 8B 45 08 85 C0 74 0B 8B 48 54 8B 78 0C 8B 40 08 EB 09");
        auto movieTickPattern = hook::pattern("A1 ? ? ? ? 25 FF 00 00 00 FF 14 85 ? ? ? ? A1 ? ? ? ? F6 C4 80 74");
        auto storyPattern = hook::pattern("51 53 55 56 57 BE ? ? ? ? BB ? ? ? ? C7 44 24 10 02 00 00 00 BF ? ? ? ? BD ? ? ? ? 56 E8");
        if (storyPattern.size() != 1 || rendererPattern.size() != 1 || nextPattern.size() != 1 || flatPattern.size() != 1
            || texturedPattern.size() != 1 || endPattern.size() != 1 || readVideoPattern.size() != 1
            || acceptModePattern.size() != 1 || scoreModePattern.size() != 1 || movieTickPattern.size() != 1)
            return;

        auto* renderer = *rendererPattern.get_first<uintptr_t*>(2);
        const auto display = Presentation::OutputDisplaySize();
        if (!display.width || !display.height) return;
        outputWidth = display.width; outputHeight = display.height;
        rectangleEdges.clear();
        auto floor = FindRectangleEdges(renderer[328 / sizeof(uintptr_t)], 1, 4, 0x0C);
        if (!floor || !FindRectangleEdges(renderer[332 / sizeof(uintptr_t)], 1, 4, 0x0A, floor)
            || !FindRectangleEdges(renderer[276 / sizeof(uintptr_t)], 2, 8, 0xC0, floor))
            return;
        storySprites = *storyPattern.get_first<uintptr_t>(6);
        frameAddress = renderer[32 / sizeof(uintptr_t)];
        shCopyPacket = safetyhook::create_inline(renderer[220 / sizeof(uintptr_t)], CopyPacketHook, SafetyHookInline::StartDisabled);
        shDrawList = safetyhook::create_inline(renderer[88 / sizeof(uintptr_t)], DrawListHook, SafetyHookInline::StartDisabled);
        shFrame = safetyhook::create_inline(frameAddress, FrameHook, SafetyHookInline::StartDisabled);
        shNextPacket = safetyhook::create_inline(nextPattern.get_first(), NextPacketHook, SafetyHookInline::StartDisabled);
        shFlat = safetyhook::create_inline(flatPattern.get_first(), FlatHook, SafetyHookInline::StartDisabled);
        shTextured = safetyhook::create_inline(texturedPattern.get_first(), TexturedHook, SafetyHookInline::StartDisabled);
        shEndScene = safetyhook::create_inline(endPattern.get_first(), EndSceneHook, SafetyHookInline::StartDisabled);
        shReadVideo = safetyhook::create_inline(readVideoPattern.get_first(), ReadVideoHook, SafetyHookInline::StartDisabled);
        shAcceptMode = safetyhook::create_inline(acceptModePattern.get_first(), AcceptModeHook, SafetyHookInline::StartDisabled);
        shScoreMode = safetyhook::create_inline(scoreModePattern.get_first(), ScoreModeHook, SafetyHookInline::StartDisabled);
        shFloor = safetyhook::create_inline(floor, FloorHook, SafetyHookInline::StartDisabled);
        shMovie = safetyhook::create_inline(renderer[292 / sizeof(uintptr_t)], MovieHook, SafetyHookInline::StartDisabled);
        shTile = safetyhook::create_inline(renderer[332 / sizeof(uintptr_t)], TileHook, SafetyHookInline::StartDisabled);
        shMovieTick = safetyhook::create_inline(movieTickPattern.get_first(), MovieTickHook, SafetyHookInline::StartDisabled);
        const auto hooks = { &shDrawList, &shFrame, &shNextPacket, &shFlat, &shTextured, &shEndScene,
            &shReadVideo, &shAcceptMode, &shScoreMode, &shFloor, &shMovie, &shTile, &shMovieTick, &shCopyPacket };
        for (auto* hook : hooks)
            if (!*hook)
            {
                for (auto* installed : hooks)
                    installed->reset();
                return;
            }
        for (auto* hook : hooks)
            if (!hook->enable())
            {
                for (auto* installed : hooks)
                    installed->reset();
                return;
            }
        Menu::rendererReady = true;
    }
}

class Widescreen
{
public:
    Widescreen()
    {
        FusionFix::onInitEvent() += []() { PanAndScan::Init(); };
    }
} Widescreen;
