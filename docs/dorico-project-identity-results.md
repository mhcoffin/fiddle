# Dorico project identity restoration

Dorico project state now records Fiddle's stable branch UUID and exact version
UUID alongside the legacy branch name. When the project is reopened,
FiddleServer resolves that identity in this order:

1. the exact saved version, selecting its owning branch;
2. the current head of the saved branch UUID if the version was removed;
3. the current head of the legacy branch name, only for older Dorico projects
   that recorded no IDs (a project with IDs never falls back to a branch that
   merely shares its name, such as a fresh database's unrelated "Main").

If none of those identities exists, Fiddle keeps the current branch and the
project keeps its own identity: config status and Dorico saves echo the
project's IDs back to the plug-in, and Fiddle's state isn't committed under
them, until the user chooses a setup: **Use current setup** in the restore
banner, checking out a branch or version, or creating a branch. Merges and
other History maintenance don't count as choosing. Unsaved Fiddle edits are saved as a version before Fiddle
switches to the project's setup; if they can't be saved, Fiddle doesn't switch
and offers **Load project setup** once the user has saved or undone them.

## Restore report

Each connection that loads or fails to load a project's setup produces a report
(`ProjectRestoreReport.h`) shown as a banner in the main window and returned by
`fiddle_get_status` as `projectRestore`: which setup loaded and by which route
(exact version, latest version of the branch, by branch name), or why not. The
banner also lists layers that can't play as saved: plug-ins not installed or
that failed to load, and Fiddle scripts not found. A plain exact restore shows
the banner only when there is such a problem, or when unsaved edits were saved
first. Reconnecting to the version already loaded produces no report unless it
was reached by a fallback.

Branch checkout, branch creation, merges into the current branch, and explicit
Fiddle saves all publish the updated branch/version identity to the native VST3
plug-in. The plug-in appends the IDs to its previous length-prefixed state
format, so existing projects remain readable.

The native plug-in announces its saved identity when it connects to the server.
Repeated announcements for the already-loaded version are treated as no-ops;
this prevents a transient TCP reconnect from discarding unsaved mixer edits.

Regression coverage includes identity codec compatibility, protobuf round
tripping, exact-version selection, branch-head fallback, and legacy-name
fallback. The manual acceptance sequence is:

1. select B2 in Fiddle;
2. save and close the Dorico project;
3. select B1 in Fiddle;
4. reopen the Dorico project and verify that Fiddle selects B2.
