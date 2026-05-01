<script>
  /** @type {import('svelte')} */
  import { onMount } from "svelte";
  import { onFromCpp, dispatchCpp } from "./ipc.js";
  import ensembleData from "./standard_ensembles.json";
  import { FAMILY_ORDER, canonicalFamily, populateScoreOrder, instrumentScoreOrder } from "./orchestralOrder.js";

  const standardEnsembles = ensembleData.ensembles;

  // ── Expression maps from C++ ───────────────────────
  let availableXmaps = $state([]);
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
      instruments = (data.instruments || []).map((inst, i) => ({
        id:        `inst-${i}`,
        entityID:  inst.entityID || "",
        name:      inst.name || "",
        family:    inst.family || "",
        category:  inst.category || "",
        size:      (inst.isSolo !== undefined && !inst.isSolo) ? "section" : "solo",
        instanceNums: Array.isArray(inst.instanceNums) ? [...inst.instanceNums] : [inst.instanceNum || 1],
        vstPlugin: Number(inst.vstPlugin) || inst.pluginUid || 0,
        exprMap:   inst.exprMap || "",
        note:      inst.note || "",
        committed: true,
        stripId:   "",
        pluginUid: inst.pluginUid || 0,
        hasPluginState: inst.hasPluginState || false,
      }));
      nextId = instruments.length;
      searchQuery = "";
      modalView = "editor";
      editorDirty = false;
    } catch (e) { /* ignore */ }
  });

  // ── Scanned plugins from C++ ───────────────────────
  let scannedPlugins = $state([]);
  onFromCpp("setPluginList", (data) => {
    try { scannedPlugins = data; } catch (e) { /* ignore */ }
  });

  /**
   * Preview a plugin: create a temporary mixer strip, load the
   * chosen VST, and immediately open the editor window.
   * Uses a one-shot setMixerState listener to discover the new strip ID.
   */
  let _knownStripIds = new Set();
  onFromCpp("setMixerState", (data) => {
    // Keep a running set of known strip IDs so we can detect new ones.
    _knownStripIds = new Set(data.map((s) => s.id));
  });

  const previewPlugin = (pluginUid, instrumentId) => {
    // Snapshot current strip IDs
    const before = new Set(_knownStripIds);
    // One-shot listener: detect the newly-added strip
    const unsub = onFromCpp("setMixerState", (data) => {
      unsub();  // remove this one-shot listener
      const newStrip = data.find((s) => !before.has(s.id));
      if (newStrip) {
        // Link this strip to the instrument row so saveLibrary can capture state
        if (instrumentId) {
          instruments = instruments.map(inst =>
            inst.id === instrumentId ? { ...inst, stripId: newStrip.id } : inst
          );
        }
        dispatchCpp("setStripPlugin", newStrip.id, pluginUid);
        // Small delay so plugin has time to instantiate before showing editor
        setTimeout(() => dispatchCpp("showStripEditor", newStrip.id), 300);
      }
    });
    dispatchCpp("addMixerStrip");
  };

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
  let searchFocused = $state(false);

  // ── Editor dirty tracking ─────────────────────────────
  let editorDirty = $state(false);

  // ── Dorico instrument database ─────────────────────────
  let allDoricoInstruments = $state([]);

  onFromCpp("setDoricoInstruments", (arr) => {
    allDoricoInstruments = arr;
    populateScoreOrder(arr);
    console.log(`[LibMgr] Received ${arr.length} Dorico instruments`);
  });

  /** Filtered search results — only when search has text.
   *  Each instrument produces two entries: solo (👤) and section (👥).
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
      // Default (root) instruments before variants
      const da = a.isDefault ? 0 : 1, db = b.isDefault ? 0 : 1;
      if (da !== db) return da - db;
      // Final tiebreaker: XML document order
      return (a.xmlIndex ?? 99999) - (b.xmlIndex ?? 99999);
    });
    return matches.slice(0, 20);
  });

  /**
   * @typedef {Object} LibInstrument
   * @property {string}  id
   * @property {string}  entityID
   * @property {string}  name
   * @property {string}  family
   * @property {string}  category
   * @property {string}  size
   * @property {number[]} instanceNums
   * @property {number}  vstPlugin
   * @property {string}  exprMap
   * @property {string}  note
   * @property {boolean} committed
   * @property {string}  stripId
   * @property {number}  pluginUid
   * @property {boolean} hasPluginState
   */

  /** @type {LibInstrument[]} */
  let instruments = $state([]);
  let nextId = 0;
  /** @type {string} */ let pendingDeleteId = $state("");

  // ── Portal container for modals ───────────────────────
  // Attach modal DOM directly to <body> so overflow:hidden parents
  // can't clip position:fixed overlays in WKWebView.
  let portalTarget = $state(null);
  onMount(() => {
    dispatchCpp("requestExpressionMaps");
    dispatchCpp("requestPluginsState");
    dispatchCpp("requestLibraries");
    dispatchCpp("requestSetupData");  // load Dorico instrument database
    const el = document.createElement("div");
    el.id = "lm-portal";
    document.body.appendChild(el);
    portalTarget = el;
    return () => { el.remove(); };
  });

  // ── Actions: create modal ─────────────────────────────
  const openCreate = () => {
    newLibName = ""; newLibVendor = ""; newLibVariant = "";
    modalView = "create";
  };
  const closeCreate = () => { modalView = "none"; };

  // ── Build Playback Template ──────────────────────────
  let buildingTemplate = $state(false);
  let buildResult = $state("");
  let templateDirty = $state(false);

  onFromCpp("setTemplateDirty", (data) => {
    templateDirty = !!data;
  });

  onFromCpp("buildPlaybackTemplateResult", (data) => {
    buildingTemplate = false;
    buildResult = typeof data === "string" ? data : JSON.stringify(data);
    // Clear the result after a few seconds
    setTimeout(() => { buildResult = ""; }, 5000);
  });

  const handleBuildTemplate = () => {
    if (buildingTemplate) return;
    buildingTemplate = true;
    buildResult = "";
    dispatchCpp("buildPlaybackTemplate", []);
  };

  const handleInitialize = () => {
    if (!canSubmit) return;
    editorLibId = crypto.randomUUID();
    editorLibName = newLibName.trim();
    editorLibVendor = newLibVendor.trim();
    editorLibVariant = newLibVariant.trim();
    instruments = [];
    nextId = 0;
    searchQuery = "";
    modalView = "editor";
    editorDirty = false;
  };

  // ── Actions: editor modal ─────────────────────────────
  const closeEditor = () => { modalView = "none"; };

  const familyToCategory = (/** @type {string} */ family) => {
    const map = {
      strings:            "LIBRARIES/SYMPHONIC/STRINGS",
      wind:               "LIBRARIES/WOODWINDS/PRO",
      brass:              "LIBRARIES/BRASS/LOW_END",
      keyboard:           "LIBRARIES/KEYS/STUDIO",
      "pitched-percussion":"LIBRARIES/PERCUSSION/ORCH",
      percussion:         "LIBRARIES/PERCUSSION/ORCH",
    };
    return map[family] || "LIBRARIES/GENERAL";
  };

  const addInstrument = (/** @type {{ entityID: string, name: string, family: string }} */ inst, /** @type {string} */ type = "solo") => {
    // Compute the lowest unused instance number for this entityID+size
    const usedNums = new Set(
      instruments
        .filter(i => i.entityID === inst.entityID && i.size === type)
        .map(i => i.instanceNums[0])
    );
    let num = 1;
    while (usedNums.has(num)) num++;

    instruments = [...instruments, {
      id:        `inst-${nextId++}`,
      entityID:  inst.entityID,
      name:      inst.name,
      family:    inst.family || "",
      category:  familyToCategory(inst.family),
      size:      type,
      instanceNums: [num],
      vstPlugin: 0,
      exprMap:   "",
      note:      "",
      committed: false,
      stripId:   "",
      pluginUid: 0,
      hasPluginState: false,
    }];
    editorDirty = true;
  };

  /** Convert a small integer to a Roman numeral */
  const toRoman = (/** @type {number} */ n) => {
    const numerals = ['', 'I', 'II', 'III', 'IV', 'V', 'VI', 'VII', 'VIII', 'IX', 'X',
                      'XI', 'XII', 'XIII', 'XIV', 'XV', 'XVI', 'XVII', 'XVIII', 'XIX', 'XX'];
    return numerals[n] || String(n);
  };

  /**
   * Instruments sorted in orchestral score order.
   * Sort: score order → solo before section → instance number.
   * Display: "Violin 1", "Violin 2" for solo; "Violin I", "Violin II" for section.
   */
  let sortedInstruments = $derived.by(() => {
    const sorted = [...instruments].sort((a, b) => {
      const ordA = instrumentScoreOrder(a.entityID);
      const ordB = instrumentScoreOrder(b.entityID);
      if (ordA !== ordB) return ordA - ordB;
      if (a.size !== b.size) return a.size === 'solo' ? -1 : 1;
      return a.instanceNums[0] - b.instanceNums[0];
    });
    return sorted.map(inst => {
      const num = inst.instanceNums[0];
      const suffix = inst.size === 'section' ? ` ${toRoman(num)}` : ` ${num}`;
      return { ...inst, displayName: inst.name + suffix };
    });
  });

  /** Increment the chair number for a single-instance row */
  const incrementInstance = (/** @type {string} */ rowId) => {
    instruments = instruments.map(inst =>
      inst.id === rowId ? { ...inst, instanceNums: [inst.instanceNums[0] + 1] } : inst
    );
    editorDirty = true;
  };

  /** Decrement the chair number (min 1) */
  const decrementInstance = (/** @type {string} */ rowId) => {
    instruments = instruments.map(inst =>
      inst.id === rowId && inst.instanceNums[0] > 1
        ? { ...inst, instanceNums: [inst.instanceNums[0] - 1] }
        : inst
    );
    editorDirty = true;
  };

  const addEnsemble = (/** @type {typeof standardEnsembles[0]} */ ens) => {
    for (const inst of ens.instruments) {
      addInstrument(inst, inst.type || "solo");
    }
  };

  const removeInstrument = (/** @type {string} */ id) => {
    instruments = instruments.filter(r => r.id !== id);
    editorDirty = true;
  };

  /** Open the VST editor for an instrument row.
   *  If a live strip exists (previewPlugin was used this session), show its editor.
   *  Otherwise, create a strip, load the plugin, restore saved state, and show editor. */
  const openVstEditor = (/** @type {typeof instruments[0]} */ row) => {
    if (row.stripId) {
      // Live strip exists — just show its editor
      dispatchCpp("showStripEditor", row.stripId);
      return;
    }
    const uid = Number(row.vstPlugin) || row.pluginUid;
    if (!uid) return;

    // Phase 1: create strip, detect its ID from setMixerState
    const before = new Set(_knownStripIds);
    const unsub1 = onFromCpp("setMixerState", (data) => {
      unsub1();
      const newStrip = data.find((s) => !before.has(s.id));
      if (!newStrip) return;

      const stripId = newStrip.id;
      instruments = instruments.map(inst =>
        inst.id === row.id ? { ...inst, stripId } : inst
      );

      // Phase 2: load plugin, then wait for it to be ready
      // Listen for the next setMixerState where this strip has a pluginUid
      const unsub2 = onFromCpp("setMixerState", (data2) => {
        const updated = data2.find((s) => s.id === stripId && s.pluginUid);
        if (!updated) return;  // plugin not loaded yet, keep listening
        unsub2();
        // Plugin is loaded — always attempt to restore saved state from library DB
        if (editorLibId) {
          console.log("[openVstEditor] Plugin loaded, restoring state for", row.entityID,
                      "hasPluginState=", row.hasPluginState, "libraryId=", editorLibId);
          dispatchCpp("restoreLibraryPluginState", stripId, editorLibId, row.entityID, row.instanceNums[0]);
          // Delay editor open to let state restore complete
          setTimeout(() => dispatchCpp("showStripEditor", stripId), 500);
        } else {
          dispatchCpp("showStripEditor", stripId);
        }
      });

      // Trigger plugin load on the new strip
      dispatchCpp("setStripPlugin", stripId, uid);
    });

    dispatchCpp("addMixerStrip");
  };



  const commitRow = (/** @type {string} */ id) => {
    instruments = instruments.map(r =>
      r.id === id ? { ...r, committed: true } : r
    );
  };

  const commitToLibrary = () => {
    const libData = {
      id: editorLibId,
      name: editorLibName,
      vendor: editorLibVendor,
      variant: editorLibVariant,
      instruments: instruments.map(inst => ({
        entityID:    inst.entityID,
        name:        inst.name,
        family:      inst.family,
        category:    inst.category,
        instanceNums: inst.instanceNums,
        size:        inst.size,
        vstPlugin:   inst.vstPlugin,
        exprMap:     inst.exprMap,
        note:        inst.note,
        stripId:     inst.stripId,
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
        <button class="lm-build-btn" onclick={handleBuildTemplate} disabled={buildingTemplate || !templateDirty}>
          {buildingTemplate ? "Installing…" : "⚙ Install Playback Template"}
        </button>
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
          >{pendingDeleteId === lib.id ? '✓ Delete?' : '×'}</button>
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
    onkeydown={(e) => { if (e.key === "Escape") { if (searchQuery) searchQuery = ""; else closeEditor(); } }}
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
        <!-- Left: ensembles sidebar -->
        <aside class="ens-sidebar">
          <h3 class="ens-heading">SAMPLE LIBRARY PACKAGES</h3>
          <p class="ens-sub">BATCH INSERTION</p>
          <div class="ens-grid">
            {#each standardEnsembles as ens}
              <button class="ens-card" onclick={() => addEnsemble(ens)}>
                <div class="ens-name">{ens.name.toUpperCase()}</div>
                <div class="ens-desc">{ens.description}</div>
              </button>
            {/each}
          </div>
        </aside>

        <!-- Right: instrument table -->
        <div class="inst-content">
          <!-- Search Dorico Instruments -->
          <div class="inst-search-wrapper">
            <div class="inst-search-row">
              <span class="inst-search-icon">🔍</span>
              <input
                type="text"
                class="inst-search-input"
                placeholder="Search Dorico instruments…"
                bind:value={searchQuery}
                onfocus={() => { searchFocused = true; }}
                onblur={() => { setTimeout(() => { searchFocused = false; }, 200); }}
              />
              {#if searchQuery}
                <button class="inst-search-esc" onclick={() => { searchQuery = ""; }}>✕</button>
              {/if}
            </div>
            {#if searchFocused && searchResults.length > 0}
              <div class="search-dropdown">
                {#each searchResults as instr (instr.entityID)}
                  <div class="search-result">
                    <span class="sr-name">{instr.name}</span>
                    <span class="sr-entity">{instr.entityID}</span>
                    <button class="sr-add" title="Add solo" onclick={() => { addInstrument(instr, 'solo'); }}>+👤</button>
                    <button class="sr-add" title="Add section" onclick={() => { addInstrument(instr, 'section'); }}>+👥</button>
                  </div>
                {/each}
              </div>
            {/if}
          </div>

          <!-- Table header -->
          <div class="inst-table-header">
            <span class="th-name">INSTRUMENT</span>
            <span class="th-note">NOTE</span>
            <span class="th-vst">VST_PLUGIN</span>
            <span class="th-expr">EXPRESSION_MAP</span>
            <span class="th-actions">ACTIONS</span>
          </div>

          <!-- Table rows -->
          <div class="inst-table-body">
            {#each sortedInstruments as row (row.id)}
              <div class="inst-row">
                <div class="ir-name">
                  <span class="ir-size-icon" title={row.size === 'section' ? 'Section' : 'Solo'}>{row.size === 'section' ? '👥' : '👤'}</span>
                  <div class="ir-name-text">
                    <div class="ir-name-primary">{row.displayName}</div>
                  </div>
                  <div class="ir-instance-stepper">
                    <button class="stepper-btn" disabled={row.instanceNums[0] <= 1}
                      onclick={() => decrementInstance(row.id)} title="Lower chair number">−</button>
                    <span class="stepper-val">{row.instanceNums[0]}</span>
                    <button class="stepper-btn"
                      onclick={() => incrementInstance(row.id)} title="Higher chair number">+</button>
                  </div>
                </div>
                <div class="ir-note">
                  <input class="ir-input" type="text" placeholder="—"
                    value={row.note}
                    oninput={(e) => {
                      const val = /** @type {HTMLInputElement} */ (e.target).value;
                      instruments = instruments.map(i =>
                        i.id === row.id ? { ...i, note: val } : i
                      );
                      editorDirty = true;
                    }}
                  />
                </div>
                <div class="ir-vst">
                  <select class="ir-select" value={row.vstPlugin}
                    onchange={(e) => {
                      const uid = Number(/** @type {HTMLSelectElement} */ (e.target).value);
                      instruments = instruments.map(i =>
                        i.id === row.id ? { ...i, vstPlugin: uid } : i
                      );
                      editorDirty = true;
                      if (uid) previewPlugin(uid, row.id);
                    }}
                  >
                    <option value={0}>— VSTi —</option>
                    {#each scannedPlugins.filter((p) => p.valid !== false) as plugin}
                      <option value={plugin.uid}>{plugin.name}</option>
                    {/each}
                  </select>
                  {#if row.vstPlugin || row.pluginUid}
                    <button class="action-vst-edit" onclick={() => openVstEditor(row)} title="Open VST Editor">⚙️</button>
                  {/if}
                </div>
                <div class="ir-expr">
                  <select class="ir-select" value={row.exprMap}
                    onchange={(e) => {
                      const val = /** @type {HTMLSelectElement} */ (e.target).value;
                      instruments = instruments.map(i =>
                        i.id === row.id ? { ...i, exprMap: val } : i
                      );
                      editorDirty = true;
                    }}>
                    <option value="">— xmap —</option>
                    {#each availableXmaps as xmap}
                      <option value={xmap.entityID}>{xmap.name}</option>
                    {/each}
                  </select>
                </div>
                <div class="ir-actions">
                  <button class="action-delete" onclick={() => removeInstrument(row.id)} title="Remove">🗑</button>
                </div>
              </div>
            {/each}

            {#if instruments.length === 0}
              <div class="inst-empty">
                <p>No instruments added yet.</p>
                <p class="inst-empty-hint">Use the ensembles panel or search to add instruments.</p>
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
  .lm-build-btn {
    background: linear-gradient(135deg, #3b5bdb, #5f3dc4);
    border: none; color: #e5e3ff; font-family: "Inter",sans-serif;
    font-size: .82rem; font-weight: 600; padding: 7px 18px;
    border-radius: 4px; cursor: pointer;
    transition: opacity .15s, transform .1s;
  }
  .lm-build-btn:hover:not(:disabled) { opacity: .85; transform: scale(1.02); }
  .lm-build-btn:disabled { opacity: .4; cursor: not-allowed; }
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
    position: absolute; top: 6px; right: 6px;
    background: transparent; border: 1px solid transparent; border-radius: 3px;
    color: #72719c; font-size: .85rem; cursor: pointer;
    padding: 1px 5px; line-height: 1; opacity: 0;
    transition: opacity .15s, color .15s, border-color .15s;
  }
  .lm-card:hover .lm-card-delete { opacity: 1; }
  .lm-card-delete:hover { color: #ff6e84; border-color: #ff6e84; }
  .lm-card-delete--confirm {
    opacity: 1 !important; color: #fff; background: #c44; border-color: #c44;
    font-size: .7rem; padding: 2px 8px;
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
  .inst-search-row {
    display: flex; align-items: center; gap: 10px; position: relative;
    padding: 14px 20px; background: #131342;
    border-bottom: 1px solid #44446c;
  }
  .inst-search-icon { font-size: .9rem; color: #72719c; }
  .inst-search-input {
    flex: 1; background: transparent; border: none; outline: none;
    color: #e5e3ff; font-family: "Inter",sans-serif; font-size: .8rem;
    font-weight: 500; letter-spacing: .04em;
  }
  .inst-search-input::placeholder { color: #44446c; }
  .inst-search-esc {
    background: #1f1f55; border: 1px solid #44446c; color: #a8a7d5;
    font-family: "Inter",sans-serif; font-size: .62rem; font-weight: 700;
    letter-spacing: .08em; padding: 4px 10px; border-radius: 2px;
    cursor: pointer; transition: background .15s;
  }
  .inst-search-esc:hover { background: #2a2a6a; }
  .inst-search-wrapper { position: relative; z-index: 10; }
  .search-dropdown {
    position: absolute; top: 100%; left: 0; right: 0; z-index: 100;
    max-height: 440px; overflow-y: auto; overflow-x: hidden;
    background: #131342; border: 1px solid #44446c; border-top: none;
    border-radius: 0 0 4px 4px; box-shadow: 0 8px 24px rgba(0,0,0,.5);
  }
  .search-result {
    display: flex; align-items: center; gap: 8px; width: 100%; box-sizing: border-box;
    padding: 5px 10px 5px 14px; background: transparent;
    color: #e5e3ff; font-family: "Inter",sans-serif; font-size: .75rem;
    transition: background .1s;
  }
  .search-result:hover { background: rgba(149,169,255,.1); }
  .sr-name { flex-shrink: 0; font-weight: 600; }
  .sr-entity { flex: 1; min-width: 0; font-size: .65rem; color: #7271a0; font-weight: 400; letter-spacing: .02em; text-align: right; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; margin-right: 8px; }
  .sr-add {
    flex-shrink: 0; border: 1px solid #44446c; background: rgba(149,169,255,.08);
    color: #c5c3ff; font-size: .7rem; padding: 2px 8px; border-radius: 4px;
    cursor: pointer; transition: background .1s, border-color .1s;
  }
  .sr-add:hover { background: rgba(149,169,255,.22); border-color: #6a6aaa; }
  .inst-table-header {
    display: grid;
    grid-template-columns: 2fr 1fr 1.2fr 1.2fr 70px;
    gap: 8px; padding: 10px 20px;
    background: #0e0d38; border-bottom: 1px solid #2a2a5a;
  }
  .inst-table-header span {
    font-family: "Inter",sans-serif; font-size: .58rem; font-weight: 700;
    letter-spacing: .08em; text-transform: uppercase; color: #72719c;
  }
  .th-actions { text-align: center; }
  .inst-table-body { flex: 1; overflow-y: auto; padding-bottom: 12px; }
  .inst-row {
    display: grid;
    grid-template-columns: 2fr 1fr 1.2fr 1.2fr 70px;
    gap: 8px; align-items: center;
    padding: 12px 20px;
    border-bottom: 1px solid rgba(68,68,108,.3);
    transition: background .1s;
  }
  .inst-row:hover { background: rgba(149,169,255,.04); }
  .ir-name-primary {
    font-family: "Inter",sans-serif; font-size: .78rem; font-weight: 600;
    color: #e5e3ff; letter-spacing: .01em;
  }
  .ir-instance-stepper {
    display: flex; align-items: center; gap: 2px; margin-left: auto; flex-shrink: 0;
  }
  .stepper-btn {
    background: #1e1e50; border: 1px solid #44446c; border-radius: 3px;
    color: #a8a7d5; cursor: pointer; font-size: .65rem; font-weight: 700;
    width: 18px; height: 18px; display: flex; align-items: center; justify-content: center;
    padding: 0; line-height: 1; transition: all .15s;
  }
  .stepper-btn:hover:not(:disabled) { border-color: #7c3aed; color: #c4b5fd; background: #2a2a5c; }
  .stepper-btn:disabled { opacity: .3; cursor: default; }
  .stepper-val {
    font-family: "Inter", sans-serif; font-size: .65rem; font-weight: 700;
    color: #c4b5fd; min-width: 14px; text-align: center;
  }
  .ir-name { display: flex; align-items: center; gap: 8px; }
  .ir-size-icon { font-size: 1rem; flex-shrink: 0; }
  .ir-select {
    width: 100%; background: #131342; border: 1px solid #44446c;
    border-radius: 2px; color: #a8a7d5; font-family: "Inter",sans-serif;
    font-size: .68rem; padding: 5px 8px; outline: none; cursor: pointer;
    -webkit-appearance: none; appearance: none;
  }
  .ir-select:focus { border-color: #95a9ff; color: #e5e3ff; }
  .ir-input {
    width: 100%; background: #131342; border: 1px solid #44446c;
    border-radius: 2px; color: #a8a7d5; font-family: "Inter",sans-serif;
    font-size: .68rem; padding: 5px 8px; outline: none;
    box-sizing: border-box;
  }
  .ir-input:focus { border-color: #95a9ff; color: #e5e3ff; }
  .ir-input::placeholder { color: #44446c; }
  .ir-actions { display: flex; align-items: center; justify-content: center; gap: 6px; }
  .action-delete {
    background: transparent; border: none; cursor: pointer;
    font-size: .85rem; padding: 2px 4px; color: #72719c; transition: color .15s;
  }
  .action-delete:hover { color: #ff6e84; }
  .ir-vst { display: flex; align-items: center; gap: 4px; }
  .ir-vst .ir-select { flex: 1; min-width: 0; }
  .action-vst-edit {
    background: transparent; border: 1px solid #33335c; border-radius: 3px;
    cursor: pointer; font-size: .8rem; padding: 2px 5px; color: #95a9ff;
    transition: background .15s, border-color .15s; flex-shrink: 0;
  }
  .action-vst-edit:hover { background: #2a2a5c; border-color: #95a9ff; }
  .inst-empty {
    display: flex; flex-direction: column; align-items: center;
    justify-content: center; padding: 60px 20px; color: #44446c;
  }
  .inst-empty p { margin: 4px 0; font-family: "Inter",sans-serif; font-size: .85rem; }
  .inst-empty-hint { font-size: .72rem; color: #2a2a5a; }
  .editor-footer {
    display: flex; align-items: center; justify-content: flex-end;
    padding: 14px 28px; gap: 20px;
    border-top: 1px solid #44446c; background: #0e0d38;
  }
</style>
