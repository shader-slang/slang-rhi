#include "example-browser.h"
#include "example-runner.h"

#include <emscripten.h>

namespace rhi::detail {
namespace {

// Emscripten's GLFW captures keys on window and prevents Tab's default action.
// Wrap its hooks before registration so page controls keep their normal DOM events.
// clang-format off
EM_JS_DEPS(exampleBrowserInput, "$GLFW");
EM_JS(void, installBrowserInputHooks, (), {
    const canvas = Module['canvas'];
    const handlers = {};
    Module['rhiCloseRequested'] = false;
    for (const name of ['onKeydown', 'onKeyup', 'onKeyPress']) {
        handlers[name] = GLFW[name];
        GLFW[name] = event => {
            if (document.activeElement !== canvas || event.key === 'Tab') return;
            // Between windows, GLFW drops input while device creation can yield.
            // Retain Escape for the app; an active window still handles picker cancellation.
            if (!GLFW.active && name === 'onKeydown' && event.key === 'Escape')
                Module['rhiCloseRequested'] = true;
            handlers[name](event);
        };
    }
    // GLFW listens for mouse-up on the canvas only. Capture mouse drags so a
    // release over page controls still reaches it, and clear held input on blur
    // or capture cancellation (which need not produce a mouse-up).
    let capturedPointer = null;
    let clientX = 0, clientY = 0;
    const trackPointer = event => { clientX = event.clientX; clientY = event.clientY; };
    const capturePointer = event => {
        trackPointer(event);
        canvas.focus();
        if (event.pointerType === 'mouse') {
            capturedPointer = event.pointerId;
            canvas.setPointerCapture(event.pointerId);
        }
    };
    const releaseButtons = () => {
        const buttons = GLFW.active?.buttons || 0;
        for (let button = 0; button < 3; ++button) {
            if (buttons & (1 << button)) {
                canvas.dispatchEvent(new MouseEvent('mouseup', {
                    button: [0, 2, 1][button], clientX, clientY, bubbles: true
                }));
            }
        }
    };
    const releaseInput = () => {
        GLFW.onBlur();
        releaseButtons();
        if (capturedPointer !== null && canvas.hasPointerCapture(capturedPointer))
            canvas.releasePointerCapture(capturedPointer);
        capturedPointer = null;
    };
    const releaseCapture = () => {
        capturedPointer = null;
        releaseButtons();
    };
    canvas.addEventListener('pointerdown', capturePointer);
    canvas.addEventListener('pointermove', trackPointer);
    canvas.addEventListener('lostpointercapture', releaseCapture);
    canvas.addEventListener('pointercancel', releaseCapture);
    canvas.addEventListener('blur', releaseInput);
    window.addEventListener('blur', releaseInput);
    Module['rhiRestoreInput'] = () => {
        releaseInput();
        canvas.removeEventListener('pointerdown', capturePointer);
        canvas.removeEventListener('pointermove', trackPointer);
        canvas.removeEventListener('lostpointercapture', releaseCapture);
        canvas.removeEventListener('pointercancel', releaseCapture);
        canvas.removeEventListener('blur', releaseInput);
        window.removeEventListener('blur', releaseInput);
        Object.assign(GLFW, handlers);
        delete Module['rhiCloseRequested'];
        delete Module['rhiRestoreInput'];
    };
});

EM_JS(void, restoreBrowserInputHooks, (), { Module['rhiRestoreInput']?.(); });
// clang-format on


// clang-format off
EM_JS(int, takeRequestedExampleIndex, (), {
    const index = Module['rhiRequestedExample'];
    Module['rhiRequestedExample'] = -1;
    return Number.isInteger(index) ? index : -1;
});
EM_JS(bool, takeCloseRequest, (), {
    const requested = Module['rhiCloseRequested'];
    Module['rhiCloseRequested'] = false;
    return !!requested;
});
// clang-format on

// clang-format off
EM_JS(int, takeViewportSize, (int* width, int* height, bool force), {
    const size = Module['rhiViewportSize'];
    if (!size || !Number.isFinite(size.width) || !Number.isFinite(size.height) ||
        size.width < 0 || size.height < 0 || size.width > 2147483647 || size.height > 2147483647)
        return 0;
    // GLFW treats zero as an unspecified dimension. Retain the framebuffer and
    // let the runner suspend drawing until the observer reports a visible size.
    if (size.width < 1 || size.height < 1) {
        size.dirty = false;
        return -1;
    }
    if (!force && !size.dirty) return 0;
    HEAP32[width >> 2] = Math.trunc(size.width);
    HEAP32[height >> 2] = Math.trunc(size.height);
    size.dirty = false;
    return 1;
});
// clang-format on


EM_ASYNC_JS(void, awaitBrowserAnimationFrame, (), { await new Promise(requestAnimationFrame); });

} // namespace

void initializeBrowserInput()
{
    installBrowserInputHooks();
}

void shutdownBrowserInput()
{
    restoreBrowserInputHooks();
}

void publishBrowserExamples(std::span<const ExampleDesc> registry)
{
    EM_ASM({ Module['rhiShortcuts'] = []; });
    for (const auto& shortcut : kExampleShortcuts)
        EM_ASM({ Module['rhiShortcuts'].push(UTF8ToString(arguments[0])); }, shortcut.keyName);
    EM_ASM({ Module['rhiExamples'] = []; });
    for (const auto& desc : registry)
    {
        EM_ASM(
            { Module['rhiExamples'].push({name : UTF8ToString(arguments[0]), title : UTF8ToString(arguments[1])}); },
            desc.name,
            desc.title
        );
    }
    EM_ASM({
        if (Module['onExamplesReady'])
            Module['onExamplesReady']();
    });
}

void publishBrowserExample(const ExampleDesc& desc, size_t index)
{
    EM_ASM(
        {
            Module['rhiExample'] = UTF8ToString(arguments[0]);
            Module['rhiFrameCount'] = 0;
            Module['rhiAdapterName'] = "";
            Module['rhiFps'] = 0;
            if (Module['onExampleChanged'])
                Module['onExampleChanged'](arguments[1]);
        },
        desc.name,
        int(index)
    );
}

int takeBrowserRequestedExample()
{
    return takeRequestedExampleIndex();
}

bool takeBrowserCloseRequest()
{
    return takeCloseRequest();
}

void setBrowserState(const char* state)
{
    EM_ASM({ Module['rhiState'] = UTF8ToString(arguments[0]); }, state);
}

void setBrowserAdapterName(const char* name)
{
    EM_ASM({ Module['rhiAdapterName'] = UTF8ToString(arguments[0]); }, name);
}

void setBrowserFps(double fps)
{
    EM_ASM({ Module['rhiFps'] = arguments[0]; }, fps);
}

bool updateBrowserWindowSize(GLFWwindow* window, bool force)
{
    int width = 0, height = 0;
    int result = takeViewportSize(&width, &height, force);
    if (result <= 0)
        return result == 0;
    int currentWidth, currentHeight;
    glfwGetWindowSize(window, &currentWidth, &currentHeight);
    if (width != currentWidth || height != currentHeight)
        glfwSetWindowSize(window, width, height);
    return true;
}

void finishBrowserFrame()
{
    EM_ASM({ ++Module['rhiFrameCount']; });
    awaitBrowserAnimationFrame();
}

} // namespace rhi::detail
