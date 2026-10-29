<script>
  import { onMount } from "svelte";
  import { dispatchCpp, onFromCpp } from "./ipc.js";
  let state = $state({ documents: [], assignments: [], sections: [], error: "", busy: false });
  onMount(() => {
    const unsubscribe = onFromCpp("setBridgeMapState", data => { state = data; });
    dispatchCpp("requestBridgeMaps");
    return unsubscribe;
  });
  let updated = $state(false);
  let outdated = $derived((state.sections || []).filter(s => s.updateAvailable));
  const useBuiltIn = key => { updated = true; dispatchCpp("useBuiltInBridgeMaps", ...(key ? [key] : [])); };
  const doc = hash => state.documents.find(d => d.hash === hash);
  const sectionOf = hash => (state.sections || []).find(s => s.hash === hash)?.label || doc(hash)?.name || hash;
  const playing = a => a.beacon !== "mismatched" && (a.verified || a.beacon === "matched");
  function verification(a) {
    if (a.beacon === "mismatched") return `Dorico is playing a different revision (beacon ${a.receivedBeacon.join("/")}, expected ${a.expectedBeacon.join("/")}) · notes withheld`;
    if (a.beacon === "matched") return "Beacon matches · Dorico confirmed this revision at playback start";
    if (a.verified) return "Export matches · assignment confirmed by user";
    if (a.beacon === "absent") return "Waiting for the revision beacon · start playback";
    return "Unverified · notes withheld";
  }
  function counters(a) {
    const parts = [];
    if (a.withheldNotes) parts.push(`${a.withheldNotes} notes withheld`);
    if (a.invalidCommands) parts.push(`${a.invalidCommands} invalid commands`);
    if (a.ignoredOffs) parts.push(`${a.ignoredOffs} stale Offs ignored`);
    if (a.debouncedOffs) parts.push(`${a.debouncedOffs} early Offs suppressed`);
    return parts.join(" · ");
  }
</script>

<section aria-label="Bridge maps" class="bridges">
  <div class="toolbar">
    <h2>Bridge maps</h2>
    <button onclick={() => dispatchCpp("requestBridgeMaps")}>Refresh live status</button>
  </div>
  <p>Fiddle uses one bridge map per orchestral section. <strong>Chairs → Install Playback Template</strong> gives each strings, woodwind, brass and pitched-percussion chair its section’s map; every other chair keeps the universal Fiddle map. The maps are saved with this version, so changing branch or version restores the ones it used.</p>
  {#if state.error}<p class="error" role="alert">{state.error}</p>{/if}
  {#if outdated.length}
    <div class="notice" role="status">
      <p>This version keeps the bridge maps it was set up with. Fiddle's built-in maps differ for {outdated.map(s => s.label.toLowerCase()).join(", ")}.</p>
      {#if outdated.length > 1}<button disabled={state.busy} onclick={() => useBuiltIn()}>Use all built-in maps</button>{/if}
    </div>
  {:else if updated && !state.error}
    <p class="notice" role="status">Done. With Dorico closed, open <strong>Chairs</strong> and press <strong>Install Playback Template</strong>, then reapply the Fiddle template in open projects. Until then Dorico plays the old maps and their notes are withheld.</p>
  {/if}
  <table aria-label="Section maps"><thead><tr><th>Section</th><th>Map</th><th></th></tr></thead><tbody>
    {#each state.sections || [] as section}
      <tr><td>{section.label}</td>
        {#if section.hash}
          <td>{doc(section.hash)?.name} · revision {doc(section.hash)?.revision} · {doc(section.hash)?.schema >= 2 ? "wire schema 2" : "wire schema 1"}
            {#if section.pinsPlayback === false}<br><small class="warn">Doesn’t pin Dorico’s playback options: note lengths and dynamics depend on Dorico’s settings.</small>{/if}
            {#if section.updateAvailable}<br><small>Built-in: {section.builtInName} · revision {section.builtInRevision}{section.builtInPinsPlayback ? " · pins Dorico’s playback options" : ""}</small>{/if}
          </td>
          <td class="actions">
            {#if section.updateAvailable}<button disabled={state.busy} onclick={() => useBuiltIn(section.key)}>Use built-in revision {section.builtInRevision}</button>{/if}
            <button onclick={() => dispatchCpp("exportBridgeMap", section.hash)}>Export Dorico map…</button>
          </td>
        {:else}
          <td colspan="2">Not set yet · install the playback template from Chairs</td>
        {/if}
      </tr>
    {/each}
  </tbody></table>
  <h3>Channels</h3>
  <p>When playback starts, Dorico sends each map’s revision beacon. A match confirms that Dorico is playing the map Fiddle expects. A mismatch means the score still uses another map, usually because the template was not reapplied after installing; its notes are withheld until that is fixed.</p>
  {#if state.assignments.length}
    <table aria-label="Bridge channels"><thead><tr><th>Input</th><th>Map</th><th>Status</th></tr></thead><tbody>
      {#each state.assignments as assignment}
        <tr><td>Port {assignment.port + 1}, channel {assignment.channel}</td><td>{sectionOf(assignment.hash)}</td><td class:warn={!playing(assignment)}>{verification(assignment)}{#if assignment.lastWithheld}<br /><small>Last withheld note: {assignment.lastWithheld}</small>{/if}{#if counters(assignment)}<br /><small>{counters(assignment)}</small>{/if}</td></tr>
      {/each}
    </tbody></table>
  {:else}<p>No chairs use a section map yet. Install the playback template from Chairs.</p>{/if}
</section>

<style>
  .bridges { padding: 18px 24px; overflow: auto; min-height: 0; flex: 1; color: #dbe5f4; }
  .toolbar { display: flex; align-items: center; flex-wrap: wrap; gap: 10px; margin: 12px 0; }
  h2 { margin: 0 auto 0 0; font-size: 18px; } h3 { margin-top: 24px; }
  p { color: #aebdd0; line-height: 1.5; } .error, .warn { color: #f8bd9d; } small { color: #aebdd0; }
  button { font: inherit; color: inherit; background: #172337; border: 1px solid #475569; border-radius: 5px; padding: 7px 10px; cursor: pointer; }
  table { width: 100%; border-collapse: collapse; } td, th { padding: 10px; border-bottom: 1px solid #334155; text-align: left; }
  :focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
  button:disabled { opacity: .4; cursor: default; }
  .notice { margin: 10px 0; padding: 10px 12px; border: 1px solid #7dd3fc55; border-radius: 6px; background: #14283d; color: #dbe5f4; }
  .notice p { margin: 0 0 8px; color: #dbe5f4; } .actions { display: flex; gap: 8px; flex-wrap: wrap; }
  @media (max-width: 700px) { .bridges { padding: 12px; } table { font-size: 12px; } td, th { padding: 6px; } }
</style>
