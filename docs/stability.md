# Stability

This document is a large revision to Fiddle versioning.
It attempts to make integration between Dorico and Fiddle stable and predictable, while avoiding problems caused by my previous design.

There is no requirement to save any of the existing database. Current data is only tests and can be wiped.

## Background

Fiddle creates a playback template (PT) for Dorico. The PT is stored under Dorico's Application-support directory. 
The PT contains a mapping 

```
 (midi port, midi channel) -> (instrument ID, section flag)
```

(The "section flag" is either "solo" or "section".)

 - [ ] Action Item: make sure we write out the mappings in lexicographic order by (port, channel). **This is critical and must be done before anything else.**

We can assume that Dorico will not change instrument IDs from version to version, although they may add new ones.

Dorico allocates slots from the PT based on the order in which (instrument ID, section flag) appear in the PT.
E.g, if the user adds a violin section to the score, Dorico scans the PT and finds the first slot with (violin, "section"). 
It assigns that instrument the associated (port, channel). 
If the user adds another violin section, Dorico scans the PT. It skips the first one it finds because it is already allocated. Further down the list it finds a different (port, channel) pair also associated with (violin, "section"). It assigns that instrument the associated (port, channel). 

Dorico will not change the (port, channel) of an instrument unless the user explicitly changes it. If the user change it, that's outside the scope of this document. We can't do anything about it.

Fiddle keeps the PT mapping stable between changes to the PT:

 - Fiddle uses a consistent ordering --- lexicographic on (port, channel) --- of instruments in the PT. E.g., the first and second violin sections in the score will always be assigned the same (port, channel) pairs. E.g., if the user adds one piccolo to the PT, it will be assigned a (port, channel) which will thereafter not change. 
 - Fiddle does not re-use (port, channel) pairs for different (instrument, section flag) pairs. So even if the user removes the second violin section from the PT and adds it back, it will be assigned the same (port, channel) pair.

So we can assume that 
- existing instrument IDs are stable, although new ones may be added
- existing (port, channel) pairs are stable, although new ones may be added when PT is changed
- if an instrument is removed and then added back, it will be assigned the same (port, channel) pair.

## Libraries

A *library* represents one VST stack routed to a given `(port, channel)`. The most common use case is layering — e.g., routing the same violin notes to both "BBCSO" and "SSP" simultaneously and mixing the results.

Each library has:
- a **library UUID** — permanent, never changes
- a **library name** — mutable, user-facing (e.g., "BBCSO", "SSP Pro")

The UUID→name mapping is stored in a dedicated DB table, the same pattern as branches. All internal data (channel strip blobs, versions) reference the UUID. The name is only resolved for display.

Renaming a library is therefore free: no history is broken, no strip data changes.

A **default library** with name `""` (empty string) is automatically created for every `(port, channel)` pair. Users who do not layer simply use this default and never think about library names.

## Fiddle Server

The **strip identity** is the triple `(port, channel, libraryUUID)`. This triple is stable across:
- PT edits (graveyard mechanism keeps port/channel stable)
- library renames (UUID is stable)
- branching and merging

A Fiddle *channel strip blob* contains:
- the strip identity `(port, channel, libraryUUID)`
- all settings: VST ID, VST state blob, gain, mute, solo, expression map, etc.

Channel strip blobs are content-addressable in the DB with a hash of the content as primary key.

A *Fiddle version* contains

- an unique version ID
- a creation timestamp
- the version ID of the parent version (the previous version on the same branch)
- the version ID of the merge-parent (empty if this version is not the result of a merge)
- some global Fiddle data such as output level,
- a set of channel-strip hashes

The primary key for versions is the version ID. 
Versions are not immutable and are not content addressable.
If version V is deleted from the database, the children of V have their parent changed to parent(V). 

- [ ] Action item: the primary key for versions is currently a hash of the content. This doesn't make sense because versions are not immutable. 

### Save in FiddleServer

When the user hits "Save" in FiddleServer, Fiddle performs a db transaction that amounts to the following:

1. All dirty channel strips are added to the DB, indexed by hash.
2. Fiddle creates a new version and adds it to the database:
 - version ID = new unique version ID
 - timestamp = current time
 - parent = current version ID
 - merge-parent = empty
 - global Fiddle data = capture of current global data
 - channel-strip hashes = the current set

### Load in Fiddle Server

When a version V is loaded, that version becomes the current version. 
Global data is set from V.
Channel-strip settings are copied from V into the current set of channels, matching by `(port, channel, libraryUUID)`.
Channel-strips that are missing in V are cleared.
Channel-strips that are in V but not in the current set are skipped.
- [ ] Action item: warn the user if any channel strips are skipped. We should also emphasize that removing instruments from the PT is almost always a bad idea. 

Result is that the set of (port, channel) pairs in the current set of channel-strips does not change on load, but the contents changes to match V where possible. 

- [ ] Action item: this is changed from current behavior.

### Merge in FiddleServer

When the user executes a "Merge" operation in FiddleServer, a 3-way merge is performed.
The merge takes place *from* a version V *to* a branch head H, with nearest common ancestor C:

- the result is a version with H as the parent and V as the merge-parent
- the result inherits the branch of H.
- channel strips are matched between versions by `(port, channel, libraryUUID)`
- a strip present in V but not C is an **addition** → included in result
- a strip present in C but not V is a **deletion** → removed from result (unless already absent from H)
- a strip modified in both V and H relative to C is a **conflict** → by default V wins; a UI toggle allows switching to H wins

- [ ] Action Item: merge requires reimplementation with `(port, channel, libraryUUID)` as the match key.

### Normal save from Dorico

When Dorico saves a project, it asks the Fiddle plugin for current state. 
The plugin sends FiddleServer a correlated save request and waits for a bounded
local response. If the current Fiddle state is dirty, FiddleServer commits it
as a new version, advances the current branch, clears the dirty state, and
returns the exact committed identity. If the state is already clean, the
existing identity is returned without creating a redundant version.

The plugin writes the identity from that response into Dorico's project state.
If FiddleServer has reported dirty state but cannot confirm the save, the
plugin reports failure to Dorico rather than silently writing the previous
version identity.

In a normal save, FiddleServer sends back the following data:

- the current version ID
- the branch ID
- the branch name, retained as a compatibility fallback

A normal save relies on the FiddleServer database. If the user deletes a version from the database that is stored in a Dorico project, Load may give unexpected results. However, it is expected that branches will maintain a degree of continuity, and that it is likely that the user will think of changes to a branch as improvements and bug fixes, and will want to move projects to the head of the branch anyway.

**Persistence invariant:** without manual database alteration, reopening a
Dorico project restores the exact Fiddle version acknowledged during that
project's save. Version deletion is therefore a dangerous operation: the
History UI warns that deleting a version can prevent saved Dorico projects
from returning to their exact prior state.

Versioned strip blobs include the strip activation, mute, and solo states and
the ordered Lua plug-in chain. Databases created before these fields were
added are migrated with the legacy defaults (active, unmuted, unsoloed, and no
Lua plug-ins). Values omitted from an already-existing historical blob cannot
be reconstructed, but every newly committed version preserves them.

This identity is appended to the native plug-in's existing VST3 state. Projects
saved before stable IDs were added retain their branch-name-only behavior.

### Normal Load from Dorico

When Dorico loads a project, it sends the saved state to the Fiddle plugin.
The plugin relays that data to FiddleServer. Then

1. If the version ID is found in the database, FiddleServer loads that version into the channel strips and global data.
2. If the version ID is not not found, then FiddleServer looks for the branch ID. If it finds the branch ID in the database, it loads the *head* of that branch. Fiddle server also notifies the user of what has happened.
3. If the branch ID is not found, FiddleServer loads the most recent version from the database. Fiddle also notifies the user of what has happened. 

- [ ] Action item: this is new logic, and replaces current version loaded in FiddleServer.

After this process is complete, the user can proceed, or can switch to another branch or version if they choose.

### Super Save

This is a new feature that makes a Dorico project fully self-contained and portable.

- User clicks "Super Save" in FiddleServer. Fiddle indicates it is ready.
- User clicks "Save" in Dorico.
- FiddleServer sends back a large blob and clears the Super Save indicator. Subsequent saves revert to Normal Save.

The Super Save blob contains:
- all channel strip blobs for the current version
- global data
- the `(port, channel)` → `(instrument ID, section flag)` PT mapping
- the libraryUUID → name table (so library names survive a DB wipe)
- the branchUUID → name table
- the version ancestry chain (for reconciliation)

On **Super Save load**: if a libraryUUID or branchUUID from the blob is unknown on the target machine, it is imported into the local DB. If the name is already taken, a suffix is appended (e.g., "BBCSO 2"), same as branch import.

A Normal Save is not a reliable archive. Users should Super Save before archiving or sharing a project.

- [ ] Action item: implement Super Save, but prioritize it last.
