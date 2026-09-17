<script>
    let { label, children } = $props();
    let open = $state(false);
    let root;
    let trigger;
    const close = () => { open = false; };
</script>

<svelte:window
    onclick={(event) => { if (!root?.contains(event.target)) close(); }}
    onkeydown={(event) => {
        if (open && event.key === "Escape") {
            event.preventDefault();
            close();
            trigger?.focus();
        }
    }}
/>

<div class="toolbar-menu" bind:this={root}>
    <button bind:this={trigger} class="trigger" aria-expanded={open}
        onclick={() => { open = !open; }}>{label} <span aria-hidden="true">▾</span></button>
    {#if open}
        <div class="panel" role="group" aria-label={`${label} options`}>
            {@render children(close)}
        </div>
    {/if}
</div>

<style>
    .toolbar-menu { position: relative; flex-shrink: 0; }
    .trigger { height: 30px; padding: 4px 10px; border: 1px solid #334155; border-radius: 5px; background: #111827; color: #cbd5e1; font: inherit; font-size: .75rem; cursor: pointer; white-space: nowrap; }
    .trigger span { margin-left: 5px; color: #94a3b8; }
    .trigger:hover, .trigger[aria-expanded="true"] { background: #1e293b; border-color: #64748b; }
    .trigger:focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
    .panel { position: absolute; right: 0; top: calc(100% + 6px); z-index: 30; min-width: 220px; padding: 12px; display: flex; flex-direction: column; gap: 14px; background: #0f172a; border: 1px solid #475569; border-radius: 7px; box-shadow: 0 12px 30px #0008; }
</style>
