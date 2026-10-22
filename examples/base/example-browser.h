#pragma once

#include <cstddef>
#include <span>

struct GLFWwindow;

namespace rhi {
struct ExampleDesc;
}

// Browser-only DOM/viewer bridge. Scheduling and lifecycle decisions belong to the app and runner.
namespace rhi::detail {

// Install before glfwInit(), and restore after glfwTerminate().
void initializeBrowserInput();
void shutdownBrowserInput();
void publishBrowserExamples(std::span<const ExampleDesc> registry);
void publishBrowserExample(const ExampleDesc& desc, size_t index);
int takeBrowserRequestedExample();
bool takeBrowserCloseRequest();
void setBrowserState(const char* state);
void setBrowserAdapterName(const char* name);
void setBrowserFps(double fps);

// Apply the cached CSS size and return whether the viewport is visible. Keep
// the previous framebuffer while hidden; GLFW does not accept zero dimensions.
bool updateBrowserWindowSize(GLFWwindow* window, bool force = false);
// Publish frame completion and yield until the next browser animation frame.
void finishBrowserFrame();

} // namespace rhi::detail
