// Keep in sync with DebugUiFeed::accepts. The native side routes these only to
// the visible Debug window; this guard also protects other window modes from
// legacy senders and direct/test injections.
const debugMessages = new Set([
    "pushMidiEvent", "updateNoteState", "addLogMessage", "metronomeBeat", "setTempo", "setDebugBatch",
]);
export const isDebugMessage = (type) => debugMessages.has(type);
export const debugBatchLimit = 128;
export const debugHistoryLimit = 4096;

export function dispatchDebugBatch(batch, { reset, dispatch, dropped }) {
    if (!batch || !Array.isArray(batch.messages)) return;
    const lost = (Number.isSafeInteger(batch.dropped) && batch.dropped > 0 ? batch.dropped : 0)
        + Math.max(0, batch.messages.length - debugBatchLimit);
    if (batch.reset === true || lost > 0) reset();
    if (lost > 0) dropped(lost);
    for (const message of batch.messages.slice(-debugBatchLimit)) {
        // A diagnostic batch cannot mutate controls or recursively dispatch.
        if (message && isDebugMessage(message.type) && message.type !== "setDebugBatch") dispatch(message);
    }
}
