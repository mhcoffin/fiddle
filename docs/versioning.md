# This Document describes the versioning scheme for Fiddle.

## Requirements

- We need to be able to save a Dorico project and bring it back, even much later, and have it sound the same.  This must work even if Fiddle has been wiped out and reinstalled from scratch. This means that Fiddle settings that affect playback must be stored in the Dorico project file itself.

- The user will refine and improve the Fiddle setup over time. If a user loads an old project, they may or may not want to take advantage of changes that have been made to Fiddle since the project was saved. 

- The user will want to have different Fiddle setups for different *kinds* of projects. For example, a user might have a setup for classical orchestral mockups, another for baroque mockups, etc.  

- We will compromise between simplicity and flexibility *for the user*, but we will not be afraid to make breaking changes to the Fiddle config format or database schema if necessary.

- Currently, there is no requirement to maintain backward compatibility with old projects. We can make breaking changes to the config format and database schema if necessary.

- I understand that this is a more complex design than a simple linear history. However, I enjoy thinking about things like this, and I think it will be fun to implement. I am still open to suggestions for simplification.

## Design

### Background Concepts

Some data is not associated with any strip. We call this "global state". It is a structure that contains things like the master volume level.

Most data that affects playback is associated with a strip. Each strip has a distinct UUID. 

A "strip blob" is blob containing all settings for a strip: input instrument ID, port, channel, volume, mute, solo, VST, expression map, etc. It also contains a UUID to ensure that two strips never have the same hash. The strip hash is a hash of the strip blob. There is a SQLite table mapping from the strip hash to the strip blob. 

A "fiddle state" is the global state plus a list of strip hashes, in order from left to right. There is a SQLite table that maps the hash of a fiddle state to the fiddle state. 

We currently do not support reordering strips. If we do in the future, we will include strip order as part of the global state and treat the list of strips as a bag of UUIDs. 

A fiddle "version" is a tuple (fiddle state hash, branch ID, parent version ID, merge-parent version ID). There is a table that maps the hash of a fiddle version to the fiddle version.

Every Fiddle version is associated with a *branch*, which is a UUID. There is a table that maps the branch UUID to the branch name.

The set of all Fiddle versions forms a DAG, where the edges go from parent to child and merge-parent to child.

### Initial state

If the fiddle server starts and does not find a database, it creats one in the user's application support directory (which may depends on OS.)

In the initial database, it creates a new branch with name "Main", and a new Fiddle version. The Fiddle version has no parent and no merge-parent. The global state is initialized to sensible defaults (output level 0db, etc.), and there are no strips. This initial state is the root of the version tree: all other versions are descendants of this version.

 
### Modifications

We can create a new Fiddle version based on an existing Fiddle version "A" by applying a series of modifications to it:
  - Adding a new strip
  - Removing a strip
  - Modifying the settings of a strip.

The resulting Fiddle version has a new hash, the parent is A and the merge-parent is empty. 

A new fiddle version is created and added to the database
- when the user explicitly saves the current state, 
- when Dorico requests the current state, and
- when Dorico restores a state that is not currently in the database. 

Changes to the global state or to strip blobs are not recorded as new versions until one of the three events described above occurs.

### Branching

Another way to create a new Fiddle version is to "branch" from an existing Fiddle version "A". This creates a copy of A, but with a new branch ID. The parent of the new Fiddle version is A. The merge-parent is empty. Note that A is still on the same branch as it was before. 

A branch is just a UUID, but associated with a branch is a mutable name. (In the database, the branch UUID to name mapping is stored in a separate table.) The user can rename any branch as long as the new name is not already in use. Fiddle versions do not contain branch names. Branch names are only for user convenience.

It should be possible to delete a branch entirely, including all the information that is not accessible from any other branch. There should be a strong warning against branch deletion if the branch has not been merged into any other branch. 

### Merging 

A new version can also be created by "merging" a version "MP" onto a branch.  This is only possible if the version "MP" is not an ancestor of the branch head in the DAG.

If MP is a descendant of T, then the merge operation is a "fast-forward" merge. In this case, no new version is created. The branch head is simply updated to be MP.

Merging is a three-way operation. Let MP be the merge-parent, let T be the head of the branch we are merging into, and let A be the nearest common ancestor of MP and T in the DAG. Conceptually, we want to create a new version N that is based on T, but has the changes from MP relative to A applied to it. 

More precisely, the new version N is created as follows:
- N begins as a copy of T, with T as the parent and MP as the merge-parent. 
- If a strip is in MP but not in A, it is added to N.
- If a strip is in A but not in MP, it is removed from N (unless it isn't there to begin with). Not: this might be surprising to the user, but it's very simple and avoids the need for any human intervention. It might be worth highlighting this in the UI if it happens. 
- If the data associated with a strip has been modified in MP relative to A, the modified version from MP is used in N.

Notice that the fiddle state of the new node is constucted from existing strip blob hashes. No new strip blobs are created during a merge. If there is a merge conflict (i.e., if the user has modified the same strip in both T and MP relative to A), the merge parent wins. No human intervention is required or allowed. 

### Squashing

Squashing is a way to simplify the version history by removing intermediate versions that the user no longer cares about. Currently, squashing is only allowed if the version to be squashed has exactly one parent, no merge-parent, and one child.  The effect is simply to remove the squashed version from the DAG, and make its parent the parent of the squashed version's child. Note: there may be other cases where squashing *could* be allowed. However, this rule is very easy to explain, and the outcome is easy to understand. 

Squashing is not reversible. Once squashed, the intermediate version is gone forever.


### Dorico Integration

#### Saving

When a user saves a Dorico project, Fiddle sends back to Dorico a structure that contains:
- the fiddle state hash of the current state.
- a mapping of strip hashes to strip blobs for all strips in the current state.
- the version hashes of all parent versions, in order from the root to the current version. (If the list is too long, we can remove a prefix to shorten it. We can explore that in the future.)
- the name of the branch that the current version is on.

Speculation: in the future, we may want to support a "dangerous" mode where Fiddle sends back to Dorico only the current fiddle state hash. This would radically reduce the size of the saved project file, but would make it impossible to restore the project if the Fiddle database was lost, or if the user loaded the project on a different computer, or if the user removed the version hash by squashing the version history.

#### Loading

When a user loads a Dorico project, the Fiddle plugin sends the saved structure outlined above back to FiddleServer. Fiddle server uses that information to update the database if necessary and then load the new state. The fiddle state is added to the database if it isn't there already, and 
any strip blobs that are not in the database are added to the database. 

If the Fiddle state hash in the blob is *not* in the database, a new fiddle version is added to the database. The parent of the new version will be the closest ancestor of the Fiddle version that still exists. (We can find the closest ancestor by iterating through the list of ancestor hashes in the blob.) 

The branch is added to the database if it isn't there already, and associated with the branch name from the blob. If that branch name is already taken, the new branch should be created with a new branch name (e.g. "Branch 1", "Branch 2", etc.).

In case *no* such ancestor exists, the new version should have the root of the version tree as parent. This last case should happen only if 
- the user is loading a project that was not saved from the current Fiddle installation.
- the ancestor list had a prefix removed when we saved the project, as described in the Saving section above.

After the database has been updated, the Mixer should be updated to reflect the new state.


### Versions Mode

The main window will have a new button to switch to "Versions" mode (in addition to "Edit" and "Mixer" modes). 

In Versions mode, the user can view the version tree. The version tree should be laid out with the root at the far left. Each version should be a node in the tree. 

Hovering on a node should show the creation time. The format should be identical to how it is shown in the plugin.

The user should be able to access a context menu on any node to create a new branch from that node, or to switch to that node, or to squash that node. Note: switching to a node that is not the head of its branch should is problematic because if modifications are made there isn't any branch on which to put the new version. We could do any of the following:
- disallow switching to a node that is not the head of its branch
- allow switching but disable modifications
- allow switching to any version and automatically create a new branch (and move to that branch) if modifications are mode.

The last option is probably the most user-friendly, but adds a little complexity to the implementation. Saving would involve checking to see if the current version is the head of its branch. If not, a new branch would be created with the current version as the head, and the new branch would be set as the current branch. We might want to alert the user to the fact that a new branch was created.

We will need ways to reduce visual clutter. For example, if a branch has a long list of versions, we can elide the list by showing only the first and last versions with an ellipsis between them. We can also support hiding branches entirely, or hiding fully merged branches. In the limit, we can hide all branches except the current one and elide most of the versions on it. We will need to explore this after we have a working prototype.


### Menu Bar

There should be a menu-bar selector to switch to any branch. This should be a menu-bar item with a list of all branch names. The currently selected branch should be highlighted. Selecting a branch should switch to the head of that branch.

"New Branch" should be the last entry in branch selector refered to in the previous item. It should bring up a dialog to name the new branch. 

