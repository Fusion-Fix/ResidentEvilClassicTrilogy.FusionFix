#pragma once
#include "MouseInput.hxx"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <charconv>
#include <string_view>

namespace Presentation
{
    inline float ParseAspectRatio(std::string_view value)
    {
        constexpr float fallback = 16.0f / 9.0f;
        float numerator = 0.0f, denominator = 1.0f;
        const auto first = std::from_chars(value.data(), value.data() + value.size(), numerator);
        if (first.ec != std::errc{}) return fallback;
        if (first.ptr != value.data() + value.size())
        {
            if (*first.ptr != ':' && *first.ptr != '/') return fallback;
            const auto second = std::from_chars(first.ptr + 1, value.data() + value.size(), denominator);
            if (second.ec != std::errc{} || second.ptr != value.data() + value.size()) return fallback;
        }
        const float aspect = numerator / denominator;
        return std::isfinite(aspect) && aspect >= 4.0f / 3.0f ? aspect : fallback;
    }

    struct Viewport
    {
        float x, y, width, height;

        static Viewport Fit(float width, float height, float aspect)
        {
            const float w = std::min(width, height * aspect);
            const float h = w / aspect;
            return { (width - w) * 0.5f, (height - h) * 0.5f, w, h };
        }

        static Viewport Scene(float width, float height, float maximum)
        {
            return Fit(width, height, std::clamp(width / height, 4.0f / 3.0f, maximum));
        }

        float SourceHeight() const { return std::min(240.0f, 320.0f * height / width); }
        float CenterPan() const { return (240.0f - SourceHeight()) * 0.5f; }
    };

    struct Transform
    {
        float scaleX = 1.0f, scaleY = 1.0f, offsetX = 0.0f, offsetY = 0.0f;

        static Transform Fit(float width, float height, const Viewport& viewport)
        {
            const auto image = Viewport::Fit(viewport.width, viewport.height, 4.0f / 3.0f);
            return { image.width / width, image.height / height,
                viewport.x + image.x, viewport.y + image.y };
        }

        static Transform Crop(float width, float height, const Viewport& viewport, float pan)
        {
            const float visible = viewport.SourceHeight();
            return { viewport.width / width, viewport.height * (240.0f / visible) / height,
                viewport.x, viewport.y - std::round(pan * viewport.height / visible) };
        }
    };

    struct Pan
    {
        float position = 30.0f;
        float look = 0.0f;
        float wheelLook = 0.0f;
        float wheelPlayerY = 120.0f;
        bool releaseWheel = false;
        uint32_t camera = UINT32_MAX;

        float Center(uint32_t nextCamera, float visibleHeight = 180.0f)
        {
            // Store the crop actually shown during a cinematic. Tracking can
            // then ease out of it without revealing an unseen player offset.
            position = std::max(240.0f - visibleHeight, 0.0f) * 0.5f;
            look = 0.0f;
            wheelLook = 0.0f;
            releaseWheel = false;
            MouseInput::WheelPan(false);
            camera = nextCamera;
            return position;
        }

        float Update(float playerY, uint32_t nextCamera, float seconds, float rightStickY = 0.0f, float visibleHeight = 180.0f)
        {
            const float wheel = MouseInput::WheelPan(true);
            const float elapsed = std::clamp(seconds, 0.0f, 0.1f);
            const float travelLimit = std::max(240.0f - visibleHeight, 0.0f);
            const float target = std::clamp(playerY - visibleHeight * 0.5f, 0.0f, travelLimit);
            if (camera != nextCamera)
            {
                position = target;
                look = 0.0f;
                wheelLook = 0.0f;
                releaseWheel = false;
            }
            if (wheel != 0.0f)
            {
                wheelLook = std::clamp(wheelLook + wheel, -1.0f, 1.0f);
                wheelPlayerY = playerY;
                releaseWheel = false;
            }
            if (camera == nextCamera)
            {
                // Let the player move inside a small safe band before scrolling.
                const float difference = target - position;
                const float travel = std::copysign(std::max(std::abs(difference) - 8.0f, 0.0f), difference);
                // Remaining camera easing is not new player movement. Fresh
                // scrolling suspends it immediately, until the player moves
                // out of the wheel's own safe band and tracking resumes.
                if (std::abs(playerY - wheelPlayerY) > 8.0f
                    && std::abs(travel) > 0.1f && wheel == 0.0f) releaseWheel = true;
                const bool holdWheel = std::abs(wheelLook) >= 0.001f && !releaseWheel
                    && std::abs(rightStickY) <= 0.01f;
                if (!holdWheel) position += travel * (1.0f - std::exp(-6.0f * elapsed));
            }
            camera = nextCamera;
            position = std::clamp(position, 0.0f, travelLimit);
            // Full tilt can reveal either edge of the original image, wherever
            // automatic tracking has placed the crop. Partial tilt stays subtle.
            if (releaseWheel)
            {
                wheelLook *= std::exp(-3.0f * elapsed);
                if (std::abs(wheelLook) < 0.001f) { wheelLook = 0.0f; releaseWheel = false; }
            }
            // Hold a wheel adjustment while stationary; automatic tracking
            // smoothly takes over when the player moves beyond the safe band.
            // A deliberate right-stick tilt takes priority over wheel adjustment.
            const float stick = std::clamp(std::abs(rightStickY) > 0.01f ? rightStickY : wheelLook, -1.0f, 1.0f);
            const float targetLook = stick * (stick >= 0.0f ? position : travelLimit - position);
            look += (targetLook - look) * (1.0f - std::exp(-5.0f * elapsed));
            return std::clamp(position - look, 0.0f, travelLimit);
        }
    };

    struct Direction
    {
        float x = 0.0f, y = 0.0f;

        static Direction Digital(uint32_t held)
        {
            return { float(bool(held & 2)) - float(bool(held & 8)),
                float(bool(held & 1)) - float(bool(held & 4)) };
        }

        static Direction Analog(float x, float y)
        {
            // Radial dead zone retains every angle, rather than snapping each
            // axis separately into the native eight digital directions.
            const float length = std::hypot(x, y);
            constexpr float deadZone = 0.18f;
            if (length <= deadZone)
                return {};
            const float scale = std::min(1.0f, (length - deadZone) / (1.0f - deadZone)) / length;
            return { x * scale, y * scale };
        }

        bool Moving() const { return x * x + y * y > 0.000001f; }
    };

    struct Heading
    {
        Direction input{};
        float rightX = 1.0f, rightZ = 0.0f;
        bool valid = false;

        void Reset() { valid = false; }

        int16_t Update(Direction next, float cameraRightX, float cameraRightZ)
        {
            const float inputLength = std::hypot(next.x, next.y);
            if (inputLength > 0.001f)
            {
                next.x /= inputLength;
                next.y /= inputLength;
            }
            // Keep the world-space basis until the player changes the input.
            // Changing stick tilt to walk/run does not change the direction.
            // Holding a direction through a camera cut must not reverse movement.
            if (!valid || std::abs(next.x - input.x) + std::abs(next.y - input.y) > 0.2f)
            {
                const float length = std::hypot(cameraRightX, cameraRightZ);
                if (length > 0.01f)
                {
                    rightX = cameraRightX / length;
                    rightZ = cameraRightZ / length;
                }
                valid = true;
                input = next;
            }
            const float x = next.x * rightX - next.y * rightZ;
            const float z = next.x * rightZ + next.y * rightX;
            // Native walk velocity is local +X; the PSX yaw rotates clockwise.
            return int16_t(int(std::lround(std::atan2(-z, x) * (4096.0f / 6.28318530718f))) & 4095);
        }
    };
}
