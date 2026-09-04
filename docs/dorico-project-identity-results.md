# Dorico project identity restoration

Dorico project state now records Fiddle's stable branch UUID and exact version
UUID alongside the legacy branch name. When the project is reopened,
FiddleServer resolves that identity in this order:

1. the exact saved version, selecting its owning branch;
2. the current head of the saved branch UUID if the version was removed;
3. the current head of the legacy branch name for older Dorico projects.

If none of those identities exists, Fiddle keeps the current branch and reports
the problem instead of silently loading an unrelated version.

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
