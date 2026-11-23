# Metronome Clicks from Dorico

Summary of the tables below:

* Simple meters (4/4, 2/4, 3/4, 3/8) have two kinds of beats: strong and medium. The strong beats are on beat 0, and are note 83 with velocity 64. The medium beats are on the remaining beats, and are note 79 with velocity 56.

* Compound meters (6/8, 12/8) have three kinds of beats: strong, medium, and weak. The strong beats are on beat 0, and are note 83 with velocity 64. The medium beats are on beats 3 and 6 (although other compound meters would have different beat indices), and are note 79 with velocity 56. The weak beats are on the remaining beats, and are note 71 with velocity 48.

* Irregular meters (5/4, 7/8, etc.) have different patterns of beats, but the initial beat is always a strong beat (note 83 with velocity 64). 

So we can detect bar lines by finding the strong beats (note 83 with velocity 64). 

## Getting the click to Fiddle

Fiddle reserves port 1, channel 1 (Dorico's numbering; port 0 inside Fiddle)
for the click (`MainComponent::kMetronomePort`). Each Dorico project needs
the **Tempo** track routed there in Play mode (**Set Click Routing**) and the
click on for playback (the metronome button in Play mode's toolbar, the
project's `enableClick` playback option); routing alone sends nothing.
Fiddle's **Chairs → Install Playback Template** can't set either: the endpoint
configuration it writes holds only plugin slots and instruments.

A **Dorico project template** holds both (found 2026-11-22): set up a project
with the Tempo track routed to Fiddle and the click on, save it as a project
template, and new projects made from it start with both set. Fiddle might ship
its own project templates later; for now each user can make one in a few
minutes.

Fiddle's tempo (`MetronomeTempoTracker`) and every note's beat prominence
(`NoteStreamTracker`, a script's `note.beat`) come from these clicks. A note
takes the strength of a click within about 50 ms of it, else 3 (off the
beat). The click and a note on the same beat reach the plugin at the same
sample position; the plugin passes the click on before the other events
there (`HostMidiBlock::putClicksFirst`), so the beat is known when the note
arrives. A
trace of a 2026-11-22 playthrough found every on-beat note at exactly its
click's position, and the click already received in every case.



## 4/4

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 79   | 56       |
| 2    | 79   | 56       |
| 3    | 79   | 56       |

## 2/4

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 79   | 56       |

## 3/4

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 79   | 56       |
| 2    | 79   | 56       |

## 3/8  

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 79   | 56       |
| 2    | 79   | 56       |

## 6/8

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 71   | 48       |
| 2    | 71   | 48       |
| 3    | 79   | 56       |
| 4    | 71   | 48       |
| 5    | 71   | 48       |

## 12/8

| Beat | Note | Velocity |
|------|------|----------|
| 0    | 83   | 64       |
| 1    | 71   | 48       |
| 2    | 71   | 48       |
| 3    | 79   | 56       |
| 4    | 71   | 48       |
| 5    | 71   | 48       |
| 6    | 79   | 56       |
| 7    | 71   | 48       |
| 8    | 71   | 48       |
| 9    | 79   | 56       |
| 10   | 71   | 48       |
| 11   | 71   | 48       |

