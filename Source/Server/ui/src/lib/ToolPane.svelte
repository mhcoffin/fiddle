<script>
    import { onMount, onDestroy } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import { historyShortcut, installHistoryGestures } from "./historyGestures.js";
    import PrintPanel from "./PrintPanel.svelte";
    import ChairManager from "./ChairManager.svelte";
    import AudioDiagnostics from "./AudioDiagnostics.svelte";
    import ScoreTests from "./ScoreTests.svelte";

    let { toolId } = $props();
    let visible = $state(true);
    const close = () => dispatchCpp("hideToolWindow", toolId);
    const off = [onFromCpp("setToolWindowVisible", value => { visible = !!value; })];
    onMount(() => {
        dispatchCpp("toolWindowReady", toolId);
        const removeGestures = installHistoryGestures(window, dispatchCpp);
        const keydown = event => {
            if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "w") {
                event.preventDefault(); close(); return;
            }
            if (toolId !== "chairs") return;
            const command = historyShortcut(event);
            if (command) { event.preventDefault(); dispatchCpp(command); }
        };
        window.addEventListener("keydown", keydown);
        return () => { removeGestures(); window.removeEventListener("keydown", keydown); };
    });
    onDestroy(() => off.forEach(unsubscribe => unsubscribe()));
</script>

<div class="tool-pane">
    {#if toolId === "print"}<PrintPanel />
    {:else if toolId === "chairs"}<ChairManager onClose={close} />
    {:else if toolId === "audio-performance"}<AudioDiagnostics standalone {visible} onClose={close} />
    {:else if toolId === "score-tests"}<ScoreTests {visible} onClose={close} />
    {/if}
</div>

<style>
    :global(body) { margin: 0; overflow: hidden; }
    .tool-pane { width: 100%; height: 100vh; color: #e2e8f0; background: #0b1220; font-family: Inter, system-ui, sans-serif; line-height: 1.5; color-scheme: dark; }
</style>
