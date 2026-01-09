<script>
  import { onMount } from "svelte";
  import LogPanel from "./lib/LogPanel.svelte";
  import ActiveNotes from "./lib/ActiveNotes.svelte";

  // Use Svelte 5 Runes for reactivity
  let logs = $state([]);
  let activeNotes = $state([]);
  let activeNotesList = $derived([...activeNotes].reverse());

  let logId = 0;
  window.addLogMessage = (msg, isError = false) => {
    const newLog = {
      id: logId++,
      msg,
      isError,
      time: new Date().toLocaleTimeString(),
    };
    logs = [newLog, ...logs].slice(0, 200);
  };

  const getNative = (name) => {
    return (
      (window.__JUCE__ &&
        window.__JUCE__.backend &&
        window.__JUCE__.backend[name]) ||
      window[name] ||
      (window.juce && window.juce[name]) ||
      (window.__juce__ && window.__juce__[name])
    );
  };

  const nativeLog = (msg) => {
    const f = getNative("nativeLog");
    if (f) f(msg);
  };

  nativeLog("JS Booting: Bundle loaded");
  window.addLogMessage("<i>JS Booting: Bundle loaded</i>");

  window.onerror = (msg, url, line, col, error) => {
    const errorMsg = `[JS Error] ${msg} at ${line}:${col}`;
    nativeLog(errorMsg);
    window.addLogMessage(`<b>${errorMsg}</b>`, true);
    return false;
  };

  window.updateNoteState = (id, noteNumber, channel, status) => {
    const idStr = id.toString();

    if (status === "started") {
      if (!activeNotes.some((n) => n.id === idStr)) {
        activeNotes.push({
          id: idStr,
          noteNumber,
          channel,
          subnoteCount: 0,
        });
      }
    } else if (status === "ended") {
      activeNotes = activeNotes.filter((n) => n.id !== idStr);
    } else if (status === "subnote") {
      const note = activeNotes.find((n) => n.id === idStr);
      if (note) note.subnoteCount++;
    }
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
</script>

<div class="app-container">
  <main class="main-content">
    <div class="panel-left">
      <LogPanel {logs} clear={clearLogs} />
    </div>
    <div class="panel-right">
      <ActiveNotes activeNotes={activeNotesList} />
    </div>
  </main>
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

  .panel-left {
    flex: 1;
    min-width: 0;
  }

  .panel-right {
    flex-shrink: 0;
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
</style>
