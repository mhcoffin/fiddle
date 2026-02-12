<script>
    let { midiEvents, sessionOffset = 0 } = $props();

    const SAMPLE_RATE = 44100;
    const BPM = 120;

    const formatTime = (samples) => {
        const adjustedSamples = samples - sessionOffset;
        const seconds = adjustedSamples / SAMPLE_RATE;
        const totalBeats = (seconds * BPM) / 60;
        const bar = Math.floor(totalBeats / 4) + 1;
        const beat = Math.floor(totalBeats % 4) + 1;
        const sub = (totalBeats % 1).toFixed(3).substring(2);

        return {
            clock: `${bar}.${beat}.${sub}`,
            seconds: seconds.toFixed(3) + "s",
        };
    };

    const getEventName = (type) => {
        switch (type) {
            case 3:
                return "Note On";
            case 4:
                return "Note Off";
            case 5:
                return "CC";
            case 6:
                return "Pitch Bend";
            case 7:
                return "Program Change";
            case 11:
                return "Context Update";
            case 12:
                return "Transport";
            default:
                return "Other (" + type + ")";
        }
    };

    const getEventClass = (type) => {
        switch (type) {
            case 3:
                return "event-note-on";
            case 4:
                return "event-note-off";
            case 5:
                return "event-cc";
            case 6:
                return "event-pb"; // Reuse or new class
            case 7:
                return "event-pc";
            case 11:
                return "event-other";
            case 12:
                return "event-transport";
            default:
                return "";
        }
    };
</script>

<div class="event-log-container">
    <table class="event-log-table">
        <thead>
            <tr>
                <th>Time (Beats)</th>
                <th>Time (Sec)</th>
                <th>Type</th>
                <th>Ch</th>
                <th>Details</th>
            </tr>
        </thead>
        <tbody>
            {#each midiEvents as event (event.id)}
                {@const time = formatTime(event.timestamp)}
                <tr class={getEventClass(event.type)}>
                    <td class="cell-time">{time.clock}</td>
                    <td class="cell-sec">{time.seconds}</td>
                    <td class="cell-type">{getEventName(event.type)}</td>
                    <td class="cell-chan">{event.channel}</td>
                    <td class="cell-details">
                        {#if event.type === 3 || event.type === 4}
                            Note: <strong>{event.note}</strong> | Velocity: {event.velocity}
                        {:else if event.type === 5}
                            CC <strong>{event.cc}</strong>:
                            <span class="cc-old">{event.oldValue ?? "?"}</span>
                            &rarr;
                            <span class="cc-new">{event.value}</span>
                        {:else if event.type === 6}
                            Value: <strong>{event.value}</strong>
                        {:else if event.type === 7}
                            Program: <strong>{event.program}</strong>
                        {:else if event.type === 11}
                            <span class="context-desc">{event.description}</span
                            >
                        {:else if event.type === 12}
                            <span class="transport-badge">
                                {event.transportType === 0 ? "START" : "STOP"}
                            </span>
                            <em>(Session Anchor Reset)</em>
                        {:else}
                            Raw: {JSON.stringify(event)}
                        {/if}
                    </td>
                </tr>
            {/each}
        </tbody>
    </table>
</div>

<style>
    .event-log-container {
        flex: 1;
        overflow-y: auto;
        background: #020617;
        font-family: "JetBrains Mono", "Fira Code", monospace;
        font-size: 0.85rem;
    }

    .event-log-table {
        width: 100%;
        border-collapse: collapse;
        text-align: left;
    }

    .event-log-table th {
        position: sticky;
        top: 0;
        background: #0f172a;
        padding: 10px;
        color: #38bdf8;
        border-bottom: 1px solid #1e293b;
        z-index: 10;
    }

    .event-log-table td {
        padding: 6px 10px;
        border-bottom: 1px solid #1e293b;
        color: #cbd5e1;
    }

    .event-log-table tr:hover {
        background: rgba(255, 255, 255, 0.05);
    }

    .event-note-on td {
        color: #4ade80;
    }
    .event-note-off td {
        color: #94a3b8;
    }
    .event-cc td {
        color: #fbbf24;
    }
    .event-pc td {
        color: #c084fc; /* Purple */
    }
    .event-other td {
        color: #2dd4bf; /* Teal */
    }
    .event-transport {
        background: rgba(239, 68, 68, 0.15);
        border-top: 1px solid #ef4444;
        border-bottom: 1px solid #ef4444;
    }
    .event-transport td {
        color: #fca5a5;
        font-weight: bold;
    }
    .transport-badge {
        background: #ef4444;
        color: white;
        padding: 2px 6px;
        border-radius: 4px;
        font-size: 0.75rem;
        font-weight: bold;
        margin-right: 8px;
    }

    .cell-time {
        color: #38bdf8 !important;
        width: 100px;
    }
    .cell-sec {
        color: #64748b !important;
        font-size: 0.75rem;
        width: 80px;
    }
    .cell-type {
        width: 100px;
        font-weight: 600;
    }
    .cell-chan {
        width: 40px;
        text-align: center;
    }

    .cc-old {
        color: #ef4444;
        text-decoration: line-through;
        opacity: 0.7;
    }
    .cc-new {
        color: #4ade80;
        font-weight: bold;
        font-size: 1rem;
    }

    .cell-details strong {
        color: #f1f5f9;
    }
</style>
