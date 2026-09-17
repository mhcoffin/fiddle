<script>
    import { dispatchCpp } from "./ipc.js";

    let {
        buses = [],
        strips = [],
        master = { gainDb: 0, peakDb: -120, inserts: [] },
        busMeters = {},
        masterPeakDb = -120,
        onManage = () => {},
        onOpenBusAudio = () => {},
        onOpenMasterAudio = () => {},
    } = $props();

    let collapsed = $state(false);
    let anySoloed = $derived(buses.some((bus) => bus.soloed));

    const MIN_DB = -120;
    const STRIP_MAX_DB = 6;
    const BUS_MAX_DB = 12;
    const SKEW = 0.25;
    const dbToPos = (db, maximum = STRIP_MAX_DB) => {
        if (db <= MIN_DB) return 0;
        if (db >= maximum) return 1;
        return Math.pow((db - MIN_DB) / (maximum - MIN_DB), 1 / SKEW);
    };
    const posToDb = (pos, maximum = STRIP_MAX_DB) => {
        if (pos <= 0) return MIN_DB;
        if (pos >= 1) return maximum;
        return MIN_DB + (maximum - MIN_DB) * Math.pow(pos, SKEW);
    };
    const insertCount = (bus) => Number(
        bus.audio?.insertCount ||
        (bus.audio?.preFaderInserts?.length || 0) +
        (bus.audio?.postFaderInserts?.length || 0),
    );
    const routeCount = (busId) =>
        strips.filter((strip) => strip.directOutputBusId === busId).length;
    const busInserts = (bus) => [
        ...(bus.audio?.preFaderInserts || []).map((insert) => ({ ...insert, location: "Pre" })),
        ...(bus.audio?.postFaderInserts || []).map((insert) => ({ ...insert, location: "Post" })),
    ];
</script>

<aside class="bank" class:collapsed aria-label="Audio buses">
    <div class="header">
        <button
            class="header-toggle"
            onclick={() => { collapsed = !collapsed; }}
            aria-expanded={!collapsed}
            title={collapsed ? "Expand Audio Buses" : "Collapse Audio Buses"}
        >
            <span class="chevron" class:collapsed>▾</span>
            <strong>Audio Buses</strong>
            <span class="count">{buses.length}</span>
        </button>
        {#if !collapsed}
            <button class="manage" onclick={onManage}>Manage</button>
        {/if}
    </div>

    {#if !collapsed}
        <div class="channels">
            {#each buses as bus (bus.id)}
                {@const routed = routeCount(bus.id)}
                <section class="bus-group">
                    <div class="bus-header" title={bus.name}>
                        <div class="identity">
                        <strong>{bus.name}</strong>
                        <span>Group · {routed} routed {routed === 1 ? "strip" : "strips"}</span>
                        </div>
                    </div>
                    <article class="channel" class:suppressed={bus.muted || (anySoloed && !bus.soloed)}>
                    <div class="fx-zone">
                        <button class="fx" class:has-effects={insertCount(bus) > 0} onclick={() => onOpenBusAudio(bus.id)}>
                            Audio FX{insertCount(bus) ? ` · ${insertCount(bus)}` : ""}
                        </button>
                        <div class="fx-summary" aria-label={`Audio effects on ${bus.name}`}>
                            {#if insertCount(bus)}
                                {#each busInserts(bus) as insert (insert.id)}
                                    <div class="fx-row" class:bypassed={insert.bypassed}>
                                        <div class="fx-name" title={`${insert.location}-fader: ${insert.name || "Unavailable plug-in"}`}>
                                            <span>{insert.location}</span>
                                            <strong>{insert.name || "Unavailable plug-in"}</strong>
                                        </div>
                                        <div class="fx-actions">
                                            <button
                                                class:active={insert.bypassed}
                                                onclick={() => dispatchCpp("setGroupBusInsertBypassed", bus.id, insert.id, !insert.bypassed)}
                                            >{insert.bypassed ? "Enable" : "Bypass"}</button>
                                            <button
                                                onclick={() => dispatchCpp("toggleGroupBusInsertEditor", bus.id, insert.id)}
                                                disabled={insert.status !== "loaded"}
                                            >{insert.editorOpen ? "Hide" : "Edit"}</button>
                                        </div>
                                    </div>
                                {/each}
                            {:else}
                                <div class="fx-empty">No audio effects</div>
                            {/if}
                        </div>
                    </div>
                    <div class="fader">
                        <span class="tick">+12</span>
                        <div class="fader-meter-row">
                            <div class="fader-track">
                                <input
                                    class="fader-slider"
                                    type="range"
                                    min="0"
                                    max="1000"
                                    step="1"
                                    value={Math.round(dbToPos(bus.gainDb ?? 0, BUS_MAX_DB) * 1000)}
                                    oninput={(event) => dispatchCpp(
                                        "setGroupBusGain",
                                        bus.id,
                                        Math.round(posToDb(Number(event.currentTarget.value) / 1000, BUS_MAX_DB) * 10) / 10,
                                    )}
                                    ondblclick={() => dispatchCpp("setGroupBusGain", bus.id, 0)}
                                    aria-label={`${bus.name} gain`}
                                />
                            </div>
                            <div class="meter-track">
                                <div class="meter-fill" class:hot={busMeters[bus.id]?.[0] > 0} style="height: {dbToPos(busMeters[bus.id]?.[0] ?? MIN_DB, BUS_MAX_DB) * 100}%"></div>
                                <div class="meter-hold" class:hot={busMeters[bus.id]?.[1] > 0} style="bottom: {dbToPos(busMeters[bus.id]?.[1] ?? MIN_DB, BUS_MAX_DB) * 100}%"></div>
                            </div>
                        </div>
                        <span class="tick">-∞</span>
                        <input
                            class="fader-value"
                            type="number"
                            min="-120"
                            max="12"
                            step="0.1"
                            value={Number(bus.gainDb ?? 0).toFixed(1)}
                            onchange={(event) => dispatchCpp(
                                "setGroupBusGain",
                                bus.id,
                                Math.max(MIN_DB, Math.min(BUS_MAX_DB, Number(event.currentTarget.value) || 0)),
                            )}
                            aria-label={`${bus.name} gain in decibels`}
                        />
                    </div>
                    <div class="mute-solo">
                        <button class="ms mute" class:active={bus.muted} onclick={() => dispatchCpp("setGroupBusMute", bus.id, !bus.muted)}>M</button>
                        <button class="ms solo" class:active={bus.soloed} onclick={() => dispatchCpp("setGroupBusSolo", bus.id, !bus.soloed)}>S</button>
                    </div>
                    <div class="output">Output · Master</div>
                    </article>
                </section>
            {/each}

            <section class="bus-group master-group">
                <div class="bus-header">
                    <div class="identity">
                        <strong>Master</strong>
                        <span>Final output</span>
                    </div>
                </div>
                <article class="channel master">
                <div class="fx-zone">
                    <button class="fx" class:has-effects={(master.inserts?.length || 0) > 0} onclick={onOpenMasterAudio}>
                        Audio FX{master.inserts?.length ? ` · ${master.inserts.length}` : ""}
                    </button>
                    <div class="fx-summary" aria-label="Master audio effects">
                        {#if master.inserts?.length}
                            {#each master.inserts as insert (insert.id)}
                                <div class="fx-row" class:bypassed={insert.bypassed}>
                                    <div class="fx-name" title={insert.name || "Unavailable plug-in"}>
                                        <span>FX</span>
                                        <strong>{insert.name || "Unavailable plug-in"}</strong>
                                    </div>
                                    <div class="fx-actions">
                                        <button
                                            class:active={insert.bypassed}
                                            onclick={() => dispatchCpp("setMasterInsertBypassed", insert.id, !insert.bypassed)}
                                        >{insert.bypassed ? "Enable" : "Bypass"}</button>
                                        <button
                                            onclick={() => dispatchCpp("toggleMasterInsertEditor", insert.id)}
                                            disabled={insert.status !== "loaded"}
                                        >{insert.editorOpen ? "Hide" : "Edit"}</button>
                                    </div>
                                </div>
                            {/each}
                        {:else}
                            <div class="fx-empty">No audio effects</div>
                        {/if}
                    </div>
                </div>
                <div class="fader">
                    <span class="tick">+6</span>
                    <div class="fader-meter-row">
                        <div class="fader-track">
                            <input
                                class="fader-slider"
                                type="range"
                                min="0"
                                max="1000"
                                step="1"
                                value={Math.round(dbToPos(master.gainDb ?? 0) * 1000)}
                                oninput={(event) => dispatchCpp(
                                    "setMasterGain",
                                    Math.round(posToDb(Number(event.currentTarget.value) / 1000) * 10) / 10,
                                )}
                                ondblclick={() => dispatchCpp("setMasterGain", 0)}
                                aria-label="Master gain"
                            />
                        </div>
                        <div class="meter-track">
                            <div class="meter-fill" class:hot={masterPeakDb > 0} style="height: {dbToPos(masterPeakDb) * 100}%"></div>
                        </div>
                    </div>
                    <span class="tick">-∞</span>
                    <input
                        class="fader-value"
                        type="number"
                        min="-120"
                        max="6"
                        step="0.1"
                        value={Number(master.gainDb ?? 0).toFixed(1)}
                        onchange={(event) => dispatchCpp(
                            "setMasterGain",
                            Math.max(MIN_DB, Math.min(STRIP_MAX_DB, Number(event.currentTarget.value) || 0)),
                        )}
                        aria-label="Master gain in decibels"
                    />
                </div>
                <div class="master-spacer"></div>
                <div class="output">Output · Dorico</div>
                </article>
            </section>
        </div>
    {/if}
</aside>

<style>
    .bank { display: flex; min-width: max-content; flex-direction: column; flex-shrink: 0; border-left: 3px solid #0e7490; background: #08111f; box-shadow: -10px 0 24px rgba(0,0,0,.28); }
    .bank.collapsed { min-width: 38px; width: 38px; }
    .header { position: relative; height: 25px; padding: 0 10px; box-sizing: border-box; display: flex; align-items: center; gap: 6px; flex-shrink: 0; border: 0; border-bottom: 3px solid #22d3ee; background: #164e63; color: #cffafe; font-size: .7rem; font-weight: 700; letter-spacing: .04em; }
    .header-toggle { min-width: 0; padding: 0 62px 0 0; display: flex; align-items: center; gap: 6px; flex: 1; border: 0; background: transparent; color: inherit; font: inherit; cursor: pointer; text-align: left; }
    .header-toggle:hover { background: rgba(8,47,73,.5); }
    .header-toggle strong { white-space: nowrap; }
    .chevron { font-size: .65rem; transform: rotate(0deg); transition: transform .15s; }
    .chevron.collapsed { transform: rotate(-90deg); }
    .count { min-width: 22px; padding: 2px 5px; border-radius: 999px; background: rgba(34,211,238,.16); color: #67e8f9; font-size: .65rem; text-align: center; }
    .manage { position: absolute; right: 5px; top: 50%; height: 19px; padding: 1px 7px; transform: translateY(-50%); border: 1px solid rgba(165,243,252,.55); border-radius: 4px; background: rgba(8,47,73,.72); color: #cffafe; font: inherit; font-size: .6rem; font-weight: 650; letter-spacing: 0; cursor: pointer; }
    .collapsed .header { height: 100%; padding: 8px 4px; flex-direction: column; justify-content: flex-start; }
    .collapsed .header-toggle { writing-mode: vertical-rl; flex: 0 0 auto; gap: 8px; }
    /* Match the layer-bank bottom inset so every fader track ends on the same row. */
    .channels { min-height: 0; padding-bottom: 21px; box-sizing: border-box; flex: 1; display: flex; }
    .bus-group { width: 146px; min-width: 146px; min-height: 0; display: flex; flex-direction: column; }
    .bus-header { display: grid; grid-template-columns: minmax(0, 1fr); grid-template-rows: 20px 28px; align-items: center; gap: 4px; padding: 4px; border-bottom: 2px solid #22d3ee; background: rgba(8, 47, 73, .48); flex-shrink: 0; }
    .channel { width: 100%; min-width: 0; min-height: 0; padding: 8px; box-sizing: border-box; display: flex; flex: 1; flex-direction: column; gap: 7px; border-right: 1px solid #27445b; background: #0b1727; transition: opacity .14s; }
    .channel.suppressed { opacity: .52; }
    .master-group { border-left: 2px solid #38bdf8; }
    .master { background: #111827; }
    .identity { min-width: 0; grid-row: 1 / -1; display: grid; grid-template-rows: 20px 28px; align-items: center; text-align: center; }
    .identity strong, .identity span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .identity strong { color: #e0f2fe; font-size: .76rem; }
    .identity span { color: #7dd3fc; font-size: .62rem; }
    /* Follow the mixer size's pre-fader span. One pixel compensates for
       the audio bank's local gap/tick geometry. */
    .fx-zone { height: calc(var(--strip-pre-fader-height, 166px) - 1px); min-height: calc(var(--strip-pre-fader-height, 166px) - 1px); display: flex; flex-shrink: 0; flex-direction: column; gap: 5px; }
    .fx { width: 100%; min-height: 34px; padding: 5px 8px; flex-shrink: 0; border: 1px solid #36506f; border-radius: 5px; background: #10243a; color: #cbd5e1; font: inherit; font-size: .72rem; font-weight: 650; cursor: pointer; }
    .fx:hover, .fx.has-effects { border-color: #22d3ee; color: #cffafe; }
    .fx-summary { min-height: 0; flex: 1; display: flex; flex-direction: column; gap: 5px; overflow-y: auto; }
    .fx-empty { height: 100%; display: grid; place-items: center; border: 1px dashed #29384f; border-radius: 5px; color: #64748b; font-size: .68rem; }
    .fx-row { padding: 6px; border: 1px solid #334155; border-radius: 5px; background: #0d1728; }
    .fx-row.bypassed { opacity: .62; }
    .fx-name { min-width: 0; display: flex; align-items: baseline; gap: 5px; }
    .fx-name span { flex-shrink: 0; color: #7dd3fc; font-size: .6rem; font-weight: 700; text-transform: uppercase; }
    .fx-name strong { min-width: 0; overflow: hidden; color: #e2e8f0; font-size: .72rem; font-weight: 600; text-overflow: ellipsis; white-space: nowrap; }
    .fx-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 5px; margin-top: 5px; }
    .fx-actions button { min-width: 0; min-height: 27px; padding: 3px 5px; border: 1px solid #3d4c62; border-radius: 4px; background: #172337; color: #cbd5e1; font-size: .68rem; font-weight: 600; cursor: pointer; }
    .fx-actions button:hover:not(:disabled) { border-color: #7dd3fc; color: #e0f2fe; }
    .fx-actions button.active { border-color: #f59e0b; color: #fbbf24; }
    .fx-actions button:disabled { opacity: .38; cursor: default; }
    .fader { min-height: 0; flex: 1; display: flex; flex-direction: column; align-items: center; gap: 2px; }
    .tick { color: #cbd5e1; font-size: .65rem; line-height: 1; }
    .fader-meter-row { display: flex; min-height: 40px; width: 100%; flex: 1; gap: 2px; align-items: stretch; }
    .fader-track { position: relative; flex: 1; display: flex; align-items: center; justify-content: center; }
    .fader-slider { width: 24px; height: 100%; margin: 0; writing-mode: vertical-lr; direction: rtl; cursor: pointer; accent-color: #3b82f6; -webkit-appearance: slider-vertical; appearance: slider-vertical; }
    .meter-track { position: relative; width: 6px; flex-shrink: 0; overflow: hidden; border-radius: 2px; background: #0f172a; }
    .meter-fill { position: absolute; inset: auto 0 0; border-radius: 2px; background: #22c55e; }
    .meter-fill.hot, .meter-hold.hot { background: #ef4444; }
    .meter-hold { position: absolute; left: 0; right: 0; height: 2px; background: #4ade80; }
    .fader-value { width: 100%; min-height: 28px; padding: 3px 5px; box-sizing: border-box; flex-shrink: 0; border: 1px solid #334155; border-radius: 3px; background: #0f172a; color: #cbd5e1; font-size: .75rem; text-align: center; -moz-appearance: textfield; }
    .mute-solo { display: flex; justify-content: center; gap: 8px; padding: 4px 0; }
    .ms { width: 32px; height: 28px; padding: 0; border: 1px solid #555; border-radius: 3px; background: #2a2a2a; color: #aaa; font-size: .8rem; font-weight: 700; cursor: pointer; }
    .mute.active { border-color: #f59e0b; background: rgba(245,158,11,.25); color: #fbbf24; }
    .solo.active { border-color: #22c55e; background: rgba(34,197,94,.2); color: #4ade80; }
    .output { min-height: 26px; display: grid; place-items: center; border-top: 1px solid #20374b; color: #7dd3fc; font-size: .64rem; }
    .master-spacer { height: 36px; flex-shrink: 0; }
</style>
