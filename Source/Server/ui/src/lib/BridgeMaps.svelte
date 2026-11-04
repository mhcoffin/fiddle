<script>
  import { dispatchCpp } from "./ipc.js";
  let { bridgeState } = $props();
  let updated = $state(false);
  let outdated = $derived((bridgeState.sections || []).filter(s => s.updateAvailable));
  const useBuiltIn = key => { updated = true; dispatchCpp("useBuiltInBridgeMaps", ...(key ? [key] : [])); };
  const doc = hash => (bridgeState.documents || []).find(d => d.hash === hash);
</script>

<details class="bridges" aria-label="Bridge maps" open={outdated.length > 0 || undefined}>
  <summary>Bridge maps</summary>
  <p>Installing the playback template gives each strings, woodwind, brass and pitched-percussion chair its section’s bridge map; every other chair keeps the universal Fiddle map. The maps are saved with this version. When playback starts, each chair below reports whether Dorico’s revision beacon matched.</p>
  {#if bridgeState.error}<p class="error" role="alert">{bridgeState.error}</p>{/if}
  {#if outdated.length}
    <div class="notice" role="status">
      <p>This version keeps the bridge maps it was set up with. Fiddle's built-in maps differ for {outdated.map(s => s.label.toLowerCase()).join(", ")}.</p>
      {#if outdated.length > 1}<button disabled={bridgeState.busy} onclick={() => useBuiltIn()}>Use all built-in maps</button>{/if}
    </div>
  {:else if updated && !bridgeState.error}
    <p class="notice" role="status">Done. With Dorico closed, press <strong>Install Playback Template</strong>, then reapply the Fiddle template in open projects. Until then Dorico plays the old maps and their notes are withheld.</p>
  {/if}
  <table aria-label="Section maps"><tbody>
    {#each bridgeState.sections || [] as section}
      <tr><td>{section.label}</td>
        {#if section.hash}
          <td>{doc(section.hash)?.name} · revision {doc(section.hash)?.revision} · {doc(section.hash)?.schema >= 2 ? "wire schema 2" : "wire schema 1"}
            {#if section.pinsPlayback === false}<br><small class="warn">Doesn’t pin Dorico’s playback options: note lengths and dynamics depend on Dorico’s settings.</small>{/if}
            {#if section.updateAvailable}<br><small>Built-in: {section.builtInName} · revision {section.builtInRevision}{section.builtInPinsPlayback ? " · pins Dorico’s playback options" : ""}</small>{/if}
          </td>
          <td class="actions">
            {#if section.updateAvailable}<button disabled={bridgeState.busy} onclick={() => useBuiltIn(section.key)}>Use built-in revision {section.builtInRevision}</button>{/if}
            <button onclick={() => dispatchCpp("exportBridgeMap", section.hash)}>Export Dorico map…</button>
          </td>
        {:else}
          <td colspan="2">Set when the playback template is installed</td>
        {/if}
      </tr>
    {/each}
  </tbody></table>
  <button onclick={() => dispatchCpp("requestBridgeMaps")}>Refresh live status</button>
</details>

<style>
  .bridges { border: 1px solid #334155; border-radius: 8px; padding: 10px 14px; margin: 12px 0; color: #dbe5f4; }
  summary { cursor: pointer; font-weight: 600; }
  p { color: #aebdd0; line-height: 1.5; font-size: 13px; } .error, .warn { color: #f8bd9d; } small { color: #aebdd0; }
  table { width: 100%; border-collapse: collapse; margin-bottom: 10px; font-size: 13px; }
  td { padding: 6px 8px; border-bottom: 1px solid #273449; }
  button { font: inherit; color: inherit; background: #172337; border: 1px solid #475569; border-radius: 5px; padding: 5px 9px; cursor: pointer; }
  button:disabled { opacity: .4; cursor: default; }
  :focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
  .notice { margin: 10px 0; padding: 10px 12px; border: 1px solid #7dd3fc55; border-radius: 6px; background: #14283d; color: #dbe5f4; }
  .notice p { margin: 0 0 8px; color: #dbe5f4; } .actions { display: flex; gap: 8px; flex-wrap: wrap; }
</style>
