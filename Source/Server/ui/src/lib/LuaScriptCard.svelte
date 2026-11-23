<script>
    // One of a layer's Lua scripts: its name, bypass switch, controls and
    // file status (docs/lua-scripts.md). Layer details and the strip's MIDI
    // FX panel both show scripts this way.
    import LuaParamControls from "./LuaParamControls.svelte";
    import { luaFileNote, useFileTitle, hasModifiedParams, paramMessages } from "./luaScripts.js";

    let {
        plugin,
        showPath = false,
        highlighted = false,
        onSetParam = (_name, _value) => {},
        onResetParams = () => {},
        onSetEnabled = (_enabled) => {},
        onUseFile = (_fileName) => {},
        onRemove = null,
        onGestureBegin = () => {},
        onGestureEnd = () => {},
    } = $props();

    const name = $derived(plugin?.name || "Unknown processor");
    const note = $derived(luaFileNote(plugin));
    const description = $derived(plugin?.description && plugin.description !== "No description" ? plugin.description : "");
    const enabled = $derived(plugin?.enabled !== false);
</script>

<article class="lua-card" class:bypassed={!enabled} class:error={!plugin?.loaded} class:highlighted
    aria-label={`${name}${enabled ? "" : " (bypassed)"}`}>
    <header>
        <div class="title">
            <strong>{name}</strong>
            {#if showPath && plugin?.filePath}<span>{plugin.filePath}</span>{/if}
        </div>
        {#if plugin?.loaded}
            <button class="switch" role="switch" aria-checked={enabled} aria-label={`${name} on`}
                title={enabled ? `Bypass ${name}: the layer's notes pass through untouched` : `Turn ${name} back on`}
                onclick={() => onSetEnabled(!enabled)}></button>
        {/if}
    </header>
    {#if description}<p class="description">{description}</p>{/if}
    {#if !plugin?.loaded}
        <p class="lua-note">This script didn't load, so the layer plays without it.{plugin?.loadError ? ` ${plugin.loadError}` : ""}</p>
    {/if}
    {#if plugin?.params?.length}
        <div class="controls">
            <LuaParamControls {plugin} onSet={onSetParam} {onGestureBegin} {onGestureEnd} />
        </div>
    {/if}
    {#each paramMessages(plugin) as message}
        <p class="lua-note">{message}</p>
    {/each}
    {#if note}<p class="lua-note">{note.text}</p>{/if}
    {#if note?.canUseFile || hasModifiedParams(plugin) || onRemove}
        <footer>
            {#if note?.canUseFile}
                <button title={useFileTitle(plugin)} onclick={() => onUseFile(plugin.fileName)}>Use new file</button>
            {/if}
            {#if hasModifiedParams(plugin)}
                <button title={`Put every ${name} parameter back to its default`} onclick={onResetParams}>Reset to defaults</button>
            {/if}
            {#if onRemove}<button onclick={onRemove}>Remove</button>{/if}
        </footer>
    {/if}
</article>

<style>
    .lua-card { display: flex; flex-direction: column; gap: 6px; padding: 10px 12px; border: 1px solid #334155; border-radius: 7px; background: #111c2e; }
    .lua-card.highlighted { box-shadow: inset 3px 0 0 #38bdf8; }
    .lua-card.error { border-color: #991b1b; }
    header { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
    .title { min-width: 0; display: flex; flex-direction: column; gap: 2px; }
    .title strong, .title span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .title strong { color: #e2e8f0; font-size: 0.86rem; }
    .title span { color: #64748b; font-size: 0.72rem; }
    .description { margin: 0; color: #64748b; font-size: 0.74rem; line-height: 1.4; }
    .bypassed .controls, .bypassed .description { opacity: 0.45; }
    .lua-note { margin: 0; color: #fbbf24; font-size: 0.74rem; line-height: 1.35; }
    footer { display: flex; flex-wrap: wrap; justify-content: flex-end; gap: 8px; margin-top: 2px; }
    footer button { min-height: 32px; padding: 5px 11px; border: 1px solid #475569; border-radius: 6px; background: #172337; color: #e2e8f0; font: inherit; font-size: 0.8rem; font-weight: 600; cursor: pointer; }
    footer button:hover { border-color: #7dd3fc; background: #1e3048; }
    footer button:focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
    .switch { position: relative; flex-shrink: 0; width: 34px; height: 19px; padding: 0; border: 0; border-radius: 10px; background: #334155; cursor: pointer; }
    .switch::after { content: ""; position: absolute; top: 2px; left: 2px; width: 15px; height: 15px; border-radius: 50%; background: #cbd5e1; transition: transform 0.15s; }
    .switch[aria-checked="true"] { background: #0e7490; }
    .switch[aria-checked="true"]::after { transform: translateX(15px); background: #e0f2fe; }
    .switch:focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
    @media (prefers-reduced-motion: reduce) { .switch::after { transition: none; } }
</style>
