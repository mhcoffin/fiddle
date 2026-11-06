<script>
    let {
        bus,
        routedCount = 0,
        sendCount = 0,
        onRename = (_name) => {},
        onDelete = () => {},
        onClose = () => {},
    } = $props();

    const plural = (count, word) => `${count} ${word}${count === 1 ? "" : "s"}`;
    const rename = (event) => {
        const name = event.currentTarget.value.trim();
        if (!name) { event.currentTarget.value = bus.name; return; }
        if (name !== bus.name) onRename(name);
    };
    const closeFromBackdrop = (event) => {
        if (event.target === event.currentTarget) onClose();
    };
    let consequences = $derived([
        sendCount ? `its ${plural(sendCount, "send")} are removed` : "",
        routedCount ? `${plural(routedCount, "layer")} routed to it go back to Master` : "",
    ].filter(Boolean));
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="backdrop" onclick={closeFromBackdrop}>
    <dialog class="panel" aria-label={`Bus details for ${bus.name}`} open>
        <header>
            <div>
                <h2>Bus Details</h2>
                <p>{bus.name}</p>
            </div>
            <button class="close" onclick={onClose}>Close</button>
        </header>

        <div class="content">
            <section>
                <label>
                    <span>Name</span>
                    <input aria-label="Bus name" value={bus.name} autocomplete="off" spellcheck="false"
                        onchange={rename}
                        onkeydown={(event) => { if (event.key === "Enter") event.currentTarget.blur(); }} />
                </label>
                <p class="usage">{plural(routedCount, "layer")} routed directly · {plural(sendCount, "send")} · Output: Master</p>
            </section>

            <section class="danger-zone">
                <div>
                    <h3>Bus operations</h3>
                    <p>{consequences.length
                        ? `When you delete this bus, ${consequences.join(" and ")}. Undo restores them.`
                        : "Nothing is routed or sent to this bus. Undo restores it."}</p>
                </div>
                <button class="danger" onclick={onDelete}>Delete bus</button>
            </section>
        </div>
    </dialog>
</div>

<style>
    .backdrop { position: absolute; inset: 0; z-index: 52; display: flex; align-items: center; justify-content: center; background: rgba(2, 6, 23, 0.62); backdrop-filter: blur(2px); }
    .panel { position: relative; margin: 0; padding: 0; width: min(560px, calc(100% - 48px)); max-height: calc(100% - 64px); display: flex; flex-direction: column; overflow: hidden; background: #0b1220; border: 1px solid #334155; border-radius: 12px; box-shadow: 0 16px 48px rgba(0, 0, 0, 0.5); color: #e2e8f0; }
    header, .danger-zone { display: flex; align-items: center; justify-content: space-between; gap: 20px; }
    header { padding: 22px 26px; border-bottom: 1px solid #273449; background: #111c2e; }
    h2, h3, p { margin: 0; }
    h2 { color: #f8fafc; font-size: 1.45rem; }
    h3 { color: #f1f5f9; font-size: 1.02rem; }
    p { margin-top: 4px; color: #94a3b8; font-size: 0.84rem; line-height: 1.4; }
    button, input { min-height: 38px; box-sizing: border-box; border: 1px solid #475569; border-radius: 6px; background: #172337; color: #e2e8f0; font: inherit; }
    button { padding: 7px 12px; cursor: pointer; font-weight: 600; flex-shrink: 0; }
    button:hover { border-color: #7dd3fc; background: #1e3048; }
    input { width: 100%; padding: 8px 11px; outline: none; }
    input:focus { border-color: #22d3ee; }
    .close { min-width: 72px; }
    .content { min-height: 0; padding: 24px 26px 8px; overflow-y: auto; }
    section { margin-bottom: 18px; padding: 20px; border: 1px solid #26364d; border-radius: 10px; background: #0f1929; }
    label { display: flex; flex-direction: column; gap: 6px; }
    label > span { color: #94a3b8; font-size: 0.72rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.06em; }
    .usage { margin-top: 10px; font-size: 0.78rem; }
    .danger { border-color: #7f1d1d; color: #fecaca; }
    .danger:hover { border-color: #f87171; background: rgba(127, 29, 29, 0.35); }
    @media (max-width: 560px) { .danger-zone { align-items: flex-start; flex-direction: column; } }
</style>
