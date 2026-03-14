// ── JS → C++ ──────────────────────────────────────────────────────────────────

export function dispatchCpp(type, ...args) {
    const win = window;

    // In browser test environment without JUCE
    if (!win.__JUCE__ || !win.__JUCE__.backend || typeof win.__JUCE__.backend.emitEvent !== "function") {
        console.warn(`[IPC Fallback] C++ backend not found for ${type}`, args);
        return;
    }

    // Use a monotonically increasing ID — we don't need the promise result
    // for fire-and-forget calls like signalReady, nativeLog, scanPlugins, etc.
    let nextId = Math.floor(Math.random() * 1000000);
    win.__JUCE__.backend.emitEvent("__juce__invoke", {
        name: "dispatchMessage",
        params: [{ type: type, payload: args }],
        resultId: nextId,
    });
}

// ── C++ → JS ──────────────────────────────────────────────────────────────────

/** Registry: message type → handler function */
const _handlers = {};

/**
 * Register a handler for messages sent from C++.
 * @param {string} type  - The message type string (e.g. "setMixerState")
 * @param {Function} handler - Called with msg.data as its sole argument
 */
export function onFromCpp(type, handler) {
    _handlers[type] = handler;
}

/**
 * Called by C++ via evaluateJavascript:
 *   window.__dispatchFromCpp({type: "xxx", data: <value>})
 * Routes to the registered handler for that type.
 */
window.__dispatchFromCpp = function (msg) {
    const h = _handlers[msg.type];
    if (h) {
        h(msg.data);
    } else {
        console.warn("[IPC] No handler for:", msg.type);
    }
};
