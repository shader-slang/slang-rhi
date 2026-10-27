#pragma once

#include <slang-rhi.h>

namespace rhi {

inline WindowHandle getWindowHandleFromGLFW(GLFWwindow* window)
{
#if SLANG_WINDOWS_FAMILY && defined(GLFW_EXPOSE_NATIVE_WIN32)
    HWND hwnd = glfwGetWin32Window(window);
    return WindowHandle::fromHwnd(hwnd);
#elif SLANG_LINUX_FAMILY && (defined(GLFW_EXPOSE_NATIVE_X11) || defined(GLFW_EXPOSE_NATIVE_WAYLAND))
#if defined(GLFW_EXPOSE_NATIVE_WAYLAND)
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
        return WindowHandle::fromWaylandSurface(glfwGetWaylandDisplay(), glfwGetWaylandWindow(window));
#endif
#if defined(GLFW_EXPOSE_NATIVE_X11)
    if (glfwGetPlatform() == GLFW_PLATFORM_X11)
        return WindowHandle::fromXlibWindow(glfwGetX11Display(), glfwGetX11Window(window));
#endif
#elif SLANG_APPLE_FAMILY && defined(GLFW_EXPOSE_NATIVE_COCOA)
    id nswindow = glfwGetCocoaWindow(window);
    return WindowHandle::fromNSWindow(nswindow);
#endif
    return {};
}

} // namespace rhi
