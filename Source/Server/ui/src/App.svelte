<script>
  /** @type {import('svelte')} */
  import { onMount } from "svelte";
  import Timeline from "./lib/Timeline.svelte";
  import EventLog from "./lib/EventLog.svelte";

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
    // channel is 0-based from C++, Timeline uses 1-based
    channelInstruments = {
      ...channelInstruments,
      [channel + 1]: name,
    };
  };

  const getNative = (name) => {
    const w = /** @type {any} */ (window);
    return (
      (w.__JUCE__ && w.__JUCE__.backend && w.__JUCE__.backend[name]) ||
      window[name] ||
      (w.juce && w.juce[name]) ||
      (w.__juce__ && w.__juce__[name])
    );
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
  <header class="app-header">
    <h1>
      Fiddle Server <small style="font-size: 0.6em; color: #64748b;"
        >v{serverVersion || "?.?"}</small
      >
    </h1>
    <div class="header-controls">
      <div
        class="connection-indicator {isConnected
          ? 'connected'
          : 'disconnected'}"
        title={isConnected ? "Connected to Plugin" : "Disconnected"}
      ></div>
      <span class="status-pill"
        >Active: {activeNotes.length} | History: {noteHistory.length}</span
      >
      <button onclick={addTestNote}>Add Test Note</button>
      <button onclick={clearHistory}>Clear History</button>
      <button onclick={clearLogs}>Clear Logs</button>
    </div>
    <div class="nav-actions">
      <button class="reset-btn" onclick={() => resetSession(false)}
        >Reset Session</button
      >
    </div>
  </header>
  <nav class="tab-nav">
    <button
      class:active={activeTab === "timeline"}
      onclick={() => (activeTab = "timeline")}>Timeline</button
    >
    <button
      class:active={activeTab === "eventlog"}
      onclick={() => (activeTab = "eventlog")}>Event Log</button
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
          onHover={handleHover}
          onLeave={handleLeave}
        />
      </div>
    {:else}
      <div class="panel-eventlog">
        <EventLog {midiEvents} {sessionOffset} />
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

  .app-header {
    height: 50px;
    background: #0f172a;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 20px;
    border-bottom: 1px solid #1e293b;
  }

  .app-header h1 {
    font-size: 1.2rem;
    color: #38bdf8;
    margin: 0;
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
  .panel-eventlog {
    flex: 1;
    min-width: 0;
    position: relative;
    display: flex;
    flex-direction: column;
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

  .nav-actions {
    margin-left: 20px;
  }

  .reset-btn {
    padding: 6px 12px;
    background: #475569;
    border: 1px solid #64748b;
    border-radius: 4px;
    color: white;
    font-size: 0.8rem;
    cursor: pointer;
    transition: all 0.2s;
  }

  .reset-btn:hover {
    background: #ef4444;
    border-color: #f87171;
  }

  .connection-indicator {
    width: 12px;
    height: 12px;
    border-radius: 50%;
    background: #ef4444; /* Red */
    box-shadow: 0 0 5px rgba(239, 68, 68, 0.5);
    transition: all 0.3s;
    margin-right: 15px;
  }

  .connection-indicator.connected {
    background: #4ade80; /* Green */
    box-shadow: 0 0 8px rgba(74, 222, 128, 0.6);
  }
</style>
