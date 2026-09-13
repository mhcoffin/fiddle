// Draft history never dispatches project Undo. Player instances are retained by
// previewId, so restoring a row also restores its particular player setup.
export class LibraryHistory {
    constructor(limit = 100) { this.limit = limit; this.reset([], true); }
    snapshot(rows) {
        return rows.map(({ usageCount, outOfDateLayerCount, pluginStatePending, hasPluginState, ...row }) => ({ ...row }));
    }
    reset(rows, saved = true) {
        this.current = this.snapshot(rows);
        this.past = []; this.future = [];
        this.saved = saved ? JSON.stringify(this.current) : null;
        this.playerRevision = 0; this.savedPlayerRevision = 0;
    }
    record(rows, label) {
        const next = this.snapshot(rows);
        if (JSON.stringify(next) === JSON.stringify(this.current)) return false;
        this.past.push({ rows: this.current, label });
        if (this.past.length > this.limit) this.past.shift();
        this.current = next; this.future = [];
        return true;
    }
    travel(direction) {
        const from = direction === 'undo' ? this.past : this.future;
        const to = direction === 'undo' ? this.future : this.past;
        if (!from.length) return null;
        const entry = from.pop();
        to.push({ rows: this.current, label: entry.label });
        this.current = entry.rows;
        return this.current.map(row => ({ ...row }));
    }
    playerChanged() { ++this.playerRevision; }
    markSaved() {
        this.saved = JSON.stringify(this.current);
        this.savedPlayerRevision = this.playerRevision;
    }
    get dirty() { return this.saved !== JSON.stringify(this.current) || this.playerRevision !== this.savedPlayerRevision; }
    get undoLabel() { return this.past.at(-1)?.label || ''; }
    get redoLabel() { return this.future.at(-1)?.label || ''; }
    get retainedPreviewIds() {
        return [...new Set([this.current, ...this.past.map(e => e.rows), ...this.future.map(e => e.rows)]
            .flatMap(rows => rows.map(row => row.previewId || row.id)))];
    }
}
