#!/usr/bin/env python3
"""Browser integration checks for the combined and standalone example runners.

Requires Playwright and Chromium. Pass the directory containing example-runner.html
and example-triangle.html, and optionally --browser-executable for a system browser.
"""

import argparse
import functools
import http.server
from pathlib import Path
import threading
from urllib.parse import parse_qs, urlparse

from playwright.sync_api import sync_playwright


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_args):
        pass


def check_runner(browser, base_url, scale):
    page = browser.new_page(viewport={"width": 1000, "height": 720}, device_scale_factor=scale)
    errors = []
    wasm_requests = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.on("console", lambda message: errors.append(message.text) if message.type == "error" else None)
    page.on("request", lambda request: wasm_requests.append(request.url) if urlparse(request.url).path.endswith(".wasm") else None)
    page.set_default_timeout(120000)

    def wait_example(name):
        page.wait_for_function(
            """name => (Module.rhiExample === name && Module.rhiReady &&
                Module.rhiFrameCount >= 8 && !Module.rhiSwitching && !Module.rhiStopped) ||
                Module.rhiStopped || document.querySelector('#log').open""",
            arg=name,
        )
        assert not errors, errors
        assert page.evaluate("Module.rhiReady && !Module.rhiStopped"), page.locator("#output").inner_text()
        assert parse_qs(urlparse(page.url).query)["example"] == [name]
        print(f"PASS {name}", flush=True)

    def select(name):
        index = names.index(name)
        page.select_option("#example-select", str(index))
        wait_example(name)
        assert page.locator("#example-select").input_value() == str(index)

    try:
        page.goto(base_url + "/example-runner.html?example=triangle")
        wait_example("triangle")
        names = page.evaluate("Module.rhiExamples.map(example => example.name)")
        assert names == ["surface", "triangle", "shader-toy", "raster", "particles"], names
        assert page.locator("#example-picker").is_visible()

        # Every example must work after another example has released its resources.
        for name in ["raster", "particles", "shader-toy", "surface", "triangle"]:
            select(name)

        page.locator("#canvas").focus()
        page.keyboard.press("F4")
        wait_example("shader-toy")
        page.keyboard.press("Shift+F4")
        wait_example("triangle")
        select("surface")
        page.keyboard.press("Shift+F4")
        wait_example("particles")
        page.keyboard.press("F4")
        wait_example("surface")

        # Multiple DOM requests before the next frame coalesce, including a return
        # to the already active selection. They must not leave the UI stuck loading.
        page.evaluate("""() => {
            const select = document.querySelector('#example-select');
            for (const value of ['3', '4', '0']) {
                select.value = value;
                select.dispatchEvent(new Event('change'));
            }
        }""")
        page.wait_for_timeout(200)
        wait_example("surface")

        select("raster")
        frames = page.evaluate("Module.rhiFrameCount")
        page.set_viewport_size({"width": 800, "height": 640})
        page.wait_for_function("frames => Module.rhiFrameCount > frames + 5", arg=frames)
        size = page.evaluate("({width: Module.canvas.width, height: Module.canvas.height})")
        assert size == {"width": 800 * scale, "height": 530 * scale}, size
        page.locator("#canvas").focus()
        page.keyboard.press("F1")
        page.keyboard.press("F3")
        select("triangle")

        # Switching uses the existing module and document: no second compiler download.
        assert len(wasm_requests) == 1, wasm_requests
        assert len(page.evaluate("performance.getEntriesByType('navigation')")) == 1
        page.keyboard.press("Escape")
        page.wait_for_function("Module.rhiStopped")
        page.wait_for_function("document.querySelector('#example-select').disabled")
        assert not errors, errors
        print("PASS switching, wraparound, resize, controls, one WASM download, and shutdown", flush=True)

        # A close request racing a switch must win instead of opening another example.
        page.goto(base_url + "/example-runner.html?example=triangle")
        wait_example("triangle")
        page.evaluate("""() => {
            Module.rhiRequestedExample = 3;
            const canvas = Module.canvas;
            canvas.focus();
            window.dispatchEvent(new KeyboardEvent('keydown', {key:'Escape', code:'Escape', keyCode:27, bubbles:true}));
        }""")
        page.wait_for_function("Module.rhiStopped")
        assert not errors, errors
        print("PASS close during a pending switch", flush=True)

        # Standalone executables retain a single entry and no example selector.
        page.goto(base_url + "/example-triangle.html")
        page.wait_for_function("Module.rhiReady && Module.rhiFrameCount >= 8")
        assert page.evaluate("Module.rhiExamples.map(example => example.name)") == ["triangle"]
        assert page.locator("#example-picker").is_hidden()
        page.locator("#canvas").focus()
        page.keyboard.press("F4")
        assert page.evaluate("Module.rhiExample") == "triangle"
        page.keyboard.press("Escape")
        page.wait_for_function("Module.rhiStopped")
        assert not errors, errors
        print("PASS standalone example", flush=True)
    except Exception:
        print("Browser errors:", errors, flush=True)
        print(page.locator("#output").inner_text(), flush=True)
        raise
    finally:
        page.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--browser-executable")
    parser.add_argument("--scale", type=int, default=1, choices=[1, 2])
    args = parser.parse_args()
    for filename in ["example-runner.html", "example-triangle.html"]:
        if not (args.build_dir / filename).is_file():
            parser.error(f"Missing {filename} in {args.build_dir}; build both targets first.")
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", 0), functools.partial(Handler, directory=str(args.build_dir.resolve()))
    )
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=args.browser_executable, headless=True)
            try:
                check_runner(browser, f"http://127.0.0.1:{server.server_port}", args.scale)
            finally:
                browser.close()
    finally:
        server.shutdown()
        server.server_close()
        worker.join()


if __name__ == "__main__":
    main()
