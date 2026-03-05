<script>
  /** @type {import('svelte')} */
  import { onMount } from "svelte";
  import Timeline from "./lib/Timeline.svelte";
  import EventLog from "./lib/EventLog.svelte";
  import SetupPanel from "./lib/SetupPanel.svelte";
  import PluginsPanel from "./lib/PluginsPanel.svelte";
  import MixerPanel from "./lib/MixerPanel.svelte";

  // Determine window mode from URL parameter
  const urlParams = new URLSearchParams(window.location.search);
  const windowMode = urlParams.get("mode") || "main";

  // Main window mode: "mixer" or "setup"
  let mainMode = $state("mixer");

  // Use Svelte 5 Runes for reactivity
  let logs = $state([]);
  let activeNotes = $state([]);
  let noteHistory = $state([]);
  let diagnostics = $state([]);
  let heartbeat = $state(0);
  let hoveredNote = $state(null);
  let tooltipPos = $state({ x: 0, y: 0 });
  let tooltipPlacement = $state("top");
  let activeTab = $state("timeline");
  let channelInstruments = $state({});
  let midiEvents = $state([]);
  let serverVersion = $state("");
  let isConnected = $state(false);
  let instrumentMap = $state({}); // "port:channel" → { name, family, isSolo }
  let sessionOffset = $derived.by(() => {
    let min = Infinity;
    if (noteHistory.length > 0) {
      const earliestNote = Math.min(
        ...noteHistory.map((n) => Number(n.startSample)),
      );
      if (earliestNote < min) min = earliestNote;
    }
    if (midiEvents.length > 0) {
      // midiEvents are pushed newest-first, so the oldest is at the end
      const earliestEvent = midiEvents[midiEvents.length - 1].timestamp;
      if (earliestEvent < min) min = earliestEvent;
    }
    return min === Infinity ? 0 : min;
  });

  /* 
    Reset Session State.
    @param {boolean} keepEvents - If true, midiEvents remain (useful for Transport Start).
  */
  const resetSession = (keepEvents = false) => {
    noteHistory = [];
    activeNotes = [];
    if (keepEvents !== true) midiEvents = [];
    logs = [];
    heartbeat = 0;
    channelInstruments = {};
  };

  let logId = 0;

  window.setHeartbeat = (val) => {
    heartbeat = val;
  };

  window.setServerVersion = (ver) => {
    serverVersion = ver;
  };

  window.setConnectionState = (connected) => {
    isConnected = connected;
  };

  window.setChannelInstrument = (channel, name) => {
    channelInstruments = {
      ...channelInstruments,
      [channel]: name,
    };
  };

  window.setInstrumentMap = (jsonStr) => {
    try {
      const arr = JSON.parse(jsonStr);
      const map = {};
      for (const item of arr) {
        const key = `${item.port}:${item.channel}`;
        map[key] = {
          name: item.label || item.name,
          family: item.family,
          isSolo: item.isSolo,
        };
      }
      instrumentMap = map;
      console.log(`[Setup] Instrument map: ${Object.keys(map).length} entries`);
    } catch (e) {
      console.error("[Setup] Failed to parse instrument map:", e);
    }
  };

  /**
   * Returns a callable function that invokes the named native function
   * registered via withNativeFunction() on the C++ side.
   *
   * JUCE native functions work through the __juce__invoke event system,
   * NOT as properties on __JUCE__.backend.
   */
  const getNative = (name) => {
    const w = /** @type {any} */ (window);
    if (
      w.__JUCE__ &&
      w.__JUCE__.backend &&
      typeof w.__JUCE__.backend.emitEvent === "function"
    ) {
      // Use a monotonically increasing ID — we don't need the promise result
      // for fire-and-forget calls like signalReady, nativeLog, scanPlugins, etc.
      let nextId = 0;
      return function () {
        w.__JUCE__.backend.emitEvent("__juce__invoke", {
          name: name,
          params: Array.prototype.slice.call(arguments),
          resultId: nextId++,
        });
      };
    }
    return null;
  };

  const nativeLog = (msg) => {
    const f = getNative("nativeLog");
    if (f) f(msg);
  };

  window.addLogMessage = (msg, isError = false) => {
    const newLog = {
      id: logId++,
      msg,
      isError,
      time: new Date().toLocaleTimeString(),
    };
    logs = [newLog, ...logs].slice(0, 200);
  };

  window.onerror = (msg, url, line, col) => {
    const errorMsg = `[JS Error] ${msg} at ${line}:${col}`;
    nativeLog(errorMsg);
    window.addLogMessage(`<b>${errorMsg}</b>`, true);
    return false;
  };

  // Register stubs for window functions called by C++ before their
  // owning components mount. The real handlers overwrite these.
  window.setMixerState = window.setMixerState || (() => {});
  window.setPluginList = window.setPluginList || (() => {});
  window.setExpressionMaps = window.setExpressionMaps || (() => {});
  window.setAvailableInputs = window.setAvailableInputs || (() => {});
  window.setPlaybackDelay = window.setPlaybackDelay || (() => {});

  // Allow C++ to set the main window mode
  window.setMainMode = (mode) => {
    if (mode === "mixer" || mode === "setup") {
      mainMode = mode;
    }
  };

  nativeLog("JS Booting: Bundle loaded");
  window.addLogMessage("<i>JS Booting: Bundle loaded</i>");

  window.updateNoteState = (noteData, status) => {
    try {
      if (!noteData || noteData.id === undefined || noteData.id === null) {
        window.addLogMessage(
          `<b>[Bridge Error]</b> updateNoteState called with missing ID (status: ${status})`,
          true,
        );
        return;
      }
      const idStr = String(noteData.id);
      diagnostics = [
        { timestamp: Date.now(), status, data: JSON.stringify(noteData) },
        ...diagnostics,
      ].slice(0, 10);

      if (status === "started") {
        if (!activeNotes.some((n) => n.id === idStr)) {
          activeNotes = [
            ...activeNotes,
            { ...noteData, id: idStr, subnoteCount: 0 },
          ];
        }
        if (!noteHistory.some((n) => n.id === idStr)) {
          noteHistory = [...noteHistory, { ...noteData, id: idStr }];
        }
      } else if (status === "ended") {
        activeNotes = activeNotes.filter((n) => n.id !== idStr);
        noteHistory = noteHistory.map((n) => {
          if (n.id === idStr) {
            return {
              ...n,
              durationSamples: noteData.durationSamples,
              endVelocity: noteData.endVelocity,
            };
          }
          return n;
        });
      } else if (status === "subnote") {
        activeNotes = activeNotes.map((n) => {
          if (n.id === idStr)
            return { ...n, subnoteCount: (n.subnoteCount || 0) + 1 };
          return n;
        });
      } else if (status === "updated") {
        const dataToMerge = { ...noteData, id: idStr }; // Ensure ID remains string
        activeNotes = activeNotes.map((n) => {
          if (n.id === idStr) return { ...n, ...dataToMerge };
          return n;
        });
        noteHistory = noteHistory.map((n) => {
          if (n.id === idStr) return { ...n, ...dataToMerge };
          return n;
        });
      }

      // Debug: log the state of Legato for this note
      const currentNote = noteHistory.find((n) => n.id === idStr);
      if (
        currentNote &&
        currentNote.dimensions &&
        currentNote.dimensions.Legato !== undefined
      ) {
        const isDef = currentNote.notation_is_default?.Legato;
        console.log(
          `[UI Debug] Note ${idStr} Legato=${currentNote.dimensions.Legato} isDefault=${isDef}`,
        );
      }
    } catch (e) {
      window.addLogMessage(
        `<b>[JS Crash]</b> updateNoteState: ${e.message}`,
        true,
      );
    }
  };

  window.pushMidiEvent = (event) => {
    if (event.transportType === 0) {
      // Keep existing event log, but reset other state
      resetSession(true);
    }

    // Parse ContextUpdate messages to extract instrument names
    if (event.description && event.description.startsWith("ContextUpdate:")) {
      // Format: "ContextUpdate: Index=0, Name='Violin 1', Namespace='VST3'"
      const indexMatch = event.description.match(/Index=(\d+)/);
      const nameMatch = event.description.match(/Name='([^']+)'/);

      if (indexMatch && nameMatch) {
        const channelIndex = parseInt(indexMatch[1]);
        const instrumentName = nameMatch[1];
        // Channel index is 0-based, but we display 1-based
        const channel = channelIndex + 1;

        channelInstruments = {
          ...channelInstruments,
          [channel]: instrumentName,
        };
        console.log(`[Context] Ch ${channel}: ${instrumentName}`);
      }
    }

    midiEvents = [
      { ...event, localTime: Date.now(), id: Date.now() + Math.random() },
      ...midiEvents,
    ].slice(0, 1000);
  };

  const clearLogs = () => {
    logs = [];
  };

  onMount(() => {
    // Signal readiness to JUCE using the modern Native Function API
    const signalReady = () => {
      const nativeFunc = getNative("signalReady");

      if (nativeFunc) {
        nativeFunc();
        window.addLogMessage("<i>UI signaled readiness to C++</i>");
        console.log("UI Ready signaled via native function");
      } else {
        // Fallback or retry if bridge is not ready
        setTimeout(signalReady, 100);
      }
    };

    signalReady();

    // Undo/Redo keyboard shortcuts
    const handleKeyDown = (e) => {
      if ((e.metaKey || e.ctrlKey) && e.key === "z") {
        e.preventDefault();
        if (e.shiftKey) {
          const redo = getNative("redo");
          if (redo) redo();
        } else {
          const undo = getNative("undo");
          if (undo) undo();
        }
      }
    };
    window.addEventListener("keydown", handleKeyDown);

    return () => {
      window.removeEventListener("keydown", handleKeyDown);
    };
  });
  const clearHistory = () => {
    noteHistory = [];
    activeNotes = [];
    window.addLogMessage("<i>History and Active Notes cleared</i>");
  };

  const addTestNote = () => {
    const id = Date.now();
    window.updateNoteState(
      {
        id,
        noteNumber: 60,
        channel: 1,
        startVelocity: 100,
        startSample: 1000,
      },
      "started",
    );
    setTimeout(() => {
      window.updateNoteState(
        {
          id,
          endVelocity: 50,
          durationSamples: 22050,
        },
        "ended",
      );
    }, 1000);
  };

  const handleHover = (rect, note) => {
    hoveredNote = note;
    if (window.nativeLog) {
      window.nativeLog(
        "Hovered Note: " + note.noteNumber + " (Ch" + note.channel + ")",
      );
      window.nativeLog("  Dimensions: " + JSON.stringify(note.dimensions));
      window.nativeLog(
        "  Defaults: " + JSON.stringify(note.notation_is_default),
      );
    }
    // If note is in the top 350px of the window, flip tooltip to bottom
    const shouldFlip = rect.top < 350;
    tooltipPlacement = shouldFlip ? "bottom" : "top";

    tooltipPos = {
      x: rect.left + rect.width / 2,
      y: shouldFlip ? rect.bottom + 10 : rect.top - 10,
    };
  };

  const handleLeave = () => {
    hoveredNote = null;
  };
</script>

<div class="app-container">
  {#if windowMode === "debug"}
    <!-- DEBUG WINDOW: tab bar with timeline, eventlog, plugins -->
    <nav class="tab-nav">
      <button
        class:active={activeTab === "timeline"}
        onclick={() => (activeTab = "timeline")}>Timeline</button
      >
      <button
        class:active={activeTab === "eventlog"}
        onclick={() => (activeTab = "eventlog")}>Event Log</button
      >
      <button
        class:active={activeTab === "plugins"}
        onclick={() => (activeTab = "plugins")}>Plugins</button
      >
    </nav>

    <main class="main-content">
      {#if activeTab === "timeline"}
        <div class="panel-timeline">
          <Timeline
            {noteHistory}
            {heartbeat}
            firstSample={sessionOffset}
            {channelInstruments}
            {instrumentMap}
            onHover={handleHover}
            onLeave={handleLeave}
          />
        </div>
      {:else if activeTab === "eventlog"}
        <div class="panel-eventlog">
          <EventLog
            {midiEvents}
            {sessionOffset}
            {isConnected}
            activeCount={activeNotes.length}
            historyCount={noteHistory.length}
            onAddTestNote={addTestNote}
            onClearHistory={clearHistory}
            onClearLogs={clearLogs}
            onResetSession={() => resetSession(false)}
          />
        </div>
      {:else if activeTab === "plugins"}
        <div class="panel-plugins">
          <PluginsPanel />
        </div>
      {/if}
    </main>

    {#if hoveredNote}
      <div
        class="global-floating-tooltip {tooltipPlacement}"
        style="left: {tooltipPos.x}px; top: {tooltipPos.y}px;"
      >
        <strong>Note {hoveredNote?.noteNumber ?? "N/A"}</strong><br />
        Channel: {hoveredNote?.channel ?? "N/A"}<br />
        Velocity: Start {hoveredNote?.startVelocity ?? 0}, End {hoveredNote?.endVelocity ??
          0}<br />
        Start: {hoveredNote?.startSample ?? 0}<br />
        Duration: {hoveredNote?.durationSamples ?? 0}<br />
        <hr />
        {#if hoveredNote?.dimensions}
          <strong>Dimensions:</strong><br />
          {#each Object.entries(hoveredNote.dimensions) as [dim, val]}
            {#if !hoveredNote?.notation_is_default?.[dim]}
              {dim}: {hoveredNote?.techniques?.[dim] || val}<br />
            {/if}
          {/each}
        {/if}
      </div>
    {/if}
  {:else}
    <!-- MAIN WINDOW: mixer or setup mode -->
    <main class="main-content">
      {#if mainMode === "setup"}
        <div class="panel-setup">
          <div class="setup-toolbar">
            <h2>Edit Playback Template</h2>
            <div class="setup-toolbar-right">
              <button
                class="setup-cancel-btn"
                onclick={() => {
                  mainMode = "mixer";
                  const sm = getNative("setMode");
                  if (sm) sm("mixer");
                  const f = getNative("cancelSetup");
                  if (f) f();
                }}>Cancel</button
              >
              <button
                class="setup-save-btn"
                onclick={() => {
                  if (window.triggerSaveSetup) window.triggerSaveSetup();
                  mainMode = "mixer";
                  const sm = getNative("setMode");
                  if (sm) sm("mixer");
                }}>Save</button
              >
            </div>
          </div>
          <SetupPanel />
        </div>
      {:else}
        <div class="panel-mixer">
          <MixerPanel
            onEditSetup={() => {
              mainMode = "setup";
              const sm = getNative("setMode");
              if (sm) sm("setup");
              const f = getNative("requestSetupData");
              if (f) f();
            }}
          />
        </div>
      {/if}
    </main>
  {/if}
</div>

<style>
  :global(:root) {
    font-family: Inter, system-ui, Avenir, Helvetica, Arial, sans-serif;
    line-height: 1.5;
    font-weight: 400;
    color-scheme: dark;
    color: #f8fafc;
    background-color: #020617;
    font-synthesis: none;
    text-rendering: optimizeLegibility;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
  }

  :global(body) {
    margin: 0;
    display: flex;
    place-content: stretch;
    min-width: 320px;
    min-height: 100vh;
    overflow: hidden;
  }

  .app-container {
    width: 100vw;
    height: 100vh;
    display: flex;
    flex-direction: column;
  }

  .main-content {
    flex: 1;
    display: flex;
    overflow: hidden;
  }

  .tab-nav {
    display: flex;
    background: #0f172a;
    border-bottom: 1px solid #1e293b;
    padding: 0 10px;
    height: 40px;
  }

  .tab-nav button {
    background: transparent;
    border: none;
    color: #94a3b8;
    padding: 0 20px;
    cursor: pointer;
    font-size: 0.9rem;
    font-weight: 500;
    border-bottom: 2px solid transparent;
    transition: all 0.2s;
  }

  .tab-nav button:hover {
    color: #f1f5f9;
  }

  .tab-nav button.active {
    color: #38bdf8;
    border-bottom-color: #38bdf8;
    background: rgba(56, 189, 248, 0.05);
  }

  .panel-timeline,
  .panel-eventlog,
  .panel-setup,
  .panel-plugins {
    flex: 1;
    min-width: 0;
    min-height: 0;
    position: relative;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  .panel-mixer {
    flex: 1;
    min-width: 0;
    position: relative;
    display: flex;
    flex-direction: column;
  }

  /* Setup toolbar with Cancel/Save */
  .setup-toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    padding: 8px 12px;
    background: #0f172a;
    border-bottom: 1px solid #1e293b;
  }
  .setup-toolbar h2 {
    margin: 0;
    font-size: 1rem;
    font-weight: 600;
    color: #e2e8f0;
  }
  .setup-toolbar-right {
    display: flex;
    gap: 8px;
  }
  .setup-cancel-btn {
    background: transparent;
    border: 1px solid #475569;
    color: #94a3b8;
    padding: 5px 16px;
    border-radius: 4px;
    cursor: pointer;
    font-size: 0.85rem;
    transition: all 0.2s;
  }
  .setup-cancel-btn:hover {
    background: rgba(100, 116, 139, 0.2);
    color: #f1f5f9;
  }
  .setup-save-btn {
    background: #0ea5e9;
    border: none;
    color: white;
    padding: 5px 16px;
    border-radius: 4px;
    cursor: pointer;
    font-size: 0.85rem;
    font-weight: 500;
    transition: all 0.2s;
  }
  .setup-save-btn:hover {
    background: #38bdf8;
  }

  /* Global scrollbar styling */
  :global(::-webkit-scrollbar) {
    width: 8px;
  }

  :global(::-webkit-scrollbar-track) {
    background: #0f172a;
  }

  :global(::-webkit-scrollbar-thumb) {
    background: #334155;
    border-radius: 4px;
  }

  :global(::-webkit-scrollbar-thumb:hover) {
    background: #475569;
  }

  .global-floating-tooltip {
    position: fixed;
    pointer-events: none;
    background: #1e293b;
    color: #f1f5f9;
    padding: 10px;
    border-radius: 8px;
    border: 1px solid #38bdf8;
    white-space: nowrap;
    z-index: 9999;
    font-size: 0.8rem;
    box-shadow: 0 10px 15px -3px rgba(0, 0, 0, 0.5);
  }

  .global-floating-tooltip.top {
    transform: translate(-50%, -100%);
  }

  .global-floating-tooltip.bottom {
    transform: translate(-50%, 0);
  }

  .global-floating-tooltip hr {
    border: 0;
    border-top: 1px solid #334155;
    margin: 5px 0;
  }
</style>
