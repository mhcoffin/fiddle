<script>
    // One row per parameter of a layer's Lua script: a switch, a knob, a
    // number field or a set of choices (docs/lua-scripts.md, "Parameters").
    // Values go to C++ through onSet; the mixer state brings them back.
    import Knob from "./Knob.svelte";
    import LuaParamField from "./LuaParamField.svelte";
    import { paramSteps, formatParamValue, parseParamText, isParamModified } from "./luaScripts.js";

    let {
        plugin,
        onSet = (_name, _value) => {},
        onGestureBegin = () => {},
        onGestureEnd = () => {},
    } = $props();

    // Ids unique to this instance: Layer details and the strip's panel can
    // show the same script at once.
    const uid = $props.id();
    const scriptName = $derived(plugin?.name || "Script");
    const withUnit = (param, value) => {
        const text = formatParamValue(param, value);
        return param.unit && text !== param.zeroLabel ? `${text} ${param.unit}` : text;
    };
    // A single change is its own Undo step, never merged with a nearby one.
    const commit = (name, value) => { onGestureBegin(); onSet(name, value); onGestureEnd(); };
</script>

<div class="lua-params">
    {#each plugin?.params ?? [] as param (param.name)}
        {@const modified = isParamModified(param)}
        {@const id = `${uid}-${plugin.index}-${param.name}`}
        {@const label = `${scriptName}: ${param.label}`}
        <div class="param-row" title={param.description || ""}>
            <div class="param-label">
                {#if modified}
                    <button class="modified" aria-label={`Reset ${param.label} to ${withUnit(param, param.default)}`}
                        title={`Changed from ${withUnit(param, param.default)}. Click to reset.`}
                        onclick={() => commit(param.name, param.default)}></button>
                {/if}
                {#if param.type === "bool" || param.control === "field" || (param.type === "enum" && param.options?.length > 4)}
                    <label for={id}>{param.label}</label>
                {:else}
                    <span id={`${id}-label`}>{param.label}</span>
                {/if}
            </div>
            <div class="param-control">
                {#if param.type === "bool"}
                    <button {id} class="switch" role="switch" aria-checked={Boolean(param.value)}
                        aria-label={label} onclick={() => commit(param.name, !param.value)}></button>
                {:else if param.type === "enum"}
                    {#if (param.options?.length ?? 0) <= 4}
                        <div class="choices" role="group" aria-labelledby={`${id}-label`}>
                            {#each param.options as option}
                                <button aria-pressed={param.value === option}
                                    onclick={() => { if (param.value !== option) commit(param.name, option); }}>{option}</button>
                            {/each}
                        </div>
                    {:else}
                        <select {id} value={param.value} onchange={(event) => commit(param.name, event.currentTarget.value)}>
                            {#each param.options as option}<option value={option}>{option}</option>{/each}
                        </select>
                    {/if}
                {:else if param.control === "field"}
                    <LuaParamField {param} {id} label={`${label}${param.unit ? ` in ${param.unit}` : ""}`}
                        onCommit={(value) => commit(param.name, value)} />
                {:else}
                    {@const steps = paramSteps(param)}
                    <Knob value={Number(param.value)} min={param.min} max={param.max}
                        step={steps.step} fineStep={steps.fineStep} unit={param.unit || ""}
                        {label} defaultValue={Number(param.default)}
                        format={(value) => formatParamValue(param, value)}
                        parse={(text) => parseParamText(param, text)}
                        oninput={(value) => onSet(param.name, value)}
                        {onGestureBegin} {onGestureEnd} />
                {/if}
            </div>
        </div>
    {/each}
</div>

<style>
    .lua-params { display: flex; flex-direction: column; gap: 6px; }
    .param-row { display: grid; grid-template-columns: minmax(0, 1fr) auto; align-items: center; gap: 12px; min-height: 40px; }
    .param-label { display: flex; align-items: center; gap: 7px; min-width: 0; }
    .param-label label, .param-label span { color: #cbd5e1; font-size: 0.82rem; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .modified { width: 8px; height: 8px; flex-shrink: 0; padding: 0; border: 0; border-radius: 50%; background: #38bdf8; cursor: pointer; }
    .modified:hover { box-shadow: 0 0 0 3px rgba(56, 189, 248, 0.25); }
    .param-control { display: flex; align-items: center; justify-content: flex-end; }
    .switch { position: relative; width: 34px; height: 19px; padding: 0; border: 0; border-radius: 10px; background: #334155; cursor: pointer; }
    .switch::after { content: ""; position: absolute; top: 2px; left: 2px; width: 15px; height: 15px; border-radius: 50%; background: #cbd5e1; transition: transform 0.15s; }
    .switch[aria-checked="true"] { background: #0e7490; }
    .switch[aria-checked="true"]::after { transform: translateX(15px); background: #e0f2fe; }
    .choices { display: flex; border: 1px solid #475569; border-radius: 6px; overflow: hidden; }
    .choices button { padding: 5px 9px; border: 0; border-left: 1px solid #334155; background: #111c2e; color: #cbd5e1; font: inherit; font-size: 0.78rem; cursor: pointer; }
    .choices button:first-child { border-left: 0; }
    .choices button:hover { background: #1e3048; }
    .choices button[aria-pressed="true"] { background: #0e3a52; color: #e0f2fe; }
    select { max-width: 170px; height: 34px; padding: 0 8px; border: 1px solid #475569; border-radius: 6px; background: #172337; color: #e2e8f0; font: inherit; font-size: 0.82rem; }
    button:focus-visible, select:focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
    @media (prefers-reduced-motion: reduce) { .switch::after { transition: none; } }
</style>
