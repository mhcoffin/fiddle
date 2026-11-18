<script>
    import { onMount } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import { portal } from "./portal.js";
    import {
        IMPORT_STATUS_LABELS, KIND_LABELS, defaultImportSelection, filterCandidates, newRequestId,
    } from "./libraryMaps.js";

    // Import maps into Fiddle's collection (docs/maps.md): ask the server for
    // a file's or Dorico's maps, list them with their status against Fiddle's
    // copies, and import the ticked ones. Mount it to start; it unmounts
    // itself through onclose. A request ID keeps several open panels apart.
    let {
        kind = "expression",
        origin = "file",      // "file" (choose .doricolib files) or "dorico"
        onclose = () => {},
        ondone = () => {},    // ({ ok, message, imported }) after an import or an empty list
    } = $props();

    const labels = $derived(KIND_LABELS[kind]);
    const requestId = newRequestId("import");
    let offer = $state(null);
    let selection = $state(new Set());
    let search = $state("");
    let importing = $state(false);

    onMount(() => {
        const offCandidates = onFromCpp("libraryMapImportCandidates", (data) => {
            if (data?.requestId !== requestId) return;
            if (data.cancelled) { onclose(); return; } // the file chooser was cancelled
            if (!data.maps?.length) {
                ondone({ ok: false, imported: [],
                    message: data.errors?.length ? data.errors.join("; ") : `No ${labels.plural} found` });
                onclose();
                return;
            }
            offer = data;
            selection = defaultImportSelection(data.maps);
        });
        const offResult = onFromCpp("libraryMapResult", (result) => {
            if (result?.requestId !== requestId || result.action !== "import") return;
            ondone({ ok: result.success, message: result.message, imported: result.imported || [] });
            onclose();
        });
        dispatchCpp(origin === "dorico" ? "findDoricoLibraryMaps" : "chooseLibraryMapFiles", kind, requestId);
        return () => { offCandidates(); offResult(); };
    });

    let visible = $derived(offer ? filterCandidates(offer.maps, search) : []);
    let chosenCount = $derived(offer ? offer.maps.filter((c) => selection.has(c.entityId)).length : 0);
    const toggle = (id) => {
        const next = new Set(selection);
        if (next.has(id)) next.delete(id); else next.add(id);
        selection = next;
    };
    const confirm = () => {
        const chosen = offer.maps.filter((c) => selection.has(c.entityId))
            .map((c) => ({ file: c.file, entityId: c.entityId }));
        if (!chosen.length || importing) return;
        importing = true;
        dispatchCpp("importLibraryMaps", kind, chosen, requestId);
    };
    const statusText = (candidate) => {
        const base = IMPORT_STATUS_LABELS[candidate.status] || candidate.status;
        const versions = candidate.status === "changed" || candidate.status === "newer"
            ? ` (Fiddle v${candidate.existingVersion}, file v${candidate.version})` : "";
        return base + versions;
    };
    // Replacing Fiddle's copy changes every patch and layer that uses it.
    const replaces = (candidate) =>
        (candidate.status === "changed" || candidate.status === "newer") && candidate.references > 0;
</script>

{#if offer}
    <!-- svelte-ignore a11y_no_static_element_interactions -->
    <div class="mid-backdrop" use:portal
        onkeydown={(e) => { if (e.key === "Escape") { e.stopPropagation(); if (!importing) onclose(); } }}>
        <div class="mid-dialog" role="dialog" aria-labelledby="mid-heading-{requestId}">
            <header>
                <h2 id="mid-heading-{requestId}">Import {labels.plural}</h2>
                <button aria-label="Close" disabled={importing} onclick={onclose}>✕</button>
            </header>
            <div class="mid-body">
                <!-- svelte-ignore a11y_autofocus -->
                <input class="mid-search" type="search" aria-label="Search {labels.plural}" autofocus
                    placeholder="Search by name, vendor or file" bind:value={search} />
                <p class="mid-sub">{visible.length} of {offer.maps.length} shown · {chosenCount} chosen</p>
                <ul class="mid-candidates">
                    {#each visible as candidate (candidate.entityId)}
                        <li>
                            <label>
                                <input type="checkbox" checked={selection.has(candidate.entityId)}
                                    disabled={candidate.status === "same"}
                                    onchange={() => toggle(candidate.entityId)} />
                                <span class="mid-candidate">
                                    <strong>{candidate.name}</strong>
                                    <span class="mid-status {candidate.status}">{statusText(candidate)}</span>
                                    {#if replaces(candidate)}
                                        <span class="mid-warning">Replaces Fiddle's copy for the {candidate.references}
                                            {candidate.references === 1 ? "patch or layer" : "patches and layers"} using it</span>
                                    {/if}
                                    <span class="mid-id">{candidate.entityId} · {candidate.file}</span>
                                </span>
                            </label>
                        </li>
                    {/each}
                </ul>
                {#if offer.errors?.length}
                    <ul class="mid-problems">{#each offer.errors as error}<li>{error}</li>{/each}</ul>
                {/if}
            </div>
            <footer>
                <button disabled={importing} onclick={onclose}>Cancel</button>
                <button class="primary" disabled={chosenCount === 0 || importing} onclick={confirm}>
                    {importing ? "Importing…" : `Import ${chosenCount}`}</button>
            </footer>
        </div>
    </div>
{/if}

<style>
    /* Overlays sit above the Library Manager's editor modal (z-index 99999). */
    .mid-backdrop { position: fixed; inset: 0; z-index: 100400; display: flex; align-items: center; justify-content: center;
        background: rgba(0, 0, 0, .55); }
    /* Fixed height, so search results never resize it while you type. */
    .mid-dialog { width: 680px; max-width: 94vw; height: min(820px, 90vh); display: flex; flex-direction: column;
        background: #0e0d38; border: 1px solid #44446c; border-radius: 6px; box-shadow: 0 10px 30px rgba(0,0,0,.5);
        color: #e5e3ff; font-family: "Inter", sans-serif; }
    header, footer { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 14px 18px; }
    header { border-bottom: 1px solid #44446c; }
    footer { justify-content: flex-end; border-top: 1px solid #44446c; }
    h2 { margin: 0; font-family: "Sora", "Inter", sans-serif; font-size: 1rem; }
    .mid-body { flex: 1; min-height: 0; padding: 16px 18px; overflow: auto; display: flex; flex-direction: column; gap: 10px; }
    button { background: transparent; border: 1px solid #95a9ff; color: #95a9ff; font: inherit; font-size: .76rem;
        padding: 5px 10px; border-radius: 4px; cursor: pointer; }
    button:hover:not(:disabled) { background: rgba(149,169,255,.12); color: #c0ccff; }
    button:disabled { opacity: .4; cursor: default; }
    button.primary { background: #1e4cd5; border-color: #1e4cd5; color: #fff; }
    .mid-search { background: #131342; border: 1px solid #44446c; border-radius: 4px; color: #e5e3ff; font: inherit;
        font-size: .85rem; padding: 6px 8px; width: 100%; box-sizing: border-box; }
    .mid-sub { margin: 0; color: #a8a7d5; font-size: .76rem; }
    .mid-candidates { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 8px; }
    label { display: flex; align-items: flex-start; gap: 10px; }
    label input { margin-top: 3px; }
    .mid-candidate { display: flex; flex-direction: column; gap: 2px; font-size: .8rem; }
    .mid-status { font-size: .72rem; color: #a8a7d5; }
    .mid-status.new, .mid-status.newer { color: #69db7c; }
    .mid-status.changed { color: #fbbf24; }
    .mid-warning { font-size: .72rem; color: #fbbf24; }
    .mid-id { color: #72719c; font-size: .68rem; overflow-wrap: anywhere; }
    .mid-problems { margin: 0; padding: 8px 12px 8px 28px; border: 1px solid rgba(255,107,107,.25); border-radius: 4px;
        background: rgba(255,107,107,.08); color: #ff8787; font-size: .76rem; }
</style>
