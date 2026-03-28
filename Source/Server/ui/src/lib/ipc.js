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

/** Registry: message type → array of handler functions */
const _handlers = {};

/**
 * Register a handler for messages sent from C++.
 * Multiple handlers per type are supported.
 * @param {string} type  - The message type string (e.g. "setMixerState")
 * @param {Function} handler - Called with msg.data as its sole argument
 * @returns {() => void} Unsubscribe function to remove this handler
 */
export function onFromCpp(type, handler) {
    if (!_handlers[type]) _handlers[type] = [];
    _handlers[type].push(handler);
    return () => {
        const arr = _handlers[type];
        if (arr) {
            const idx = arr.indexOf(handler);
            if (idx >= 0) arr.splice(idx, 1);
        }
    };
}

/**
 * Called by C++ via evaluateJavascript:
 *   window.__dispatchFromCpp({type: "xxx", data: <value>})
 * Routes to all registered handlers for that type.
 */
window.__dispatchFromCpp = function (msg) {
    const arr = _handlers[msg.type];
    if (arr && arr.length > 0) {
        for (const h of [...arr]) h(msg.data);
    } else {
        console.warn("[IPC] No handler for:", msg.type);
    }
};
