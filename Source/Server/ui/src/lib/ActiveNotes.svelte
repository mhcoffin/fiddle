<script>
  let { activeNotes = [] } = $props();
</script>

<div class="active-notes">
  <h2>Active Notes</h2>
  <div class="notes-list">
    {#each activeNotes as note (note.id)}
      <div class="note-card">
        <div class="card-header">
          <span class="pitch">Note {note.noteNumber}</span>
          <span class="channel">Ch {note.channel}</span>
        </div>
        <div class="subnote-info">
          <span class="label">Subnotes:</span>
          <span class="count">{note.subnoteCount}</span>
        </div>
        <div class="note-id">ID: {note.id}</div>
      </div>
    {:else}
      <div class="empty-state">No active notes</div>
    {/each}
  </div>
</div>

<style>
  .active-notes {
    width: 300px;
    display: flex;
    flex-direction: column;
    height: 100%;
    padding: 20px;
    background: #0f172a;
    overflow: hidden;
  }

  h2 {
    color: #38bdf8;
    margin: 0 0 20px 0;
    font-size: 1.2rem;
  }

  .notes-list {
    flex: 1;
    overflow-y: auto;
  }

  .note-card {
    background: #1e293b;
    border: 1px solid #334155;
    border-radius: 8px;
    padding: 12px;
    margin-bottom: 12px;
    transition: transform 0.1s ease;
  }

  .note-card:hover {
    border-color: #38bdf8;
    transform: translateY(-2px);
  }

  .card-header {
    display: flex;
    justify-content: space-between;
    margin-bottom: 8px;
  }

  .pitch {
    font-weight: bold;
    color: #f1f5f9;
  }

  .channel {
    color: #94a3b8;
    font-size: 0.8rem;
  }

  .subnote-info {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 4px;
  }

  .label {
    font-size: 0.8rem;
    color: #94a3b8;
  }

  .count {
    color: #10b981;
    font-weight: bold;
  }

  .note-id {
    font-size: 0.7rem;
    color: #475569;
  }

  .empty-state {
    text-align: center;
    color: #475569;
    margin-top: 40px;
    font-style: italic;
  }
</style>
