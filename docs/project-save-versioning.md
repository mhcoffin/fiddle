# Saving from an older version

A Dorico document records an exact Fiddle branch/version identity. Reopening
the document restores that version, even if another document has advanced the
branch in the meantime. Existing fallback behavior for deleted identities is
unchanged; do not delete versions needed by saved documents.

## Save policy

| Loaded version | Captured Fiddle state | Result |
| --- | --- | --- |
| Branch head | Unchanged | Retain the existing version |
| Branch head | Changed | Append to that branch |
| Historical version | Unchanged | Retain the historical version |
| Historical version | Changed | Create a new branch rooted at that version |

Fiddle's Save button and Dorico's dirty-state save request both call
`MainComponent::saveConfig`, which captures the mixer through StateManager and
uses `VersionStore::saveProjectState`. Dorico saves with no Fiddle dirty hint
retain their current identity without recapturing vendor plug-in state.
When capture is needed, comparison with the loaded version's state hash avoids
unnecessary branches after an edit has been undone. This comparison includes
serialized plug-in state; it is not a semantic comparison of vendor presets.

An automatic branch gets a unique name such as `Main (2)` or `Main (3)` and may
be renamed later. Its first version contains the edited state and has the
loaded historical version as its direct parent. There is no automatic merge,
no duplicate base snapshot node, and no change to the old branch's head.
Fiddle selects the new branch and returns its exact identity to Dorico.
Subsequent saves append normally; an unchanged repeated save creates nothing.

An unchanged historical save remains historical. Editing is allowed there;
the Save tooltip and History banner explain that saving changes will branch.
The historical indicator clears after a changed save creates its new branch.

## Explicit branches, undo, and errors

- The main window's **New Branch** saves the current working state on the
  named branch without first committing to the previous branch. This includes
  unsaved edits, even when starting from a historical version.
- **New Branch** on a History node starts from that stored snapshot, not the
  working mix. This retains the distinction between the two commands.
- Creating a branch is a version-history operation, not a mixer undo action.
  Mixer undo remains available after saving; saving the resulting changed
  state creates another version rather than deleting the previous save.
- Missing/mismatched base identities and duplicate explicit branch names are
  rejected rather than silently saving on an unrelated head.
- Before acknowledging a new save, the store checks referenced strip blobs
  and reads back the state, version, and branch-head publication. A failure
  does not select a new identity or clear the dirty flag. These checks do not
  make the existing multi-statement storage interface a single transaction;
  interrupted writes can leave unreferenced data, but old versions are not
  overwritten.

## Regression coverage

The pure VersionStore tests cover the four policy cases, alternating A/B
documents, continued saves, collision-free names, explicit branches, and
simulated failed writes. MixerIntegrationTest covers the production snapshot
capture path, edit/undo equality despite a dirty hint, independent branch
identities after closing/reopening SQLite, routing/effect preservation, and
the published blob's ancestry. UI model tests cover save availability and
historical wording. These are automated checks, not a claim that Dorico or
the rendered desktop UI has been driven end-to-end.
