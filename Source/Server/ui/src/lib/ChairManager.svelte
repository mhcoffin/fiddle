<script>
    import { onMount } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import { populateScoreOrder } from "./orchestralOrder.js";
    import { groupChairs, searchDoricoInstruments, suggestedChairName } from "./chairModel.js";

    let { onClose = () => {} } = $props();
    let chairs = $state([]);
    let instruments = $state([]);
    let adding = $state(false);
    let query = $state("");
    let selectedInstrument = $state(null);
    let role = $state("section");
    let chairName = $state("");
    let pendingDeleteId = $state("");
    let templateDirty = $state(true);
    let installing = $state(false);
    let message = $state("");
    let groups = $derived(groupChairs(chairs));
    let results = $derived(searchDoricoInstruments(instruments, query));

    const clearMessageLater = () => setTimeout(() => { message = ""; }, 5000);

    const unsubscribers = [];
    unsubscribers.push(onFromCpp("setChairState", (data) => {
        chairs = Array.isArray(data) ? data : [];
    }));
    unsubscribers.push(onFromCpp("setDoricoInstruments", (data) => {
        instruments = Array.isArray(data) ? data : [];
        populateScoreOrder(instruments);
    }));
    unsubscribers.push(onFromCpp("setTemplateDirty", (data) => { templateDirty = !!data; }));
    unsubscribers.push(onFromCpp("chairOperationResult", (data) => {
        message = typeof data === "string" ? data : "The chair could not be changed";
        clearMessageLater();
    }));
    unsubscribers.push(onFromCpp("chairTemplateResult", (data) => {
        installing = false;
        message = typeof data === "string" ? data : "Playback template operation finished";
        clearMessageLater();
    }));

    onMount(() => {
        dispatchCpp("requestChairs");
        dispatchCpp("requestSetupData");
        return () => unsubscribers.forEach((unsubscribe) => unsubscribe());
    });

    const openAdd = () => {
        query = "";
        selectedInstrument = null;
        role = "section";
        chairName = "";
        adding = true;
    };

    const selectInstrument = (instrument) => {
        selectedInstrument = instrument;
        chairName = suggestedChairName(instrument, role, chairs);
    };

    const changeRole = (value) => {
        role = value;
        if (selectedInstrument)
            chairName = suggestedChairName(selectedInstrument, role, chairs);
    };

    const createChair = () => {
        if (!selectedInstrument || !chairName.trim()) return;
        dispatchCpp("createChair", {
            entityID: selectedInstrument.entityID,
            family: selectedInstrument.family,
            role,
            name: chairName.trim(),
        });
        adding = false;
    };

    const renameChair = (chair, input) => {
        const name = input.value.trim();
        if (name && name !== chair.name)
            dispatchCpp("updateChair", { id: chair.id, name });
        else if (!name)
            input.value = chair.name;
    };

    const installTemplate = () => {
        if (installing || chairs.length === 0) return;
        installing = true;
        message = "";
        dispatchCpp("installChairTemplate");
    };

    const closeFromBackdrop = (event) => {
        if (event.target === event.currentTarget) onClose();
    };
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="backdrop" onclick={closeFromBackdrop}>
    <dialog class="panel" aria-label="Chair Manager" open>
        <header>
            <div>
                <h2>Chair Manager</h2>
                <p>Define the destinations Dorico sends to. Sound layers are assigned separately.</p>
            </div>
            <div class="header-actions">
                <button
                    class="install"
                    onclick={installTemplate}
                    disabled={installing || chairs.length === 0 || !templateDirty}
                    title={chairs.length === 0 ? "Add a chair first" : "Install these chairs in Dorico"}
                >{installing ? "Installing…" : "Install Playback Template"}</button>
                <button onclick={onClose}>Close</button>
            </div>
        </header>

        <div class="content">
            <div class="intro-row">
                <div>
                    <h3>Dorico chairs</h3>
                    <p>{chairs.length} {chairs.length === 1 ? "destination" : "destinations"}</p>
                </div>
                <button class="primary" onclick={openAdd}>Add Chair</button>
            </div>

            {#if message}
                <div class:error={message.startsWith("Error") || message.startsWith("Could") || message.startsWith("Add ")} class="message">{message}</div>
            {/if}

            {#if chairs.length === 0}
                <div class="empty">
                    <strong>No chairs yet</strong>
                    <span>Add the orchestral destinations that should appear in Dorico, such as Violin 1 or Solo Violin 1.</span>
                </div>
            {:else}
                <div class="groups">
                    {#each groups as group (group.key)}
                        <section class="family-group">
                            <div class="family-heading">
                                <h4>{group.label}</h4>
                                <span>{group.chairs.length}</span>
                            </div>
                            <div class="chair-list">
                                {#each group.chairs as chair (chair.id)}
                                    <article class="chair-row">
                                        <input
                                            class="chair-name"
                                            value={chair.name}
                                            aria-label={`Name for ${chair.name}`}
                                            onblur={(event) => renameChair(chair, event.currentTarget)}
                                            onkeydown={(event) => {
                                                if (event.key === "Enter") event.currentTarget.blur();
                                                if (event.key === "Escape") {
                                                    event.currentTarget.value = chair.name;
                                                    event.currentTarget.blur();
                                                }
                                            }}
                                        />
                                        <div class="identity">
                                            <strong>{instruments.find((item) => item.entityID === chair.entityID)?.name || chair.entityID}</strong>
                                            <span>{chair.role === "solo" ? "Solo player" : "Section player"} · instance {chair.ordinal}</span>
                                        </div>
                                        <div class="midi">
                                            <span>MIDI destination</span>
                                            <strong>Port {chair.port + 1} · Channel {chair.channel + 1}</strong>
                                        </div>
                                        <button
                                            class:confirm={pendingDeleteId === chair.id}
                                            class="delete"
                                            onclick={() => {
                                                if (pendingDeleteId === chair.id) {
                                                    dispatchCpp("deleteChair", chair.id);
                                                    pendingDeleteId = "";
                                                } else pendingDeleteId = chair.id;
                                            }}
                                        >{pendingDeleteId === chair.id ? "Confirm delete" : "Delete"}</button>
                                    </article>
                                {/each}
                            </div>
                        </section>
                    {/each}
                </div>
            {/if}
        </div>

        {#if adding}
            <div class="add-backdrop">
                <section class="add-dialog" aria-label="Add Chair">
                    <div class="add-heading">
                        <div><h3>Add Chair</h3><p>Choose the Dorico instrument and player type.</p></div>
                        <button onclick={() => { adding = false; }}>Cancel</button>
                    </div>

                    {#if !selectedInstrument}
                        <label for="chair-search">Dorico instrument</label>
                        <input
                            id="chair-search"
                            class="search"
                            type="search"
                            bind:value={query}
                            placeholder="Type an instrument name, such as violin or horn"
                        />
                        <div class="search-results">
                            {#if !query.trim()}
                                <div class="search-hint">Start typing to search Dorico’s instrument catalog.</div>
                            {:else if results.length === 0}
                                <div class="search-hint">No matching Dorico instruments.</div>
                            {:else}
                                {#each results as instrument (instrument.entityID)}
                                    <button class="instrument-choice" onclick={() => selectInstrument(instrument)}>
                                        <strong>{instrument.name}</strong>
                                        <span>{instrument.family} · {instrument.entityID}</span>
                                    </button>
                                {/each}
                            {/if}
                        </div>
                    {:else}
                        <button class="chosen-instrument" onclick={() => { selectedInstrument = null; chairName = ""; }}>
                            <span>Instrument</span>
                            <strong>{selectedInstrument.name}</strong>
                            <small>Change</small>
                        </button>
                        <fieldset>
                            <legend>Player type</legend>
                            <label><input type="radio" name="chair-role" value="section" checked={role === "section"} onchange={() => changeRole("section")} /> Section</label>
                            <label><input type="radio" name="chair-role" value="solo" checked={role === "solo"} onchange={() => changeRole("solo")} /> Solo</label>
                        </fieldset>
                        <label for="chair-name">Chair name</label>
                        <input id="chair-name" class="search" bind:value={chairName} />
                        <div class="add-footer">
                            <button class="primary" onclick={createChair} disabled={!chairName.trim()}>Create Chair</button>
                        </div>
                    {/if}
                </section>
            </div>
        {/if}
    </dialog>
</div>

<style>
    .backdrop { position:absolute; inset:0; z-index:55; display:flex; justify-content:flex-end; background:rgba(2,6,23,.66); backdrop-filter:blur(2px); }
    .panel { position:relative; margin:0 0 0 auto; padding:0; width:min(1000px, calc(100% - 48px)); height:100%; display:flex; flex-direction:column; background:#0b1220; color:#e2e8f0; border:0; border-left:1px solid #334155; box-shadow:-16px 0 40px rgba(0,0,0,.42); }
    header,.header-actions,.intro-row,.family-heading,.chair-row,.add-heading,.add-footer { display:flex; align-items:center; }
    header { justify-content:space-between; gap:24px; padding:22px 26px; border-bottom:1px solid #273449; background:#111c2e; }
    h2,h3,h4,p { margin:0; } h2 { font-size:1.45rem; color:#f8fafc; } h3 { font-size:1.05rem; color:#f1f5f9; } h4 { font-size:.9rem; text-transform:uppercase; letter-spacing:.08em; color:#94a3b8; }
    p { margin-top:4px; color:#94a3b8; font-size:.86rem; }
    .header-actions { gap:10px; }
    button,input { font:inherit; box-sizing:border-box; }
    button { min-height:38px; padding:8px 13px; border:1px solid #475569; border-radius:6px; background:#172337; color:#e2e8f0; cursor:pointer; }
    button:hover:not(:disabled) { border-color:#7dd3fc; background:#1e3048; } button:disabled { opacity:.4; cursor:default; }
    .install,.primary { background:#075985; border-color:#38bdf8; color:#f0f9ff; font-weight:650; }
    .content { padding:24px 26px 36px; overflow-y:auto; }
    .intro-row { justify-content:space-between; margin-bottom:18px; }
    .message { margin-bottom:16px; padding:10px 13px; border:1px solid rgba(34,197,94,.35); border-radius:6px; background:rgba(34,197,94,.1); color:#86efac; }
    .message.error { border-color:rgba(248,113,113,.4); background:rgba(248,113,113,.1); color:#fca5a5; }
    .empty { min-height:220px; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:8px; border:1px dashed #334155; border-radius:10px; color:#cbd5e1; text-align:center; }
    .empty span { max-width:520px; color:#94a3b8; }
    .groups { display:flex; flex-direction:column; gap:18px; }
    .family-group { border:1px solid #26364d; border-radius:10px; overflow:hidden; background:#0f1929; }
    .family-heading { justify-content:space-between; padding:11px 15px; background:#111c2e; border-bottom:1px solid #26364d; }
    .family-heading span { min-width:25px; padding:2px 7px; border-radius:12px; background:#1e293b; text-align:center; color:#cbd5e1; font-size:.76rem; }
    .chair-list { display:flex; flex-direction:column; }
    .chair-row { gap:16px; min-height:72px; padding:12px 14px; border-bottom:1px solid #1e2d40; }
    .chair-row:last-child { border-bottom:0; }
    .chair-name { width:240px; height:42px; padding:9px 11px; border:1px solid #3b4b61; border-radius:6px; background:#0b1422; color:#f8fafc; font-weight:650; }
    .chair-name:focus,.search:focus { outline:2px solid rgba(56,189,248,.45); border-color:#38bdf8; }
    .identity { flex:1; min-width:180px; display:flex; flex-direction:column; gap:4px; }
    .identity span,.midi span { color:#94a3b8; font-size:.76rem; }
    .midi { min-width:185px; display:flex; flex-direction:column; gap:4px; font-variant-numeric:tabular-nums; }
    .midi strong { font-size:.88rem; }
    .delete { min-width:116px; } .delete.confirm { border-color:#f87171; background:#7f1d1d; color:#fee2e2; }
    .add-backdrop { position:absolute; inset:0; z-index:2; display:flex; align-items:center; justify-content:center; padding:40px; background:rgba(2,6,23,.82); }
    .add-dialog { width:min(680px, 100%); max-height:calc(100% - 40px); display:flex; flex-direction:column; padding:24px; border:1px solid #3b4b61; border-radius:12px; background:#111c2e; box-shadow:0 24px 60px rgba(0,0,0,.5); }
    .add-heading { justify-content:space-between; margin-bottom:20px; }
    .add-dialog > label { margin:14px 0 7px; color:#cbd5e1; font-size:.82rem; font-weight:650; }
    .search { width:100%; min-height:46px; padding:10px 12px; border:1px solid #475569; border-radius:6px; background:#09121f; color:#f8fafc; }
    .search-results { min-height:180px; max-height:360px; margin-top:10px; overflow-y:auto; border:1px solid #26364d; border-radius:7px; background:#09121f; }
    .search-hint { padding:24px; color:#64748b; text-align:center; }
    .instrument-choice { width:100%; min-height:58px; display:flex; flex-direction:column; align-items:flex-start; gap:3px; border:0; border-bottom:1px solid #1e293b; border-radius:0; background:transparent; text-align:left; }
    .instrument-choice span { max-width:100%; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; color:#94a3b8; font-size:.76rem; }
    .chosen-instrument { display:grid; grid-template-columns:100px 1fr auto; align-items:center; min-height:58px; text-align:left; }
    .chosen-instrument span,.chosen-instrument small { color:#94a3b8; }
    fieldset { display:flex; gap:24px; margin:18px 0 2px; padding:14px 16px; border:1px solid #334155; border-radius:7px; }
    legend { padding:0 5px; color:#cbd5e1; font-size:.82rem; font-weight:650; } fieldset label { display:flex; align-items:center; gap:7px; }
    .add-footer { justify-content:flex-end; margin-top:20px; }
    @media (max-width:800px) { .panel { width:100%; } header { align-items:flex-start; } .header-actions { flex-direction:column; align-items:stretch; } .chair-row { align-items:stretch; flex-direction:column; } .chair-name,.midi { width:100%; } }
</style>
