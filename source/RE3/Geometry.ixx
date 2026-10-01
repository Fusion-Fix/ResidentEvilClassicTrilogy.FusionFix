module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

export module Geometry;

export namespace WobbleFix
{
    template<class T> T Read(const void* base, size_t offset)
    {
        T value;
        std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof(value));
        return value;
    }

    template<class T> void Write(void* base, size_t offset, T value)
    {
        std::memcpy(static_cast<uint8_t*>(base) + offset, &value, sizeof(value));
    }

    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Vertex
    {
        Point position;
        uint32_t packed = 0;
    };

    inline Point Unpack(uint32_t packed)
    {
        return { static_cast<float>(static_cast<int16_t>(packed)),
                 static_cast<float>(static_cast<int16_t>(packed >> 16)) };
    }

    inline Point ProjectVertex(const void* gte, const void* vertex)
    {
        double transformed[3]{};
        for (size_t row = 0; row < 3; ++row)
        {
            for (size_t col = 0; col < 3; ++col)
                transformed[row] += static_cast<double>(Read<int16_t>(gte, 16 + (row * 3 + col) * 2)) * Read<int16_t>(vertex, col * 2);
            // Keep the fractional part of the fixed-point matrix product too.
            transformed[row] = transformed[row] / 4096.0 + Read<int32_t>(gte, 36 + row * 4);
        }
        const auto h = Read<uint16_t>(gte, 120);
        const auto depth = std::max(transformed[2], std::max(1.0, std::floor(h / 2.0)));
        return { static_cast<float>(Read<int32_t>(gte, 112) + transformed[0] * h / depth),
                 static_cast<float>(Read<int32_t>(gte, 116) + transformed[1] * h / depth) };
    }

    inline int32_t Facing(const std::array<Vertex, 3>& vertices)
    {
        const auto& a = vertices[0].position;
        const auto& b = vertices[1].position;
        const auto& c = vertices[2].position;
        const double area = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y)
                          - (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
        if (!std::isfinite(area) || area == 0.0)
            return 0;
        const auto magnitude = static_cast<int32_t>(std::clamp(std::abs(area), 1.0, 2147483647.0));
        return area < 0.0 ? -magnitude : magnitude;
    }

    // Handles exist only in one model's temporary screen-coordinate arrays.
    // Native GTE outputs clamp Y to [-1024, 1023], so 0x4000 is an unused Y.
    // Unlike a cache keyed by rounded X/Y, coincident vertices remain distinct.
    class VertexTable
    {
        std::vector<Vertex> vertices;
    public:
        void Clear() { vertices.clear(); }

        uint32_t Insert(Vertex vertex)
        {
            if (vertices.size() >= 0x10000)
                return vertex.packed;
            const auto handle = 0x40000000u | static_cast<uint32_t>(vertices.size());
            vertices.push_back(vertex);
            return handle;
        }

        Vertex Resolve(uint32_t value) const
        {
            const auto index = value & 0xFFFF;
            if ((value & 0xFFFF0000) == 0x40000000 && index < vertices.size())
                return vertices[index];
            return { Unpack(value), value };
        }
    };

    struct Packet
    {
        std::array<Vertex, 4> vertices{};
        size_t count = 0;

        bool Matches(const void* packet, size_t stride, size_t vertexCount) const
        {
            if (count != vertexCount)
                return false;
            for (size_t i = 0; i < count; ++i)
                if (Read<uint32_t>(packet, 8 + i * stride) != vertices[i].packed)
                    return false;
            return true;
        }

        void Apply(void* destination, float scaleX, float scaleY) const
        {
            for (size_t i = 0; i < count; ++i)
            {
                Write(destination, i * 32, vertices[i].position.x * scaleX);
                Write(destination, i * 32 + 4, vertices[i].position.y * scaleY);
            }
        }
    };
}
