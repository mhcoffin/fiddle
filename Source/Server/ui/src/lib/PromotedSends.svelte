<script>
    import Knob from "./Knob.svelte";
    import { dispatchCpp } from "./ipc.js";
    import { gainToPosition, positionToGain, formatGain, parseGain } from "./knobMath.js";
    let { strip, buses = [], horizontal = false } = $props();
    let sends = $derived((strip.sends || []).filter((send) => send.showOnStrip));
</script>

<div class="promoted-sends" class:horizontal aria-label={`Post-fader sends for ${strip.layerName || strip.library || "layer"}`}>
    {#each sends as send (send.id)}
        {@const name = buses.find((bus) => bus.id === send.busId)?.name || "Missing bus"}
        <div class="promoted-send" class:send-off={!send.enabled}>
            <span class="send-destination" title={`Send to ${name}${send.enabled ? "" : " (disabled)"}`}>
                <span class="send-arrow" aria-hidden="true">→</span>
                <span class="destination">{name}{send.enabled ? "" : " (off)"}</span>
            </span>
            <Knob compact label={`Send to ${name}`} unit="dB" min={-120} max={12} value={send.gainDb}
                toPosition={gainToPosition} fromPosition={positionToGain} format={formatGain} parse={parseGain}
                oninput={(gain) => dispatchCpp("updateStripSend", strip.id, send.id, send.busId, gain, send.enabled)}
                onGestureBegin={() => dispatchCpp("beginHistoryGesture")}
                onGestureEnd={() => dispatchCpp("endHistoryGesture")} />
        </div>
    {/each}
</div>

<style>
    .promoted-sends { height: var(--strip-send-height); flex-shrink: 0; min-width: 0; overflow-y: auto; overflow-x: hidden; scrollbar-width: thin; }
    .promoted-send { height: var(--strip-send-row-height, 52px); box-sizing: border-box; padding: 3px 0; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 2px; }
    .send-destination { display: flex; align-items: center; gap: 4px; width: 100%; min-width: 0; color: #7dd3fc; font-size: 0.65rem; line-height: 16px; }
    .send-arrow { flex-shrink: 0; font-size: 0.85rem; }
    .destination { min-width: 0; overflow: hidden; white-space: nowrap; text-overflow: ellipsis; }
    .send-off { opacity: 0.55; }
    .horizontal .promoted-send { flex-direction: row; justify-content: space-between; gap: 5px; }
    .horizontal .send-destination { flex: 1; width: auto; }
</style>
