import test from 'node:test';
import assert from 'node:assert/strict';
import { LibraryHistory } from '../src/lib/libraryHistory.js';

const row = { id: 'a', previewId: 'player-a', name: 'Violin', vstPlugin: 42 };
test('draft edits have labels, bounded history, no-op and branch semantics', () => {
    const h = new LibraryHistory(2);
    h.reset([row]);
    assert.equal(h.dirty, false);
    assert.equal(h.record([row], 'No-op'), false);
    h.record([{ ...row, name: 'One' }], 'Rename');
    h.record([{ ...row, name: 'Two' }], 'Rename again');
    h.record([{ ...row, name: 'Three' }], 'Third');
    assert.equal(h.undoLabel, 'Third');
    assert.equal(h.travel('undo')[0].name, 'Two');
    assert.equal(h.travel('undo')[0].name, 'One');
    assert.equal(h.travel('undo'), null);
    assert.equal(h.redoLabel, 'Rename again');
    h.record([{ ...row, name: 'Branch' }], 'Branch');
    assert.equal(h.travel('redo'), null);
});
test('save is a checkpoint, not a draft history reset', () => {
    const h = new LibraryHistory();
    h.reset([], false);
    assert.equal(h.dirty, true, 'even an empty new library can be saved');
    h.record([row], 'Add patch');
    h.markSaved();
    assert.equal(h.dirty, false);
    assert.deepEqual(h.travel('undo'), []);
    assert.equal(h.dirty, true);
    assert.deepEqual(h.travel('redo'), [row]);
    assert.equal(h.dirty, false);
});
test('project linkage and player notifications are not structural history', () => {
    const h = new LibraryHistory();
    h.reset([row]);
    assert.equal(h.record([{ ...row, usageCount: 8, outOfDateLayerCount: 2,
        hasPluginState: true, pluginStatePending: true }], 'Status'), false);
    h.playerChanged();
    assert.equal(h.dirty, true);
    assert.equal(h.undoLabel, '');
    h.markSaved();
    assert.equal(h.dirty, false);
});
test('player assignment, deletion and batches retain exact preview identities', () => {
    const h = new LibraryHistory();
    h.reset([row]);
    h.record([{ ...row, previewId: 'player-b', vstPlugin: 77 }], 'Assign player');
    h.record([], 'Delete patch');
    assert.equal(h.travel('undo')[0].previewId, 'player-b');
    assert.equal(h.travel('undo')[0].previewId, 'player-a');
    assert.equal(h.dirty, false);
    h.record([row, { ...row, id: 'b', previewId: 'player-c' }], 'Ensemble');
    assert.equal(h.travel('undo').length, 1);
    assert.equal(h.travel('redo').length, 2);
});
test('preview retention follows undo/redo reachability and bounded eviction', () => {
    const h = new LibraryHistory(1);
    h.reset([row]);
    h.record([{ ...row, previewId: 'b' }], 'Replace');
    assert.deepEqual(h.retainedPreviewIds.sort(), ['b', 'player-a']);
    h.record([{ ...row, previewId: 'c' }], 'Replace again');
    assert.deepEqual(h.retainedPreviewIds.sort(), ['b', 'c']);
    h.travel('undo');
    h.record([{ ...row, previewId: 'd' }], 'New branch');
    assert.deepEqual(h.retainedPreviewIds.sort(), ['b', 'd']);
});
