#pragma once

#include "utils.h"
#include "text-renderer.h"
#include "profiler.h"

#include <slang.h>
#include <slang-rhi.h>

#include <GLFW/glfw3.h>

#include <memory>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

namespace rhi {

struct ExampleDesc;

namespace detail {
struct ExampleRunner;
} // namespace detail

// Base class for examples.
// Derive from this class to implement an example.
// The runner calls lifecycle methods and input callbacks on one dedicated thread per native example.
// Browser examples run on the main thread and yield between frames and during asynchronous GPU work.
// Use the window helpers below; GLFW window operations belong to the main thread.
class ExampleBase
{
public:
    // Constructs an example without creating device, window, or overlay resources.
    ExampleBase();
    // Destroys the window and releases the overlay; call shutdown() first.
    virtual ~ExampleBase();

    // Called to initialize the example for the specified device type.
    virtual Result init(DeviceType deviceType) = 0;
    // Called to shut down the example, including after a partially failed init().
    virtual void shutdown() = 0;
    // Called every frame to update the example. All examples receive the same time unless --free-running is used.
    virtual Result update(double time) = 0;
    // Called every frame to render the example.
    virtual Result draw() = 0;

    // Event callbacks

    // Called when the window is resized.
    virtual Result onResize(int width, int height, int framebufferWidth, int framebufferHeight) { return SLANG_OK; }
    // Called when the mouse is moved.
    virtual void onMousePosition(float x, float y) {}
    // Called when the mouse enters or leaves any backend window.
    virtual void onMouseEnter(bool entered) {}
    // Called when a mouse button is pressed or released.
    virtual void onMouseButton(int button, int action, int mods) {}
    // Called when the mouse wheel is scrolled.
    virtual void onScroll(float x, float y) {}
    // Called when a key is pressed, released, or repeated.
    virtual void onKey(int key, int scancode, int action, int mods) {}

    // Accessors for mouse state

    // Returns true if the specified mouse button is currently down.
    bool isMouseDown(int button) const
    {
        return (button >= 0 && button < SLANG_COUNT_OF(m_mouseDown)) ? m_mouseDown[button] : false;
    }

    // Returns the current mouse X position.
    float getMouseX() const { return m_mousePos[0]; }
    // Returns the current mouse Y position.
    float getMouseY() const { return m_mousePos[1]; }

    // Device management

    // Create a device and retain the missing features if the example requires
    // features that this backend does not support.
    // deviceExtensions supplies the optional DeviceDesc::next chain.
    Result createDevice(
        DeviceType deviceType,
        std::vector<Feature> requiredFeatures,
        std::vector<std::pair<std::string, std::string>> preprocessorMacros,
        IDevice** outDevice,
        const void* deviceExtensions = nullptr
    );

    // Window management

    // Creates a window using the registered title and the common initial size.
    // The initial layout reduces the size as needed to fit all backend windows.
    // Automatically appends device, adapter, FPS, and VSync information to the title.
    Result createWindow(IDevice* device);
    // Destroys the window.
    void destroyWindow();
    // Returns whether the window has been asked to close.
    bool shouldClose() const;
    // Returns the cached window size in the same coordinates as mouse input.
    int getWindowWidth() const { return m_windowWidth; }
    int getWindowHeight() const { return m_windowHeight; }

    // Surface management

    // Creates a surface for the window with the specified format.
    // Use Format::Undefined to use the preferred format.
    Result createSurface(IDevice* device, Format format, ISurface** outSurface);
    // Configures a surface using the shared VSync preference, including after resizing.
    Result configureSurface(ISurface* surface, SurfaceConfig config);
    // Presents the window surface and records application frame timing.
    Result presentSurface();
    // Applied by the runner between frames; input callbacks run on the owning worker.
    void toggleVsync()
    {
        m_vsync = !m_vsync;
        m_vsyncChanged = true;
    }

    // Returns the current frame statistics.
    const FrameStats& getFrameStats() const { return m_frameStats; }

    // Profiler

    // Initializes optional profiling of explicitly marked regions on the example's queue.
    // Initialize once; shutdown() must wait for submissions before destruction.
    Result initProfiler(IDevice* device);

    // Returns the profiler. Requires successful initProfiler().
    Profiler& getProfiler();

    // Toggles profiling independently of overlay visibility. Does nothing if uninitialized.
    void toggleProfiler();

    // Overlay utility

    // Initializes the optional text overlay for the output format.
    // The window content scale multiplies the native 6x9 font size, rounded to whole pixels.
    // Uses rasterization or a compute fallback, depending on the device and target.
    Result initOverlay(IDevice* device, Format format);

    // Clears accumulated overlay text. Call before composing a new frame's text, even if drawing is skipped.
    void clearOverlay();

    // Prints the common shortcuts as the first overlay line. Call after clearOverlay(),
    // before appending example-specific controls and information.
    void printOverlayHeader();

    // Prints the shared CameraController shortcuts.
    void printCameraControls();

    // Appends printf-formatted text; end each line with a newline, including the final status line.
    // ^0 restores the default color; ^1-^7 select red, green, yellow, blue, cyan, magenta, white.
    // Color persists until changed or clearOverlay(); unknown codes are printed literally.
    // Does nothing when hidden or uninitialized. Failed formatting is skipped without changing accumulated text.
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    void
    printOverlay(const char* format, ...);

    // Draws the picker when open, otherwise the accumulated overlay text, over the existing image.
    // Call after the example has finished rendering, before submission.
    // The picker ignores normal overlay visibility. The image must use the initialized output format.
    Result drawOverlay(ICommandEncoder* encoder, ITexture* image);

    // Returns whether the normal overlay is enabled, independently of the picker and initialization.
    bool isOverlayVisible() const { return m_overlayVisible; }
    // Toggles overlay visibility without creating or destroying its resources.
    void toggleOverlay() { m_overlayVisible = !m_overlayVisible; }

public:
    DeviceType m_deviceType = DeviceType::Default;
    std::vector<Feature> m_missingFeatures;
    GLFWwindow* m_window = nullptr;

    float m_mousePos[2] = {0.0f, 0.0f};
    bool m_mouseDown[3] = {false, false, false};

private:
    Result updateWindowTitle();
    Result applyVsync();

    const ExampleDesc* m_desc = nullptr;
    std::string m_windowTitle;
    FrameStats m_frameStats;
    bool m_vsync = true;
    bool m_vsyncChanged = false;
    ComPtr<ISurface> m_presentationSurface;
    ComPtr<ICommandQueue> m_presentationQueue;
    detail::ExampleRunner* m_runner = nullptr;
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    float m_contentScale = 1.f; // Use the X scale uniformly to preserve the bitmap font's aspect ratio.
    bool m_minimized = false;
    bool m_overlayVisible = true;
    std::string m_examplePickerText;
    std::unique_ptr<TextRenderer> m_textRenderer;
    std::unique_ptr<Profiler> m_profiler;

    friend struct detail::ExampleRunner;
};

// A stable identifier, display name, and factory shared by standalone and combined executables.
// Capability checks remain in init(), so there is only one source of feature requirements.
struct ExampleDesc
{
    const char* name;
    const char* title;
    const char* shaderSearchPath;
    ExampleBase* (*create)();
};

namespace detail {
struct ExampleRegistration
{
    explicit ExampleRegistration(ExampleDesc desc);
};

int runRegisteredExamples(int argc, const char** argv, std::string_view defaultExample = {});
} // namespace detail

} // namespace rhi

// Each translation unit registers its descriptor without constructing an example.
// Only standalone builds define main().
#if defined(EXAMPLE_COMBINED)
#define EXAMPLE_STANDALONE_MAIN()
#else
#define EXAMPLE_STANDALONE_MAIN()                                                                                      \
    int main(int argc, const char** argv)                                                                              \
    {                                                                                                                  \
        return rhi::detail::runRegisteredExamples(argc, argv);                                                         \
    }
#endif

#define EXAMPLE_MAIN(Example, Name, Title)                                                                             \
    namespace {                                                                                                        \
    const rhi::detail::ExampleRegistration register##Example({                                                         \
        Name,                                                                                                          \
        Title,                                                                                                         \
        EXAMPLE_DIR,                                                                                                   \
        []() -> rhi::ExampleBase*                                                                                      \
        {                                                                                                              \
            return new Example();                                                                                      \
        },                                                                                                             \
    });                                                                                                                \
    }                                                                                                                  \
    EXAMPLE_STANDALONE_MAIN()
