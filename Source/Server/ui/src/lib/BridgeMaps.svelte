<script>
  import { onMount } from "svelte";
  import { dispatchCpp, onFromCpp } from "./ipc.js";
  let state = $state({ documents: [], assignments: [], error: "", busy: false });
  let selectedHash = $state("");
  let draft = $state("");
  let low = $state(21), high = $state(114), port = $state(1), channel = $state(2);
  let loadedHash = "";
  let selected = $derived(state.documents.find(d => d.hash === selectedHash));
  $effect(() => {
    if (selected && selected.hash !== loadedHash) {
      loadedHash = selected.hash; draft = selected.sourceXml; low = selected.low; high = selected.high;
    }
  });
  onMount(() => {
    const unsubscribe = onFromCpp("setBridgeMapState", data => {
      state = data;
      if (!state.documents.some(d => d.hash === selectedHash)) selectedHash = state.documents[0]?.hash || "";
    });
    dispatchCpp("requestBridgeMaps");
    return unsubscribe;
  });
  const name = hash => state.documents.find(d => d.hash === hash)?.name || hash;
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
    <h2>Project bridge maps</h2>
    <button onclick={() => dispatchCpp("importBridgeMap")} disabled={state.busy}>Import map or bundle…</button>
    <button onclick={() => dispatchCpp("exportBridgeMap", "")} disabled={!state.documents.length}>Export project bundle…</button>
  </div>
  <p>Revisions and channel assignments are saved with Fiddle project history. Each channel supports one Dorico voice.</p>
  <div class="toolbar"><span>Add a starting map:</span>
    {#each ["Strings", "Woodwinds", "Brass"] as section}
      <button disabled={state.busy} onclick={() => dispatchCpp("addBridgeTemplate", section)}>{section}</button>
    {/each}
  </div>
  {#if state.error}<p class="error" role="alert">{state.error}</p>{/if}
  {#if state.busy}<p role="status">Stop playback and release notes, then refresh status to edit bridge maps. <button onclick={() => dispatchCpp("requestBridgeMaps")}>Refresh status</button></p>{/if}
  {#if state.documents.length}
    <label>Revision<select aria-label="Bridge revision" bind:value={selectedHash}>
      {#each state.documents as doc}<option value={doc.hash}>{doc.name} · revision {doc.revision} · {doc.hash.slice(0, 10)}</option>{/each}
    </select></label>
    {#if selected}
      <p>{selected.dimensions} encoded dimensions · MIDI range {selected.low}–{selected.high} · {selected.schema >= 2 ? "wire schema 2 (typed Off, revision beacon)" : "wire schema 1 (legacy)"}<br /><code>{selected.entityId}</code></p>
      <div class="toolbar">
        <button onclick={() => dispatchCpp("exportBridgeMap", selectedHash)}>Export Dorico map…</button>
        <label>Port <input aria-label="Bridge port" type="number" min="1" max="16" bind:value={port} /></label>
        <label>Channel <input aria-label="Bridge channel" type="number" min="1" max="16" bind:value={channel} /></label>
        <button disabled={state.busy || !Number.isInteger(port) || !Number.isInteger(channel)} onclick={() => dispatchCpp("assignBridgeRevision", { port: port - 1, channel, hash: selectedHash })}>Assign revision</button>
      </div>
      <p>After assigning revisions, use Chairs → Install Playback Template to install the maps and matching Dorico destinations. Restart Dorico and apply the Fiddle template. You can also export and assign a map manually. Port 1/channel 1 is reserved for the metronome.</p>
      <details>
        <summary>Advanced XML draft</summary>
        <p>Publishing validates the encoding and creates a separate immutable revision with a new Dorico ID. Existing assignments keep their revision. Unsupported actions or ambiguous encodings are reported before publication.</p>
        <label>Map XML<textarea aria-label="Bridge map XML" spellcheck="false" bind:value={draft}></textarea></label>
        <div class="toolbar">
          <label>Lowest musical note <input type="number" min="0" max="127" bind:value={low} /></label>
          <label>Highest musical note <input type="number" min="0" max="127" bind:value={high} /></label>
          <button disabled={state.busy || !Number.isInteger(low) || !Number.isInteger(high)} onclick={() => dispatchCpp("publishBridgeRevision", { parent: selectedHash, sourceXml: draft, low, high })}>Publish new revision</button>
        </div>
      </details>
    {/if}
  {:else}<p>Add a starting map or import a Dorico bridge map. No existing playback assignments are changed.</p>{/if}
  <h3>Dorico assignment checks</h3>
  <p>Schema 2 maps send a revision beacon with Dorico’s Init commands. When playback starts, Fiddle checks it against the assigned revision, with no export needed. A mismatching beacon means Dorico is playing a different map, and notes on that channel are withheld.</p>
  <p>For schema 1 maps, or to check before playing, export the map assigned to each channel from Dorico and select <strong>Verify assigned map</strong>. A match confirms the exported contents; you confirm that it is the assigned map. Reopening, checking out a version, or changing assignments clears these checks.</p>
  <p>Notes play once the base articulation has arrived. Add-on dimensions Dorico has not sent yet count as unspecified. <button onclick={() => dispatchCpp("requestBridgeMaps")}>Refresh live status</button></p>
  {#if state.assignments.length}
    <table><thead><tr><th>Input</th><th>Map</th><th>Status</th><th></th></tr></thead><tbody>
      {#each state.assignments as assignment}
        <tr><td>Port {assignment.port + 1}, channel {assignment.channel}</td><td>{name(assignment.hash)}</td><td class:warn={!playing(assignment)}>{verification(assignment)}{#if assignment.lastWithheld}<br /><small>Last withheld note: {assignment.lastWithheld}</small>{/if}{#if counters(assignment)}<br /><small>{counters(assignment)}</small>{/if}</td><td><button disabled={state.busy} onclick={() => dispatchCpp("verifyBridgeAssignment", { port: assignment.port, channel: assignment.channel })}>Verify assigned map…</button> <button disabled={state.busy} onclick={() => dispatchCpp("assignBridgeRevision", { port: assignment.port, channel: assignment.channel, hash: "" })}>Remove assignment</button></td></tr>
      {/each}
    </tbody></table>
  {:else}<p>No project bridge revisions are assigned. Existing legacy decoding remains active.</p>{/if}
  {#if state.assignments.length}<p>Removing an assignment restores legacy decoding for that input. Match the map in Dorico before playing.</p>{/if}
</section>

<style>
  .bridges { padding: 18px 24px; overflow: auto; min-height: 0; flex: 1; color: #dbe5f4; }
  .toolbar { display: flex; align-items: center; flex-wrap: wrap; gap: 10px; margin: 12px 0; }
  h2 { margin: 0 auto 0 0; font-size: 18px; } h3 { margin-top: 24px; }
  p { color: #aebdd0; line-height: 1.5; } .error, .warn { color: #f8bd9d; } small { color: #aebdd0; }
  button, input, select, textarea { font: inherit; color: inherit; background: #172337; border: 1px solid #475569; border-radius: 5px; padding: 7px 10px; }
  button { cursor: pointer; } button:disabled { opacity: .5; cursor: default; }
  label { display: flex; align-items: center; gap: 8px; } input { width: 65px; }
  select { max-width: 100%; flex: 1; min-width: 0; }
  details { border: 1px solid #334155; padding: 12px; margin-top: 16px; } summary { cursor: pointer; }
  details > label { display: block; } textarea { display: block; box-sizing: border-box; width: 100%; height: 340px; resize: vertical; font-family: monospace; font-size: 12px; margin-top: 8px; }
  table { width: 100%; border-collapse: collapse; } td, th { padding: 10px; border-bottom: 1px solid #334155; text-align: left; } code { overflow-wrap: anywhere; font-size: 11px; }
  :focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
  @media (max-width: 700px) { .bridges { padding: 12px; } table { font-size: 12px; } td, th { padding: 6px; } }
</style>
