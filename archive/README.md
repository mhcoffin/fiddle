# Retired build directories

On 2026-09-19, the following unused top-level directories were moved into
`archive/builds/` without deleting their contents:

`build-cov`, `build_ag`, `build_check`, `build_final`, `build_juce9`,
`build_proxy`, `build_retry`, `build_tmp`, `test_cmake`,
`FiddleServer_artefacts`, `MidiLogger_artefacts`, and `_deps`.

The current Release build and running app did not reference these directories.
In particular, the archived root `_deps` is NOT the active `build/_deps`.
Archived generated output is local-only/ignored, except for three files already
tracked historically (the two `build_check/midi_event.pb.*` files and
`test_cmake/CMakeLists.txt`), which are retained in version control at their new locations.

Old CMake caches contain absolute paths: do not build these archived trees in
place. To investigate one, restore that specific directory to its original
location (only if that location is free), or configure a fresh build under
`build/`. Normal builds and launches use `build/release`; see the root README.
