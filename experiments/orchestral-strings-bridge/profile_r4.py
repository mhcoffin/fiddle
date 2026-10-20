"""Bowed orchestral strings, revision 4: r3 vocabulary on wire schema 2.

Allocation, values and grouping are unchanged from r3. Only the wire encoding
changes: typed Off values and an Init revision beacon (see generate.py).
"""
from profile_r3 import (ALIASES, BASE_IDS, DIMENSIONS, EXCLUSIONS,  # noqa: F401
                        FAMILY_EXCLUSIONS, DEFERRED_DIMENSIONS, DEFERRED_VALUES)

REVISION = 4
WIRE_SCHEMA = 2
NAME = f"Fiddle Bridge - Orchestral Strings r{REVISION} (host test)"
ENTITY_ID = f"xmap.user.Fiddle.OrchestralStrings.r{REVISION}"
