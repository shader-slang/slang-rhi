#pragma once

#include <slang-rhi.h>

namespace rhi {

inline WindowHandle getWindowHandleFromGLFW(GLFWwindow* window)
{
#if SLANG_WINDOWS_FAMILY && defined(GLFW_EXPOSE_NATIVE_WIN32)
    HWND hwnd = glfwGetWin32Window(window);
    return WindowHandle::FromHwnd(hwnd);
#elif SLANG_LINUX_FAMILY && defined(GLFW_EXPOSE_NATIVE_X11)
    Window xwindow = glfwGetX11Window(window);
    return WindowHandle::FromXWindow(0, xwindow);
#elif SLANG_APPLE_FAMILY && defined(GLFW_EXPOSE_NATIVE_COCOA)
    id nswindow = glfwGetCocoaWindow(window);
    return WindowHandle::FromNSWindow(nswindow);
#endif
    return WindowHandle{WindowHandle::Type::Unknown};
}

} // namespace rhi
