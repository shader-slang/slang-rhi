#pragma once

#include "math.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cstdint>

namespace rhi {

struct Camera
{
    uint32_t width = 100;
    uint32_t height = 100;
    float aspectRatio = 1.0f;
    math::float3 position = {1, 1, 1};
    math::float3 target = {0, 0, 0};
    math::float3 fwd;
    math::float3 right;
    math::float3 up = {0, 1, 0};
    float fov = 70.0f;

    math::float3 imageU;
    math::float3 imageV;
    math::float3 imageW;

    Camera() { recompute(); }

    void recompute()
    {
        aspectRatio = float(width) / float(height);

        fwd = math::normalize(target - position);
        right = math::normalize(math::cross(fwd, up));
        up = math::normalize(math::cross(right, fwd));

        float fovRad = math::radians(fov);

        imageU = right * std::tan(fovRad * 0.5f) * aspectRatio;
        imageV = up * std::tan(fovRad * 0.5f);
        imageW = fwd;
    }
};

struct FlyCameraController
{
    Camera* camera = nullptr;
    bool mouseDown = false;
    math::float2 mousePos;
    enum class Movement
    {
        Left,
        Right,
        Up,
        Down,
        Forward,
        Backward
    };
    std::array<bool, 6> movement = {};
    std::array<bool, 2> shiftDown = {};
    math::float3 moveDelta = {};
    math::float2 rotateDelta = {};
    float moveSpeed = 1.0f;
    float rotateSpeed = 0.002f;

    static constexpr float kMoveShiftFactor = 10.0f;
    void setCamera(Camera* value) { camera = value; }

    // Clear held input and pending motion, preserving the camera, speeds, and last cursor position.
    void reset()
    {
        mouseDown = false;
        movement.fill(false);
        shiftDown.fill(false);
        moveDelta = {};
        rotateDelta = {};
    }

    void setMovement(Movement direction, bool down)
    {
        movement[size_t(direction)] = down;
        moveDelta = {
            float(movement[1]) - float(movement[0]),
            float(movement[2]) - float(movement[3]),
            float(movement[4]) - float(movement[5])
        };
    }

    // Interpret forwarded events only; GLFW/window access stays on the main thread.
    // Return true for camera bindings so callers can handle other controls themselves.
    bool onKey(int key, int action)
    {
        bool down = action != GLFW_RELEASE;
        switch (key)
        {
        case GLFW_KEY_A:
            setMovement(Movement::Left, down);
            break;
        case GLFW_KEY_D:
            setMovement(Movement::Right, down);
            break;
        case GLFW_KEY_E:
            setMovement(Movement::Up, down);
            break;
        case GLFW_KEY_Q:
            setMovement(Movement::Down, down);
            break;
        case GLFW_KEY_W:
            setMovement(Movement::Forward, down);
            break;
        case GLFW_KEY_S:
            setMovement(Movement::Backward, down);
            break;
        case GLFW_KEY_LEFT_SHIFT:
            shiftDown[0] = down;
            break;
        case GLFW_KEY_RIGHT_SHIFT:
            shiftDown[1] = down;
            break;
        default:
            return false;
        }
        return true;
    }

    bool onMouseButton(int button, int action)
    {
        if (button != GLFW_MOUSE_BUTTON_LEFT)
            return false;
        mouseDown = action != GLFW_RELEASE;
        return true;
    }

    void onMousePosition(float x, float y)
    {
        math::float2 pos = math::float2(x, y);
        if (mouseDown)
        {
            rotateDelta += pos - mousePos;
        }
        mousePos = pos;
    }

    bool update(float dt)
    {
        if (!camera)
            return false;
        bool changed = false;
        math::float3 position = camera->position;
        math::float3 fwd = camera->fwd;
        math::float3 up = camera->up;
        math::float3 right = camera->right;

        // Move
        if (math::length(moveDelta) > 0)
        {
            math::float3 offset = right * moveDelta.x;
            offset += up * moveDelta.y;
            offset += fwd * moveDelta.z;
            float factor = (shiftDown[0] || shiftDown[1]) ? kMoveShiftFactor : 1.0f;
            offset = math::normalize(offset) * moveSpeed * factor * dt;
            position += offset;
            changed = true;
        }

        // Rotate
        if (math::length(rotateDelta) > 0)
        {
            float yaw = std::atan2(fwd.z, fwd.x);
            float pitch = std::asin(std::clamp(fwd.y, -1.0f, 1.0f));
            yaw += rotateSpeed * rotateDelta.x;
            const float maxPitch = math::kPI * 0.5f - 1e-3f;
            pitch = std::clamp(pitch - rotateSpeed * rotateDelta.y, -maxPitch, maxPitch);
            fwd = math::float3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
            rotateDelta = math::float2();
            changed = true;
        }

        if (changed)
        {
            camera->position = position;
            camera->target = position + fwd;
            camera->up = math::float3(0, 1, 0);
            camera->recompute();
        }

        return changed;
    }
};

} // namespace rhi
