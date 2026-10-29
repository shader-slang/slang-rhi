#!/usr/bin/env python3
"""Browser integration checks for the combined example runner.

Requires Playwright and Chromium. Pass a build output directory, or the packaged
site directory with --site. Use --browser-executable for a system browser.
"""

import argparse
import functools
import http.server
from pathlib import Path
import threading
from urllib.parse import parse_qs, quote, urlparse

from playwright.sync_api import sync_playwright


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_args):
        pass


def check_runner(browser, base_url, scale, site, screenshot_dir):
    page = browser.new_page(viewport={"width": 1000, "height": 720}, device_scale_factor=scale)
    errors = []
    wasm_requests = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.on("console", lambda message: errors.append(message.text) if message.type == "error" else None)
    page.on("request", lambda request: wasm_requests.append(request.url) if urlparse(request.url).path.endswith(".wasm") else None)
    page.set_default_timeout(120000)

    def wait_example(name):
        page.wait_for_function(
            """name => (Module.rhiExample === name && Module.rhiState === 'running' &&
                Module.rhiFrameCount >= 8) || ['stopped', 'failed'].includes(Module.rhiState) ||
                document.querySelector('#message').hasAttribute('data-error')""",
            arg=name,
        )
        assert not errors, errors
        assert page.evaluate("name => Module.rhiExample === name && Module.rhiState === 'running'", name), page.locator("#output").inner_text()
        assert parse_qs(urlparse(page.url).query)["example"] == [name]
        print(f"PASS {name}", flush=True)

    def select(name):
        index = names.index(name)
        page.select_option("#example-select", str(index))
        wait_example(name)
        assert page.locator("#example-select").input_value() == str(index)

    try:
        entry = "/index.html" if site else "/example-runner.html"
        page.goto(base_url + entry)
        wait_example("triangle")
        names = page.evaluate("Module.rhiExamples.map(example => example.name)")
        assert names == ["autodiff", "particles", "raster", "shader-toy", "surface", "triangle"], names
        assert page.locator("#example-picker").is_visible()

        # Every example must work after another example has released its resources.
        for name in ["raster", "autodiff", "particles", "shader-toy", "surface", "triangle"]:
            select(name)

        # History changes the existing runner, including returning to a direct link.
        select("raster")
        select("particles")
        page.go_back()
        wait_example("raster")
        page.go_back()
        wait_example("triangle")
        page.go_forward()
        wait_example("raster")
        page.go_forward()
        wait_example("particles")
        select("triangle")
        print("PASS Back/Forward navigation", flush=True)

        def cycle(key, expected):
            page.locator("#canvas").focus()
            page.keyboard.press("F5")
            page.keyboard.press(key)
            page.keyboard.press("Enter")
            wait_example(expected)

        # Navigate away and back while a new demo is still requesting its device.
        # Returning to that initializing demo can be coalesced without a callback;
        # the next keyboard selection must still update the URL.
        page.evaluate("""() => {
            const request = GPUAdapter.prototype.requestDevice;
            GPUAdapter.prototype.requestDevice = async function(...args) {
                GPUAdapter.prototype.requestDevice = request;
                await new Promise(resolve => setTimeout(resolve, 500));
                return request.apply(this, args);
            };
        }""")
        page.select_option("#example-select", str(names.index("raster")))
        page.wait_for_function("Module.rhiExample === 'raster' && Module.rhiState === 'loading'")
        page.go_back()
        page.go_forward()
        wait_example("raster")
        cycle("ArrowDown", "shader-toy")
        select("triangle")
        print("PASS history during initialization and subsequent keyboard selection", flush=True)

        cycle("ArrowUp", "surface")
        cycle("ArrowDown", "triangle")
        cycle("ArrowDown", "autodiff")
        cycle("ArrowUp", "triangle")

        # Escape cancels the picker without shutting down the example.
        page.keyboard.press("F5")
        page.keyboard.press("ArrowDown")
        page.keyboard.press("Escape")
        wait_example("triangle")
        select("surface")

        # Multiple DOM requests before the next frame coalesce, including a return
        # to the already active selection. They must not leave the UI stuck loading.
        page.evaluate("""() => {
            const select = document.querySelector('#example-select');
            for (const name of ['raster', 'particles', 'surface']) {
                select.value = String(Module.rhiExamples.findIndex(example => example.name === name));
                select.dispatchEvent(new Event('change'));
            }
        }""")
        page.wait_for_timeout(200)
        wait_example("surface")

        select("raster")
        frames = page.evaluate("Module.rhiFrameCount")
        page.set_viewport_size({"width": 800, "height": 640})
        page.wait_for_function("frames => Module.rhiFrameCount > frames + 5", arg=frames)
        def check_size():
            page.wait_for_function("""() => {
                const rect = document.querySelector('#viewport').getBoundingClientRect();
                return Module.canvas.width === Math.trunc(rect.width) * devicePixelRatio &&
                    Module.canvas.height === Math.trunc(rect.height) * devicePixelRatio;
            }""")
            assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")

        check_size()
        page.wait_for_function("Module.rhiFps > 0 && !document.querySelector('#fps').textContent.startsWith('--')")
        assert page.title() == "Raster | slang-rhi Examples"
        assert page.locator("header #fps").is_visible()
        assert page.locator("footer #fps").count() == 0
        assert page.locator("footer summary").all_text_contents() == ["Diagnostics"]
        assert page.locator("#source").get_attribute("href").endswith("/examples/raster")
        if screenshot_dir:
            page.screenshot(path=str(screenshot_dir / "viewer-desktop.png"), full_page=True)
        page.locator("#log summary").click()
        check_size()
        page.set_viewport_size({"width": 390, "height": 844})
        check_size()
        if screenshot_dir:
            page.screenshot(path=str(screenshot_dir / "viewer-narrow.png"), full_page=True)
        page.locator("#log summary").click()
        page.set_viewport_size({"width": 800, "height": 640})
        check_size()

        # Hidden containers suspend rendering and resume at the observed size.
        page.evaluate("document.querySelector('#viewport').style.display = 'none'")
        page.wait_for_function("Module.rhiViewportSize.width === 0 && !Module.rhiViewportSize.dirty")
        frames = page.evaluate("Module.rhiFrameCount")
        page.wait_for_timeout(100)
        assert page.evaluate("Module.rhiFrameCount") == frames
        page.evaluate("document.querySelector('#viewport').style.display = ''")
        check_size()
        page.wait_for_function("frames => Module.rhiFrameCount > frames + 5", arg=frames)

        # Page controls retain their keyboard events, and Tab can leave the canvas.
        page.locator("#canvas").focus()
        page.keyboard.press("Tab")
        assert page.locator("footer summary").evaluate("element => element === document.activeElement")
        page.keyboard.press("Escape")
        # F5 outside the canvas is a browser shortcut; intercept it in this test
        # at the DOM target so we can also verify that page handlers still run.
        page.locator("footer summary").evaluate("""element => {
            element.addEventListener('keydown', event => {
                if (event.key === 'F5') event.preventDefault();
            });
        }""")
        page.keyboard.press("F5")
        page.locator("#canvas").focus()
        page.keyboard.press("ArrowDown")
        page.keyboard.press("Enter")
        frames = page.evaluate("Module.rhiFrameCount")
        page.wait_for_function("frames => Module.rhiFrameCount > frames + 5", arg=frames)
        assert page.evaluate("Module.rhiExample === 'raster' && Module.rhiState === 'running'")
        print("PASS canvas focus, page keyboard controls, and hidden viewport", flush=True)
        page.locator("#canvas").focus()
        page.keyboard.press("F1")
        assert page.evaluate("Module.rhiShortcuts") == ["F1", "F4", "F5", "Escape"]

        # Raster is static with its overlay hidden. After releasing a drag over
        # the footer, merely moving back across the canvas must not rotate it.
        canvas = page.locator("#canvas")
        bounds = canvas.bounding_box()
        x, y = bounds["x"] + bounds["width"] / 2, bounds["y"] + bounds["height"] / 2
        footer = page.locator("footer").bounding_box()

        def settle():
            frames = page.evaluate("Module.rhiFrameCount")
            page.wait_for_function("frames => Module.rhiFrameCount > frames + 5", arg=frames)

        def assert_released():
            settle()
            before = canvas.screenshot()
            page.mouse.move(x + 80, y)
            settle()
            assert canvas.screenshot() == before, "Camera moved after the drag was released"

        for button in ["left", "right", "middle"]:
            page.mouse.move(x, y)
            page.mouse.down(button=button)
            page.mouse.move(x, footer["y"] + 20)
            page.mouse.up(button=button)
            assert_released()

        # Blurring the canvas or losing pointer capture may omit mouse-up.
        for cancel in [
            "document.querySelector('footer summary').focus()",
            "window.dispatchEvent(new Event('blur'))",
            "Module.canvas.releasePointerCapture(Module.testPointerId)",
            "Module.canvas.dispatchEvent(new PointerEvent('pointercancel', {pointerId: Module.testPointerId, pointerType: 'mouse'}))",
        ]:
            page.evaluate("() => { Module.canvas.onpointerdown = event => { Module.testPointerId = event.pointerId; }; }")
            page.mouse.move(x, y)
            page.mouse.down()
            # Capture requested by pointer-down becomes active on the next
            # pointer event; cancelling a pending request emits no lost-capture.
            page.mouse.move(x + 1, y)
            page.evaluate(cancel)
            assert_released()
            page.mouse.up()
        page.evaluate("Module.canvas.onpointerdown = null; delete Module.testPointerId")
        print("PASS mouse release outside canvas, blur, and lost capture", flush=True)
        select("triangle")

        # Switching uses the existing module and document: no second compiler download.
        assert len(wasm_requests) == 1, wasm_requests
        assert len(page.evaluate("performance.getEntriesByType('navigation')")) == 1
        page.keyboard.press("Escape")
        page.wait_for_function("Module.rhiState === 'stopped'")
        page.wait_for_function("document.querySelector('#example-select').disabled")
        assert not errors, errors
        print("PASS switching, wraparound, resize, controls, one WASM download, and shutdown", flush=True)

        # A close request racing a switch must win instead of opening another example.
        page.goto(base_url + entry + "?example=triangle")
        wait_example("triangle")
        page.evaluate("""() => {
            Module.rhiRequestedExample = Module.rhiExamples.findIndex(example => example.name === 'raster');
            const canvas = Module.canvas;
            canvas.focus();
            window.dispatchEvent(new KeyboardEvent('keydown', {key:'Escape', code:'Escape', keyCode:27, bubbles:true}));
        }""")
        page.wait_for_function("Module.rhiState === 'stopped'")
        assert not errors, errors
        print("PASS close during a pending switch", flush=True)

        # Device creation yields after the old GLFW window has been destroyed.
        # Escape must survive that interval and win over a subsequent selection.
        page.goto(base_url + entry + "?example=triangle")
        wait_example("triangle")
        page.evaluate("""() => {
            const request = GPUAdapter.prototype.requestDevice;
            GPUAdapter.prototype.requestDevice = async function(...args) {
                GPUAdapter.prototype.requestDevice = request;
                await new Promise(resolve => { Module.testResumeDevice = resolve; });
                delete Module.testResumeDevice;
                return request.apply(this, args);
            };
        }""")
        page.select_option("#example-select", str(names.index("raster")))
        page.wait_for_function("typeof Module.testResumeDevice === 'function'")
        page.locator("#canvas").focus()
        page.keyboard.press("Escape")
        page.evaluate("""() => {
            Module.rhiRequestedExample = Module.rhiExamples.findIndex(example => example.name === 'particles');
            Module.testResumeDevice();
        }""")
        page.wait_for_function("Module.rhiState === 'stopped'", timeout=10000)
        assert page.evaluate("Module.rhiExample === 'raster' && Module.rhiFrameCount === 0")
        assert not errors, errors
        print("PASS close while device creation is suspended", flush=True)

        page.goto(base_url + entry + "?example=raster")
        wait_example("raster")
        print("PASS direct demo link", flush=True)
    except Exception:
        print("Browser errors:", errors, flush=True)
        print(page.locator("#output").inner_text(), flush=True)
        raise
    finally:
        page.close()


def check_browser_support(browser, base_url, site):
    entry = "/index.html" if site else "/example-runner.html"
    for script, expected in [
        ("Object.defineProperty(navigator, 'gpu', {value: undefined})", "WebGPU is unavailable"),
        ("WebAssembly.Suspending = undefined", "WebAssembly JSPI"),
    ]:
        page = browser.new_page()
        requests = []
        page.on("request", lambda request: requests.append(request.url))
        try:
            page.add_init_script(script)
            page.goto(base_url + entry)
            assert expected in page.locator("#status").inner_text()
            assert not any(urlparse(url).path.endswith((".wasm", ".data", "example-runner.js")) for url in requests), requests
        finally:
            page.close()
    print("PASS unsupported browsers avoid runtime downloads", flush=True)


def check_lifecycle(browser, base_url, site):
    entry = "/index.html" if site else "/example-runner.html"
    page = browser.new_page()
    page.set_default_timeout(120000)

    def check_stable_status(collapse_diagnostics=False):
        # Let the viewer observe the terminal state before interacting with it.
        page.evaluate("async () => { for (let i = 0; i < 2; ++i) await new Promise(requestAnimationFrame); }")
        if collapse_diagnostics:
            assert page.locator("#log").evaluate("element => element.open")
            page.locator("#log summary").click()
        mutations = page.evaluate("""async () => {
            let mutations = 0;
            const observer = new MutationObserver(records => mutations += records.length);
            observer.observe(document.querySelector('#status'), {childList: true, characterData: true, subtree: true});
            for (let i = 0; i < 5; ++i) await new Promise(requestAnimationFrame);
            observer.disconnect();
            return mutations;
        }""")
        assert mutations == 0, "Unchanged status text kept updating the live region"
        if collapse_diagnostics:
            assert not page.locator("#log").evaluate("element => element.open"), "Diagnostics reopened after being collapsed"

    try:
        page.goto(base_url + entry)
        page.wait_for_function("Module.rhiState === 'running' && Module.rhiFrameCount >= 8")
        page.evaluate("""() => {
            Module.printErr('[Warning] A multiline diagnostic');
            Module.printErr('    continuation without a severity prefix');
            Module.printErr('[Error] A diagnostic is not a lifecycle transition');
        }""")
        frames = page.evaluate("Module.rhiFrameCount")
        page.wait_for_function("frames => Module.rhiFrameCount > frames + 5", arg=frames)
        assert page.evaluate("Module.rhiState === 'running'")
        assert page.locator("#example-select").is_enabled()
        assert page.locator("#message").is_hidden()

        # One failed selection falls back. The historical CLI error must not
        # turn a subsequent normal close into a failed browser lifecycle.
        page.evaluate("""() => {
            const request = GPUAdapter.prototype.requestDevice;
            GPUAdapter.prototype.requestDevice = function(...args) {
                GPUAdapter.prototype.requestDevice = request;
                return Promise.reject(new Error('Injected device failure'));
            };
        }""")
        raster = page.evaluate("Module.rhiExamples.findIndex(example => example.name === 'raster')")
        page.select_option("#example-select", str(raster))
        page.wait_for_function("document.querySelector('#output').textContent.includes('returning to Triangle')")
        page.wait_for_function("Module.rhiExample === 'triangle' && Module.rhiState === 'running' && Module.rhiFrameCount >= 8")
        assert page.locator("#example-select").is_enabled()
        assert parse_qs(urlparse(page.url).query)["example"] == ["triangle"]

        # The fallback must also clear pending navigation for subsequent canvas selections.
        page.locator("#canvas").focus()
        page.keyboard.press("F5")
        page.keyboard.press("ArrowUp")
        page.keyboard.press("Enter")
        page.wait_for_function("Module.rhiExample === 'surface' && Module.rhiState === 'running' && Module.rhiFrameCount >= 8")
        assert parse_qs(urlparse(page.url).query)["example"] == ["surface"]
        page.keyboard.press("Escape")
        page.wait_for_function("Module.rhiState === 'stopped'")
        page.wait_for_function("document.querySelector('#status').textContent.includes('Example stopped')")
        check_stable_status()

        page.goto(base_url + entry + "?example=invalid-example")
        page.wait_for_function("Module.rhiState === 'failed'")
        page.wait_for_function("document.querySelector('#message').hasAttribute('data-error')")
        assert "Unknown example" in page.locator("#output").inner_text()
        check_stable_status(collapse_diagnostics=True)

        # Download failures belong to the viewer; C++ never starts in this case.
        page.route("**/example-runner.js", lambda route: route.abort())
        page.goto(base_url + entry)
        page.wait_for_function("document.querySelector('#status').textContent.includes('Could not download')")
        assert page.locator("#example-select").is_disabled()
        assert page.locator("#restart").is_visible()
        check_stable_status(collapse_diagnostics=True)
        print("PASS diagnostics, failed selection recovery, terminal failure, and download failure", flush=True)
    except Exception:
        print(page.locator("#output").inner_text(), flush=True)
        raise
    finally:
        page.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--browser-executable")
    parser.add_argument("--scale", type=int, default=1, choices=[1, 2])
    parser.add_argument("--site", action="store_true", help="Test a packaged site instead of the development runner")
    parser.add_argument("--screenshot-dir", type=Path)
    args = parser.parse_args()
    if args.screenshot_dir:
        args.screenshot_dir.mkdir(parents=True, exist_ok=True)
    filename = "index.html" if args.site else "example-runner.html"
    if not (args.build_dir / filename).is_file():
        parser.error(f"Missing {filename} in {args.build_dir}; build the runner or site first.")
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), functools.partial(Handler, directory=str(args.build_dir.resolve().parent))
    )
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=args.browser_executable, headless=True)
            try:
                # Serve below a subpath, as on a repository's static site.
                base_url = f"http://127.0.0.1:{server.server_port}/{quote(args.build_dir.resolve().name)}"
                check_runner(browser, base_url, args.scale, args.site, args.screenshot_dir)
                check_browser_support(browser, base_url, args.site)
                check_lifecycle(browser, base_url, args.site)
            finally:
                browser.close()
    finally:
        server.shutdown()
        server.server_close()
        worker.join()


if __name__ == "__main__":
    main()
