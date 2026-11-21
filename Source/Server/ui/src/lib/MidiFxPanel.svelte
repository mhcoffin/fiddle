<script>
    // A layer's MIDI FX: its Lua scripts in chain order, each with its
    // controls (docs/lua-scripts.md, "Controls"). A floating panel that stays
    // open while the user plays, solos or rides a fader: it opens beside the
    // strip, moves by its header, and closes with × or Escape, never on an
    // outside click. Clicking another strip's band shows that layer instead.
    import { tick } from "svelte";
    import { portal } from "./portal.js";
    import LuaScriptCard from "./LuaScriptCard.svelte";

    let {
        strip,
        anchor = null,          // the band's bounding rect when it was opened
        focusIndex = -1,        // the script to show first, or -1
        focusToken = 0,         // changes on every open, so the same script scrolls again
        catalog = [],
        position = null,        // where the user dragged it, kept for the session
        onMove = (_position) => {},
        onClose = () => {},
        onAdd = (_filePath) => {},
        onRemove = (_index) => {},
        onSetParam = (_index, _name, _value) => {},
        onResetParams = (_index) => {},
        onSetEnabled = (_index, _enabled) => {},
        onUseFile = (_fileName) => {},
        onGestureBegin = () => {},
        onGestureEnd = () => {},
    } = $props();

    const WIDTH = 320, GAP = 8, MARGIN = 8;
    let panel = $state(null);
    let drag = null;

    const scripts = $derived(strip?.luaPlugins ?? []);
    const title = $derived(strip?.layerName || strip?.library || "Layer");
    let viewport = $state({ width: window.innerWidth, height: window.innerHeight });

    // Beside the band: to the right of the strip, or to its left when there's
    // no room.
    function placeBeside(rect, width) {
        if (!rect) return { left: MARGIN, top: 80 };
        const right = rect.right + GAP;
        return { left: right + WIDTH <= width - MARGIN ? right : rect.left - GAP - WIDTH, top: rect.top };
    }
    // Always on screen, even after the window shrinks under a dragged position.
    const placed = $derived.by(() => {
        const wanted = position ?? placeBeside(anchor, viewport.width);
        const panelWidth = Math.min(WIDTH, viewport.width - 2 * MARGIN);
        return {
            left: Math.max(MARGIN, Math.min(wanted.left, viewport.width - panelWidth - MARGIN)),
            top: Math.max(MARGIN, Math.min(wanted.top, viewport.height - 160)),
        };
    });
    // Never past the bottom of the window; the scripts scroll instead.
    const maxHeight = $derived(Math.max(160, Math.min(720, viewport.height - placed.top - MARGIN)));

    function pointerDown(event) {
        if (event.button !== 0 || event.target.closest("button")) return;
        event.preventDefault();
        drag = { id: event.pointerId, dx: event.clientX - placed.left, dy: event.clientY - placed.top };
        event.currentTarget.setPointerCapture(event.pointerId);
    }
    function pointerMove(event) {
        if (!drag || event.pointerId !== drag.id) return;
        onMove({
            left: Math.max(0, Math.min(event.clientX - drag.dx, window.innerWidth - 80)),
            top: Math.max(0, Math.min(event.clientY - drag.dy, window.innerHeight - 40)),
        });
    }
    function pointerUp(event) {
        if (drag && event.pointerId === drag.id) drag = null;
    }

    // Opening on a script shows its card. Opening moves the keyboard focus
    // into the panel, unless it's already there.
    $effect(() => {
        const index = focusIndex, stripId = strip?.id;
        void focusToken;
        if (!stripId) return;
        tick().then(() => {
            if (!panel?.contains(document.activeElement)) panel?.focus({ preventScroll: true });
            if (index >= 0) panel?.querySelector(`[data-script-index="${index}"]`)?.scrollIntoView({ block: "nearest" });
        });
    });
</script>

<svelte:window onresize={() => { viewport = { width: window.innerWidth, height: window.innerHeight }; }} />

<div class="midi-fx-panel" bind:this={panel} use:portal role="dialog" aria-modal="false" tabindex="-1"
    aria-label={`MIDI FX for ${title}`} style:left={`${placed.left}px`} style:top={`${placed.top}px`}
    style:max-height={`${maxHeight}px`}>
    <!-- svelte-ignore a11y_no_static_element_interactions -->
    <header onpointerdown={pointerDown} onpointermove={pointerMove} onpointerup={pointerUp} onpointercancel={pointerUp}
        title="Drag to move">
        <span class="grip" aria-hidden="true">⋮⋮</span>
        <div class="who">
            <strong>{title} · MIDI FX</strong>
            {#if strip?.layerName && strip?.library}<small>{strip.library}</small>{/if}
        </div>
        <button class="close" aria-label="Close MIDI FX" title="Close (Esc)" onclick={onClose}>×</button>
    </header>
    <!-- Another layer gets fresh controls: nothing typed for one layer can
         reach the next. -->
    {#key strip?.id}
    <div class="scripts">
        {#each scripts as plugin (plugin.index)}
            <div data-script-index={plugin.index}>
                <LuaScriptCard {plugin} highlighted={plugin.index === focusIndex}
                    onSetParam={(name, value) => onSetParam(plugin.index, name, value)}
                    onResetParams={() => onResetParams(plugin.index)}
                    onSetEnabled={(enabled) => onSetEnabled(plugin.index, enabled)}
                    {onUseFile} onRemove={() => onRemove(plugin.index)}
                    {onGestureBegin} {onGestureEnd} />
            </div>
        {:else}
            <p class="empty">No MIDI FX on this layer. Scripts change its notes before the expression map and the instrument.</p>
        {/each}
    </div>
    {/key}
    {#if catalog.length || scripts.length}
    <footer>
        {#if catalog.length}
            <select aria-label="Add a script" value="" onchange={(event) => {
                if (event.currentTarget.value) onAdd(event.currentTarget.value);
                event.currentTarget.value = "";
            }}>
                <option value="">Add script…</option>
                {#each catalog as plugin}<option value={plugin.filePath}>{plugin.name}</option>{/each}
            </select>
        {/if}
        {#if scripts.length}<span class="hint">Changes apply from the next note.</span>{/if}
    </footer>
    {/if}
</div>

<style>
    /* Above the mixer and its toolbar, below every modal dialog (z-index 50 and
       up), matching MixerPanel's Escape order. */
    .midi-fx-panel { position: fixed; z-index: 40; width: 320px; max-width: calc(100vw - 16px); display: flex; flex-direction: column; overflow: hidden;
        border: 1px solid #2b3b52; border-radius: 10px; background: #0f1a2b; color: #e2e8f0; box-shadow: 0 18px 40px rgba(0, 0, 0, 0.45); }
    .midi-fx-panel:focus { outline: none; }
    header { display: flex; align-items: center; gap: 8px; padding: 9px 8px 9px 12px; border-bottom: 1px solid #22324a; background: #13213a; cursor: grab; touch-action: none; user-select: none; }
    header:active { cursor: grabbing; }
    .grip { color: #475569; font-size: 0.8rem; letter-spacing: -2px; }
    .who { flex: 1; min-width: 0; display: flex; flex-direction: column; }
    .who strong, .who small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .who strong { font-size: 0.86rem; }
    .who small { color: #64748b; font-size: 0.72rem; }
    .close { width: 28px; height: 28px; padding: 0; border: 0; border-radius: 5px; background: transparent; color: #94a3b8; font-size: 1.2rem; line-height: 1; cursor: pointer; }
    .close:hover { background: #1e3048; color: #e2e8f0; }
    .scripts { display: flex; flex-direction: column; gap: 8px; padding: 10px; overflow-y: auto; min-height: 0; }
    .empty { margin: 4px 2px; color: #94a3b8; font-size: 0.8rem; line-height: 1.45; }
    footer { display: flex; align-items: center; justify-content: space-between; gap: 8px; padding: 8px 10px; border-top: 1px solid #1d2a3f; }
    footer select { min-width: 0; flex: 1; max-width: 180px; height: 32px; padding: 0 8px; border: 1px solid #475569; border-radius: 6px; background: #172337; color: #e2e8f0; font: inherit; font-size: 0.8rem; }
    .hint { color: #64748b; font-size: 0.72rem; text-align: right; }
    button:focus-visible, select:focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
</style>
