module;

#include <common.hxx>
#include <safetyhook.hpp>
#include <optional>
#include <unordered_map>

export module Rendering;

import common;
import Geometry;
import Game;

namespace WobbleFix
{
    SafetyHookInline shDrawModel = {};
    SafetyHookInline shDrawSkinnedModel = {};
    SafetyHookInline shDrawObject = {};
    SafetyHookInline shDrawStage = {};
    SafetyHookInline shDrawFloor = {};
    SafetyHookInline shProjectVertices = {};
    SafetyHookInline shLoadScreenVertices = {};
    SafetyHookInline shStoreScreenVertices = {};
    SafetyHookInline shStoreScreenArray = {};
    SafetyHookInline shStoreTriangle = {};
    SafetyHookInline shStoreQuad = {};
    SafetyHookInline shNormalClip = {};
    SafetyHookInline shSubdivideTriangle = {};
    SafetyHookInline shDrawGouraudTriangle = {};
    SafetyHookInline shDrawGouraudQuad = {};
    SafetyHookInline shAdjustTextureCoordinates = {};
    SafetyHookInline shProjectVertex = {};
    SafetyHookInline shStoreScreenVertex = {};
    SafetyHookInline shDrawFloorDetail = {};
    SafetyHookInline shBlendVertices = {};
    SafetyHookInline shBlendFlattenedVertices = {};
    SafetyHookInline shQueueTriangle = {};
    SafetyHookInline shQueueQuad = {};
    SafetyHookInline shSubmitPacket = {};
    SafetyHookInline shResetQueue = {};
    SafetyHookInline shSubmitScene = {};

    struct Blend
    {
        uint32_t* output;
        std::vector<std::array<uint8_t, 3>> groups;
        std::unordered_map<uint32_t*, Vertex> vertices;
        size_t next = 0;
    };

    struct Context
    {
        unsigned depth = 0;
        bool enabled = false;
        void* gte = nullptr;
        std::array<Vertex, 3> screen{};
        bool valid = false;
        bool directOutput = false;
        Blend* blend = nullptr;
        VertexTable temporary;
        const Packet* copying = nullptr;
        const Packet* drawing = nullptr;
        void* renderer = nullptr;
    };
    thread_local Context context;

    // Native game tasks produce primitives on worker threads, then hand them
    // to the main thread. Packet metadata belongs to the frame, not its producer.
    struct FrameData
    {
        std::mutex mutex;
        std::unordered_map<void*, Packet> packets;
        std::unordered_map<void*, Vertex> coordinates;
        std::unordered_map<void*, std::unordered_map<uintptr_t, Packet>> queues;
    } frameData;

    static bool Active() { return context.depth != 0 && context.enabled; }

    struct ModelScope
    {
        ModelScope()
        {
            if (context.depth++ == 0)
            {
                context.enabled = Game::Enabled(Game::Option::WobbleFix);
                context.temporary.Clear();
                context.valid = false;
            }
        }
        ~ModelScope()
        {
            if (--context.depth == 0)
                context.valid = false;
        }
    };

    template<SafetyHookInline& Hook> static uintptr_t __cdecl Model3(uintptr_t a, uint32_t b, uintptr_t c)
    {
        ModelScope scope;
        return Hook.unsafe_ccall<uintptr_t>(a, b, c);
    }

    template<SafetyHookInline& Hook> static uintptr_t __cdecl Model2(uintptr_t a, uint32_t b)
    {
        ModelScope scope;
        return Hook.unsafe_ccall<uintptr_t>(a, b);
    }

    static bool MatchesGte(void* gte)
    {
        if (!Active() || !context.valid || context.gte != gte)
            return false;
        for (size_t i = 0; i < 3; ++i)
            if (Read<uint32_t>(gte, 176 + i * 4) != context.screen[i].packed)
                return false;
        return true;
    }

    static int __fastcall ProjectHook(void* gte, void*)
    {
        std::array<Point, 3> projected{};
        if (Active())
            for (size_t i = 0; i < 3; ++i)
                projected[i] = ProjectVertex(gte, static_cast<uint8_t*>(gte) + 136 + i * 8);
        const auto result = shProjectVertices.unsafe_thiscall<int>(gte);
        context.valid = Active();
        if (context.valid)
        {
            context.gte = gte;
            for (size_t i = 0; i < 3; ++i)
                context.screen[i] = { projected[i], Read<uint32_t>(gte, 176 + i * 4) };
        }
        return result;
    }

    static int __fastcall ProjectSingleHook(void* gte, void*)
    {
        const auto position = Active() ? ProjectVertex(gte, static_cast<uint8_t*>(gte) + 136) : Point{};
        const bool valid = MatchesGte(gte);
        const auto result = shProjectVertex.unsafe_thiscall<int>(gte);
        if (Active())
        {
            context.screen[0] = valid ? context.screen[1] : Vertex{ Unpack(Read<uint32_t>(gte, 176)), Read<uint32_t>(gte, 176) };
            context.screen[1] = valid ? context.screen[2] : Vertex{ Unpack(Read<uint32_t>(gte, 180)), Read<uint32_t>(gte, 180) };
            context.screen[2] = { position, Read<uint32_t>(gte, 184) };
            context.gte = gte;
            context.valid = true;
        }
        else
            context.valid = false;
        return result;
    }

    static void StoreCoordinate(uint32_t* destination, const Vertex& vertex)
    {
        if (context.directOutput)
            frameData.coordinates.insert_or_assign(destination, vertex);
        else
            *destination = context.temporary.Insert(vertex);
    }

    static int __fastcall StoreSingleHook(void* gte, void*, uint32_t* destination)
    {
        const auto result = shStoreScreenVertex.unsafe_thiscall<int>(gte, destination);
        const std::lock_guard lock(frameData.mutex);
        frameData.coordinates.erase(destination);
        if (MatchesGte(gte))
            StoreCoordinate(destination, context.screen[2]);
        return result;
    }

    static uintptr_t __cdecl FloorDetailHook(uintptr_t a, uint32_t b, uint32_t c, uintptr_t d)
    {
        // The floor-overlay builder writes stSXY results straight into packets.
        const auto previous = context.directOutput;
        context.directOutput = true;
        const auto result = shDrawFloorDetail.unsafe_ccall<uintptr_t>(a, b, c, d);
        context.directOutput = previous;
        return result;
    }

    static int __fastcall LoadHook(void* gte, void*, uint32_t a, uint32_t b, uint32_t c)
    {
        if (!Active())
        {
            context.valid = false;
            return shLoadScreenVertices.unsafe_thiscall<int>(gte, a, b, c);
        }
        context.screen = { context.temporary.Resolve(a), context.temporary.Resolve(b), context.temporary.Resolve(c) };
        context.gte = gte;
        context.valid = true;
        return shLoadScreenVertices.unsafe_thiscall<int>(gte, context.screen[0].packed, context.screen[1].packed, context.screen[2].packed);
    }

    static int __fastcall Store3Hook(void* gte, void*, uint32_t* a, uint32_t* b, uint32_t* c)
    {
        const auto result = shStoreScreenVertices.unsafe_thiscall<int>(gte, a, b, c);
        const std::lock_guard lock(frameData.mutex);
        frameData.coordinates.erase(a);
        frameData.coordinates.erase(b);
        frameData.coordinates.erase(c);
        if (context.blend)
        {
            // Skinning averages these native XY values with a previous bone's
            // projection. Never expose vertex handles to that arithmetic.
            auto& blend = *context.blend;
            const auto& indices = blend.groups.at(blend.next++);
            const uint32_t packed[] = { *a, *b, *c };
            const bool valid = MatchesGte(gte);
            for (size_t i = 0; i < 3; ++i)
            {
                if (i != 0 && indices[i] == 255)
                    continue;
                auto& vertex = blend.vertices.at(blend.output + indices[i]);
                const auto position = valid ? context.screen[i].position : Unpack(packed[i]);
                vertex.position.x = (vertex.position.x + position.x) * 0.5f;
                vertex.position.y = (vertex.position.y + position.y) * 0.5f;
            }
            return result;
        }
        if (MatchesGte(gte))
        {
            StoreCoordinate(a, context.screen[0]);
            StoreCoordinate(b, context.screen[1]);
            StoreCoordinate(c, context.screen[2]);
        }
        return result;
    }

    template<SafetyHookInline& Hook> static uintptr_t __cdecl BlendHook(uint32_t* output, void* depths, void* settings, const uint8_t* bones)
    {
        if (!Active())
            return Hook.unsafe_ccall<uintptr_t>(output, depths, settings, bones);

        Blend blend{ output };
        auto* cursor = bones;
        do
        {
            const auto count = cursor[1];
            cursor += 2;
            for (size_t group = 0; group < count; ++group, cursor += 3)
            {
                blend.groups.push_back({ cursor[0], cursor[1], cursor[2] });
                for (size_t i = 0; i < 3; ++i)
                {
                    if (i != 0 && cursor[i] == 255)
                        continue;
                    auto* coordinate = output + cursor[i];
                    if (!blend.vertices.contains(coordinate))
                    {
                        const auto vertex = context.temporary.Resolve(*coordinate);
                        blend.vertices.emplace(coordinate, vertex);
                        *coordinate = vertex.packed;
                    }
                }
            }
        } while (*cursor != 255);

        auto* previous = context.blend;
        context.blend = &blend;
        const auto result = Hook.unsafe_ccall<uintptr_t>(output, depths, settings, bones);
        context.blend = previous;
        for (auto& [coordinate, vertex] : blend.vertices)
        {
            vertex.packed = *coordinate;
            *coordinate = context.temporary.Insert(vertex);
        }
        return result;
    }

    static uintptr_t __fastcall StoreArrayHook(void* gte, void*, uint32_t* destination)
    {
        const auto result = shStoreScreenArray.unsafe_thiscall<uintptr_t>(gte, destination);
        const std::lock_guard lock(frameData.mutex);
        for (size_t i = 0; i < 3; ++i)
            frameData.coordinates.erase(&destination[i]);
        if (MatchesGte(gte))
            for (size_t i = 0; i < 3; ++i)
                StoreCoordinate(&destination[i], context.screen[i]);
        return result;
    }

    template<SafetyHookInline& Hook, size_t Count> static uintptr_t __cdecl StorePacket(void* destination)
    {
        const auto result = Hook.unsafe_ccall<uintptr_t>(destination);
        const std::lock_guard lock(frameData.mutex);
        frameData.packets.erase(destination);
        for (size_t i = 0; i < Count; ++i)
            frameData.coordinates.erase(static_cast<uint8_t*>(destination) + 8 + i * 12);
        if (Active())
        {
            Packet packet;
            packet.count = Count;
            const bool valid = context.gte && MatchesGte(context.gte);
            for (size_t i = 0; i < 3; ++i)
            {
                const auto packed = Read<uint32_t>(destination, 8 + i * 12);
                packet.vertices[i] = valid && packed == context.screen[i].packed
                    ? context.screen[i] : Vertex{ Unpack(packed), packed };
            }
            if constexpr (Count == 4)
            {
                // All native quad builders copy vertex four before stSXY3.
                packet.vertices[3] = context.temporary.Resolve(Read<uint32_t>(destination, 44));
                Write(destination, 44, packet.vertices[3].packed);
            }
            frameData.packets.insert_or_assign(destination, packet);
        }
        return result;
    }

    static int __fastcall CullHook(void* gte, void*)
    {
        const auto result = shNormalClip.unsafe_thiscall<int>(gte);
        if (MatchesGte(gte))
            Write(gte, 208, Facing(context.screen));
        return result;
    }

    static uintptr_t __fastcall SubdivideHook(void* gte, void*, uintptr_t settings, void* a, void* b, void* c, uintptr_t output)
    {
        Packet packet;
        if (Active())
        {
            packet.count = 3;
            packet.vertices[0].position = ProjectVertex(gte, a);
            packet.vertices[1].position = ProjectVertex(gte, b);
            packet.vertices[2].position = ProjectVertex(gte, c);
        }
        const auto result = shSubdivideTriangle.unsafe_thiscall<uintptr_t>(gte, settings, a, b, c, output);
        if (result == output + 40)
        {
            const std::lock_guard lock(frameData.mutex);
            auto* destination = reinterpret_cast<void*>(output);
            frameData.packets.erase(destination);
            for (size_t i = 0; i < 3; ++i)
                frameData.coordinates.erase(static_cast<uint8_t*>(destination) + 8 + i * 12);
            if (Active())
            {
                for (size_t i = 0; i < 3; ++i)
                    packet.vertices[i].packed = Read<uint32_t>(destination, 8 + i * 12);
                frameData.packets.insert_or_assign(destination, packet);
            }
        }
        // The native subdivision projector also modifies the GTE registers.
        context.valid = false;
        return result;
    }

    static std::optional<Packet> FindPacket(void* source, size_t count, size_t stride)
    {
        if (!Game::Enabled(Game::Option::WobbleFix))
            return std::nullopt;
        const std::lock_guard lock(frameData.mutex);
        std::optional<Packet> packet;
        const auto it = frameData.packets.find(source);
        if (it != frameData.packets.end() && it->second.Matches(source, stride, count))
            packet = it->second;
        if (!packet)
        {
            Packet direct;
            direct.count = count;
            bool found = false;
            for (size_t i = 0; i < count; ++i)
            {
                auto* address = static_cast<uint8_t*>(source) + 8 + i * stride;
                const auto packed = Read<uint32_t>(address, 0);
                const auto coordinate = frameData.coordinates.find(address);
                direct.vertices[i] = { Unpack(packed), packed };
                if (coordinate != frameData.coordinates.end() && coordinate->second.packed == packed)
                {
                    direct.vertices[i] = coordinate->second;
                    found = true;
                }
            }
            if (found)
                packet = direct;
        }
        return packet;
    }

    template<SafetyHookInline& Hook, size_t Count> static uintptr_t __fastcall QueueHook(void* renderer, void*, void* source)
    {
        // The ordering-table walker first copies the primitive onto its stack.
        // Carry its identity through that copy to the native queue submission.
        const auto packet = FindPacket(source, Count, 12);
        const auto* previous = context.copying;
        context.copying = packet ? &*packet : nullptr;
        const auto result = Hook.unsafe_thiscall<uintptr_t>(renderer, source);
        context.copying = previous;
        return result;
    }

    static int __fastcall SubmitPacketHook(void* renderer, void*, void* source, int size)
    {
        auto* queue = static_cast<uint8_t*>(renderer) + 64;
        const auto offset = Read<uintptr_t>(queue, 8) - Read<uintptr_t>(queue, 0) + 4;
        const auto result = shSubmitPacket.unsafe_thiscall<int>(renderer, source, size);
        if (result)
        {
            const std::lock_guard lock(frameData.mutex);
            if (context.copying && size == static_cast<int>(4 + context.copying->count * 12)
                && context.copying->Matches(source, 12, context.copying->count))
            {
                // The native queue uses realloc: offsets survive buffer moves.
                frameData.queues[queue].insert_or_assign(offset, *context.copying);
            }
            else if (const auto it = frameData.queues.find(queue); it != frameData.queues.end())
                it->second.erase(offset);
        }
        return result;
    }

    static uintptr_t __fastcall ResetQueueHook(void* queue, void*)
    {
        {
            const std::lock_guard lock(frameData.mutex);
            frameData.queues.erase(queue);
        }
        return shResetQueue.unsafe_thiscall<uintptr_t>(queue);
    }

    static uintptr_t __cdecl SubmitSceneHook()
    {
        const auto result = shSubmitScene.unsafe_ccall<uintptr_t>();
        const std::lock_guard lock(frameData.mutex);
        // PutDrawEnv can render the previous queue before this scene's DrawOTag
        // calls. Source metadata must survive that render and all ordering lists.
        frameData.packets.clear();
        frameData.coordinates.clear();
        context.valid = false;
        return result;
    }

    template<SafetyHookInline& Hook, size_t Count, size_t Stride> static uintptr_t __fastcall RenderHook(void* renderer, void*, void* source)
    {
        const auto* previous = context.drawing;
        const auto previousRenderer = context.renderer;
        auto packet = FindPacket(source, Count, Stride);
        auto* queue = static_cast<uint8_t*>(renderer) + 64;
        {
            const std::lock_guard lock(frameData.mutex);
            if (const auto it = frameData.queues.find(queue); it != frameData.queues.end())
            {
                const auto offset = reinterpret_cast<uintptr_t>(source) - Read<uintptr_t>(queue, 0);
                const auto queued = it->second.find(offset);
                if (queued != it->second.end() && queued->second.Matches(source, Stride, Count))
                    packet = queued->second;
            }
        }
        context.drawing = packet ? &*packet : nullptr;
        context.renderer = renderer;
        const auto result = Hook.unsafe_thiscall<uintptr_t>(renderer, source);
        context.drawing = previous;
        context.renderer = previousRenderer;
        return result;
    }

    static void __stdcall UVHook(void* vertices, int count)
    {
        // This call follows native X/Y conversion and precedes UV adjustment,
        // texture subdivision and submission. Keep all of those native stages.
        if (Game::Enabled(Game::Option::WobbleFix) && context.drawing && context.drawing->count == static_cast<size_t>(count))
        {
            const auto scaleX = Read<float>(context.renderer, 34004);
            const auto scaleY = Read<float>(context.renderer, 34008);
            context.drawing->Apply(vertices, scaleX, scaleY);
        }
        shAdjustTextureCoordinates.unsafe_stdcall<void>(vertices, count);
    }

    void Init()
    {
        // Resolve everything before installing any hooks. Missing or ambiguous
        // patterns leave the game untouched, including an active REbirth patch.
        auto modelPattern = hook::pattern("81 EC B4 02 00 00 8B 84 24 BC 02 00");
        auto skinnedPattern = hook::pattern("81 EC B4 02 00 00 8B 0D ? ? ? ?");
        auto objectPattern = hook::pattern("81 EC B4 02 00 00 8B 8C 24 B8 02 00");
        auto stagePattern = hook::pattern("8B 4C 24 04 83 EC 1C 66 8B 81 C0 00");
        auto floorPattern = hook::pattern("81 EC A4 01 00 00 53 55 8B AC 24 B0");
        auto trianglePattern = hook::pattern("8B 0D ? ? ? ? 8B 54 24 04 52 8B 01 FF 90 6C");
        auto quadPattern = hook::pattern("8B 0D ? ? ? ? 8B 54 24 04 52 8B 01 FF 90 70");
        auto subdividePattern = hook::pattern("83 EC 0C 53 55 56 57 8B 7C 24 24 BE");
        auto floorDetailPattern = hook::pattern("81 EC 94 00 00 00 53 8B 9C 24 A0 00");
        auto blendPattern = hook::pattern("83 EC 2C 8B 44 24 38 53 55 8B 6C 24 44 8B 08 8B 58 0C");
        auto flattenedBlendPattern = hook::pattern("83 EC 44 53 55 56 8B 74 24 5C 8B 5C 24 54 57 8B 4E 08");
        auto queueTrianglePattern = hook::pattern("83 EC 28 53 55 8B 6C 24 34 56 8B D9 57 B9 0A 00 00 00 8B F5 8B 03 8D 7C 24 10 F3 A5 8D 4C 24 10 6A 28 51 8B CB FF 90 BC 00 00 00");
        auto queueQuadPattern = hook::pattern("83 EC 34 53 55 8B 6C 24 40 56 8B D9 57 B9 0D 00 00 00 8B F5");
        auto resetQueuePattern = hook::pattern("8B 01 89 41 08 89 41 0C C3 ? ? ? ? ? ? ? 8B 01 89 41 0C C3");
        auto submitScenePattern = hook::pattern("A0 ? ? ? ? 83 EC 0C 53 33 DB 3A C3 74 05 E8 ? ? ? ? 53 E8");
        auto gtePattern = hook::pattern("C7 06 ? ? ? ? E8 ? ? ? ? 8B 4C 24 08 8B C6 5E 64 89 0D");
        // Japanese Steam builds move a trailing renderer flag by eight bytes;
        // the vtable, packet queue, vertex buffer and XY scales are unchanged.
        auto rendererPattern = hook::pattern("C7 06 ? ? ? ? 89 BE ? CC 10 00 68 E0 01 00 00 68 80 02 00 00");
        if (modelPattern.size() != 1
            || skinnedPattern.size() != 1
            || objectPattern.size() != 1
            || stagePattern.size() != 1
            || floorPattern.size() != 1
            || trianglePattern.size() != 1
            || quadPattern.size() != 1
            || subdividePattern.size() != 1
            || floorDetailPattern.size() != 1
            || blendPattern.size() != 1
            || flattenedBlendPattern.size() != 1
            || queueTrianglePattern.size() != 1
            || queueQuadPattern.size() != 1
            || resetQueuePattern.size() != 1
            || submitScenePattern.size() != 1
            || gtePattern.size() != 1
            || rendererPattern.size() != 1)
        {
            return;
        }

        auto* gte = *gtePattern.get_first<uintptr_t*>(2);
        auto* renderer = *rendererPattern.get_first<uintptr_t*>(2);

        shDrawModel = safetyhook::create_inline(modelPattern.get_first(), Model3<shDrawModel>, SafetyHookInline::StartDisabled);
        shDrawSkinnedModel = safetyhook::create_inline(skinnedPattern.get_first(), Model3<shDrawSkinnedModel>, SafetyHookInline::StartDisabled);
        shDrawObject = safetyhook::create_inline(objectPattern.get_first(), Model2<shDrawObject>, SafetyHookInline::StartDisabled);
        shDrawStage = safetyhook::create_inline(stagePattern.get_first(), Model3<shDrawStage>, SafetyHookInline::StartDisabled);
        shDrawFloor = safetyhook::create_inline(floorPattern.get_first(), Model2<shDrawFloor>, SafetyHookInline::StartDisabled);
        shProjectVertices = safetyhook::create_inline(gte[320 / sizeof(uintptr_t)], ProjectHook, SafetyHookInline::StartDisabled);
        shLoadScreenVertices = safetyhook::create_inline(gte[356 / sizeof(uintptr_t)], LoadHook, SafetyHookInline::StartDisabled);
        shStoreScreenVertices = safetyhook::create_inline(gte[352 / sizeof(uintptr_t)], Store3Hook, SafetyHookInline::StartDisabled);
        shStoreScreenArray = safetyhook::create_inline(gte[360 / sizeof(uintptr_t)], StoreArrayHook, SafetyHookInline::StartDisabled);
        shStoreTriangle = safetyhook::create_inline(trianglePattern.get_first(), StorePacket<shStoreTriangle, 3>, SafetyHookInline::StartDisabled);
        shStoreQuad = safetyhook::create_inline(quadPattern.get_first(), StorePacket<shStoreQuad, 4>, SafetyHookInline::StartDisabled);
        shNormalClip = safetyhook::create_inline(gte[312 / sizeof(uintptr_t)], CullHook, SafetyHookInline::StartDisabled);
        shSubdivideTriangle = safetyhook::create_inline(subdividePattern.get_first(), SubdivideHook, SafetyHookInline::StartDisabled);
        shDrawGouraudTriangle = safetyhook::create_inline(renderer[320 / sizeof(uintptr_t)], RenderHook<shDrawGouraudTriangle, 3, 12>, SafetyHookInline::StartDisabled);
        shDrawGouraudQuad = safetyhook::create_inline(renderer[324 / sizeof(uintptr_t)], RenderHook<shDrawGouraudQuad, 4, 12>, SafetyHookInline::StartDisabled);
        shAdjustTextureCoordinates = safetyhook::create_inline(renderer[364 / sizeof(uintptr_t)], UVHook, SafetyHookInline::StartDisabled);
        shProjectVertex = safetyhook::create_inline(gte[316 / sizeof(uintptr_t)], ProjectSingleHook, SafetyHookInline::StartDisabled);
        shStoreScreenVertex = safetyhook::create_inline(gte[324 / sizeof(uintptr_t)], StoreSingleHook, SafetyHookInline::StartDisabled);
        shDrawFloorDetail = safetyhook::create_inline(floorDetailPattern.get_first(), FloorDetailHook, SafetyHookInline::StartDisabled);
        shBlendVertices = safetyhook::create_inline(blendPattern.get_first(), BlendHook<shBlendVertices>, SafetyHookInline::StartDisabled);
        shBlendFlattenedVertices = safetyhook::create_inline(flattenedBlendPattern.get_first(), BlendHook<shBlendFlattenedVertices>, SafetyHookInline::StartDisabled);
        shQueueTriangle = safetyhook::create_inline(queueTrianglePattern.get_first(), QueueHook<shQueueTriangle, 3>, SafetyHookInline::StartDisabled);
        shQueueQuad = safetyhook::create_inline(queueQuadPattern.get_first(), QueueHook<shQueueQuad, 4>, SafetyHookInline::StartDisabled);
        shSubmitPacket = safetyhook::create_inline(renderer[188 / sizeof(uintptr_t)], SubmitPacketHook, SafetyHookInline::StartDisabled);
        shResetQueue = safetyhook::create_inline(resetQueuePattern.get_first(), ResetQueueHook, SafetyHookInline::StartDisabled);
        shSubmitScene = safetyhook::create_inline(submitScenePattern.get_first(), SubmitSceneHook, SafetyHookInline::StartDisabled);

        const auto hooks = {
            &shDrawModel, &shDrawSkinnedModel, &shDrawObject, &shDrawStage, &shDrawFloor,
            &shProjectVertices, &shLoadScreenVertices, &shStoreScreenVertices, &shStoreScreenArray,
            &shStoreTriangle, &shStoreQuad, &shNormalClip, &shSubdivideTriangle,
            &shDrawGouraudTriangle, &shDrawGouraudQuad,
            &shAdjustTextureCoordinates, &shProjectVertex, &shStoreScreenVertex, &shDrawFloorDetail,
            &shBlendVertices, &shBlendFlattenedVertices, &shQueueTriangle, &shQueueQuad, &shSubmitPacket, &shResetQueue, &shSubmitScene
        };
        for (auto* hook : hooks)
        {
            if (!*hook)
            {
                for (auto* installed : hooks)
                    installed->reset();
                return;
            }
        }
        for (auto* hook : hooks)
        {
            if (!hook->enable())
            {
                for (auto* installed : hooks)
                    installed->reset();
                return;
            }
        }
    }
}

class Rendering
{
public:
    Rendering()
    {
        FusionFix::onInitEvent() += []()
        {
            WobbleFix::Init();
        };
    }
} Rendering;
