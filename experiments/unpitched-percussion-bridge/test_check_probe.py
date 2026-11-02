"""check_probe.py against synthetic captures of unpitched-probe.musicxml:
one per way Dorico might behave, so each verdict is exercised before a real
capture exists."""
import unittest

import check_probe as check

RATE = 44100.0
ON, OFF, CC = check.NOTE_ON, check.NOTE_OFF, check.CONTROL_CHANGE


class Dorico:
    """Emits what Dorico would send for the probe score under one hypothesis."""

    def __init__(self, routing="A1", rolls="sustained", fallback="sustained", combo="exact",
                 unmatched="natural", keyswitch=True, beater="both", snares="addon", velocity="exact",
                 stress=False, cc1_on_hits=False, written_roll="roll", combo_shape=None):
        self.o = dict(routing=routing, rolls=rolls, fallback=fallback, combo=combo, unmatched=unmatched,
                      keyswitch=keyswitch, beater=beater, snares=snares, velocity=velocity, stress=stress,
                      cc1_on_hits=cc1_on_hits, written_roll=written_roll, combo_shape=combo_shape or rolls)
        self.events = []

    def at(self, ms):
        return int(round(10000 + ms * RATE / 1000.0))

    def cc(self, t, number, value, ch=1):
        self.events.append([self.at(t), CC, ch, number, value])

    def ks(self, t, pitch, velocity, ch=1):
        self.events.append([self.at(t), ON, ch, pitch, velocity])
        self.events.append([self.at(t + 5), OFF, ch, pitch, 64])

    def note(self, t, pitch, velocity, length, ch=1, cc1=None):
        self.events.append([self.at(t), ON, ch, pitch, velocity])
        if cc1 is not None:
            self.events.append([self.at(t + 3), CC, ch, 1, cc1])
        self.events.append([self.at(t + length), OFF, ch, pitch, 64])

    def hit(self, bar, beat, level, pitch=60, length=550, ch=1, addons=()):
        t = check.beat(bar, beat)
        for number, value in addons:
            self.cc(t - 5, number, value, ch)
        velocity = level if self.o["velocity"] == "exact" else level + 3
        if self.o["stress"] and beat == 1:
            velocity += 4
        self.note(t, pitch, velocity, length, ch, cc1=level if self.o["cc1_on_hits"] else None)

    def roll(self, bar, beat, pitch, ch, beats, shape, from_level, to_level):
        t, length = check.beat(bar, beat), beats * check.BEAT
        if shape == "sustained":
            self.cc(t - 5, 102, 5 if self.o["written_roll"] == "roll" else 10, ch)
            self.events.append([self.at(t), ON, ch, pitch, 100])
            steps = 8
            for k in range(steps + 1):
                self.cc(t + 3 + length * k / steps, 1, round(from_level + (to_level - from_level) * k / steps), ch)
            self.events.append([self.at(t + length - 1), OFF, ch, pitch, 64])
        elif shape == "strokes":
            for k in range(int(beats * 4)):
                self.note(t + k * check.BEAT / 4, pitch, from_level, check.BEAT / 4 - 10, ch)
        # "dropped": nothing

    def build(self):
        o = self.o
        both = o["routing"] == "A1"
        for i, level in enumerate(check.LADDER):
            self.hit(1 + i // 4, 1 + i % 4, level)
        for k in range(8):
            self.hit(3, 1 + k / 2, 68, length=250)
        # Bar 4: accent (entry 70 and/or CC104=1), marcato (CC104=2 only), ghost (KS4=2 only), plain.
        accent_pitch = 70 if o["routing"] in ("A1", "A2") else 60
        self.hit(4, 1, 68, pitch=accent_pitch, addons=[(104, 1)] if both or o["routing"] == "B-pitch" else [])
        self.hit(4, 2, 68, addons=[(104, 2)] if o["routing"] != "B" else [])
        if o["routing"] != "B":
            self.ks(check.beat(4, 3) - 5, 4, 2)
        self.hit(4, 3, 68)
        self.hit(4, 4, 68)
        # Bar 5: roll p<f on both maps; bar 6: buzz roll, rim+roll.
        roll_pitch = 64 if o["written_roll"] == "roll" else 65
        self.roll(5, 1, roll_pitch, 1, 4, o["rolls"], 56, 72)
        self.roll(5, 1, 60, 2, 4, o["fallback"], 56, 72)
        self.roll(6, 1, 66, 1, 2, o["rolls"], 72, 72)
        combo_pitch = dict(exact=67, roll=roll_pitch, rim=61, natural=60)[o["combo"]]
        self.roll(6, 3, combo_pitch, 1, 2, o["combo_shape"], 72, 72)
        # Bar 7: rim, side stick, x head (cross stick), dead stroke (no entry).
        self.hit(7, 1, 72, pitch=61, addons=[(106, 3)] if both else [])
        self.hit(7, 2, 72, pitch=62)
        self.hit(7, 3, 72, pitch=63)
        if o["unmatched"] != "drop":
            self.hit(7, 4, 72, pitch=60 if o["unmatched"] == "natural" else 99,
                     addons=[(108, 1)] if o["routing"] in ("A1", "A2") else [])
        # Bars 8-9: Rods; bar 9 also rim.
        rods_pitch = 68 if o["beater"] in ("both", "pitch") else 60
        for k in range(4):
            if o["keyswitch"] and rods_pitch == 68:
                self.ks(check.beat(8, 1 + k) - 10, 15, 100)
            self.hit(8, 1 + k, 72, pitch=rods_pitch, addons=[(105, 2)] if o["beater"] in ("both", "addon") and k == 0 else [])
        for k in range(4):
            self.hit(9, 1 + k, 72, pitch=69 if rods_pitch == 68 else 61)
        # Bars 10-11: Snares off, Snares on.
        for k in range(4):
            self.hit(10, 1 + k, 72, addons=[(112, 2)] if o["snares"] == "addon" and k == 0 else [])
            self.hit(11, 1 + k, 72, addons=[(112, 1)] if o["snares"] == "addon" and k == 0 else [])
        return self.events


def verdicts(**options):
    return {k: v["verdict"] for k, v in check.analyze(Dorico(**options).build(), RATE).items()}


class Verdicts(unittest.TestCase):
    def test_target_design(self):
        v = verdicts()
        self.assertEqual(v["U1"], "A1")
        self.assertEqual(v["U2"], "A SUSTAINED; B FALLBACK-SUSTAINED")
        self.assertEqual(v["U3"], "EXACT-COMBO; unmatched technique falls back to Natural")
        self.assertTrue(v["U4"].startswith("SENT x4 (per note)"), v["U4"])
        self.assertEqual(v["U5"], "beater BOTH (bar 9 rods+rim -> rods+rim entry); snares ADDON")
        self.assertEqual(v["U6"], "EXACT; NO-BEAT-STRESS; no CC1 on hits")

    def test_only_unconsumed_techniques_reach_the_expression_map(self):
        self.assertEqual(verdicts(routing="A2")["U1"], "A2")

    def test_expression_map_never_switches(self):
        v = verdicts(routing="B", keyswitch=False, beater="pitch", snares="none")
        self.assertEqual(v["U1"], "B")
        self.assertEqual(v["U4"], "NOT-SENT")
        self.assertEqual(v["U5"], "beater PITCH (bar 9 rods+rim -> rods+rim entry); snares NEITHER")

    def test_roll_shapes(self):
        self.assertEqual(verdicts(rolls="strokes", fallback="strokes")["U2"], "A STROKES; B FALLBACK-STROKES")
        self.assertEqual(verdicts(fallback="dropped")["U2"], "A SUSTAINED; B DROPPED")

    def test_combination_choices(self):
        self.assertTrue(verdicts(combo="roll")["U3"].startswith("BEST-SUBSET (roll wins)"))
        self.assertTrue(verdicts(combo="rim")["U3"].startswith("BEST-SUBSET (rim wins)"))
        self.assertTrue(verdicts(combo="natural")["U3"].startswith("NATURAL-FALLBACK"))
        self.assertTrue(verdicts(unmatched="drop")["U3"].endswith("DROP-WHEN-UNMATCHED"))

    def test_dorico_6_capture_shape(self):
        """results-01.md: the written roll is pt.tremolo, the combination
        takes the rim entry and plays its tremolo as strokes, map B strokes."""
        v = verdicts(written_roll="tremolo", combo="rim", combo_shape="strokes", fallback="strokes")
        self.assertEqual(v["U1"], "A1")
        self.assertEqual(v["U2"], "A SUSTAINED; B FALLBACK-STROKES")
        self.assertEqual(v["U3"], "BEST-SUBSET (rim wins) - the rim+roll entry never applied; "
                                  "EXACT-COMBO shown by bar 9 rods+rim; the combination's roll played as STROKES; "
                                  "unmatched technique falls back to Natural")
        text = check.report(check.analyze(Dorico(written_roll="tremolo").build(), RATE))
        self.assertIn("CC102 10 before the note (tremolo base - a roll-family base)", text)

    def test_velocity_deviations(self):
        self.assertTrue(verdicts(velocity="off")["U6"].startswith("OFF +3 +3"))
        self.assertTrue("BEAT-STRESS" in verdicts(stress=True)["U6"])
        self.assertTrue(verdicts(cc1_on_hits=True)["U6"].endswith("CC1 also sent on hits"))

    def test_report_lists_every_unknown(self):
        text = check.report(check.analyze(Dorico().build(), RATE))
        for key in ("U1 ", "U2 ", "U3 ", "U4 ", "U5 ", "U6 "):
            self.assertIn(key, text)
        self.assertIn("ladder 8: velocity 88 (expected 88)", text)

    def test_no_notes(self):
        self.assertIsNone(check.analyze([], RATE))


if __name__ == "__main__":
    unittest.main()
