# Fiddle Design Summary

This document provides a high-level overview of the Fiddle architectural design, as summarized from the [original design doc](https://docs.google.com/document/d/1bAAWKn_Re_blij8pZzO3kwpqk3LvO_QVZN7YigjvsdI/edit?usp=sharing).

## Project Goal
Fiddle is a specialized DAW that produces high-quality audio playback directly from Dorico scores, bridging the gap between notation software and professional-grade audio by providing an intelligent and flexible playback engine.

## Core Architecture
- **Distributed System**: Dorico hosts a Fiddle plug-in that relays MIDI via TCP to a standalone Fiddle application.
- **Note stream & Subnotes**: Incoming MIDI is converted into "Note streams" containing high-level metadata. Long notes are split into "Subnotes" (typically 1 second) to handle latency and allow for lookahead context.
- **Lookahead**: A user-specified latency allows the engine to analyze phrases and apply humanization before rendering.

## Processing Units (Fiddles & Drivers)
Both Fiddles and Drivers are scriptable using **AngelScript**.

- **Fiddles**: Logic units that consume and emit Note streams. 
    - Function: Annotate notes, manipulate dynamics/vibrato, and generate new notes (e.g., ornaments).
- **Drivers**: Responsible for turning the Note stream into audio via VSTi plugins. 
    - Function: Handle library-specific technical requirements (key-switches, MIDI CC mappings).

## Technical Requirements
- **Scripting**: AngelScript integration.
- **Networking**: TCP communication.
- **Standards**: VST3 and VSTi support.
