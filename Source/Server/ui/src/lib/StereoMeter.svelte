<script>
    import { dispatchCpp } from "./ipc.js";
    let { levels = [-120, -120, -120, -120, false, false], label = "Output", kind, id = "", maximum = 6 } = $props();
    const position = (db) => Math.pow(Math.max(0, Math.min(1, (db + 120) / (maximum + 120))), 4) * 100;
</script>

<div class="stereo-meter" aria-label={`${label} stereo meters`}>
    {#each ["L", "R"] as channel, index}
        <div class="meter-channel">
            <button class="clip" class:clipped={levels[index + 4]}
                aria-label={`Reset ${label} clip indicators`} title={`${channel}: ${levels[index + 4] ? "Clipped — click to clear" : "No clipping"}`}
                onclick={() => dispatchCpp("resetMeterClips", kind, id)}>{channel}</button>
            <div class="meter-track" title={`${label} ${channel}: ${levels[index * 2].toFixed(1)} dBFS`}>
                <div class="meter-fill" class:hot={levels[index * 2] >= 0} style:height={`${position(levels[index * 2])}%`}></div>
                <div class="meter-hold" class:hot={levels[index * 2 + 1] >= 0} style:bottom={`${position(levels[index * 2 + 1])}%`}></div>
            </div>
        </div>
    {/each}
</div>

<style>
    .stereo-meter { width: 20px; flex-shrink: 0; display: flex; gap: 2px; align-self: stretch; min-height: 0; }
    .meter-channel { display: flex; flex: 1; min-width: 0; flex-direction: column; gap: 2px; }
    .clip { width: 100%; height: 12px; flex-shrink: 0; padding: 0; border: 0; border-radius: 2px; color: #94a3b8; background: #203047; font-size: 8px; line-height: 12px; cursor: pointer; }
    .clip.clipped { background: #dc2626; color: white; }
    .clip:focus-visible { outline: 2px solid #7dd3fc; }
    .meter-track { flex: 1; min-height: 0; position: relative; overflow: hidden; border-radius: 2px; background: #0f172a; }
    .meter-fill { position: absolute; inset: auto 0 0; background: #22c55e; }
    .meter-hold { position: absolute; left: 0; right: 0; height: 2px; background: #4ade80; }
    .hot { background: #ef4444; }
</style>
