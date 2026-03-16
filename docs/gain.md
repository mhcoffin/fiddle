# Gain Calculations

We want to change the way the gain is calculated for groups of strips with the same input.

Currently, the group fader is always set to the sum of the db of the strips in the group:

- If the group fader is locked and a strip fader is changed, the other strip faders are adjusted to compensate so that the sum of the db of the strips in the group is equal to the group fader.
- If the group fader is not locked and a strip fader is changed, the other strip faders are unchanged, but the group fader is set to the sum of the db of the strips in the group.

This is not what we want.

We want group fader to reflect the total *power* of the strips in the group. Here is how it should work:

- The *gain* of a strip is determined from its fader position in db, using the function `juce::Decibels::decibelsToGain(float db)`.
- The *power* of a strip is the square of its gain.
- The *power* of a group is the sum of the powers of the strips in the group.
- The *gain* of a group is the square root of the group power.
- The *fader position* of a group fader is the db equivalent of its gain, using the function `juce::Decibels::gainToDecibels(float gain)`.

Important: at all times, the power of the group fader should equal to the sum of the powers of the strips in the group. Muted strips have 0 power.

When the group fader is locked and a strip fader is changed, the other strip faders should have their power adjusted proportionally to keep the group power constant. 

When the group fader is not locked and a strip fader is changed, the other strip faders are unchanged, but the group fader is adjusted to reflect the new group power:
- add up the group power of all the strips in the group
- calculate the group gain by taking the square root of the group power
- calculate the group fader position by getting the db equivalent of the group gain using juce::Decibels::gainToDecibels(float gain)

Example 1:
- There are three strip faders, each set at 0db.  None are muted. The group fader is not locked.
- The gain of each strip is 1. 
- The power of each strip is 1. 
- The group power is 3.
- The group gain is sqrt(3) = 1.732.
- The group fader position is in db is juce::Decibels::gainToDecibels(1.732).

Example 2:
- Same as above, but now the user mutes one strip.
- The gain of the muted strip is 0.
- The power of the muted strip is 0.
- The group power is 2.
- The group gain is sqrt(2) = 1.414.
- The group fader position is in db is juce::Decibels::gainToDecibels(1.414).   

Example 3: 
- There are three strip faders, each set at 0db. But two are muted. The group fader is locked. 
- The user moves non-muted fader up by 3db. 
- There is no way to keep the power constant, so the group fader should change in spite of being locked. I.e., if there is only one non-muted strip, the group fader should always track the strip fader.

Example 4: There are three strip faders, all set at 0db. The group fader is locked.
- The group fader will have the value of juce::Decibels::gainToDecibels(sqrt(3)).
- The user mutes one strip. This changes the power of the group from 3 to 2. 
- The other two strip faders should be adjusted so that the group power is constant, so they must have their power multiplied by 3/2.
- This means that their gain is multipled by sqrt(3/2).
- The strip faders should be adjusted to reflect this change in gain.

Example 5: There are three strip faders, all set at 0db. The group fader is locked. 
- The group power is 3. The group gain is sqrt(3).
- The group fader will have the value juce::Decibels::gainToDecibels(sqrt(3)).
- The user moves the group fader down by 3db. This changes the gain of the group to
  g = juce::Decibels::decibelsToGain(juce::Decibels::gainToDecibels(sqrt(3)) - 3.0f) 
- The power of the group is now g*g
- The power of each strip should be g*g/3
- The strip faders should be adjusted to reflect this change in power. 

In general, muting a strip should have the same effect on other strips and on the group fader as moving the strip fader to -infinity, except that the value of the fader is remembered.

In general, unmuting a strip should have the same effect on other strips and on the group fader as moving the strip fader from -infinity to its previous value.

If all strips are muted, the group gain is 0 and the group fader should show -infinity, using JUCE's default value for -infinity.









