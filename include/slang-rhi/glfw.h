#include <slang-rhi.h>

namespace rhi {

WindowHandle getWindowHandleFromGLFW(GLFWwindow* window)
{
#if SLANG_APPLE_FAMILY
    id nswindow = glfwGetCocoaWindow(window);
    return WindowHandle::FromNSWindow(nswindow);
#else
#error "Unsupported platform!"
#endif
}

} // namespace rhi
