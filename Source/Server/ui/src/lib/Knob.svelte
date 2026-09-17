<script>
    import { onDestroy, untrack } from "svelte";
    import { clamp, knobKeyValue } from "./knobMath.js";

    let {
        value = 0, min = 0, max = 1, step = 0.5, fineStep = 0.1,
        label = "Value", unit = "", disabled = false,
        toPosition = (v) => (v - min) / (max - min),
        fromPosition = (p) => min + p * (max - min),
        format = (v) => v.toFixed(1),
        parse = (text) => text.trim() && Number.isFinite(Number(text)) ? Number(text) : null,
        oninput = () => {}, onGestureBegin = () => {}, onGestureEnd = () => {},
    } = $props();

    let current = $state(0), draft = $state("");
    let editing = false, dragging = false, keyActive = false, gesture = false;
    let pointerId = null, dragY = 0, dragPosition = 0, knob;
    let pending = null, frame = null;
    const position = $derived(clamp(toPosition(current), 0, 1));
    const angle = $derived(-135 + 270 * position);

    // Keep backend updates (including Undo) authoritative, without an echo
    // fighting an in-progress local drag or replacing partially typed text.
    $effect(() => {
        const next = clamp(value, min, max);
        untrack(() => {
            if (!dragging && !keyActive) current = next;
            if (!editing && !dragging && !keyActive) draft = format(next);
        });
    });
    $effect(() => { if (disabled) finishGesture(); });

    function beginGesture() {
        if (!gesture) { gesture = true; onGestureBegin(); }
    }
    function flush() {
        if (frame !== null) cancelAnimationFrame(frame);
        frame = null;
        if (pending !== null) { const next = pending; pending = null; oninput(next); }
    }
    function setValue(next) {
        next = clamp(Number((Math.round(next / fineStep) * fineStep).toFixed(6)), min, max);
        if (next === current) return;
        current = next;
        draft = format(next);
        pending = next;
        if (frame === null) frame = requestAnimationFrame(flush);
    }
    function finishGesture() {
        flush(); // The final edit must arrive before the end-history marker.
        dragging = false;
        keyActive = false;
        if (pointerId !== null && knob?.hasPointerCapture(pointerId)) knob.releasePointerCapture(pointerId);
        pointerId = null;
        if (gesture) { gesture = false; onGestureEnd(); }
    }
    function pointerDown(event) {
        if (disabled || event.button !== 0 || pointerId !== null) return;
        event.preventDefault();
        knob.focus(); // Commits any text edit before starting a new gesture.
        finishGesture();
        beginGesture();
        dragging = true;
        pointerId = event.pointerId;
        dragY = event.clientY;
        dragPosition = clamp(toPosition(current), 0, 1);
        knob.setPointerCapture(pointerId);
    }
    function pointerMove(event) {
        if (!dragging || event.pointerId !== pointerId) return;
        dragPosition = clamp(dragPosition + (dragY - event.clientY) / 180 * (event.shiftKey ? 0.1 : 1), 0, 1);
        dragY = event.clientY;
        setValue(fromPosition(dragPosition));
    }
    function keyDown(event) {
        if (disabled || dragging || event.metaKey || event.ctrlKey || event.altKey) return;
        const next = knobKeyValue(event.key, current, min, max, step, fineStep, event.shiftKey);
        if (next === null) return;
        event.preventDefault();
        beginGesture();
        keyActive = true;
        setValue(next);
    }
    function keyUp(event) {
        if (knobKeyValue(event.key, current, min, max, step, fineStep, event.shiftKey) !== null)
            finishGesture();
    }
    function commitText() {
        if (!editing) return;
        editing = false;
        const next = parse(draft);
        if (next !== null && Number.isFinite(next) && !disabled) {
            beginGesture();
            setValue(next);
            finishGesture();
        }
        draft = format(current);
    }
    function textKeyDown(event) {
        if (event.key === "Enter") {
            event.preventDefault(); event.stopPropagation();
            commitText(); event.currentTarget.blur();
        } else if (event.key === "Escape") {
            event.preventDefault(); event.stopPropagation();
            editing = false; draft = format(current); event.currentTarget.blur();
        }
    }
    onDestroy(finishGesture);
</script>

<svelte:window onblur={finishGesture} />

<div class="knob-control" class:disabled>
    <div class="knob" bind:this={knob} role="slider" tabindex={disabled ? -1 : 0}
        aria-label={label} aria-valuemin={min} aria-valuemax={max} aria-valuenow={current}
        aria-valuetext={`${format(current)} ${unit}`} aria-disabled={disabled} aria-orientation="vertical"
        title="Drag up or down. Hold Shift for fine adjustment. Arrow keys adjust; Home sets minimum; End sets maximum."
        onpointerdown={pointerDown} onpointermove={pointerMove}
        onpointerup={(event) => { if (event.pointerId === pointerId) finishGesture(); }}
        onpointercancel={(event) => { if (event.pointerId === pointerId) finishGesture(); }}
        onlostpointercapture={finishGesture} onkeydown={keyDown} onkeyup={keyUp}
        onblur={() => { if (keyActive) finishGesture(); }}>
        <svg viewBox="0 0 48 48" aria-hidden="true">
            <path class="track" d="M 9.86 38.14 A 20 20 0 1 1 38.14 38.14" pathLength="100" />
            <path class="value" d="M 9.86 38.14 A 20 20 0 1 1 38.14 38.14" pathLength="100"
                stroke-dasharray={`${position * 100} 100`} />
            <circle cx="24" cy="24" r="15" />
            <line x1="24" y1="13" x2="24" y2="19" transform={`rotate(${angle} 24 24)`} />
        </svg>
    </div>
    <label class="readout">
        <input type="text" inputmode="decimal" aria-label={`${label}${unit ? ` in ${unit}` : ""}`}
            {disabled} bind:value={draft} onfocus={() => { editing = true; }}
            onblur={commitText} onkeydown={textKeyDown} spellcheck="false" />
        {#if unit}<span>{unit}</span>{/if}
    </label>
</div>

<style>
    .knob-control { display: flex; align-items: center; gap: 8px; flex-shrink: 0; }
    .knob { width: 48px; height: 48px; border-radius: 50%; cursor: ns-resize; touch-action: none; user-select: none; }
    .knob:focus-visible { outline: 2px solid #7dd3fc; outline-offset: 3px; }
    svg { width: 100%; height: 100%; display: block; pointer-events: none; }
    path { fill: none; stroke-width: 3; stroke-linecap: round; }
    .track { stroke: #334155; }
    .value { stroke: #38bdf8; }
    circle { fill: #1e3048; stroke: #64748b; stroke-width: 1; }
    line { stroke: #e0f2fe; stroke-width: 2.5; stroke-linecap: round; }
    .knob:hover circle { fill: #29415e; stroke: #7dd3fc; }
    .readout { display: flex; align-items: center; gap: 5px; font-size: 0.85rem; color: #cbd5e1; }
    input { width: 64px; height: 34px; box-sizing: border-box; padding: 6px; border: 1px solid #475569; border-radius: 6px; background: #111c2e; color: #e2e8f0; font: inherit; text-align: right; font-variant-numeric: tabular-nums; }
    input:focus { outline: 2px solid #38bdf8; outline-offset: 1px; }
    .disabled { opacity: 0.45; }
    .disabled .knob { cursor: default; }
</style>
