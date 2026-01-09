<script>
  let { logs = [], clear } = $props();
  let logList = $state(null);

  $effect(() => {
    if (logList && logs) {
      // Svelte takes a moment to update the DOM
      setTimeout(() => {
        if (logList) logList.scrollTop = 0;
      }, 0);
    }
  });
</script>

<div class="log-panel">
  <div class="header">
    <h2>MIDI & Event Log</h2>
    <button onclick={clear} class="clear-btn">Clear</button>
  </div>
  <div class="log-list" bind:this={logList}>
    {#each logs as log (log.id)}
      <div class="log-item {log.isError ? 'error' : ''}">
        <div>{@html log.msg}</div>
        <div class="time">{log.time}</div>
      </div>
    {/each}
  </div>
</div>

<style>
  .log-panel {
    display: flex;
    flex-direction: column;
    height: 100%;
    padding: 20px;
    border-right: 1px solid #334155;
    overflow: hidden;
  }

  .header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 20px;
  }

  h2 {
    color: #38bdf8;
    margin: 0;
    font-size: 1.2rem;
  }

  .clear-btn {
    background: transparent;
    border: 1px solid #475569;
    color: #94a3b8;
    padding: 4px 12px;
    border-radius: 4px;
    cursor: pointer;
    font-size: 0.8rem;
  }

  .clear-btn:hover {
    background: #334155;
    color: #f8fafc;
  }

  .log-list {
    flex: 1;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
  }

  .log-item {
    margin-bottom: 8px;
    padding: 10px;
    background: #1e293b;
    border-radius: 6px;
    border-left: 4px solid #38bdf8;
    font-size: 0.9rem;
    color: #e2e8f0;
    position: relative;
  }

  .log-item .time {
    font-size: 0.7rem;
    color: #64748b;
    margin-top: 4px;
    text-align: right;
  }

  .log-item.error {
    border-left-color: #ef4444;
    background: #451a1a;
  }
</style>
