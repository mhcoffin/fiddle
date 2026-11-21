<script>
    // A number parameter typed as text (control = "field"): shows zero_label
    // for 0, and with randomize a New button (docs/lua-scripts.md). Like the
    // Knob, the backend's value wins unless the user is typing, so Undo and
    // refused values put the field back.
    import { untrack } from "svelte";
    import { formatParamValue, parseParamText, randomParamValue } from "./luaScripts.js";
    import { clamp } from "./knobMath.js";

    let { param, id, label, onCommit = (_value) => {} } = $props();

    let draft = $state("");
    let editing = false;

    $effect(() => {
        const shown = formatParamValue(param, param.value);
        untrack(() => { if (!editing) draft = shown; });
    });

    function commit() {
        if (!editing) return;
        editing = false;
        const typed = parseParamText(param, draft);
        if (typed !== null) {
            let next = clamp(typed, param.min, param.max);
            if (param.type === "int") next = Math.round(next);
            if (next !== Number(param.value)) onCommit(next);
        }
        draft = formatParamValue(param, param.value);
    }
    function cancel() {
        editing = false;
        draft = formatParamValue(param, param.value);
    }
    function keyDown(event) {
        if (event.key === "Enter") {
            event.preventDefault(); event.stopPropagation();
            commit(); event.currentTarget.blur();
        } else if (event.key === "Escape") {
            // Cancels the edit only; the panel around it stays open.
            event.preventDefault(); event.stopPropagation();
            cancel(); event.currentTarget.blur();
        }
    }
    const showsUnit = $derived(Boolean(param.unit) && !(param.zeroLabel && Number(param.value) === 0));
</script>

<div class="field">
    <input {id} type="text" inputmode="numeric" spellcheck="false" aria-label={label}
        bind:value={draft}
        onfocus={(event) => { editing = true; draft = String(param.value); event.currentTarget.select(); }}
        onblur={commit} onkeydown={keyDown} />
    {#if showsUnit}<span class="unit">{param.unit}</span>{/if}
    {#if param.randomize}
        <button class="new" title={`Pick another ${param.label.toLowerCase()}`}
            onclick={() => onCommit(randomParamValue(param))}>New</button>
    {/if}
</div>

<style>
    .field { display: flex; align-items: center; gap: 6px; }
    input { width: 76px; height: 34px; box-sizing: border-box; padding: 6px; border: 1px solid #475569; border-radius: 6px; background: #111c2e; color: #e2e8f0; font: inherit; font-size: 0.85rem; text-align: right; font-variant-numeric: tabular-nums; }
    input:focus { outline: 2px solid #38bdf8; outline-offset: 1px; }
    .unit { color: #cbd5e1; font-size: 0.85rem; }
    .new { height: 34px; padding: 0 10px; border: 1px solid #475569; border-radius: 6px; background: #172337; color: #e2e8f0; font: inherit; font-size: 0.78rem; font-weight: 600; cursor: pointer; }
    .new:hover { border-color: #7dd3fc; background: #1e3048; }
    .new:focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
</style>
