<script>
  import { onMount } from "svelte";
  import { onFromCpp, dispatchCpp } from "./ipc.js";
  import {
    PERCUSSION_TECHNIQUES, blankEntry, defaultImportSelection, duplicatePercussionMap, formatTechniques,
    isUnpitchedInstrument, newPercussionMap, parseKeyswitches, parseTechniques, pitchName, validatePercussionMap,
  } from "./percussionMaps.js";

  // Library percussion maps (docs/library-percussion-maps.md): Dorico
  // DrumKitNoteMapDefinitions that turn an unpitched note's techniques into
  // a sample library's pitch and keyswitch.
  let { instruments = [] } = $props();

  let maps = $state([]);
  let message = $state(null); // { text, ok }
  let pendingDeleteId = $state("");
  let draft = $state(null);   // the map being edited
  let draftIsNew = $state(false);
  let saving = $state(false);
  let importOffer = $state(null); // { origin, maps, errors }
  let importSelection = $state(new Set());

  onFromCpp("setPercussionMaps", (data) => { maps = Array.isArray(data) ? data : []; });
  onFromCpp("percussionMapResult", (result) => {
    message = { text: result.message, ok: result.success };
    if (result.action === "save") {
      saving = false;
      if (result.success) draft = null;
    }
    if (result.action === "delete") pendingDeleteId = "";
  });
  onFromCpp("percussionMapImportCandidates", (offer) => {
    importOffer = offer;
    importSelection = defaultImportSelection(offer.maps);
    if (!offer.maps?.length)
      message = { text: offer.errors?.length ? offer.errors.join("; ") : "No percussion maps found", ok: false };
  });
  onMount(() => dispatchCpp("requestPercussionMaps"));

  let unpitched = $derived(instruments.filter((i) => isUnpitchedInstrument(i.entityID))
    .sort((a, b) => String(a.name).localeCompare(String(b.name))));
  const instrumentName = (id) => instruments.find((i) => i.entityID === id)?.name || id;
  let problems = $derived(draft ? validatePercussionMap(draft) : []);
  let knownTechniques = $derived([...new Set([
    ...PERCUSSION_TECHNIQUES,
    ...maps.flatMap((m) => (m.map?.entries || []).flatMap((e) => e.techniques)),
  ])].sort());

  const startNew = () => {
    draft = newPercussionMap("New percussion map", unpitched[0]?.entityID || "");
    draftIsNew = true;
  };
  const edit = (map) => { draft = structuredClone($state.snapshot(map.map)); draftIsNew = false; };
  const duplicate = (map) => { draft = duplicatePercussionMap($state.snapshot(map.map)); draftIsNew = true; };
  const save = () => {
    if (problems.length || saving) return;
    saving = true;
    dispatchCpp("savePercussionMap", $state.snapshot(draft));
  };
  const updateEntry = (index, patch) => {
    draft.entries = draft.entries.map((entry, i) => (i === index ? { ...entry, ...patch } : entry));
  };
  const moveEntry = (index, offset) => {
    const target = index + offset;
    if (target < 0 || target >= draft.entries.length) return;
    const entries = [...draft.entries];
    [entries[index], entries[target]] = [entries[target], entries[index]];
    draft.entries = entries;
  };
  const addEntry = () => {
    const last = draft.entries[draft.entries.length - 1];
    draft.entries = [...draft.entries, { ...blankEntry(last?.instrument || unpitched[0]?.entityID || ""),
      techniques: [], name: "", pitch: Math.min(127, (last?.pitch ?? 59) + 1) }];
  };
  const toggleImport = (id) => {
    const next = new Set(importSelection);
    if (next.has(id)) next.delete(id); else next.add(id);
    importSelection = next;
  };
  const confirmImport = () => {
    const chosen = importOffer.maps.filter((c) => importSelection.has(c.map.entityId)).map((c) => c.map);
    if (chosen.length) dispatchCpp("importPercussionMaps", chosen);
    importOffer = null;
  };
  const statusLabel = { new: "New", newer: "Newer version", same: "Already in library", changed: "Differs from library" };
</script>

<section class="pm-section" aria-labelledby="pm-heading">
  <div class="pm-toolbar">
    <div>
      <h2 id="pm-heading" class="pm-title">Percussion Maps</h2>
      <p class="pm-sub">Instrument + techniques → library pitch, for layers on unpitched chairs.</p>
    </div>
    <div class="pm-actions">
      <button onclick={startNew}>+ New Map</button>
      <button onclick={() => dispatchCpp("choosePercussionMapFiles")}>Import .doricolib…</button>
      <button onclick={() => dispatchCpp("scanDoricoPercussionMaps")}>Find in Dorico Library…</button>
    </div>
  </div>
  {#if message}
    <div class="pm-message" class:ok={message.ok} role="status">
      {message.text}<button class="pm-dismiss" aria-label="Dismiss" onclick={() => { message = null; }}>✕</button>
    </div>
  {/if}
  {#if maps.length}
    <table class="pm-table">
      <thead><tr><th>Name</th><th>Instruments</th><th>Entries</th><th>Used by</th><th></th></tr></thead>
      <tbody>
        {#each maps as map (map.entityId)}
          <tr>
            <td><strong>{map.name}</strong><span class="pm-id">{map.entityId}</span></td>
            <td>{(map.instruments || []).map(instrumentName).join(", ")}</td>
            <td>{map.entryCount}</td>
            <td title={(map.references || []).join("\n")}>{map.references?.length ? `${map.references.length}` : "—"}</td>
            <td class="pm-row-actions">
              <button onclick={() => edit(map)}>Edit</button>
              <button onclick={() => duplicate(map)}>Duplicate</button>
              <button onclick={() => dispatchCpp("exportPercussionMap", map.entityId)}>Export</button>
              <button class="danger" class:confirm={pendingDeleteId === map.entityId}
                title={map.references?.length ? `In use: ${map.references.join(", ")}` : "Delete map"}
                onclick={() => {
                  if (pendingDeleteId === map.entityId) dispatchCpp("deletePercussionMap", map.entityId);
                  else pendingDeleteId = map.entityId;
                }}>{pendingDeleteId === map.entityId ? "Confirm" : "Delete"}</button>
            </td>
          </tr>
        {/each}
      </tbody>
    </table>
  {:else}
    <div class="pm-empty">No percussion maps yet. Import a vendor “(PM)” .doricolib or create one.</div>
  {/if}
</section>

{#if draft}
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="pm-backdrop" onkeydown={(e) => { if (e.key === "Escape" && !saving) draft = null; }}>
    <div class="pm-dialog pm-editor" role="dialog" aria-labelledby="pm-editor-heading">
      <header>
        <h2 id="pm-editor-heading">{draftIsNew ? "New percussion map" : "Edit percussion map"}</h2>
        <button aria-label="Close" disabled={saving} onclick={() => { draft = null; }}>✕</button>
      </header>
      <div class="pm-dialog-body">
        <div class="pm-fields">
          <label><span>Name</span><input bind:value={draft.name} /></label>
          <label><span>Entity ID</span><input value={draft.entityId} readonly /></label>
          <label class="pm-narrow"><span>Version</span>
            <input type="number" min="1" value={draft.version}
              oninput={(e) => { draft.version = Math.max(1, Number(e.currentTarget.value) || 1); }} /></label>
        </div>
        <p class="pm-hint">Each note plays the entry whose techniques best match it (an exact combination beats its parts; ties go to the earlier row). A technique with no entry plays the Natural entry.</p>
        <datalist id="pm-techniques">
          {#each knownTechniques as technique}<option value={technique}></option>{/each}
        </datalist>
        <table class="pm-entries">
          <thead><tr><th>Instrument</th><th>Techniques (+)</th><th>Pitch</th><th>Keyswitches</th><th>Entry name</th><th></th></tr></thead>
          <tbody>
            {#each draft.entries as entry, index}
              <tr>
                <td>
                  <select value={entry.instrument} aria-label="Instrument"
                    onchange={(e) => updateEntry(index, { instrument: e.currentTarget.value })}>
                    {#if entry.instrument && !unpitched.some((i) => i.entityID === entry.instrument)}
                      <option value={entry.instrument}>{instrumentName(entry.instrument)}</option>
                    {/if}
                    {#each unpitched as instrument}
                      <option value={instrument.entityID}>{instrument.name}</option>
                    {/each}
                  </select>
                </td>
                <td><input list="pm-techniques" aria-label="Techniques" value={formatTechniques(entry.techniques)}
                  onchange={(e) => updateEntry(index, { techniques: parseTechniques(e.currentTarget.value) })} /></td>
                <td class="pm-pitch">
                  <input type="number" min="0" max="127" aria-label="Pitch" value={entry.pitch}
                    oninput={(e) => updateEntry(index, { pitch: Number(e.currentTarget.value) })} />
                  <span>{pitchName(entry.pitch)}</span>
                </td>
                <td><input aria-label="Keyswitches" placeholder="none" value={(entry.keyswitches || []).join(", ")}
                  onchange={(e) => {
                    const keys = parseKeyswitches(e.currentTarget.value);
                    if (keys) updateEntry(index, { keyswitches: keys });
                    else e.currentTarget.value = (entry.keyswitches || []).join(", ");
                  }} /></td>
                <td><input aria-label="Entry name" value={entry.name}
                  onchange={(e) => updateEntry(index, { name: e.currentTarget.value })} /></td>
                <td class="pm-entry-actions">
                  <button aria-label="Move up" disabled={index === 0} onclick={() => moveEntry(index, -1)}>↑</button>
                  <button aria-label="Move down" disabled={index === draft.entries.length - 1} onclick={() => moveEntry(index, 1)}>↓</button>
                  <button aria-label="Remove entry" onclick={() => { draft.entries = draft.entries.filter((_, i) => i !== index); }}>✕</button>
                </td>
              </tr>
            {/each}
          </tbody>
        </table>
        <button class="pm-add" onclick={addEntry}>+ Add entry</button>
        {#if problems.length}
          <ul class="pm-problems">{#each problems as problem}<li>{problem}</li>{/each}</ul>
        {/if}
      </div>
      <footer>
        <button disabled={saving} onclick={() => { draft = null; }}>Cancel</button>
        <button class="primary" disabled={saving || problems.length > 0} onclick={save}>{saving ? "Saving…" : "Save map"}</button>
      </footer>
    </div>
  </div>
{/if}

{#if importOffer?.maps?.length}
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="pm-backdrop" onkeydown={(e) => { if (e.key === "Escape") importOffer = null; }}>
    <div class="pm-dialog" role="dialog" aria-labelledby="pm-import-heading">
      <header>
        <h2 id="pm-import-heading">Import percussion maps</h2>
        <button aria-label="Close" onclick={() => { importOffer = null; }}>✕</button>
      </header>
      <div class="pm-dialog-body">
        <ul class="pm-candidates">
          {#each importOffer.maps as candidate (candidate.map.entityId)}
            <li>
              <label>
                <input type="checkbox" checked={importSelection.has(candidate.map.entityId)}
                  disabled={candidate.status === "same"}
                  onchange={() => toggleImport(candidate.map.entityId)} />
                <span class="pm-candidate">
                  <strong>{candidate.map.name}</strong>
                  <span class="pm-status {candidate.status}">{statusLabel[candidate.status] || candidate.status}{candidate.status === "changed" || candidate.status === "newer" ? ` (library v${candidate.existingVersion}, file v${candidate.map.version})` : ""}</span>
                  <span class="pm-id">{candidate.map.entries.length} entries · {candidate.file}</span>
                </span>
              </label>
            </li>
          {/each}
        </ul>
        {#if importOffer.errors?.length}
          <ul class="pm-problems">{#each importOffer.errors as error}<li>{error}</li>{/each}</ul>
        {/if}
      </div>
      <footer>
        <button onclick={() => { importOffer = null; }}>Cancel</button>
        <button class="primary" disabled={importSelection.size === 0} onclick={confirmImport}>Import {importSelection.size}</button>
      </footer>
    </div>
  </div>
{/if}

<style>
  .pm-section { margin-top: 36px; }
  .pm-toolbar { display: flex; align-items: flex-end; justify-content: space-between; gap: 16px; margin-bottom: 16px; }
  .pm-title { font-family: "Sora","Inter",sans-serif; font-size: 1.15rem; font-weight: 700; margin: 0; color: #e5e3ff; }
  .pm-sub { margin: 4px 0 0; color: #a8a7d5; font-size: .78rem; }
  .pm-actions, .pm-row-actions, .pm-entry-actions { display: flex; gap: 8px; }
  button { background: transparent; border: 1px solid #95a9ff; color: #95a9ff; font: inherit; font-size: .8rem;
    padding: 6px 12px; border-radius: 4px; cursor: pointer; }
  button:hover:not(:disabled) { background: rgba(149,169,255,.12); color: #c0ccff; }
  button:disabled { opacity: .4; cursor: default; }
  button.danger { border-color: #595879; color: #ff6e84; }
  button.danger.confirm { background: #e33; border-color: #e33; color: #fff; }
  button.primary { background: #1e4cd5; border-color: #1e4cd5; color: #fff; }
  .pm-message { display: flex; justify-content: space-between; align-items: center; gap: 12px; margin-bottom: 12px;
    padding: 8px 12px; border-radius: 4px; font-size: .8rem;
    background: rgba(255,107,107,.12); color: #ff8787; border: 1px solid rgba(255,107,107,.25); }
  .pm-message.ok { background: rgba(64,192,87,.12); color: #69db7c; border-color: rgba(64,192,87,.25); }
  .pm-dismiss { border: 0; padding: 2px 6px; color: inherit; }
  table { width: 100%; border-collapse: collapse; font-size: .82rem; }
  th { text-align: left; color: #a8a7d5; font-weight: 600; font-size: .7rem; text-transform: uppercase;
    letter-spacing: .06em; padding: 6px 8px; border-bottom: 1px solid #44446c; }
  td { padding: 8px; border-bottom: 1px solid rgba(68,68,108,.35); vertical-align: middle; }
  .pm-table td strong { display: block; color: #e5e3ff; }
  .pm-id { display: block; color: #72719c; font-size: .7rem; overflow-wrap: anywhere; }
  .pm-empty { padding: 18px; border: 1px dashed #44446c; border-radius: 4px; color: #a8a7d5; font-size: .82rem; text-align: center; }
  .pm-backdrop { position: fixed; inset: 0; z-index: 1000; display: flex; align-items: center; justify-content: center;
    background: rgba(0,0,0,.55); }
  .pm-dialog { width: 620px; max-width: 94vw; max-height: 90vh; display: flex; flex-direction: column;
    background: #0e0d38; border: 1px solid #44446c; border-radius: 4px; box-shadow: 0 10px 30px rgba(0,0,0,.5); color: #e5e3ff; }
  .pm-editor { width: 1100px; }
  .pm-dialog header, .pm-dialog footer { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 14px 18px; }
  .pm-dialog header { border-bottom: 1px solid #44446c; }
  .pm-dialog footer { justify-content: flex-end; border-top: 1px solid #44446c; }
  .pm-dialog h2 { margin: 0; font-family: "Sora","Inter",sans-serif; font-size: 1rem; }
  .pm-dialog-body { padding: 16px 18px; overflow: auto; }
  .pm-fields { display: grid; grid-template-columns: 2fr 2fr 90px; gap: 12px; margin-bottom: 10px; }
  label { display: flex; flex-direction: column; gap: 4px; }
  label > span { color: #a8a7d5; font-size: .7rem; font-weight: 600; text-transform: uppercase; letter-spacing: .06em; }
  input, select { background: #131342; border: 1px solid #44446c; border-radius: 4px; color: #e5e3ff; font: inherit;
    font-size: .8rem; padding: 6px 8px; min-width: 0; width: 100%; box-sizing: border-box; }
  input[readonly] { color: #a8a7d5; }
  .pm-hint { color: #a8a7d5; font-size: .76rem; margin: 0 0 12px; }
  .pm-entries td { padding: 4px; }
  .pm-pitch { display: flex; align-items: center; gap: 6px; }
  .pm-pitch input { width: 64px; }
  .pm-pitch span { color: #a8a7d5; font-size: .72rem; min-width: 30px; }
  .pm-entry-actions button { padding: 4px 8px; }
  .pm-add { margin-top: 10px; }
  .pm-problems { margin: 12px 0 0; padding: 8px 12px 8px 28px; border: 1px solid rgba(255,107,107,.25); border-radius: 4px;
    background: rgba(255,107,107,.08); color: #ff8787; font-size: .78rem; }
  .pm-candidates { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 8px; }
  .pm-candidates label { flex-direction: row; align-items: flex-start; gap: 10px; }
  .pm-candidates input { width: auto; margin-top: 3px; }
  .pm-candidate { display: flex; flex-direction: column; gap: 2px; font-size: .82rem; }
  .pm-status { font-size: .72rem; color: #a8a7d5; }
  .pm-status.new, .pm-status.newer { color: #69db7c; }
  .pm-status.changed { color: #fbbf24; }
</style>
