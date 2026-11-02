"""Bowed orchestral strings, revision 5: r4 with written note durations.

Allocation, values, grouping and wire schema are unchanged from r4. The map
pins Dorico's six note-duration playback options to 100% so Dorico sends
written lengths; Fiddle applies the duration percentages per layer.
"""
from profile_r4 import (ALIASES, BASE_IDS, DIMENSIONS, EXCLUSIONS,  # noqa: F401
                        FAMILY_EXCLUSIONS, DEFERRED_DIMENSIONS, DEFERRED_VALUES)

REVISION = 5
WIRE_SCHEMA = 2
WRITTEN_DURATIONS = True
NAME = f"Fiddle Bridge - Orchestral Strings r{REVISION} (host test)"
ENTITY_ID = f"xmap.user.Fiddle.OrchestralStrings.r{REVISION}"
