<script>
    import { dispatchCpp } from "./ipc.js";

    let {
        buses = [],
        strips = [],
        selectedStripIds = [],
        onClose = () => {},
    } = $props();

    let newName = $state("");
    let pendingRemovalId = $state("");
    const routeCount = (busId) => strips.filter((strip) => strip.directOutputBusId === busId).length;

    const add = () => {
        const name = newName.trim();
        if (!name) return;
        dispatchCpp("addGroupBus", name, JSON.stringify(selectedStripIds));
        newName = "";
    };

    const confirmRemoval = (busId) => {
        dispatchCpp("removeGroupBus", busId);
        pendingRemovalId = "";
    };
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="backdrop" onclick={(event) => event.target === event.currentTarget && onClose()}>
    <dialog class="panel" aria-label="Bus Manager" open>
        <header>
            <div>
                <h2>Bus Manager</h2>
                <p>Create stereo group buses for shared processing and submix control.</p>
            </div>
            <button class="close" onclick={onClose}>Close</button>
        </header>

        <div class="content">
            <section class="create">
                <div>
                    <h3>Add group bus</h3>
                    <p>{selectedStripIds.length
                        ? `${selectedStripIds.length} selected ${selectedStripIds.length === 1 ? "strip" : "strips"} will be routed to it.`
                        : "The new bus will be ready for manual strip assignment."}</p>
                </div>
                <div class="create-controls">
                    <input
                        aria-label="New group bus name"
                        placeholder="e.g. Strings"
                        bind:value={newName}
                        onkeydown={(event) => event.key === "Enter" && add()}
                    />
                    <button class="primary" disabled={!newName.trim()} onclick={add}>
                        {selectedStripIds.length ? "Create from selection" : "Add group bus"}
                    </button>
                </div>
            </section>

            <section>
                <div class="section-heading">
                    <div>
                        <h3>Group buses</h3>
                        <p>Every group bus feeds Master. Nested routing is intentionally unavailable.</p>
                    </div>
                    <span class="count">{buses.length}</span>
                </div>

                {#if buses.length}
                    <div class="bus-list">
                        {#each buses as bus, index (bus.id)}
                            <article class="bus-row">
                                <div class="identity">
                                    <input
                                        class="name"
                                        aria-label="Bus name"
                                        value={bus.name}
                                        onchange={(event) => dispatchCpp("renameGroupBus", bus.id, event.currentTarget.value)}
                                    />
                                    <span>Group · {routeCount(bus.id)} routed {routeCount(bus.id) === 1 ? "strip" : "strips"}</span>
                                </div>
                                <label class="gain">
                                    <span>Gain</span>
                                    <input
                                        type="number"
                                        min="-120"
                                        max="12"
                                        step="0.1"
                                        value={Number(bus.gainDb ?? 0).toFixed(1)}
                                        onchange={(event) => dispatchCpp("setGroupBusGain", bus.id, Number(event.currentTarget.value))}
                                    />
                                    <span>dB</span>
                                </label>
                                <div class="monitor" aria-label={`Monitor controls for ${bus.name}`}>
                                    <button class:active={bus.muted} onclick={() => dispatchCpp("setGroupBusMute", bus.id, !bus.muted)}>Mute</button>
                                    <button class:active={bus.soloed} onclick={() => dispatchCpp("setGroupBusSolo", bus.id, !bus.soloed)}>Solo</button>
                                </div>
                                <div class="actions">
                                    <button disabled={index === 0} onclick={() => dispatchCpp("moveGroupBus", bus.id, index - 1)}>Earlier</button>
                                    <button disabled={index === buses.length - 1} onclick={() => dispatchCpp("moveGroupBus", bus.id, index + 1)}>Later</button>
                                    {#if pendingRemovalId === bus.id}
                                        <button onclick={() => { pendingRemovalId = ""; }}>Cancel</button>
                                        <button class="danger confirm" onclick={() => confirmRemoval(bus.id)}>Confirm remove</button>
                                    {:else}
                                        <button class="danger" onclick={() => { pendingRemovalId = bus.id; }}>Remove</button>
                                    {/if}
                                </div>
                            </article>
                            {#if pendingRemovalId === bus.id}
                                <div class="removal-warning" role="alert">
                                    Remove <strong>{bus.name}</strong>?
                                    {#if routeCount(bus.id)}
                                        Its {routeCount(bus.id)} routed {routeCount(bus.id) === 1 ? "strip" : "strips"} will return to Master.
                                    {/if}
                                </div>
                            {/if}
                        {/each}
                    </div>
                {:else}
                    <div class="empty">No group buses yet.</div>
                {/if}
            </section>
        </div>
    </dialog>
</div>

<style>
    .backdrop { position: absolute; inset: 0; z-index: 54; display: flex; justify-content: center; align-items: flex-start; padding: 5vh 32px; box-sizing: border-box; background: rgba(2, 6, 23, 0.7); backdrop-filter: blur(3px); }
    .panel { margin: 0; padding: 0; width: min(1040px, 100%); max-height: 90vh; display: flex; flex-direction: column; border: 1px solid #334155; border-radius: 12px; background: #0b1220; color: #e2e8f0; box-shadow: 0 24px 70px rgba(0,0,0,.52); }
    header, .section-heading, .create, .create-controls, .bus-row, .gain, .monitor, .actions { display: flex; align-items: center; }
    header { justify-content: space-between; gap: 24px; padding: 24px 28px; border-bottom: 1px solid #273449; background: #111c2e; }
    h2, h3, p { margin: 0; }
    h2 { color: #f8fafc; font-size: 1.5rem; }
    h3 { color: #f1f5f9; font-size: 1.05rem; }
    p { margin-top: 5px; color: #94a3b8; font-size: .86rem; line-height: 1.4; }
    button, input { min-height: 40px; box-sizing: border-box; border: 1px solid #475569; border-radius: 7px; background: #172337; color: #e2e8f0; font: inherit; }
    button { padding: 8px 13px; cursor: pointer; font-weight: 650; }
    button:hover:not(:disabled) { border-color: #7dd3fc; background: #1e3048; }
    button:disabled { opacity: .35; cursor: default; }
    input { padding: 8px 11px; }
    .close { min-width: 76px; }
    .content { padding: 24px 28px 30px; overflow-y: auto; }
    section { margin-bottom: 18px; padding: 20px; border: 1px solid #26364d; border-radius: 10px; background: #0f1929; }
    .create, .section-heading { justify-content: space-between; gap: 24px; }
    .create-controls { gap: 10px; min-width: min(480px, 55%); }
    .create-controls input { flex: 1; }
    .primary { border-color: #0f766e; background: #115e59; }
    .count { min-width: 30px; padding: 5px 10px; border-radius: 999px; background: #1e293b; color: #94a3b8; text-align: center; }
    .bus-list { display: flex; flex-direction: column; gap: 10px; margin-top: 18px; }
    .bus-row { gap: 18px; padding: 14px; border: 1px solid #334155; border-radius: 9px; background: #111c2e; }
    .identity { flex: 1; min-width: 210px; display: flex; flex-direction: column; gap: 5px; }
    .identity .name { width: 100%; font-weight: 700; }
    .identity span, .gain > span { color: #94a3b8; font-size: .76rem; }
    .gain { gap: 7px; }
    .gain input { width: 82px; text-align: right; }
    .monitor, .actions { gap: 7px; }
    .monitor .active { border-color: #f59e0b; background: #713f12; color: #fde68a; }
    .danger { border-color: #7f1d1d; color: #fecaca; }
    .danger.confirm { background: #7f1d1d; }
    .removal-warning { margin: -4px 4px 4px; padding: 10px 14px; border: 1px solid #7f1d1d; border-radius: 7px; background: rgba(127,29,29,.2); color: #fecaca; font-size: .82rem; }
    .empty { margin-top: 18px; padding: 28px; border: 1px dashed #334155; border-radius: 8px; color: #94a3b8; text-align: center; }
    @media (max-width: 850px) { .create { align-items: stretch; flex-direction: column; } .create-controls { min-width: 0; } .bus-row { align-items: stretch; flex-direction: column; } }
</style>
