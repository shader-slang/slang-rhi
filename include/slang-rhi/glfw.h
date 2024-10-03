#include <slang-rhi.h>

namespace rhi {

WindowHandle getWindowHandleFromGLFW(GLFWwindow* window)
{
#if SLANG_WINDOWS_FAMILY
    HWND hwnd = glfwGetWin32Window(window);
    return WindowHandle::FromHwnd(hwnd);
#elif SLANG_LINUX_FAMILY
    Window xwindow = glfwGetX11Window(window);
    return WindowHandle::FromXWindow(0, xwindow);
#elif SLANG_APPLE_FAMILY
    id nswindow = glfwGetCocoaWindow(window);
    return WindowHandle::FromNSWindow(nswindow);
#else
#error "Unsupported platform!"
#endif
}

} // namespace rhi
