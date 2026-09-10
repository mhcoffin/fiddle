export function historyShortcut(event) {
    if (event.defaultPrevented || event.isComposing || event.altKey ||
        !(event.metaKey || event.ctrlKey)) return null;
    const target = event.target;
    if (target?.isContentEditable || target?.closest?.('[contenteditable="true"]') ||
        target?.tagName === "TEXTAREA" ||
        (target?.tagName === "INPUT" && !["range", "checkbox", "radio", "button"].includes(target.type)))
        return null;
    const key = event.key.toLowerCase();
    if (key === "z") return event.shiftKey ? "redo" : "undo";
    if (key === "y" && event.ctrlKey && !event.metaKey) return "redo";
    return null;
}

// Capturing listeners run before each control's oninput/onchange. One pointer
// drag (including pauses), held arrow key, or numeric commit is one transaction.
export function installHistoryGestures(root, send) {
    let active = false;
    const begin = () => {
        if (!active) { active = true; send("beginHistoryGesture"); }
    };
    const end = () => {
        if (active) { active = false; send("endHistoryGesture"); }
    };
    const range = (event) => event.target?.tagName === "INPUT" && event.target.type === "range";
    const pointerDown = (event) => { if (range(event)) begin(); };
    const keys = new Set(["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Home", "End", "PageUp", "PageDown"]);
    const keyDown = (event) => { if (range(event) && keys.has(event.key)) begin(); };
    const keyUp = (event) => { if (keys.has(event.key)) end(); };
    const change = (event) => {
        if (event.target?.tagName === "INPUT" && event.target.type === "number") {
            end(); begin(); queueMicrotask(end);
        }
    };
    const doubleClick = (event) => { if (range(event)) { end(); begin(); queueMicrotask(end); } };
    // Captured descendant blur also fires when a pointer press moves focus
    // onto the slider. That must not end the drag just begun on pointerdown.
    const blur = (event) => { if (event.target === root) end(); };
    const listeners = { pointerdown: pointerDown, pointerup: end, pointercancel: end,
        keydown: keyDown, keyup: keyUp, blur, change, dblclick: doubleClick };
    for (const [name, handler] of Object.entries(listeners)) root.addEventListener(name, handler, true);
    return () => {
        end();
        for (const [name, handler] of Object.entries(listeners)) root.removeEventListener(name, handler, true);
    };
}
