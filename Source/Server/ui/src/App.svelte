<script>
  /** @type {import('svelte')} */
  import { onMount } from "svelte";
  import Timeline from "./lib/Timeline.svelte";
  import EventLog from "./lib/EventLog.svelte";

  import PluginsPanel from "./lib/PluginsPanel.svelte";
  import MidiCapture from "./lib/MidiCapture.svelte";
  import VersionHistory from "./lib/VersionHistory.svelte";
  import MixerPanel from "./lib/MixerPanel.svelte";
  import LibraryManager from "./lib/LibraryManager.svelte";
  import { dispatchCpp, onFromCpp } from "./lib/ipc.js";
  import {
    DEFAULT_UI_ZOOM,
    UI_ZOOM_STEP,
    readUiZoom,
    writeUiZoom,
  } from "./lib/uiPreferences.js";

  // Determine window mode from URL parameter
  const urlParams = new URLSearchParams(window.location.search);
  const windowMode = urlParams.get("mode") || "main";
  const viewMode = urlParams.get("view") || "default";



  // Use Svelte 5 Runes for reactivity
  let logs = $state([]);
  let activeNotes = $state([]);
  let noteHistory = $state([]);
  let diagnostics = $state([]);
  let heartbeat = $state(0);
  // Ordered list of tempo changes: { bpm, samplePosition, timeSigNumerator, timeSigDenominator }
  // Used by the Timeline tempo track. Reset on transport start.
  let tempoChanges = $state([]);
  // Latest BPM (derived from the last entry in tempoChanges, default 120)
  let liveBpm = $derived(tempoChanges.length > 0 ? tempoChanges[tempoChanges.length - 1].bpm : 120);
  let hoveredNote = $state(null);
  let tooltipPos = $state({ x: 0, y: 0 });
  let tooltipPlacement = $state("top");
  let activeTab = $state("timeline");
  let channelInstruments = $state({});
  let midiEvents = $state([]);
  let serverVersion = $state("");
  let isConnected = $state(false);
  let connectionWarning = $state("");
  let instrumentMap = $state({}); // "port:channel" → { name, family, isSolo }
  let metronomeActive = $state(false); // true when tempo is derived from click track
  let metronomeBeats = $state([]); // [{samplePosition, isDownbeat}] — actual beat positions from click track
  let uiZoom = $state(readUiZoom(window.localStorage));

  const setUiZoom = (value) => {
    uiZoom = writeUiZoom(window.localStorage, value);
  };
  const zoomIn = () => setUiZoom(uiZoom + UI_ZOOM_STEP);
  const zoomOut = () => setUiZoom(uiZoom - UI_ZOOM_STEP);
  const resetZoom = () => setUiZoom(DEFAULT_UI_ZOOM);
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
    // When the metronome is active, the click track will provide fresh
    // tempo data — start clean so stale values don't linger.
    // When inactive, preserve the last tempo so the beat grid stays correct.
    if (metronomeActive) {
      tempoChanges = [];
    } else if (tempoChanges.length > 0) {
      const last = tempoChanges[tempoChanges.length - 1];
      tempoChanges = [{ ...last, samplePosition: 0 }];
    }
    metronomeBeats = [];
  };

  let logId = 0;

  const nativeLog = (msg) => {
    dispatchCpp("nativeLog", msg);
  };

  // ── C++ → JS message handlers ───────────────────────────────────────────────

  onFromCpp("setHeartbeat", (val) => {
    heartbeat = val;
  });

  onFromCpp("setTempo", ({ bpm, timeSigNumerator: n, timeSigDenominator: d, samplePosition, source }) => {
    if (bpm > 0) {
      // Track whether this tempo came from the metronome click track
      metronomeActive = (source === "metronome");

      // Deduplicate: skip if BPM hasn't changed (Dorico sends on every block)
      const last = tempoChanges[tempoChanges.length - 1];
      if (!last || Math.abs(last.bpm - bpm) > 0.01) {
        tempoChanges = [...tempoChanges, {
          bpm,
          samplePosition: Number(samplePosition ?? 0),
          timeSigNumerator: n ?? 4,
          timeSigDenominator: d ?? 4,
          source: source || "host",
        }];
      }
    }
  });

  onFromCpp("metronomeBeat", ({ samplePosition, isDownbeat, noteNumber, velocity }) => {
    metronomeBeats = [...metronomeBeats, {
      samplePosition: Number(samplePosition),
      isDownbeat: !!isDownbeat,
      noteNumber: noteNumber ?? 0,
      velocity: velocity ?? 0,
    }];
  });

  onFromCpp("setServerVersion", (ver) => {
    serverVersion = ver;
  });

  onFromCpp("setConnectionState", (connected) => {
    isConnected = connected;
  });

  onFromCpp("setConnectionWarning", (message) => {
    connectionWarning = String(message || "");
  });

  // data = { channel, name }
  onFromCpp("setChannelInstrument", ({ channel, name }) => {
    channelInstruments = {
      ...channelInstruments,
      [channel]: name,
    };
  });

  // data = array of { port, channel, label, name, family, isSolo }
  // Note: port/channel from getChannelMapAsJson() are 0-based,
  // but protobuf note.channel() is 1-based. Convert to 1-based here
  // so the instrumentMap keys match the note-derived keys in Timeline.
  onFromCpp("setInstrumentMap", (arr) => {
    try {
      const map = {};
      for (const item of arr) {
        const key = `${item.port}:${item.channel + 1}`;
        map[key] = {
          name: item.label || item.name,
          family: item.family,
          isSolo: item.isSolo,
        };
      }
      instrumentMap = map;
      console.log(`[Setup] Instrument map: ${Object.keys(map).length} entries`);
    } catch (e) {
      console.error("[Setup] Failed to process instrument map:", e);
    }
  });

  // data = { msg: string, isError: bool }
  onFromCpp("addLogMessage", ({ msg, isError = false }) => {
    const newLog = {
      id: logId++,
      msg,
      isError,
      time: new Date().toLocaleTimeString(),
    };
    logs = [newLog, ...logs].slice(0, 200);
  });

  // Expose addLogMessage on window so onerror can access it
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



  nativeLog("JS Booting: Bundle loaded");
  window.addLogMessage("<i>JS Booting: Bundle loaded</i>");

  // data = { noteData: object, status: string }
  onFromCpp("updateNoteState", ({ noteData, status }) => {
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
        const dataToMerge = { ...noteData, id: idStr };
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
  });

  // data = the event object directly
  onFromCpp("pushMidiEvent", (event) => {
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
  });

  const clearLogs = () => {
    logs = [];
  };

  onMount(() => {
    // Signal readiness to JUCE using the modern Native Function API
    const signalReady = () => {
      // Small timeout to simulate fallback retry if juce bridge isn't immediately ready
      // though typically `dispatchCpp` handles warnings if missing.
      dispatchCpp("signalReady", viewMode === "history" ? "history" : viewMode === "library" ? "library" : "mixer");
      window.addLogMessage("<i>UI signaled readiness to C++</i>");
      console.log("UI Ready signaled via native function");
    };

    signalReady();

    // Global keyboard shortcuts (Undo/Redo, Zoom)
    const handleKeyDown = (e) => {
      if (e.metaKey || e.ctrlKey) {
        if (e.key === "z") {
          e.preventDefault();
          if (e.shiftKey) {
            dispatchCpp("redo");
          } else {
            dispatchCpp("undo");
          }
          return;
        }
        if (e.key === "=" || e.key === "+") {
          e.preventDefault();
          zoomIn();
          return;
        }
        if (e.key === "-") {
          e.preventDefault();
          zoomOut();
          return;
        }
        if (e.key === "0") {
          e.preventDefault();
          resetZoom();
          return;
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

<div class="app-container" style="zoom: {uiZoom};">
  {#if connectionWarning}
    <div class="connection-warning" role="alert">
      <div>
        <strong>Additional Fiddle plug-in rejected</strong>
        <span>{connectionWarning}</span>
      </div>
      <button
        type="button"
        aria-label="Dismiss connection warning"
        onclick={() => {
          connectionWarning = "";
          dispatchCpp("dismissConnectionWarning");
        }}>Dismiss</button
      >
    </div>
  {/if}
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
      <button
        class:active={activeTab === "capture"}
        onclick={() => (activeTab = "capture")}>MIDI Capture</button
      >
    </nav>

    <main class="main-content">
      {#if activeTab === "timeline"}
        <div class="panel-timeline">
          <Timeline
            {noteHistory}
            {heartbeat}
            bpm={liveBpm}
            {tempoChanges}
            firstSample={sessionOffset}
            {channelInstruments}
            {instrumentMap}
            {metronomeActive}
            {metronomeBeats}
            onHover={handleHover}
            onLeave={handleLeave}
            onTempoChange={(updated) => tempoChanges = updated}
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
      {:else if activeTab === "capture"}
        <div class="panel-capture">
          <MidiCapture />
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
  {:else if viewMode === "history"}
    <!-- HISTORY WINDOW -->
    <main class="main-content">
      <div class="panel-versions" style="width: 100%; height: 100%;">
        <VersionHistory />
      </div>
    </main>

  {:else if viewMode === "library"}
    <!-- LIBRARY MANAGER WINDOW -->
    <main class="main-content">
      <div class="panel-library" style="width: 100%; height: 100%;">
        <LibraryManager />
      </div>
    </main>
  {:else}
    <!-- MAIN WINDOW: mixer only -->
    <main class="main-content">
      <div class="panel-mixer">
        <MixerPanel {uiZoom} onZoomIn={zoomIn} onZoomOut={zoomOut} onResetZoom={resetZoom} />
      </div>
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

  .connection-warning {
    flex: 0 0 auto;
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 20px;
    padding: 12px 18px;
    border-bottom: 1px solid rgba(251, 191, 36, 0.55);
    background: #3f2f0b;
    color: #fef3c7;
    box-shadow: 0 4px 14px rgba(0, 0, 0, 0.28);
    z-index: 100;
  }

  .connection-warning > div {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .connection-warning strong {
    color: #fde68a;
  }

  .connection-warning span {
    color: #f8e7b0;
    font-size: 0.9rem;
  }

  .connection-warning button {
    flex: 0 0 auto;
    padding: 7px 13px;
    border: 1px solid #d6a629;
    border-radius: 6px;
    background: #5b4311;
    color: #fff4bd;
    font: inherit;
    font-weight: 650;
    cursor: pointer;
  }

  .connection-warning button:hover {
    background: #765719;
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
    color: #cbd5e1;
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
  .panel-plugins,
  .panel-capture {
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


  /* Scrollbar: match macOS overlay-style (thin, transparent track, auto-hide) */
  :global(::-webkit-scrollbar) {
    width: 6px;
    height: 10px; /* taller hit area for horizontal; visually thin via thumb borders below */
  }
  :global(::-webkit-scrollbar-track) {
    background: transparent;
    cursor: default; /* suppress WebKit's ns-resize cursor on the track */
  }
  :global(::-webkit-scrollbar-thumb) {
    background: rgba(148, 163, 184, 0.35); /* slate-400 @ 35% */
    border-radius: 5px;
    /* 2px top+bottom transparent border shrinks visual height to ~6px while keeping 10px hit area */
    border: 2px solid transparent;
    background-clip: padding-box;
    cursor: default;
    transition: background 0.2s;
  }
  :global(::-webkit-scrollbar-thumb:hover) {
    background: rgba(148, 163, 184, 0.6);
    background-clip: padding-box;
  }
  :global(::-webkit-scrollbar-button) {
    display: none; /* remove arrow buttons */
  }
  :global(::-webkit-scrollbar-corner) {
    background: transparent;
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
