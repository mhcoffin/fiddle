---
name: fiddle-ipc
description: Message-passing patterns between the C++ backend (MainComponent) and the Svelte/JS frontend in FiddleServer. Covers JS → C++ dispatch and C++ → JS broadcast, including how to add new message types on both sides.
---

# FiddleServer IPC — C++ ↔ JS Message Bus

## Overview

All communication between the C++ backend (`MainComponent`) and the Svelte UI goes through a structured typed message bus. **Never inject raw JavaScript strings** — use the helpers below.

```
JS side                           C++ side
──────────────────────────────    ────────────────────────────────
dispatchCpp("type", ...args)  →   handleJsMessage(type, payload)
onFromCpp("type", handler)    ←   broadcastMessage("type", data)
```

---

## JS → C++

**Send a message from JS:**
```js
import { dispatchCpp } from "./lib/ipc.js";

dispatchCpp("myAction", arg1, arg2);
// payload arrives in C++ as a juce::Array<juce::var>: [arg1, arg2]
```

**Handle it in C++ (`MainComponent.cpp`, inside `handleJsMessage`):**
```cpp
if (type == "myAction") {
    juce::Array<juce::var> args;
    if (payload.isArray())
        args = *payload.getArray();
    // args[0], args[1], ...
    return;
}
```

---

## C++ → JS

**Send from C++ (`MainComponent.cpp`):**
```cpp
// data can be any juce::var: bool, int, string, Array, DynamicObject
broadcastMessage("myEvent", juce::var(someValue));
```

**Receive in a Svelte component (module-level `<script>` block):**
```js
import { onFromCpp } from "./lib/ipc.js";

onFromCpp("myEvent", (data) => {
    // data is the JS-decoded value (object, array, string, number…)
    myState = data;
});
```

> [!IMPORTANT]
> **Register `onFromCpp` handlers at the top of the component's `<script>` block**, not inside `onMount`. They run at component instantiation time, before `onMount` fires — ensuring they're ready when C++ pushes state in response to `signalReady`.

---

## Key Files

| File | Role |
|---|---|
| `Source/Server/ui/src/lib/ipc.js` | `dispatchCpp` + `onFromCpp` + `window.__dispatchFromCpp` router |
| `Source/Server/MainComponent.cpp` | `broadcastMessage()`, `handleJsMessage()` |
| `Source/Server/MainComponent.h` | `void broadcastMessage(const juce::String&, const juce::var&)` declaration |

---

## How `broadcastMessage` Works Under the Hood

```cpp
// MainComponent.cpp
void MainComponent::broadcastMessage(const juce::String &type, const juce::var &data) {
    // Wraps {type, data} as JSON and evaluates it in both WebViews
    juce::String js = "window.__dispatchFromCpp && window.__dispatchFromCpp("
                      + json + ")";
    broadcastJavascript(js);  // sends to main window + history window
}
```

The JS router in `ipc.js` dispatches to whichever `onFromCpp` handler is registered for that type.

---

## Adding a New Message Type

### New C++ → JS message
1. Call `broadcastMessage("myNewType", data)` wherever needed in `MainComponent.cpp`.
2. Register in the relevant Svelte component: `onFromCpp("myNewType", handler)`.

### New JS → C++ message
1. Call `dispatchCpp("myNewAction", ...args)` in the Svelte component.
2. Add a handler block in `handleJsMessage` in `MainComponent.cpp`.

---

## Common Mistakes

| Mistake | Fix |
|---|---|
| Using `getNative("functionName")` to call C++ | Use `dispatchCpp("functionName", ...args)`. `getNative` is a legacy low-level escape hatch that bypasses the message bus and often silently fails. Avoid it except where absolutely necessary. |
| `broadcastJavascript("if(window.setX) setX(...)")` | Use `broadcastMessage("setX", data)` |
| Registering `onFromCpp` inside `onMount` | Move it to the module-level `<script>` block |
| Forgetting to rebuild UI after adding a new `onFromCpp` | Run `cd Source/Server/ui && pnpm build` then `cmake --build build` |
