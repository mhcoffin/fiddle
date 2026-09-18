<script>
    import Knob from "./Knob.svelte";
    import { dispatchCpp } from "./ipc.js";
    import { formatBalance, parseBalance } from "./balanceMath.js";
    let { value = 0, label = "Stereo balance", command, id } = $props();
    const begin = () => dispatchCpp("beginHistoryGesture");
    const end = () => dispatchCpp("endHistoryGesture");
    const set = (percent) => dispatchCpp(command, id, percent / 100);
</script>

<div class="balance-control" title="Stereo balance: center leaves both channels unchanged. Left reduces right; right reduces left. Pre-fader sends are unaffected.">
    <button class="balance-label" aria-label={`Center ${label}`} title="Reset balance to center"
        onclick={() => { begin(); set(0); end(); }}>Balance</button>
    <Knob compact showRing={false} {label} value={value * 100} min={-100} max={100} step={1} fineStep={1}
        format={formatBalance} parse={parseBalance} oninput={set}
        onGestureBegin={begin} onGestureEnd={end} />
</div>

<style>
    .balance-control { height: 42px; flex-shrink: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 2px; }
    .balance-label { padding: 0; border: 0; background: none; color: #a3b4c8; font-size: .6rem; line-height: 12px; cursor: pointer; }
    .balance-label:hover { color: #bae6fd; }
    .balance-label:focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
</style>
