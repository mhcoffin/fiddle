# Tool windows and mixer layout

The application View menu separates **Windows** from **Mixer layout**. The
mixer toolbar's View menu uses the same two headings.

Chairs, Audio Bus Manager, Audio Performance, Print Audio and Technique Catalog use the shared
`ToolWindow` native host and an isolated `ToolPane` UI entry point. Each tool
has one reusable, non-modal window: opening it again brings that window forward.
Close, the native close button and Cmd/Ctrl-W hide it, retaining its current UI.
Size and position are stored locally, not in project versions. Windows do not
reopen unsolicited on app launch; disconnected-display positions are brought
back onto an available display.

The Layer Inspector and Audio Buses & Master remain part of the mixer layout.
Small contextual editors/confirmations, such as Add Chair, remain attached to
their parent task. Library Manager, Project History, Settings and Debug retain
their existing separate window implementations; Debug commands select a tab in
the same Debug window.

## State and performance

- Tool windows send edits through the existing backend commands and share the
  project's Undo/Redo history. Text-field Undo remains native text editing.
- Audio Bus Manager follows the main mixer's current strip selection, including
  changes while the bus window is open. Create from selection uses that selection.
- Only each tool's relevant state topics reach its WebView. Debug/MIDI events
  are not broadcast to these windows.
- State updates are coalesced by topic with at most one state evaluation in
  flight. Hidden/minimized windows retain the latest snapshot without rendering
  it, then refresh when shown. Print polling and performance sampling pause.
- Closing Print Audio never stops an armed/active take. Audio processing and
  file writing belong to the backend, not the window.

## Native smoke test

1. Open each tool through View; invoke the same command again and confirm it
   focuses the existing window. The main mixer remains usable.
2. Resize/move a tool, close/reopen it, then restart Fiddle and reopen it. Check
   that geometry is remembered. Minimize/restore Audio Performance and verify
   its readings resume.
3. Select strips in the mixer with Audio Bus Manager open. Check its selection
   count, create a bus and Undo. Rename a chair, then Undo from the main mixer.
4. Arm a print, close its window, play/stop Dorico, then reopen Print Audio.
   Confirm the take continued and completed normally.

The isolated browser regression covers layout and bridge commands without
touching a live project. Native window focus/geometry and Dorico playback still
need the smoke test above.
