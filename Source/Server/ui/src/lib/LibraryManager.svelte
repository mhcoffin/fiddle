<script>
  /** @type {import('svelte')} */
  import { onMount } from "svelte";
  import { onFromCpp, dispatchCpp } from "./ipc.js";
  import ExpressionMapPicker from "./ExpressionMapPicker.svelte";
  import ensembleData from "./standard_ensembles.json";
  import { populateScoreOrder, instrumentScoreOrder } from "./orchestralOrder.js";
  import { addStripRange } from "./mixerSelection.js";
  import { createBlankLibraryPatch, createLibraryPatch, duplicateLibraryPatch, moveLibraryPatch, moveLibraryPatchByOffset, pluginSetupStatus, shouldMarkLibraryEditorDirtyForPreviewChange, sortLibraryPatchesOrchestrally, togglePatchSelection, updateSelectedPatches } from "./libraryPatchModel.js";

  const standardEnsembles = ensembleData.ensembles;

  // ── Expression maps from C++ ───────────────────────
  let availableXmaps = $state([]);
  let expressionMapNames = $derived(
    new Map(availableXmaps.map((map) => [map.entityID, map.name])),
  );
  onFromCpp("setExpressionMaps", (data) => {
    try { availableXmaps = data; } catch (e) { /* ignore */ }
  });

  // ── Saved libraries from C++ ────────────────────────
  /** @type {{ id: string, name: string, vendor: string, variant: string }[]} */
  let savedLibraries = $state([]);
  onFromCpp("setLibraryList", (data) => {
    try { savedLibraries = data; } catch (e) { /* ignore */ }
  });

  // ── Load a library into the editor from C++ ─────────
  onFromCpp("setLibraryData", (data) => {
    try {
      editorLibId = data.id;
      editorLibName = data.name;
      editorLibVendor = data.vendor ?? "";
      editorLibVariant = data.variant ?? "";
      patches = (data.patches || []).map((patch) => ({
        id:        patch.id || crypto.randomUUID(),
        entityID:  patch.entityID || "",
        instrumentName: patch.instrumentName || "",
        name:      patch.name || "",
        family:    patch.family || "",
        character: patch.character || "",
        vstPlugin: Number(patch.vstPlugin) || patch.pluginUid || 0,
        exprMap:   patch.exprMap || "",
        pluginUid: patch.pluginUid || 0,
        hasPluginState: patch.hasPluginState || false,
        pluginStatePending: false,
        usageCount: Number(patch.usageCount) || 0,
        outOfDateLayerCount: Number(patch.outOfDateLayerCount) || 0,
      }));
      selectedPatchIds = new Set();
      searchQuery = "";
      instrumentChooserPatchId = "";
      modalView = "editor";
      editorDirty = false;
    } catch (e) { /* ignore */ }
  });

  // ── Scanned plugins from C++ ───────────────────────
  let scannedPlugins = $state([]);
  onFromCpp("setPluginList", (data) => {
    try { scannedPlugins = data; } catch (e) { /* ignore */ }
  });

  // ── Dashboard state ───────────────────────────────────
  const libraryIcons = ["𝄞", "♩", "𝅘𝅥𝅮", "⏚", "♫", "♬", "🎹", "🎵"];

  // ── Single modal state: "none" | "create" | "editor" ─
  /** @type {"none" | "create" | "editor"} */
  let modalView = $state("none");

  // ── Create-library fields ─────────────────────────────
  let newLibName =    $state("");
  let newLibVendor =  $state("");
  let newLibVariant = $state("");
  let canSubmit = $derived(newLibName.trim().length > 0);

  // ── Editor state ──────────────────────────────────────
  let editorLibId = $state("");
  let editorLibName = $state("");
  let editorLibVendor = $state("");
  let editorLibVariant = $state("");
  let searchQuery = $state("");
  let instrumentChooserPatchId = $state("");
  let searchInput = $state(null);

  // ── Editor dirty tracking ─────────────────────────────
  let editorDirty = $state(false);
  let selectedPatchIds = $state(new Set());
  let selectionAnchorId = $state("");
  let batchPluginUid = $state(0);
  let draggedPatchId = $state("");
  let dropTargetPatchId = $state("");
  let dropAfterTarget = $state(false);

  // ── Dorico instrument database ─────────────────────────
  let allDoricoInstruments = $state([]);

  onFromCpp("setDoricoInstruments", (arr) => {
    allDoricoInstruments = arr;
    populateScoreOrder(arr);
    console.log(`[LibMgr] Received ${arr.length} Dorico instruments`);
  });

  const instrumentDisplayName = (patch) =>
    allDoricoInstruments.find((instrument) => instrument.entityID === patch.entityID)?.name
      || patch.instrumentName
      || patch.entityID;

  /** Filtered Dorico instrument results for optional patch classification.
   *  Sorted by name-match relevance, then XML document order as tiebreaker
   *  (instruments.xml lists the default variant first in each group). */
  let searchResults = $derived.by(() => {
    const q = searchQuery.trim().toLowerCase();
    if (!q) return [];
    const matches = allDoricoInstruments.filter(
      (i) => i.name.toLowerCase().includes(q) || i.entityID.toLowerCase().includes(q)
    );
    // Relevance tier: exact name match (0) > starts-with (1) > contains (2)
    const relevance = (name) => {
      const n = name.toLowerCase();
      if (n === q) return 0;
      if (n.startsWith(q)) return 1;
      return 2;
    };
    matches.sort((a, b) => {
      const ra = relevance(a.name), rb = relevance(b.name);
      if (ra !== rb) return ra - rb;
      // Default (root) instruments before variants.
      const da = a.isDefault ? 0 : 1, db = b.isDefault ? 0 : 1;
      if (da !== db) return da - db;
      // Final tiebreaker: XML document order
      return (a.xmlIndex ?? 99999) - (b.xmlIndex ?? 99999);
    });
    return matches.slice(0, 20);
  });

  /**
   * @typedef {Object} LibraryPatch
   * @property {string}  id
   * @property {string}  entityID
   * @property {string}  instrumentName
   * @property {string}  name
   * @property {string}  family
   * @property {string}  character
   * @property {number}  vstPlugin
   * @property {string}  exprMap
   * @property {number}  pluginUid
   * @property {boolean} hasPluginState
   */

  /** @type {LibraryPatch[]} */
  let patches = $state([]);
  /** @type {string} */ let pendingDeleteId = $state("");
  /** @type {string} */ let pendingLayerUpdateId = $state("");
  let layerUpdateResult = $state("");

  // ── Portal container for modals ───────────────────────
  // Attach modal DOM directly to <body> so overflow:hidden parents
  // can't clip position:fixed overlays in WKWebView.
  let portalTarget = $state(null);
  onMount(() => {
    dispatchCpp("requestExpressionMaps");
    dispatchCpp("requestPluginsState");
    dispatchCpp("requestLibraries");
    dispatchCpp("requestDoricoInstruments");
    const el = document.createElement("div");
    el.id = "lm-portal";
    document.body.appendChild(el);
    portalTarget = el;
    return () => {
      dispatchCpp("closeLibraryPatchPreviews");
      el.remove();
    };
  });

  // ── Actions: create modal ─────────────────────────────
  const openCreate = () => {
    newLibName = ""; newLibVendor = ""; newLibVariant = "";
    modalView = "create";
  };
  const closeCreate = () => { modalView = "none"; };

  // ── Operation feedback ───────────────────────────────
  let buildResult = $state("");

  onFromCpp("setLibraryDeleteResult", (data) => {
    buildResult = typeof data === "string" ? data : "Could not delete the library";
    setTimeout(() => { buildResult = ""; }, 5000);
  });
  onFromCpp("libraryPatchPreviewResult", (data) => {
    buildResult = typeof data === "string" ? data : "Could not open plug-in";
    setTimeout(() => { buildResult = ""; }, 5000);
  });
  onFromCpp("libraryPatchPreviewChanged", (patchId) => {
    if (shouldMarkLibraryEditorDirtyForPreviewChange(modalView)) editorDirty = true;
    if (patchId) {
      patches = patches.map((patch) =>
        patch.id === patchId
          ? { ...patch, hasPluginState: true, pluginStatePending: true }
          : patch
      );
    }
  });
  onFromCpp("libraryLayersUpdateResult", (result) => {
    layerUpdateResult = typeof result === "string"
      ? result
      : result?.message || "Could not update layers from the patch";
    if (result?.success && result.patchId) {
      patches = patches.map((patch) =>
        patch.id === result.patchId
          ? { ...patch, outOfDateLayerCount: 0 }
          : patch
      );
    }
    pendingLayerUpdateId = "";
    setTimeout(() => { layerUpdateResult = ""; }, 5000);
  });

  const handleInitialize = () => {
    if (!canSubmit) return;
    editorLibId = crypto.randomUUID();
    editorLibName = newLibName.trim();
    editorLibVendor = newLibVendor.trim();
    editorLibVariant = newLibVariant.trim();
    patches = [];
    selectedPatchIds = new Set();
    searchQuery = "";
    instrumentChooserPatchId = "";
    modalView = "editor";
    editorDirty = false;
  };

  // ── Actions: editor modal ─────────────────────────────
  const closeEditor = () => {
    dispatchCpp("closeLibraryPatchPreviews");
    instrumentChooserPatchId = "";
    modalView = "none";
  };

  const addPatch = (/** @type {{ entityID: string, name: string, family: string }} */ inst, /** @type {string} */ character = "") => {
    const defaults = { vstPlugin: batchPluginUid };
    patches = [...patches,
      createLibraryPatch(inst, character, crypto.randomUUID(), defaults)];
    editorDirty = true;
  };

  const addBlankPatch = () => {
    const id = crypto.randomUUID();
    const patch = createBlankLibraryPatch(id, { vstPlugin: batchPluginUid });
    patches = [...patches, patch];
    selectedPatchIds = new Set([id]);
    selectionAnchorId = id;
    editorDirty = true;
  };

  const beginInstrumentChoice = (patchId) => {
    searchQuery = "";
    instrumentChooserPatchId = patchId;
    requestAnimationFrame(() => searchInput?.focus());
  };

  const closeInstrumentChooser = () => {
    instrumentChooserPatchId = "";
    searchQuery = "";
  };

  const classifyPatch = (instrument) => {
    patches = patches.map((patch) =>
      patch.id === instrumentChooserPatchId
        ? {
            ...patch,
            entityID: instrument.entityID,
            instrumentName: instrument.name,
            family: instrument.family || patch.family,
          }
        : patch
    );
    editorDirty = true;
    closeInstrumentChooser();
  };

  const clearPatchClassification = () => {
    patches = patches.map((patch) =>
      patch.id === instrumentChooserPatchId
        ? { ...patch, entityID: "", instrumentName: "", family: "" }
        : patch
    );
    editorDirty = true;
    closeInstrumentChooser();
  };

  const expressionMapName = (/** @type {string} */ entityID) => {
    if (!entityID) return "";
    return expressionMapNames.get(entityID) || entityID;
  };

  const setInstrumentExpressionMap = (
    /** @type {string} */ rowId,
    /** @type {string} */ entityID,
  ) => {
    patches = patches.map((patch) =>
      patch.id === rowId ? { ...patch, exprMap: entityID } : patch
    );
    editorDirty = true;
  };

  const addEnsemble = (/** @type {typeof standardEnsembles[0]} */ ens) => {
    for (const inst of ens.instruments) {
      addPatch(inst, inst.type || "");
    }
  };

  const removePatch = (/** @type {string} */ id) => {
    dispatchCpp("discardLibraryPatchPreview", id);
    patches = patches.filter(r => r.id !== id);
    selectedPatchIds.delete(id);
    selectedPatchIds = new Set(selectedPatchIds);
    editorDirty = true;
  };

  const updateLayersFromPatch = (row) => {
    if (editorDirty || !row.outOfDateLayerCount) return;
    if (pendingLayerUpdateId !== row.id) {
      pendingLayerUpdateId = row.id;
      return;
    }
    pendingLayerUpdateId = "";
    dispatchCpp("updateLayersFromLibraryPatch", row.id);
  };

  const duplicatePatch = (row) => {
    const copy = duplicateLibraryPatch(row, crypto.randomUUID());
    patches = [...patches, copy];
    selectedPatchIds = new Set([copy.id]);
    selectionAnchorId = copy.id;
    editorDirty = true;
  };

  const reorderPatch = (patchId, targetId, insertAfter = false) => {
    const reordered = moveLibraryPatch(patches, patchId, targetId, insertAfter);
    if (reordered === patches) return;
    patches = reordered;
    editorDirty = true;
  };

  const movePatchWithKeyboard = (patchId, offset) => {
    const reordered = moveLibraryPatchByOffset(patches, patchId, offset);
    if (reordered === patches) return;
    patches = reordered;
    editorDirty = true;
  };

  const sortPatchesOrchestrally = () => {
    const reordered = sortLibraryPatchesOrchestrally(patches, instrumentScoreOrder);
    if (reordered === patches) return;
    patches = reordered;
    editorDirty = true;
  };

  const beginPatchDrag = (event, patchId) => {
    draggedPatchId = patchId;
    event.dataTransfer.effectAllowed = "move";
    event.dataTransfer.setData("text/plain", patchId);
  };

  const updatePatchDropTarget = (event, patchId) => {
    if (!draggedPatchId || draggedPatchId === patchId) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = "move";
    const bounds = event.currentTarget.getBoundingClientRect();
    dropTargetPatchId = patchId;
    dropAfterTarget = event.clientY >= bounds.top + bounds.height / 2;
  };

  const dropPatch = (event, targetId) => {
    event.preventDefault();
    const patchId = draggedPatchId || event.dataTransfer.getData("text/plain");
    reorderPatch(patchId, targetId, dropAfterTarget);
    draggedPatchId = "";
    dropTargetPatchId = "";
    dropAfterTarget = false;
  };

  const finishPatchDrag = () => {
    draggedPatchId = "";
    dropTargetPatchId = "";
    dropAfterTarget = false;
  };

  /** Open the VST editor in the Library Manager's isolated preview host. */
  const openVstEditor = (/** @type {typeof patches[0]} */ row) => {
    const uid = Number(row.vstPlugin) || row.pluginUid;
    if (!uid) return;
    dispatchCpp("openLibraryPatchEditor", {
      patchId: row.id,
      sourcePatchId: row.sourcePatchId || "",
      title: `${editorLibName} — ${row.name}`,
      pluginUid: uid,
    });
  };
  const selectPatch = (id, event) => {
    const orderedIds = patches.map((patch) => patch.id);
    if (event.shiftKey && selectionAnchorId) {
      selectedPatchIds = addStripRange(
        selectedPatchIds,
        orderedIds,
        selectionAnchorId,
        id,
      );
    } else {
      const isCheckbox = event.currentTarget instanceof HTMLInputElement;
      selectedPatchIds = togglePatchSelection(
        selectedPatchIds,
        id,
        isCheckbox || event.metaKey || event.ctrlKey,
      );
    }
    selectionAnchorId = id;
  };

  const selectAllPatches = () => {
    selectedPatchIds = selectedPatchIds.size === patches.length
      ? new Set()
      : new Set(patches.map((patch) => patch.id));
  };

  const applyPluginToSelection = () => {
    if (selectedPatchIds.size === 0) return;
    for (const patch of patches)
      if (selectedPatchIds.has(patch.id))
        dispatchCpp("discardLibraryPatchPreview", patch.id);
    patches = updateSelectedPatches(patches, selectedPatchIds, {
      vstPlugin: Number(batchPluginUid),
      pluginUid: Number(batchPluginUid),
      hasPluginState: false,
      pluginStatePending: false,
      sourcePatchId: "",
    });
    editorDirty = true;
  };

  const setPatchPlugin = (row, uid) => {
    dispatchCpp("discardLibraryPatchPreview", row.id);
    patches = patches.map((patch) =>
      patch.id === row.id
        ? { ...patch, vstPlugin: uid, pluginUid: uid, hasPluginState: false, pluginStatePending: false, sourcePatchId: "" }
        : patch
    );
    editorDirty = true;
  };

  const applyExpressionMapToSelection = (entityID) => {
    if (selectedPatchIds.size === 0) return;
    patches = updateSelectedPatches(patches, selectedPatchIds, {
      exprMap: entityID,
    });
    editorDirty = true;
  };

  const commitToLibrary = () => {
    const libData = {
      id: editorLibId,
      name: editorLibName,
      vendor: editorLibVendor,
      variant: editorLibVariant,
      patches: patches.map(patch => ({
        id:          patch.id,
        entityID:    patch.entityID,
        name:        patch.name,
        family:      patch.family,
        character:   patch.character,
        vstPlugin:   patch.vstPlugin,
        exprMap:     patch.exprMap,
        hasPluginState: patch.hasPluginState,
        sourcePatchId: patch.sourcePatchId || "",
      })),
    };
    dispatchCpp("saveLibrary", libData);
    editorDirty = false;
    modalView = "none";
  };

  const openLibrary = (/** @type {string} */ libraryId) => {
    dispatchCpp("loadLibrary", libraryId);
  };

  const removeLibrary = (/** @type {string} */ libraryId) => {
    dispatchCpp("deleteLibrary", libraryId);
  };


</script>

<!-- ═══════════════════════════════════════════════════════
     DASHBOARD
     ═══════════════════════════════════════════════════════ -->
<div class="lm-root">
  <header class="lm-header">
    <span class="lm-logo">❋</span>
    <span class="lm-title">VST Library Manager</span>
  </header>
  <div class="lm-divider"></div>
  <div class="lm-body">
    <div class="lm-toolbar">
      <h2 class="lm-section-title">My Libraries</h2>
      <div class="lm-toolbar-actions">
        <button class="lm-create-btn" onclick={openCreate}>+ Add Library</button>
      </div>
    </div>
    {#if buildResult}
      <div class="lm-build-result {buildResult.startsWith('OK') ? 'lm-build-ok' : 'lm-build-error'}">
        {buildResult}
      </div>
    {/if}
    <div class="lm-grid">
      {#each savedLibraries.filter(l => l.name) as lib, i}
        <div class="lm-card" onclick={() => { pendingDeleteId = ""; openLibrary(lib.id); }} role="button" tabindex="0"
          onkeydown={(e) => { if (e.key === 'Enter' || e.key === ' ') openLibrary(lib.id); }}>
          <button class="lm-card-delete {pendingDeleteId === lib.id ? 'lm-card-delete--confirm' : ''}" title="Delete library"
            onclick={(e) => {
              e.stopPropagation();
              if (pendingDeleteId === lib.id) {
                removeLibrary(lib.id);
                pendingDeleteId = "";
              } else {
                pendingDeleteId = lib.id;
              }
            }}
          >{pendingDeleteId === lib.id ? 'Confirm Delete' : 'Delete Library…'}</button>
          <div class="lm-card-icon">{libraryIcons[i % libraryIcons.length]}</div>
          <div class="lm-card-name">{lib.name}</div>
          <div class="lm-card-sub">{[lib.vendor, lib.variant].filter(Boolean).join(" · ") || "Library"}</div>
        </div>
      {/each}
      {#if savedLibraries.filter(l => l.name).length === 0}
        <div class="lm-card lm-card--skeleton">
          <div class="skel-icon"></div>
          <div class="skel-line skel-line--long"></div>
          <div class="skel-line skel-line--short"></div>
        </div>
      {/if}
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════════════════════
     MODAL LAYER — always rendered, visibility driven by modalView
     ═══════════════════════════════════════════════════════ -->

<!-- CREATE LIBRARY MODAL -->
{#if modalView === "create"}
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div
    class="modal-backdrop"
    onclick={(e) => e.target === e.currentTarget && closeCreate()}
    onkeydown={(e) => { if (e.key === "Escape") closeCreate(); if (e.key === "Enter" && canSubmit) handleInitialize(); }}
  >
    <div class="modal-panel" role="dialog" aria-labelledby="create-heading">
      <div class="modal-header">
        <span class="mh-icon">▦</span>
        <span class="mh-label">PROJECT INITIALIZATION MATRIX</span>
      </div>
      <div class="modal-body">
        <h1 id="create-heading" class="modal-title">INITIALIZE NEW LIBRARY</h1>
        <p class="modal-subtitle">SET GLOBAL PARAMETERS FOR THE TARGET VST CONTAINER</p>
        <label class="field-label" for="lib-name">01. LIBRARY NAME</label>
        <div class="field-row field-row--primary">
          <input id="lib-name" type="text" class="field-input field-input--primary" placeholder="Duality Strings" bind:value={newLibName} />
          <span class="field-suffix">III</span>
        </div>
        <label class="field-label" for="lib-vendor">02. VENDOR</label>
        <div class="field-row">
          <input id="lib-vendor" type="text" class="field-input" placeholder="Precision Audio Labs" bind:value={newLibVendor} />
        </div>
        <label class="field-label" for="lib-variant">03. VARIANT</label>
        <div class="field-row">
          <input id="lib-variant" type="text" class="field-input" placeholder="Standard Edition" bind:value={newLibVariant} />
        </div>
      </div>
      <div class="modal-footer">
        <button class="modal-cancel" onclick={closeCreate}><span class="cancel-x">✕</span> CANCEL</button>
        <button class="modal-submit" disabled={!canSubmit} onclick={handleInitialize}>INITIALIZE &amp; OPEN EDITOR &nbsp;⚡</button>
      </div>
    </div>
  </div>
{/if}

<!-- EDITOR MODAL -->
{#if modalView === "editor"}
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div
    class="modal-backdrop"
    onkeydown={(e) => { if (e.key === "Escape") { if (instrumentChooserPatchId) closeInstrumentChooser(); else closeEditor(); } }}
  >
    <div class="editor-panel" role="dialog" aria-labelledby="editor-heading">
      <!-- Editor header -->
      <div class="editor-top-bar">
        <div class="editor-top-left">
          <span class="mh-icon">▦</span>
          <span id="editor-heading" class="editor-top-title">VST LIBRARY EDITOR</span>
        </div>
        <button class="editor-close" onclick={closeEditor}>✕</button>
      </div>

      <!-- Editor body: sidebar + content -->
      <div class="editor-body">
        <!-- Left: optional generic patch sets -->
        <aside class="ens-sidebar">
          <h3 class="ens-heading">QUICK-ADD PATCH SETS</h3>
          <p class="ens-sub">OPTIONAL STARTING POINTS</p>
          <div class="ens-grid">
            {#each standardEnsembles as ens}
              <button class="ens-card" onclick={() => addEnsemble(ens)}>
                <div class="ens-name">{ens.name.toUpperCase()}</div>
                <div class="ens-desc">{ens.description}</div>
              </button>
            {/each}
          </div>
        </aside>

        <!-- Right: patch catalog -->
        <div class="inst-content">
          <div class="patch-toolbar">
            <button class="add-patch-btn" onclick={addBlankPatch}>+ Add Patch</button>
            <button class="sort-patches-btn" disabled={patches.length < 2} onclick={sortPatchesOrchestrally}>Sort Orchestrally</button>
            <span>Creates an empty catalog row. Instrument classification is optional.</span>
          </div>

          {#if layerUpdateResult}
            <div class="layer-update-result" class:success={layerUpdateResult.startsWith("OK:")} role="status">
              {layerUpdateResult}
            </div>
          {/if}

          <div class="batch-toolbar">
            <button class="select-all-btn" onclick={selectAllPatches}>
              {selectedPatchIds.size === patches.length && patches.length > 0 ? "Clear selection" : "Select all"}
            </button>
            <span class="batch-count">{selectedPatchIds.size} selected</span>
            <select class="batch-select" bind:value={batchPluginUid}>
              <option value={0}>Choose player…</option>
              {#each scannedPlugins.filter((plugin) => plugin.valid !== false) as plugin}
                <option value={plugin.uid}>{plugin.name}</option>
              {/each}
            </select>
            <button class="batch-apply" disabled={selectedPatchIds.size === 0} onclick={applyPluginToSelection}>
              Assign player
            </button>
            <div class="batch-xmap">
              <ExpressionMapPicker
                maps={availableXmaps}
                selectedId=""
                selectedName="Assign expression map…"
                onselect={applyExpressionMapToSelection}
                onclear={() => applyExpressionMapToSelection("")}
                canLoadFromFile={false}
              />
            </div>
          </div>

          <!-- Table header -->
          <div class="inst-table-header">
            <span class="th-order">ORDER</span>
            <span class="th-name">PATCH</span>
            <span class="th-character">CHARACTER</span>
            <span class="th-vst">PLAYER</span>
            <span class="th-expr">EXPRESSION MAP</span>
            <span class="th-actions">ACTIONS</span>
          </div>

          <!-- Table rows -->
          <div class="inst-table-body">
            {#each patches as row (row.id)}
              {@const setupStatus = pluginSetupStatus(row)}
              <div
                class="inst-row"
                class:patch-selected={selectedPatchIds.has(row.id)}
                class:patch-dragging={draggedPatchId === row.id}
                class:patch-drop-before={dropTargetPatchId === row.id && !dropAfterTarget}
                class:patch-drop-after={dropTargetPatchId === row.id && dropAfterTarget}
                ondragover={(event) => updatePatchDropTarget(event, row.id)}
                ondrop={(event) => dropPatch(event, row.id)}
              >
                <div class="ir-leading">
                  <button
                    type="button"
                    class="patch-drag-handle"
                    draggable="true"
                    aria-label={`Reorder ${row.name}. Use the up and down arrow keys, or drag.`}
                    title="Drag to reorder; arrow keys also work"
                    ondragstart={(event) => beginPatchDrag(event, row.id)}
                    ondragend={finishPatchDrag}
                    onkeydown={(event) => {
                      if (event.key === "ArrowUp" || event.key === "ArrowDown") {
                        event.preventDefault();
                        movePatchWithKeyboard(row.id, event.key === "ArrowUp" ? -1 : 1);
                      }
                    }}
                  >⠿</button>
                  <div class="ir-check">
                    <input type="checkbox" checked={selectedPatchIds.has(row.id)}
                      aria-label={`Select ${row.name}`}
                      onclick={(event) => selectPatch(row.id, event)} />
                  </div>
                </div>
                <div class="ir-name">
                  <div class="ir-name-text">
                    <input class="ir-input patch-name-input" value={row.name}
                      aria-label="Patch name"
                      oninput={(event) => {
                        const name = event.currentTarget.value;
                        patches = patches.map((patch) => patch.id === row.id ? { ...patch, name } : patch);
                        editorDirty = true;
                      }} />
                    <button class="ir-instrument-name" aria-label={`Classify ${row.name}`} onclick={() => beginInstrumentChoice(row.id)}>
                      {instrumentDisplayName(row) || "Choose instrument (optional)…"}
                    </button>
                  </div>
                </div>
                <div class="ir-character">
                  <select class="ir-select" value={row.character}
                    onchange={(event) => {
                      const character = event.currentTarget.value;
                      patches = patches.map((patch) => patch.id === row.id ? { ...patch, character } : patch);
                      editorDirty = true;
                    }}>
                    <option value="">Unspecified</option>
                    <option value="solo">Solo</option>
                    <option value="section">Section</option>
                    <option value="ensemble">Ensemble</option>
                    <option value="overlay">Overlay</option>
                  </select>
                </div>
                <div class="ir-vst">
                  <select class="ir-select" value={row.vstPlugin}
                    onchange={(e) => {
                      const uid = Number(/** @type {HTMLSelectElement} */ (e.target).value);
                      setPatchPlugin(row, uid);
                    }}
                  >
                    <option value={0}>— VSTi —</option>
                    {#each scannedPlugins.filter((p) => p.valid !== false) as plugin}
                      <option value={plugin.uid}>{plugin.name}</option>
                    {/each}
                  </select>
                  {#if row.vstPlugin || row.pluginUid}
                    <div class="vst-editor-control">
                      <button class="action-vst-edit" onclick={() => openVstEditor(row)} title="Open VST Editor">⚙️</button>
                      <span
                        class="plugin-setup-status {setupStatus.kind}"
                        role="img"
                        aria-label={setupStatus.label}
                        title={setupStatus.label}>{setupStatus.symbol}</span
                      >
                    </div>
                  {/if}
                </div>
                <div class="ir-expr">
                  <ExpressionMapPicker
                    maps={availableXmaps}
                    selectedId={row.exprMap}
                    selectedName={expressionMapName(row.exprMap)}
                    onselect={(entityID) =>
                      setInstrumentExpressionMap(row.id, entityID)}
                    onclear={() => setInstrumentExpressionMap(row.id, "")}
                    canLoadFromFile={false}
                  />
                </div>
                <div class="ir-actions">
                  <button
                    class="action-update-layers"
                    class:confirm={pendingLayerUpdateId === row.id}
                    disabled={editorDirty || !row.outOfDateLayerCount}
                    title={editorDirty
                      ? "Save and reopen the library before updating layers"
                      : !row.usageCount
                        ? "This patch is not used by any current layer"
                        : !row.outOfDateLayerCount
                          ? "All linked layers already match this patch"
                          : `Replace the saved setup in ${row.usageCount} linked ${row.usageCount === 1 ? "layer" : "layers"}; ${row.outOfDateLayerCount} currently differ`}
                    onclick={() => updateLayersFromPatch(row)}
                  >{pendingLayerUpdateId === row.id ? `Confirm ${row.usageCount}` : "Update Layers"}</button>
                  <button class="action-duplicate" onclick={() => duplicatePatch(row)}>Duplicate</button>
                  <button class="action-delete" onclick={() => removePatch(row.id)}>Delete</button>
                </div>
              </div>
            {/each}

            {#if patches.length === 0}
              <div class="inst-empty">
                <p>No patches added yet.</p>
                <p class="inst-empty-hint">Choose Add Patch above or use a generic quick-add set.</p>
              </div>
            {/if}
          </div>
        </div>
      </div>

      <!-- Editor footer -->
      <div class="editor-footer">
        <button class="modal-cancel" onclick={closeEditor}><span class="cancel-x">✕</span> CANCEL</button>
        <button class="modal-submit" disabled={!editorDirty} onclick={commitToLibrary}>Save</button>
      </div>

      {#if instrumentChooserPatchId}
        <div class="instrument-chooser-backdrop">
          <div class="instrument-chooser" role="dialog" aria-labelledby="instrument-chooser-heading">
            <div class="instrument-chooser-header">
              <div>
                <h2 id="instrument-chooser-heading">Classify Patch</h2>
                <p>Optional: associate this sound with a Dorico instrument for organization and suggestions.</p>
              </div>
              <button onclick={closeInstrumentChooser}>Close</button>
            </div>
            <div class="instrument-search-row">
              <span aria-hidden="true">🔍</span>
              <input
                type="text"
                placeholder="Search Dorico instruments…"
                bind:value={searchQuery}
                bind:this={searchInput}
              />
            </div>
            <div class="instrument-results">
              {#if !searchQuery.trim()}
                <div class="chooser-empty">Type an instrument name to search.</div>
              {:else if searchResults.length === 0}
                <div class="chooser-empty">No matching Dorico instruments.</div>
              {:else}
                {#each searchResults as instrument (instrument.entityID)}
                  <button class="instrument-result" onclick={() => classifyPatch(instrument)}>
                    <span>{instrument.name}</span>
                    <small>{instrument.entityID}</small>
                  </button>
                {/each}
              {/if}
            </div>
            <div class="instrument-chooser-footer">
              <button class="clear-classification" onclick={clearPatchClassification}>Clear classification</button>
              <button class="chooser-cancel" onclick={closeInstrumentChooser}>Cancel</button>
            </div>
          </div>
        </div>
      {/if}
    </div>
  </div>
{/if}


<style>
  /* ══════════════════════════════════════════════════════
     DASHBOARD
     ══════════════════════════════════════════════════════ */
  .lm-root {
    width: 100%; height: 100%;
    display: flex; flex-direction: column;
    background: #09082f; color: #e5e3ff;
    font-family: "Inter", system-ui, sans-serif;
    overflow: auto;
  }
  .lm-header {
    display: flex; align-items: center; gap: 10px;
    padding: 18px 28px 14px; flex-shrink: 0;
  }
  .lm-logo { font-size: 1.3rem; color: #95a9ff; }
  .lm-title {
    font-family: "Sora","Inter",sans-serif;
    font-size: 1.05rem; font-weight: 600; color: #e5e3ff;
    letter-spacing: -0.01em;
  }
  .lm-divider {
    height: 1px; margin: 0 28px; flex-shrink: 0;
    background: linear-gradient(90deg, #44446c 0%, transparent 100%);
  }
  .lm-body { flex: 1; padding: 28px 32px; overflow-y: auto; }
  .lm-toolbar {
    display: flex; align-items: center;
    justify-content: space-between; margin-bottom: 24px;
  }
  .lm-section-title {
    font-family: "Sora","Inter",sans-serif;
    font-size: 1.15rem; font-weight: 700; margin: 0; color: #e5e3ff;
  }
  .lm-create-btn {
    background: transparent; border: 1px solid #95a9ff;
    color: #95a9ff; font-family: "Inter",sans-serif;
    font-size: .82rem; font-weight: 500; padding: 7px 18px;
    border-radius: 4px; cursor: pointer;
    transition: background .15s, color .15s;
  }
  .lm-create-btn:hover { background: rgba(149,169,255,.12); color: #c0ccff; }
  .lm-toolbar-actions {
    display: flex; gap: 10px; align-items: center;
  }
  .lm-build-result {
    font-family: "Inter",sans-serif; font-size: .8rem;
    padding: 8px 14px; border-radius: 4px; margin-bottom: 16px;
    animation: fadeIn .2s ease-out;
  }
  .lm-build-ok {
    background: rgba(64,192,87,.12); color: #69db7c; border: 1px solid rgba(64,192,87,.25);
  }
  .lm-build-error {
    background: rgba(255,107,107,.12); color: #ff8787; border: 1px solid rgba(255,107,107,.25);
  }
  @keyframes fadeIn { from { opacity: 0; transform: translateY(-4px); } to { opacity: 1; transform: none; } }
  .lm-grid {
    display: grid; grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
    gap: 16px;
  }
  .lm-card {
    background: #131342; border: 1px solid #44446c; border-radius: 4px;
    padding: 20px 18px 18px; display: flex; flex-direction: column; gap: 6px;
    cursor: pointer; transition: border-color .15s, background .15s;
    text-align: left; font-family: inherit; color: inherit;
    position: relative;
  }
  .lm-card-delete {
    position: absolute; top: 8px; right: 8px;
    background: rgba(255,110,132,.06); border: 1px solid #595879; border-radius: 4px;
    color: #aaa9c8; font-size: .68rem; font-weight: 600; cursor: pointer;
    padding: 5px 8px; line-height: 1.15;
    transition: color .15s, border-color .15s, background .15s;
  }
  .lm-card-delete:hover { color: #ff9aaa; border-color: #ff6e84; background: rgba(255,110,132,.12); }
  .lm-card-delete--confirm {
    color: #fff; background: #c44; border-color: #c44;
  }
  .lm-card-delete--confirm:hover { background: #e33; border-color: #e33; color: #fff; }
  .lm-card:hover { border-color: #95a9ff; background: #19194b; }
  .lm-card-icon {
    width: 36px; height: 36px; border-radius: 4px; background: #1f1f55;
    display: flex; align-items: center; justify-content: center;
    font-size: 1.1rem; color: #95a9ff; margin-bottom: 8px;
  }
  .lm-card-name {
    font-family: "Sora","Inter",sans-serif; font-size: .92rem;
    font-weight: 600; color: #e5e3ff; line-height: 1.25;
  }
  .lm-card-sub { font-size: .78rem; font-weight: 400; color: #72719c; letter-spacing: -.01em; }
  .lm-card--skeleton { opacity: .35; cursor: default; border-color: #2a2a5a; }
  .lm-card--skeleton:hover { border-color: #2a2a5a; background: #131342; }
  .skel-icon { width: 36px; height: 36px; border-radius: 4px; background: #1f1f55; margin-bottom: 8px; }
  .skel-line { height: 10px; border-radius: 2px; background: #1f1f55; }
  .skel-line--long { width: 75%; }
  .skel-line--short { width: 55%; }

  /* ══════════════════════════════════════════════════════
     SHARED MODAL CHROME
     ══════════════════════════════════════════════════════ */
  .modal-backdrop {
    position: fixed;
    top: 0; left: 0; right: 0; bottom: 0;
    width: 100vw; height: 100vh;
    background: rgba(0,0,0,.55);
    backdrop-filter: blur(3px); -webkit-backdrop-filter: blur(3px);
    display: flex; align-items: center; justify-content: center;
    z-index: 99999;
  }
  .modal-header {
    display: flex; align-items: center; gap: 8px;
    padding: 12px 20px; background: #131342;
    border-bottom: 1px solid #44446c;
  }
  .mh-icon { font-size: .85rem; color: #95a9ff; }
  .mh-label {
    font-family: "Inter",sans-serif; font-size: .68rem; font-weight: 600;
    letter-spacing: .1em; text-transform: uppercase; color: #a8a7d5;
  }
  .modal-footer {
    display: flex; align-items: center; justify-content: space-between;
    padding: 16px 28px 20px; gap: 16px;
  }
  .modal-cancel {
    background: transparent; border: none; color: #a8a7d5;
    font-family: "Inter",sans-serif; font-size: .72rem; font-weight: 600;
    letter-spacing: .1em; text-transform: uppercase; cursor: pointer;
    display: flex; align-items: center; gap: 6px; padding: 8px 4px;
    transition: color .15s;
  }
  .modal-cancel:hover { color: #e5e3ff; }
  .cancel-x { font-size: .75rem; }
  .modal-submit {
    background: #3938a0; border: none; color: #e5e3ff;
    font-family: "Inter",sans-serif; font-size: .78rem; font-weight: 700;
    letter-spacing: .06em; text-transform: uppercase;
    padding: 12px 28px; border-radius: 4px; cursor: pointer;
    transition: background .15s, opacity .15s;
  }
  .modal-submit:hover:not(:disabled) { background: #4a49b8; }
  .modal-submit:disabled { opacity: .3; cursor: not-allowed; }

  /* ══════════════════════════════════════════════════════
     CREATE MODAL
     ══════════════════════════════════════════════════════ */
  .modal-panel {
    width: 540px; max-width: 92vw; max-height: 90vh;
    background: #0e0d38; border: 1px solid #44446c; border-radius: 4px;
    display: flex; flex-direction: column; overflow: hidden;
    box-shadow: 0 20px 60px rgba(0,0,0,.6);
  }
  .modal-body { padding: 32px 28px 20px; flex: 1; overflow-y: auto; }
  .modal-title {
    font-family: "Sora","Inter",sans-serif; font-size: 1.6rem; font-weight: 800;
    color: #e5e3ff; margin: 0 0 6px; letter-spacing: .02em; text-transform: uppercase;
  }
  .modal-subtitle {
    font-family: "Inter",sans-serif; font-size: .72rem; font-weight: 500;
    letter-spacing: .08em; text-transform: uppercase; color: #72719c; margin: 0 0 32px;
  }
  .field-label {
    display: block; font-family: "Inter",sans-serif; font-size: .68rem;
    font-weight: 600; letter-spacing: .08em; text-transform: uppercase;
    color: #a8a7d5; margin-bottom: 8px;
  }
  .field-row { position: relative; margin-bottom: 24px; }
  .field-input {
    width: 100%; background: transparent; border: none;
    border-bottom: 1px solid #44446c; color: #a8a7d5;
    font-family: "Sora","Inter",sans-serif; font-size: 1rem;
    padding: 10px 0; outline: none; transition: border-color .15s, color .15s;
    box-sizing: border-box;
  }
  .field-input::placeholder { color: #4a4980; }
  .field-input:focus { border-bottom-color: #95a9ff; color: #e5e3ff; }
  .field-input--primary {
    border-bottom-color: #e5e3ff; color: #95a9ff;
    font-size: 1.2rem; font-weight: 500; padding-right: 40px;
  }
  .field-input--primary::placeholder { color: #6e8cff; opacity: .5; }
  .field-input--primary:focus { border-bottom-color: #95a9ff; color: #95a9ff; }
  .field-suffix {
    position: absolute; right: 4px; bottom: 12px;
    font-family: "Sora","Inter",sans-serif; font-size: 1.1rem;
    font-weight: 300; color: #44446c; pointer-events: none;
  }

  /* ══════════════════════════════════════════════════════
     EDITOR MODAL
     ══════════════════════════════════════════════════════ */
  .editor-panel {
    width: 96vw; max-width: 1400px; height: 94vh;
    position: relative;
    background: #0e0d38; border: 1px solid #44446c; border-radius: 4px;
    display: flex; flex-direction: column; overflow: hidden;
    box-shadow: 0 20px 60px rgba(0,0,0,.6);
  }
  .editor-top-bar {
    display: flex; align-items: center; justify-content: space-between;
    padding: 12px 20px; background: #131342;
    border-bottom: 1px solid #44446c;
  }
  .editor-top-left { display: flex; align-items: center; gap: 8px; }
  .editor-top-title {
    font-family: "Inter",sans-serif; font-size: .72rem; font-weight: 600;
    letter-spacing: .1em; text-transform: uppercase; color: #e5e3ff;
  }
  .editor-close {
    background: transparent; border: none; color: #a8a7d5;
    font-size: 1rem; cursor: pointer; padding: 4px 8px;
    transition: color .15s;
  }
  .editor-close:hover { color: #e5e3ff; }
  .editor-body { flex: 1; display: flex; overflow: hidden; }

  /* ── Ensembles sidebar ────────────────────────────── */
  .ens-sidebar {
    width: 260px; min-width: 220px; flex-shrink: 0;
    padding: 20px 16px; overflow-y: auto;
    border-right: 1px solid #44446c;
  }
  .ens-heading {
    font-family: "Inter",sans-serif; font-size: .72rem; font-weight: 700;
    letter-spacing: .06em; text-transform: uppercase; color: #95a9ff;
    margin: 0 0 2px;
  }
  .ens-sub {
    font-family: "Inter",sans-serif; font-size: .62rem; font-weight: 500;
    letter-spacing: .06em; text-transform: uppercase; color: #72719c;
    margin: 0 0 16px;
  }
  .ens-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
  .ens-card {
    background: #131342; border: 1px solid #44446c; border-radius: 4px;
    padding: 12px 10px; cursor: pointer; text-align: left;
    font-family: inherit; color: inherit;
    transition: border-color .15s, background .15s;
  }
  .ens-card:hover { border-color: #95a9ff; background: #19194b; }
  .ens-name {
    font-family: "Inter",sans-serif; font-size: .68rem; font-weight: 700;
    color: #e5e3ff; letter-spacing: .02em; margin-bottom: 4px; line-height: 1.2;
  }
  .ens-desc {
    font-family: "Inter",sans-serif; font-size: .58rem; font-weight: 400;
    color: #72719c; line-height: 1.3;
  }

  /* ── Instrument content ───────────────────────────── */
  .inst-content { flex: 1; display: flex; flex-direction: column; overflow: visible; }
  .patch-toolbar {
    display: flex; align-items: center; gap: 14px;
    padding: 14px 20px; background: #131342;
    border-bottom: 1px solid #44446c;
  }
  .patch-toolbar span { color: #8584ac; font: 500 .68rem "Inter", sans-serif; }
  .add-patch-btn, .sort-patches-btn {
    min-height: 36px; padding: 7px 14px; border: 1px solid #7877b8;
    border-radius: 4px; background: #3938a0; color: #f1f0ff;
    font: 700 .72rem "Inter", sans-serif; cursor: pointer; white-space: nowrap;
  }
  .add-patch-btn:hover { background: #4a49b8; border-color: #a8b7ff; }
  .sort-patches-btn { background: #1a194b; border-color: #555480; color: #c4c3e3; }
  .sort-patches-btn:hover:not(:disabled) { background: #292863; border-color: #95a9ff; color: #f1f0ff; }
  .sort-patches-btn:disabled { opacity: .4; cursor: default; }
  .batch-toolbar {
    display: flex; align-items: center; gap: 10px; padding: 10px 20px;
    background: #10103d; border-bottom: 1px solid #2a2a5a;
  }
  .select-all-btn, .batch-apply {
    min-height: 34px; padding: 6px 12px; border-radius: 4px;
    border: 1px solid #44446c; background: #1a194b; color: #d8d7ff;
    cursor: pointer; font-family: "Inter", sans-serif; font-size: .7rem;
  }
  .select-all-btn:hover, .batch-apply:hover:not(:disabled) {
    border-color: #95a9ff; background: #23225b;
  }
  .batch-apply:disabled { opacity: .4; cursor: default; }
  .batch-count { min-width: 72px; color: #9695bd; font-size: .68rem; }
  .batch-select {
    min-height: 34px; flex: 1; min-width: 150px; padding: 6px 10px;
    border: 1px solid #44446c; border-radius: 4px; background: #131342;
    color: #e5e3ff; font: inherit;
  }
  .batch-xmap { min-width: 210px; }
  .inst-table-header {
    display: grid;
    grid-template-columns: 64px minmax(190px, 1.5fr) 110px minmax(190px, 1.2fr) minmax(190px, 1.2fr) 240px;
    gap: 8px; padding: 10px 20px;
    background: #0e0d38; border-bottom: 1px solid #2a2a5a;
  }
  .inst-table-header span {
    font-family: "Inter",sans-serif; font-size: .58rem; font-weight: 700;
    letter-spacing: .08em; text-transform: uppercase; color: #72719c;
  }
  .th-actions { text-align: center; }
  .th-order { text-align: center; }
  .inst-table-body { flex: 1; overflow-y: auto; padding-bottom: 12px; }
  .inst-row {
    display: grid;
    grid-template-columns: 64px minmax(190px, 1.5fr) 110px minmax(190px, 1.2fr) minmax(190px, 1.2fr) 240px;
    gap: 8px; align-items: start;
    padding: 12px 20px;
    border-bottom: 1px solid rgba(68,68,108,.3);
    transition: background .1s;
  }
  .inst-row:hover { background: rgba(149,169,255,.04); }
  .inst-row.patch-selected { background: rgba(149,169,255,.09); }
  .inst-row.patch-dragging { opacity: .45; }
  .inst-row.patch-drop-before { box-shadow: inset 0 3px 0 #75d3ff; }
  .inst-row.patch-drop-after { box-shadow: inset 0 -3px 0 #75d3ff; }
  .ir-leading { display: flex; align-items: center; gap: 7px; }
  .patch-drag-handle {
    width: 30px; height: 30px; padding: 0; box-sizing: border-box;
    border: 1px solid #555480; border-radius: 4px; background: #181744;
    color: #aaa9cd; font: 700 1.15rem/1 "Inter", sans-serif; cursor: grab;
  }
  .patch-drag-handle:hover, .patch-drag-handle:focus-visible {
    border-color: #95a9ff; color: #e5e3ff; background: #292863; outline: none;
  }
  .patch-drag-handle:active { cursor: grabbing; }
  .ir-check { display: flex; justify-content: center; }
  .ir-check input { width: 18px; height: 18px; accent-color: #95a9ff; }
  .ir-actions { display: flex; justify-content: flex-end; gap: 6px; }
  .action-update-layers, .action-duplicate, .action-delete {
    width: auto; height: 30px; padding: 0 9px; box-sizing: border-box;
    border: 1px solid #44446c;
    border-radius: 4px; background: #1a194b; color: #d8d7ff;
    font: 600 .64rem "Inter", sans-serif; cursor: pointer;
  }
  .action-update-layers { color: #a7f3d0; border-color: #2f6f61; }
  .action-update-layers:hover:not(:disabled) {
    border-color: #5eead4; background: #164e46; color: #ecfdf5;
  }
  .action-update-layers.confirm {
    border-color: #fbbf24; background: #5b4311; color: #fff4bd;
  }
  .action-update-layers:disabled { opacity: .36; cursor: default; }
  .action-duplicate:hover { border-color: #95a9ff; background: #292863; }
  .action-delete:hover { border-color: #d96a78; background: #4b1e32; color: #ffd7dc; }
  .layer-update-result {
    padding: 9px 20px; border-bottom: 1px solid #7f1d1d;
    background: rgba(127, 29, 29, .24); color: #fecaca;
    font: 600 .7rem "Inter", sans-serif;
  }
  .layer-update-result.success {
    border-bottom-color: #166534; background: rgba(22, 101, 52, .22);
    color: #bbf7d0;
  }
  .ir-name { display: flex; align-items: center; gap: 8px; }
  .ir-name-text { width: 100%; min-width: 0; }
  .patch-name-input { color: #e5e3ff; font-weight: 600; }
  .ir-instrument-name {
    width: 100%; padding: 4px 3px 0; border: 0; background: transparent;
    color: #8584ac; font: 500 .62rem "Inter", sans-serif; text-align: left;
    cursor: pointer;
    overflow: hidden; white-space: nowrap; text-overflow: ellipsis;
  }
  .ir-instrument-name:hover { color: #b8c3ff; text-decoration: underline; }
  .ir-select {
    width: 100%; background: #131342; border: 1px solid #44446c;
    border-radius: 2px; color: #a8a7d5; font-family: "Inter",sans-serif;
    height: 30px; box-sizing: border-box; font-size: .68rem;
    padding: 0 8px; outline: none; cursor: pointer;
    -webkit-appearance: none; appearance: none;
  }
  .ir-select:focus { border-color: #95a9ff; color: #e5e3ff; }
  .ir-input {
    width: 100%; background: #131342; border: 1px solid #44446c;
    border-radius: 2px; color: #a8a7d5; font-family: "Inter",sans-serif;
    height: 30px; font-size: .68rem; padding: 0 8px; outline: none;
    box-sizing: border-box;
  }
  .ir-input:focus { border-color: #95a9ff; color: #e5e3ff; }
  .ir-input::placeholder { color: #44446c; }
  .ir-vst { display: flex; align-items: center; gap: 4px; }
  .ir-vst .ir-select { flex: 1; min-width: 0; }
  .vst-editor-control {
    position: relative; width: 30px; height: 30px; flex: 0 0 30px;
  }
  .ir-expr { margin-top: -1px; }
  :global(.ir-expr .xmap-control) { margin-bottom: 0; }
  :global(.ir-expr .xmap-trigger) {
    height: 30px; min-height: 30px; box-sizing: border-box;
  }
  .action-vst-edit {
    background: transparent; border: 1px solid #33335c; border-radius: 3px;
    width: 30px; height: 30px; box-sizing: border-box;
    cursor: pointer; font-size: .8rem; padding: 0; color: #95a9ff;
    transition: background .15s, border-color .15s; flex-shrink: 0;
  }
  .action-vst-edit:hover { background: #2a2a5c; border-color: #95a9ff; }
  .plugin-setup-status {
    position: absolute; right: -5px; bottom: -5px;
    display: grid; place-items: center; width: 16px; height: 16px;
    box-sizing: border-box; border: 2px solid #0e0d38; border-radius: 50%;
    color: #07110b; font: 800 .63rem/1 "Inter", sans-serif;
    pointer-events: auto;
  }
  .plugin-setup-status.saved { background: #4ade80; }
  .plugin-setup-status.pending { background: #67e8f9; font-size: .8rem; }
  .plugin-setup-status.missing { background: #fbbf24; color: #321f03; }
  .inst-empty {
    display: flex; flex-direction: column; align-items: center;
    justify-content: center; padding: 60px 20px; color: #44446c;
  }
  .inst-empty p { margin: 4px 0; font-family: "Inter",sans-serif; font-size: .85rem; }
  .inst-empty-hint { font-size: .72rem; color: #2a2a5a; }
  .instrument-chooser-backdrop {
    position: absolute; inset: 0; z-index: 300; display: flex;
    align-items: center; justify-content: center; padding: 24px;
    background: rgba(3, 3, 20, .72); backdrop-filter: blur(2px);
  }
  .instrument-chooser {
    width: min(760px, 88vw); max-height: min(680px, 82vh);
    display: flex; flex-direction: column; overflow: hidden;
    background: #11103e; border: 1px solid #66659c; border-radius: 8px;
    box-shadow: 0 20px 70px rgba(0, 0, 0, .65);
  }
  .instrument-chooser-header {
    display: flex; align-items: flex-start; justify-content: space-between;
    gap: 24px; padding: 20px 22px 16px; border-bottom: 1px solid #34335f;
  }
  .instrument-chooser-header h2 {
    margin: 0 0 5px; color: #eeedff; font: 700 1.05rem "Sora", sans-serif;
  }
  .instrument-chooser-header p {
    margin: 0; color: #9695bd; font: 500 .72rem "Inter", sans-serif;
  }
  .instrument-chooser-header button, .chooser-cancel, .clear-classification {
    min-height: 34px; padding: 7px 12px; border: 1px solid #4d4c78;
    border-radius: 4px; background: #1b1a4c; color: #dcdbff;
    font: 600 .7rem "Inter", sans-serif; cursor: pointer;
  }
  .instrument-search-row {
    display: flex; align-items: center; gap: 10px; margin: 16px 20px 10px;
    padding: 10px 12px; border: 1px solid #5a598a; border-radius: 5px;
    background: #0c0b31;
  }
  .instrument-search-row input {
    flex: 1; border: 0; outline: 0; background: transparent; color: #f0efff;
    font: 500 .82rem "Inter", sans-serif;
  }
  .instrument-results { min-height: 180px; overflow-y: auto; padding: 0 20px 14px; }
  .instrument-result {
    width: 100%; display: flex; align-items: center; justify-content: space-between;
    gap: 20px; padding: 11px 12px; border: 0; border-bottom: 1px solid #292850;
    background: transparent; color: #e9e8ff; text-align: left; cursor: pointer;
    font: 600 .76rem "Inter", sans-serif;
  }
  .instrument-result:hover { background: rgba(149, 169, 255, .12); }
  .instrument-result small {
    color: #77769f; font-size: .62rem; font-weight: 400;
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .chooser-empty { padding: 64px 20px; color: #8584ac; text-align: center; }
  .instrument-chooser-footer {
    display: flex; justify-content: space-between; gap: 12px;
    padding: 14px 20px; border-top: 1px solid #34335f;
  }
  .clear-classification { color: #bab9df; }
  .instrument-chooser-header button:hover, .chooser-cancel:hover,
  .clear-classification:hover { border-color: #929fff; background: #292863; }
  .editor-footer {
    display: flex; align-items: center; justify-content: flex-end;
    padding: 14px 28px; gap: 20px;
    border-top: 1px solid #44446c; background: #0e0d38;
  }
</style>
