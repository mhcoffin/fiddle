<script>
  import { onFromCpp, dispatchCpp } from "./ipc.js";
  import {
    IMPORT_STATUS_LABELS, KIND_LABELS, SOURCE_STATUS_LABELS, defaultImportSelection, filterCandidates,
    initialSearch, mapsFor, sourceFileName, summarizeSourceCheck,
  } from "./libraryMaps.js";
  import {
    PERCUSSION_TECHNIQUES, blankEntry, duplicatePercussionMap, formatTechniques, isUnpitchedInstrument,
    newPercussionMap, parseKeyswitches, parseTechniques, pitchName, validatePercussionMap,
  } from "./percussionMaps.js";

  // One library's maps of one kind (docs/library-map-import.md). Imports and
  // edits are catalog changes, saved at once and undone with "Undo catalog".
  // The editor keeps this mounted, so messages and update checks survive
  // closing the pane; it floats over the patch list only while `open`.
  let {
    library = { id: "", name: "", vendor: "" },
    kind = "expression",
    maps = [],          // every library's maps, from setLibraryMaps
    saved = true,       // false until the library's first Save
    instruments = [],   // Dorico instruments, for the percussion map editor
    open = false,       // shown as a floating pane over the patch list
    onclose = () => {},
  } = $props();

  const labels = $derived(KIND_LABELS[kind]);
  let ownMaps = $derived(mapsFor(maps, library.id, kind));
  let message = $state(null); // { text, ok }
  let pendingDeleteId = $state("");
  let pendingUpdateId = $state("");
  let importOffer = $state(null); // { origin, maps, errors }
  let importSelection = $state(new Set());
  let search = $state("");
  let sourceStatus = $state({}); // entityId → { status, version, file, movedTo }
  let checking = $state(false);
  // A check is near-instant, so the spinner shows for at least this long and
  // the summary line and status cells flash when a check you asked for ends.
  const MIN_CHECK_MS = 700;
  let checkStartedAt = 0;
  let lastCheck = $state(null); // { time, text, tone }
  let checkFlash = $state(0);
  let draft = $state(null);
  let draftIsNew = $state(false);
  let draftSaving = $state(false);

  const mine = (data) => data?.libraryId === library.id && (!data.kind || data.kind === kind);
  onFromCpp("libraryMapResult", (result) => {
    if (!mine(result)) return;
    message = { text: result.message, ok: result.success };
    if (result.action === "save") {
      draftSaving = false;
      if (result.success) draft = null;
    }
    if (result.action === "delete") pendingDeleteId = "";
    if (result.action === "update") pendingUpdateId = "";
  });
  onFromCpp("libraryMapImportCandidates", (offer) => {
    if (!mine(offer)) return;
    importOffer = offer;
    importSelection = defaultImportSelection(offer.maps);
    search = offer.origin === "dorico" ? initialSearch(library) : "";
    if (!offer.maps?.length)
      message = { text: offer.errors?.length ? offer.errors.join("; ") : `No ${labels.plural} found`, ok: false };
  });
  onFromCpp("libraryMapSourceStatus", (data) => {
    if (data?.libraryId !== library.id) return;
    const next = {};
    for (const status of data.statuses || [])
      if (status.kind === kind) next[status.entityId] = status;
    const finish = () => {
      const asked = checking;
      checking = false;
      sourceStatus = next;
      lastCheck = {
        time: new Date().toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }),
        ...summarizeSourceCheck(Object.values(next)),
      };
      if (asked) checkFlash += 1;
    };
    const wait = checking ? checkStartedAt + MIN_CHECK_MS - Date.now() : 0;
    if (wait > 0) setTimeout(finish, wait); else finish();
  });

  let visibleCandidates = $derived(importOffer ? filterCandidates(importOffer.maps, search) : []);
  let chosenCount = $derived(importOffer ? importOffer.maps.filter((c) => importSelection.has(c.entityId)).length : 0);
  const toggleImport = (id) => {
    const next = new Set(importSelection);
    if (next.has(id)) next.delete(id); else next.add(id);
    importSelection = next;
  };
  const confirmImport = () => {
    const chosen = importOffer.maps.filter((c) => importSelection.has(c.entityId))
      .map((c) => ({ file: c.file, entityId: c.entityId }));
    if (chosen.length) dispatchCpp("importLibraryMaps", library.id, kind, chosen);
    importOffer = null;
  };
  const checkSources = () => {
    if (checking) return; // already under way; the spinner says so
    checking = true;
    checkStartedAt = Date.now();
    dispatchCpp("checkLibraryMapSources", library.id);
  };
  const update = (map) => {
    const status = sourceStatus[map.entityId];
    if (map.edited && pendingUpdateId !== map.entityId) { pendingUpdateId = map.entityId; return; }
    dispatchCpp("updateLibraryMap", library.id, kind, map.entityId, status?.movedTo || "");
  };
  const updateLabel = (map) => {
    const status = sourceStatus[map.entityId];
    if (pendingUpdateId === map.entityId) return "Discard edits & update";
    return status?.movedTo ? "Use moved file" : "Update";
  };
  const canUpdate = (map) => {
    const status = sourceStatus[map.entityId];
    return status && (status.status === "newer" || status.status === "differs" ||
      (status.movedTo && status.status !== "missing"));
  };
  const statusText = (map) => {
    const status = sourceStatus[map.entityId];
    if (!map.sourcePath) return "";
    if (!status) return "";
    const base = SOURCE_STATUS_LABELS[status.status] || status.status;
    const moved = status.movedTo ? ` · moved to ${status.movedTo.split("/").pop()}` : "";
    const version = status.status === "newer" ? ` (v${status.version})` : "";
    return base + version + moved;
  };

  // Percussion map editor.
  let unpitched = $derived(instruments.filter((i) => isUnpitchedInstrument(i.entityID))
    .sort((a, b) => String(a.name).localeCompare(String(b.name))));
  const instrumentName = (id) => instruments.find((i) => i.entityID === id)?.name || id;
  let problems = $derived(draft ? validatePercussionMap(draft) : []);
  let knownTechniques = $derived([...new Set([
    ...PERCUSSION_TECHNIQUES,
    ...ownMaps.flatMap((m) => (m.map?.entries || []).flatMap((e) => e.techniques)),
  ])].sort());
  const startNew = () => {
    draft = newPercussionMap("New percussion map", unpitched[0]?.entityID || "");
    draftIsNew = true;
  };
  const edit = (map) => { draft = structuredClone($state.snapshot(map.map)); draftIsNew = false; };
  const duplicate = (map) => { draft = duplicatePercussionMap($state.snapshot(map.map)); draftIsNew = true; };
  const saveDraft = () => {
    if (problems.length || draftSaving) return;
    draftSaving = true;
    dispatchCpp("saveLibraryMap", library.id, $state.snapshot(draft));
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
</script>

{#if open}
<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="lmap-float" onclick={(e) => { if (e.target === e.currentTarget) onclose(); }}>
<section class="lmap" role="dialog" aria-label={labels.title}>
  <header class="lmap-head">
    <span class="lmap-title">{labels.title}</span>
    <span class="lmap-count">{ownMaps.length}</span>
    <button class="lmap-close" aria-label="Close {labels.title}" onclick={onclose}>✕</button>
  </header>
  <div class="lmap-body">
  {#if !saved}
    <p class="lmap-note">Save the library first, then import its {labels.plural}.</p>
  {:else}
    <div class="lmap-toolbar">
      <p class="lmap-sub">This library's {labels.plural}. They're saved as soon as you import them.</p>
      <div class="lmap-actions">
        {#if kind === "percussion"}<button onclick={startNew}>+ New Map</button>{/if}
        <button onclick={() => dispatchCpp("chooseLibraryMapFiles", library.id, kind)}>Import .doricolib…</button>
        <button onclick={() => dispatchCpp("findDoricoLibraryMaps", library.id, kind)}>Find in Dorico Library…</button>
        <button class="lmap-check" aria-busy={checking} disabled={!ownMaps.some((m) => m.sourcePath)}
          title="Compare each imported map with its .doricolib file" onclick={checkSources}>
          <span class="lmap-check-icon" class:spinning={checking} aria-hidden="true">↻</span>Check for updates</button>
      </div>
    </div>
    <div class="lmap-check-result" role="status">
      {#if checking}
        <span class="checking">Checking sources…</span>
      {:else if lastCheck}
        {#key checkFlash}
          <span class="lmap-check-line {lastCheck.tone}" class:flash={checkFlash > 0}>
            {lastCheck.tone === "ok" ? "✓" : "⚠"} Checked {lastCheck.time} · {lastCheck.text}</span>
        {/key}
      {/if}
    </div>
    {#if message}
      <div class="lmap-message" class:ok={message.ok} role="status">
        {message.text}<button class="lmap-dismiss" aria-label="Dismiss" onclick={() => { message = null; }}>✕</button>
      </div>
    {/if}
    {#if ownMaps.length}
      <table class="lmap-table">
        <thead><tr><th>Name</th><th>Source</th><th>Used by</th><th>Status</th><th></th></tr></thead>
        <tbody>
          {#each ownMaps as map (map.entityId)}
            {@const status = sourceStatus[map.entityId]}
            <tr>
              <td><strong>{map.name}</strong>
                <span class="lmap-id">{map.entityId}{map.version ? ` · v${map.version}` : ""}{kind === "percussion" ? ` · ${map.entryCount} entries` : ""}</span></td>
              <td title={map.sourcePath}>{map.sourcePath ? sourceFileName(map) : "Made in Fiddle"}{map.edited ? " · edited" : ""}</td>
              <td title={(map.references || []).join("\n")}>{map.references?.length || "—"}</td>
              <td class="lmap-status {status?.status || ''}">
                {#key checkFlash}<span class:flash={checkFlash > 0}>{statusText(map)}</span>{/key}</td>
              <td><div class="lmap-row-actions">
                {#if canUpdate(map)}
                  <button class:confirm={pendingUpdateId === map.entityId}
                    title={map.edited ? "Replaces the edits made in Fiddle with the file's map" : "Replace with the file's map"}
                    onclick={() => update(map)}>{updateLabel(map)}</button>
                {/if}
                {#if kind === "percussion"}
                  <button onclick={() => edit(map)}>Edit</button>
                  <button onclick={() => duplicate(map)}>Duplicate</button>
                {/if}
                <button onclick={() => dispatchCpp("exportLibraryMap", library.id, kind, map.entityId)}>Export</button>
                <button class="danger" class:confirm={pendingDeleteId === map.entityId}
                  disabled={map.references?.length > 0}
                  title={map.references?.length ? `In use: ${map.references.join(", ")}` : "Delete map"}
                  onclick={() => {
                    if (pendingDeleteId === map.entityId) dispatchCpp("deleteLibraryMap", library.id, kind, map.entityId);
                    else pendingDeleteId = map.entityId;
                  }}>{pendingDeleteId === map.entityId ? "Confirm" : "Delete"}</button>
              </div></td>
            </tr>
          {/each}
        </tbody>
      </table>
    {:else}
      <div class="lmap-empty">No {labels.plural} yet. Import a vendor .doricolib or find one in Dorico's library.</div>
    {/if}
  {/if}
  </div>
</section>
</div>
{/if}

{#if importOffer?.maps?.length}
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="lmap-backdrop" onkeydown={(e) => { if (e.key === "Escape") { e.stopPropagation(); importOffer = null; } }}>
    <div class="lmap-dialog" role="dialog" aria-labelledby="lmap-import-heading-{kind}">
      <header>
        <h2 id="lmap-import-heading-{kind}">Import {labels.plural} into {library.name}</h2>
        <button aria-label="Close" onclick={() => { importOffer = null; }}>✕</button>
      </header>
      <div class="lmap-dialog-body">
        <input class="lmap-search" type="search" aria-label="Search {labels.plural}" placeholder="Search by name, vendor or file"
          bind:value={search} />
        <p class="lmap-sub">{visibleCandidates.length} of {importOffer.maps.length} shown · {chosenCount} chosen</p>
        <ul class="lmap-candidates">
          {#each visibleCandidates as candidate (candidate.entityId)}
            <li>
              <label>
                <input type="checkbox" checked={importSelection.has(candidate.entityId)}
                  disabled={candidate.status === "same"}
                  onchange={() => toggleImport(candidate.entityId)} />
                <span class="lmap-candidate">
                  <strong>{candidate.name}</strong>
                  <span class="lmap-import-status {candidate.status}">{IMPORT_STATUS_LABELS[candidate.status] || candidate.status}{candidate.status === "changed" || candidate.status === "newer" ? ` (library v${candidate.existingVersion}, file v${candidate.version})` : ""}</span>
                  <span class="lmap-id">{candidate.entityId} · {candidate.file}</span>
                </span>
              </label>
            </li>
          {/each}
        </ul>
        {#if importOffer.errors?.length}
          <ul class="lmap-problems">{#each importOffer.errors as error}<li>{error}</li>{/each}</ul>
        {/if}
      </div>
      <footer>
        <button onclick={() => { importOffer = null; }}>Cancel</button>
        <button class="primary" disabled={chosenCount === 0} onclick={confirmImport}>Import {chosenCount}</button>
      </footer>
    </div>
  </div>
{/if}

{#if draft}
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="lmap-backdrop" onkeydown={(e) => { if (e.key === "Escape") { e.stopPropagation(); if (!draftSaving) draft = null; } }}>
    <div class="lmap-dialog lmap-editor" role="dialog" aria-labelledby="lmap-editor-heading">
      <header>
        <h2 id="lmap-editor-heading">{draftIsNew ? "New percussion map" : "Edit percussion map"}</h2>
        <button aria-label="Close" disabled={draftSaving} onclick={() => { draft = null; }}>✕</button>
      </header>
      <div class="lmap-dialog-body">
        <div class="lmap-fields">
          <label><span>Name</span><input bind:value={draft.name} /></label>
          <label><span>Entity ID</span><input value={draft.entityId} readonly /></label>
          <label class="lmap-narrow"><span>Version</span>
            <input type="number" min="1" value={draft.version}
              oninput={(e) => { draft.version = Math.max(1, Number(e.currentTarget.value) || 1); }} /></label>
        </div>
        <p class="lmap-hint">Each note plays the entry whose techniques best match it (an exact combination beats its parts; ties go to the earlier row). A technique with no entry plays the Natural entry.</p>
        <datalist id="lmap-techniques">
          {#each knownTechniques as technique}<option value={technique}></option>{/each}
        </datalist>
        <table class="lmap-entries">
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
                <td><input list="lmap-techniques" aria-label="Techniques" value={formatTechniques(entry.techniques)}
                  onchange={(e) => updateEntry(index, { techniques: parseTechniques(e.currentTarget.value) })} /></td>
                <td class="lmap-pitch">
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
                <td><div class="lmap-row-actions">
                  <button aria-label="Move up" disabled={index === 0} onclick={() => moveEntry(index, -1)}>↑</button>
                  <button aria-label="Move down" disabled={index === draft.entries.length - 1} onclick={() => moveEntry(index, 1)}>↓</button>
                  <button aria-label="Remove entry" onclick={() => { draft.entries = draft.entries.filter((_, i) => i !== index); }}>✕</button>
                </div></td>
              </tr>
            {/each}
          </tbody>
        </table>
        <button class="lmap-add" onclick={addEntry}>+ Add entry</button>
        {#if problems.length}
          <ul class="lmap-problems">{#each problems as problem}<li>{problem}</li>{/each}</ul>
        {/if}
      </div>
      <footer>
        <button disabled={draftSaving} onclick={() => { draft = null; }}>Cancel</button>
        <button class="primary" disabled={draftSaving || problems.length > 0} onclick={saveDraft}>{draftSaving ? "Saving…" : "Save map"}</button>
      </footer>
    </div>
  </div>
{/if}

<style>
  /* Centred over the blurred patch list, a little shorter than it. */
  .lmap-float { position: absolute; inset: 0; z-index: 200; display: flex; align-items: center; justify-content: center;
    background: rgba(3, 3, 20, .62); backdrop-filter: blur(2px); -webkit-backdrop-filter: blur(2px); }
  .lmap { width: min(1080px, calc(100% - 48px)); height: calc(100% - 48px); display: flex; flex-direction: column;
    overflow: hidden; box-sizing: border-box; background: #0e0d38; border: 1px solid #44446c; border-radius: 12px;
    box-shadow: 0 16px 48px rgba(0,0,0,.5); color: #e5e3ff; }
  .lmap-head { display: flex; align-items: center; gap: 10px; padding: 16px 20px; background: #131342;
    border-bottom: 1px solid #44446c; }
  .lmap-head .lmap-close { margin-left: auto; border-color: #555582; color: #d5d1ff; }
  .lmap-body { min-height: 0; overflow-y: auto; padding: 18px 20px 24px; }
  .lmap-title { font-family: "Sora","Inter",sans-serif; font-size: .95rem; font-weight: 700; color: #e5e3ff; }
  .lmap-count { color: #a8a7d5; font-size: .75rem; border: 1px solid #44446c; border-radius: 10px; padding: 1px 8px; }
  .lmap-toolbar { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 10px; margin-bottom: 6px; }
  .lmap-check { display: inline-flex; align-items: center; gap: 6px; }
  .lmap-check[aria-busy="true"] { cursor: progress; }
  .lmap-check-icon { display: inline-block; font-size: .95rem; line-height: 1; }
  .lmap-check-icon.spinning { animation: lmap-spin .7s linear infinite; }
  @keyframes lmap-spin { to { transform: rotate(360deg); } }
  /* One line, always reserved, so the table never jumps when it fills in. */
  .lmap-check-result { min-height: 18px; margin-bottom: 8px; text-align: right; font-size: .74rem; color: #a8a7d5; }
  .lmap-check-line { display: inline-block; padding: 1px 6px; border-radius: 4px; }
  .lmap-check-line.ok { color: #69db7c; }
  .lmap-check-line.warn { color: #fbbf24; }
  .flash { animation: lmap-flash 1.2s ease-out; }
  @keyframes lmap-flash { from { background: rgba(149,169,255,.32); } to { background: transparent; } }
  @media (prefers-reduced-motion: reduce) {
    .lmap-check-icon.spinning { animation: none; opacity: .5; }
  }
  .lmap-sub, .lmap-note { margin: 0; color: #a8a7d5; font-size: .76rem; }
  .lmap-note { padding-bottom: 12px; }
  @media (max-width: 680px) { .lmap { width: calc(100% - 24px); } }
  .lmap-actions, .lmap-row-actions { display: flex; flex-wrap: wrap; gap: 6px; }
  button { background: transparent; border: 1px solid #95a9ff; color: #95a9ff; font: inherit; font-size: .76rem;
    padding: 5px 10px; border-radius: 4px; cursor: pointer; }
  button:hover:not(:disabled) { background: rgba(149,169,255,.12); color: #c0ccff; }
  button:disabled { opacity: .4; cursor: default; }
  button.danger { border-color: #595879; color: #ff6e84; }
  button.confirm { background: #e33; border-color: #e33; color: #fff; }
  button.primary { background: #1e4cd5; border-color: #1e4cd5; color: #fff; }
  .lmap-message { display: flex; justify-content: space-between; align-items: center; gap: 12px; margin-bottom: 10px;
    padding: 8px 12px; border-radius: 4px; font-size: .78rem;
    background: rgba(255,107,107,.12); color: #ff8787; border: 1px solid rgba(255,107,107,.25); }
  .lmap-message.ok { background: rgba(64,192,87,.12); color: #69db7c; border-color: rgba(64,192,87,.25); }
  .lmap-dismiss { border: 0; padding: 2px 6px; color: inherit; }
  table { width: 100%; border-collapse: collapse; font-size: .8rem; }
  th { text-align: left; color: #a8a7d5; font-weight: 600; font-size: .68rem; text-transform: uppercase;
    letter-spacing: .06em; padding: 6px 8px; border-bottom: 1px solid #44446c; }
  td { padding: 7px 8px; border-bottom: 1px solid rgba(68,68,108,.35); vertical-align: middle; color: #c9c8ef; }
  .lmap-table td strong { display: block; color: #e5e3ff; }
  .lmap-id { display: block; color: #72719c; font-size: .68rem; overflow-wrap: anywhere; }
  .lmap-status { width: 24%; font-size: .74rem; color: #a8a7d5; }
  .lmap-status.newer, .lmap-status.differs { color: #fbbf24; }
  .lmap-status.missing { color: #ff8787; }
  .lmap-status.current { color: #69db7c; }
  .lmap-empty { padding: 14px; border: 1px dashed #44446c; border-radius: 4px; color: #a8a7d5; font-size: .8rem; text-align: center; }
  .lmap-backdrop { position: fixed; inset: 0; z-index: 1000; display: flex; align-items: center; justify-content: center;
    background: rgba(0,0,0,.55); }
  /* Fixed height, so search results never resize it while you type. */
  .lmap-dialog { width: 680px; max-width: 94vw; height: min(820px, 90vh); display: flex; flex-direction: column;
    background: #0e0d38; border: 1px solid #44446c; border-radius: 4px; box-shadow: 0 10px 30px rgba(0,0,0,.5); color: #e5e3ff; }
  .lmap-editor { width: 1100px; }
  .lmap-dialog header, .lmap-dialog footer { display: flex; align-items: center; justify-content: space-between; gap: 10px; padding: 14px 18px; }
  .lmap-dialog header { border-bottom: 1px solid #44446c; }
  .lmap-dialog footer { justify-content: flex-end; border-top: 1px solid #44446c; }
  .lmap-dialog h2 { margin: 0; font-family: "Sora","Inter",sans-serif; font-size: 1rem; }
  .lmap-dialog-body { flex: 1; min-height: 0; padding: 16px 18px; overflow: auto; display: flex; flex-direction: column; gap: 10px; }
  .lmap-search { font-size: .85rem; }
  .lmap-fields { display: grid; grid-template-columns: 2fr 2fr 90px; gap: 12px; }
  label { display: flex; flex-direction: column; gap: 4px; }
  label > span { color: #a8a7d5; font-size: .7rem; font-weight: 600; text-transform: uppercase; letter-spacing: .06em; }
  input, select { background: #131342; border: 1px solid #44446c; border-radius: 4px; color: #e5e3ff; font: inherit;
    font-size: .8rem; padding: 6px 8px; min-width: 0; width: 100%; box-sizing: border-box; }
  input[readonly] { color: #a8a7d5; }
  .lmap-hint { color: #a8a7d5; font-size: .76rem; margin: 0; }
  .lmap-entries td { padding: 4px; }
  .lmap-pitch { display: flex; align-items: center; gap: 6px; }
  .lmap-pitch input { width: 64px; }
  .lmap-pitch span { color: #a8a7d5; font-size: .72rem; min-width: 30px; }
  .lmap-add { align-self: flex-start; }
  .lmap-problems { margin: 0; padding: 8px 12px 8px 28px; border: 1px solid rgba(255,107,107,.25); border-radius: 4px;
    background: rgba(255,107,107,.08); color: #ff8787; font-size: .76rem; }
  .lmap-candidates { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; gap: 8px; }
  .lmap-candidates label { flex-direction: row; align-items: flex-start; gap: 10px; }
  .lmap-candidates input { width: auto; margin-top: 3px; }
  .lmap-candidate { display: flex; flex-direction: column; gap: 2px; font-size: .8rem; }
  .lmap-import-status { font-size: .72rem; color: #a8a7d5; }
  .lmap-import-status.new, .lmap-import-status.newer { color: #69db7c; }
  .lmap-import-status.changed { color: #fbbf24; }
</style>
