"""Curated bowed orchestral strings, revision 3; not a runtime decoder.

Retain the reviewed r2 grouping semantics; prune the default vocabulary rather
than pack unrelated flags together. Future guitar/custom profiles can opt into
deferred PTs. The original profile.py and r2 allocations remain historical.
"""
from profile import (ALIASES, BASE_IDS, DIMENSIONS as R2_DIMENSIONS,
                     EXCLUSIONS as R2_EXCLUSIONS, FAMILY_EXCLUSIONS)

REVISION = 3
NAME = f"Fiddle Bridge - Orchestral Strings r{REVISION} - INSPECTION ONLY"
ENTITY_ID = f"xmap.user.Fiddle.OrchestralStrings.Inspection.r{REVISION}"

# These are default-profile scope decisions, not claims that a string player
# cannot imitate a gesture. Keep canonical glissando/portamento and actual
# bowed-string pizzicato variants; defer the generic guitar/jazz FX vocabulary.
DEFERRED_DIMENSIONS = {
    "shake": "Jazz shake gesture; default strings retain the explicit trill and tremblement PTs. Revisit in another profile or customization.",
    "slide": "Generic slide gesture deferred; default strings cover explicit Glissando and Portamento IDs without inventing aliases.",
    "smear": "Generic jazz pitch effect deferred to another profile or customization.",
    "scoop": "Jazz entry gesture deferred to another profile or customization.",
    "plop": "Jazz entry gesture deferred to another profile or customization.",
    "fall": "Jazz ending gesture deferred to another profile or customization.",
    "doit": "Jazz ending gesture deferred to another profile or customization.",
    "bend": "Generic bend gesture deferred to the guitar profile/customization; explicit string Glissando/Portamento retained.",
    "dip_bend": "Guitar/jazz bend variant deferred to another profile or customization.",
    "slight_bend": "Guitar/jazz bend variant deferred to another profile or customization.",
    "pre_bend": "Guitar pre-bend gesture deferred to the future guitar profile/customization.",
    "flip": "Generic jazz pitch effect deferred to another profile or customization.",
    "jazz_turn": "Jazz ornament deferred; not part of the default orchestral-strings vocabulary.",
    "rip": "Generic jazz pitch effect deferred to another profile or customization.",
    "squeeze": "Generic jazz pitch effect deferred to another profile or customization.",
    "muffled_damped": "Generic percussion damping variant omitted; strings retain Muffled strings, Damp and Laissez vibrer as distinct IDs.",
    "release": "Generic percussion release intent omitted; no distinct bowed-string meaning established for this PT. Available for future customization.",
}
DEFERRED_VALUES = {
    "pt.guitarPizzicato": "Guitar-specific pizzicato PT deferred to the future guitar profile; string Pizzicato and Pizzicato alla chitarra retained separately.",
    "pt.pinchHarmonic": "Guitar pinch-harmonic production deferred to the future guitar profile; bowed-string harmonic modes retained.",
}

EXCLUSIONS = {**R2_EXCLUSIONS, **DEFERRED_VALUES}
DIMENSIONS = []
for key, members, natural, rationale in R2_DIMENSIONS:
    if key in DEFERRED_DIMENSIONS:
        EXCLUSIONS.update({"pt." + pt: DEFERRED_DIMENSIONS[key] for pt in members.split()})
        continue
    retained = " ".join(pt for pt in members.split() if "pt." + pt not in DEFERRED_VALUES)
    if key == "excitation":
        rationale = ("Bowed-string pizzicato methods, including buzz, nail and alla chitarra. "
                     "The separate Guitar pizzicato PT is deferred. Bowed aliases Natural.")
    elif key == "harmonic":
        rationale = ("Bowed-string harmonic production alternatives; exact Natural harmonic 1/2 "
                     "IDs retained. Harp and guitar pinch harmonics are outside this profile.")
    DIMENSIONS.append((key, retained, natural, rationale))
