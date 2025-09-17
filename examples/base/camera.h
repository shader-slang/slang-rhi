#pragma once

#include "linalg.h"
using namespace linalg::aliases;

#include <slang-rhi/shader-cursor.h>

#include <glfw/glfw3.h>

#include <map>
#include <cstdint>

struct Camera
{
    uint32_t width{100};
    uint32_t height{100};
    float aspectRatio{1.0f};
    float3 position{1, 1, 1};
    float3 target{0, 0, 0};
    float3 fwd;
    float3 right;
    float3 up{0, 1, 0};
    float fov{70.0f};

    float3 imageU;
    float3 imageV;
    float3 imageW;

    Camera() { recompute(); }

    void recompute()
    {
        aspectRatio = float(width) / float(height);

        fwd = normalize(target - position);
        right = normalize(cross(fwd, up));
        up = normalize(cross(right, fwd));

        float fovRad = radians(fov);

        imageU = right * std::tan(fovRad * 0.5f) * aspectRatio;
        imageV = up * std::tan(fovRad * 0.5f);
        imageW = fwd;
    }

    void bind(rhi::ShaderCursor cursor) const
    {
        cursor["position"].setData(position);
        cursor["imageU"].setData(imageU);
        cursor["imageV"].setData(imageV);
        cursor["imageW"].setData(imageW);
    }
};


struct CameraController
{
    Camera* camera;
    bool mouseDown{false};
    float2 mousePos;
    std::map<int, bool> keyState;
    bool shiftDown{false};
    float3 moveDelta{0.f};
    float2 rotateDelta{0.f};
    float moveSpeed{1.0f};
    float rotateSpeed{0.002f};

    static constexpr float kMoveShiftFactor = 10.0f;
    static inline const std::map<int, float3> kMoveDirection = {
        {GLFW_KEY_A, float3(-1, 0, 0)},
        {GLFW_KEY_D, float3(1, 0, 0)},
        {GLFW_KEY_E, float3(0, 1, 0)},
        {GLFW_KEY_Q, float3(0, -1, 0)},
        {GLFW_KEY_W, float3(0, 0, 1)},
        {GLFW_KEY_S, float3(0, 0, -1)},
    };

    void setCamera(Camera* camera) { this->camera = camera; }

    bool update(float dt)
    {
        bool changed = false;
        float3 position = camera->position;
        float3 fwd = camera->fwd;
        float3 up = camera->up;
        float3 right = camera->right;

        // Move
        if (length(moveDelta) > 0)
        {
            float3 offset = right * moveDelta.x;
            offset += up * moveDelta.y;
            offset += fwd * moveDelta.z;
            float factor = shiftDown ? kMoveShiftFactor : 1.0f;
            offset *= moveSpeed * factor * dt;
            position += offset;
            changed = true;
        }

        // Rotate
        if (length(rotateDelta) > 0)
        {
            float yaw = std::atan2(fwd.z, fwd.x);
            float pitch = std::asin(fwd.y);
            yaw += rotateSpeed * rotateDelta.x;
            pitch -= rotateSpeed * rotateDelta.y;
            fwd = float3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
            rotateDelta = float2();
            changed = true;
        }

        if (changed)
        {
            camera->position = position;
            camera->target = position + fwd;
            camera->up = float3(0, 1, 0);
            camera->recompute();
        }

        return changed;
    }

    void onKey(int key, int action, int mods)
    {
        if (action == GLFW_PRESS || action == GLFW_RELEASE)
        {
            bool down = action == GLFW_PRESS;
            if (key == GLFW_KEY_A || key == GLFW_KEY_D || key == GLFW_KEY_W || key == GLFW_KEY_S || key == GLFW_KEY_Q ||
                key == GLFW_KEY_E)
            {
                keyState[key] = down;
            }
            else if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)
            {
                shiftDown = down;
            }
        }
        moveDelta = float3(0.f);
        for (auto& [key, state] : keyState)
        {
            if (state)
            {
                moveDelta += kMoveDirection.at(key);
            }
        }
    }

    void onMouseButton(int button, int action, int mods)
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            if (action == GLFW_PRESS)
            {
                mouseDown = true;
            }
            else if (action == GLFW_RELEASE)
            {
                mouseDown = false;
            }
        }
    }

    void onMousePosition(float x, float y)
    {
        float2 pos = float2(x, y);
        if (mouseDown)
        {
            rotateDelta += pos - mousePos;
        }
        mousePos = pos;
    }
};
