// Module must be global: Emscripten reads it when the generated script loads.
var Module = {rhiState : 'loading'};

(() => {
  const byId = id => document.getElementById(id);
  const canvas = byId('canvas');
  const select = byId('example-select');
  const message = byId('message');
  const status = byId('status');
  const repository = 'https://github.com/shader-slang/slang-rhi';
  const initialExample = new URL(location.href).searchParams.get('example');
  let currentExample = null;
  let navigationTarget = null;
  let runtimeError = null;
  let failureShown = false;

  function showStatus(text, error = false) {
    message.hidden = false;
    message.toggleAttribute('data-error', error);
    if (status.textContent !== text) status.textContent = text;
  }

  function report(text, level = 'info') {
    // Bound diagnostics for a long-running demo or a repeating browser warning.
    byId('output').textContent = (byId('output').textContent + text + '\n').slice(-32000);
    if (level === 'error') {
      byId('log').open = true;
      console.error(text);
    } else if (level === 'warning') {
      console.warn(text);
    } else {
      console.log(text);
    }
  }

  // Logs are diagnostics, not lifecycle transitions. In particular, Emscripten
  // splits multiline stderr messages into lines without repeating their prefix.
  function reportOutput(text, fallbackLevel) {
    const level = /^\[Error\]|^FAILED:/.test(text) ? 'error' :
      /^\[Warning\]/.test(text) ? 'warning' : fallbackLevel;
    report(text, level);
  }

  // JavaScript owns failures that prevent the runtime from reporting its state.
  function failRuntime(reason) {
    runtimeError = String(reason);
    report(runtimeError, 'error');
    showStatus(runtimeError, true);
    select.disabled = true;
    byId('restart').hidden = false;
  }

  function updateURL(name, replace) {
    const url = new URL(location.href);
    if (url.searchParams.get('example') === name) return;
    url.searchParams.set('example', name);
    history[replace ? 'replaceState' : 'pushState'](null, '', url);
  }

  function requestExample(name) {
    const index = Module.rhiExamples?.findIndex(example => example.name === name);
    if (index === undefined || index < 0) return;
    // A request for the demo already starting may be coalesced by C++ without
    // another onExampleChanged callback, so it must not leave a pending target.
    navigationTarget = Module.rhiExample === name ? null : name;
    Module.rhiRequestedExample = index;
  }

  Object.assign(Module, {
    canvas,
    arguments: initialExample ? ['--example', initialExample] : [],
    onExamplesReady() {
      select.replaceChildren(...Module.rhiExamples.map((example, index) => {
        const option = document.createElement('option');
        option.value = String(index);
        option.textContent = example.title;
        return option;
      }));
      byId('example-picker').hidden = Module.rhiExamples.length < 2;
      // Back/Forward may have been used while the compiler was downloading.
      const requested = new URL(location.href).searchParams.get('example');
      if (requested !== initialExample) requestExample(requested || 'triangle');
    },
    onExampleChanged(index) {
      const example = Module.rhiExamples[index];
      byId('restart').hidden = true;
      showStatus('Loading ' + example.title + '...');
      select.value = String(index);
      document.title = example.title + ' | slang-rhi Examples';
      canvas.setAttribute('aria-label', example.title + ' interactive example');
      byId('source').href = repository + '/tree/main/examples/' + encodeURIComponent(example.name);
      if (Module.rhiExamples.length > 1) {
        // A pending navigation owns the URL until its requested demo starts.
        // Intermediate selections can finish while a newer request is queued.
        if (!navigationTarget) updateURL(example.name, currentExample === null);
        else if (navigationTarget === example.name) navigationTarget = null;
      }
      currentExample = example.name;
    },
    print: text => reportOutput(text, 'info'),
    printErr: text => reportOutput(text, 'warning'),
    onAbort: failRuntime,
    onExit: code => { if (code) report('Exited with code ' + code, 'error'); },
    setStatus: text => { if (!runtimeError && Module.rhiState !== 'failed' && text) showStatus(text); }
  });

  select.addEventListener('change', () => {
    const example = Module.rhiExamples[Number(select.value)];
    updateURL(example.name, false);
    requestExample(example.name);
    canvas.focus();
  });
  window.addEventListener('popstate', () => {
    if (runtimeError || ['stopped', 'failed'].includes(Module.rhiState)) {
      location.reload();
      return;
    }
    requestExample(new URL(location.href).searchParams.get('example') || 'triangle');
  });
  window.addEventListener('error', event => failRuntime(event.message));
  window.addEventListener('unhandledrejection', event => failRuntime(event.reason));
  canvas.addEventListener('contextmenu', event => event.preventDefault());
  window.addEventListener('keydown', event => {
    if (document.activeElement === canvas &&
        (Module.rhiShortcuts?.includes(event.key) ||
         [' ', 'ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key)))
      event.preventDefault();
  }, true);

  function updateStatus() {
    const state = Module.rhiState;
    const failed = !!runtimeError || state === 'failed';
    const running = !runtimeError && state === 'running' && Module.rhiFrameCount > 0;
    select.disabled = failed || state === 'stopped';
    byId('restart').hidden = !select.disabled;
    byId('fps').hidden = !running;
    byId('adapter').hidden = !Module.rhiAdapterName;
    if (byId('adapter').textContent !== (Module.rhiAdapterName || ''))
      byId('adapter').textContent = Module.rhiAdapterName || '';
    if (failed) {
      showStatus(runtimeError || 'Unable to run this example. See Diagnostics below.', true);
      if (!failureShown) byId('log').open = true;
    } else if (running) {
      message.hidden = true;
      const fpsText = Module.rhiFps > 0 ? Module.rhiFps.toFixed(1) + ' FPS' : '-- FPS';
      if (byId('fps').textContent !== fpsText) byId('fps').textContent = fpsText;
    } else if (state === 'stopped') {
      showStatus('Example stopped. Reload to restart.');
    } else if (state === 'loading' || state === 'stopping') {
      showStatus('Loading example...');
    }
    failureShown = failed;
    requestAnimationFrame(updateStatus);
  }

  // Check support before downloading the compiler and assets.
  let unsupported;
  if (!window.isSecureContext) unsupported = 'These examples require HTTPS or localhost.';
  else if (!navigator.gpu) unsupported = 'WebGPU is unavailable. Use a browser with WebGPU enabled.';
  else if (typeof WebAssembly === 'undefined' || typeof WebAssembly.Suspending !== 'function' || typeof WebAssembly.promising !== 'function')
    unsupported = 'These examples require WebAssembly JSPI. Please update your browser.';
  if (unsupported) {
    showStatus(unsupported, true);
    return;
  }

  // Cache layout changes without entering WASM from an observer callback. The
  // runner applies the latest size between frames, including after a switch.
  const viewport = byId('viewport');
  if (viewport) {
    const cacheSize = (width, height) => {
      width = Math.trunc(width);
      height = Math.trunc(height);
      const previous = Module.rhiViewportSize;
      if (!previous || previous.width !== width || previous.height !== height)
        Module.rhiViewportSize = { width, height, dirty: true };
    };
    const rect = viewport.getBoundingClientRect();
    cacheSize(rect.width, rect.height);
    const observer = new ResizeObserver(entries => {
      const { width, height } = entries[entries.length - 1].contentRect;
      cacheSize(width, height);
    });
    observer.observe(viewport);
  }
  requestAnimationFrame(updateStatus);
  for (const original of byId('runtime-script').content.querySelectorAll('script')) {
    const script = document.createElement('script');
    for (const attribute of original.attributes) script.setAttribute(attribute.name, attribute.value);
    script.textContent = original.textContent;
    script.onerror = () => failRuntime('Could not download the example runtime. Reload to retry.');
    document.body.append(script);
  }
})();
