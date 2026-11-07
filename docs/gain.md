# Gain Calculations

Each chair has two levels (see [chair buses](chair-buses.md)):

- **Chair level** is the chair strip's fader: the chair bus gain, applied
  after the layers are summed. It runs from −∞ to +6 dB and never changes
  layer gains, locked or not.
- **Blend level** is the layers' combined level, before the chair fader. The
  chair strip shows it as a `Σ` readout below its fader when the chair has two
  or more layers.

The blend level is a gain-based *power estimate*, not a sum of dB and not measured audio loudness:

- The *gain* of a layer is determined from its fader position in dB, using the function `juce::Decibels::decibelsToGain(float db)`.
- The *power* of a layer is the square of its gain.
- The *power* of a blend is the sum of the powers of the audible layers. Muted, inactive, and solo-excluded layers contribute no power.
- The *gain* of a blend is the square root of its power.
- The *blend level* is the dB equivalent of its gain, using the function `juce::Decibels::gainToDecibels(float gain)`.

## Keep blend level (the lock)

The lock button beside the chair level turns on **Keep blend level**. Locking captures the current blend level as a persistent target. Unlocking and locking again captures it anew; there is no control to edit the target directly, because the chair fader raises or lowers the whole chair.

While locked, changing an individual layer preserves its requested gain and adjusts audible siblings proportionally toward the stored target. If the edited layer alone exceeds the target, siblings reach silence but the target does not move. Lowering the layer below the target restores the siblings using their remembered proportions. Automatic silence does not erase that blend memory. Gain limits may also prevent the target from being reached. When the blend differs from the target by more than 0.05 dB, the `Σ` readout turns amber; it reflects the clamped gains actually sent to audio.

Targets and blend memory are saved with project settings and undone/redone atomically with compensated gain changes. Old projects containing only lock flags capture their current combined levels once, after layers load. Multi-selected layer gain/mute edits retain their existing direct-edit behavior; they do not redefine locked targets.

When the chair is not locked and a layer fader is changed, the other layer faders are unchanged, and the `Σ` readout follows the new blend:
- add up the power of all the audible layers in the chair
- calculate the blend gain by taking the square root of that power
- calculate the blend level by getting the dB equivalent of the blend gain using juce::Decibels::gainToDecibels(float gain)

Example 1:
- There are three layer faders, each set at 0 dB. None are muted. The chair is not locked.
- The gain of each layer is 1.
- The power of each layer is 1.
- The blend power is 3.
- The blend gain is sqrt(3) = 1.732.
- The blend level is juce::Decibels::gainToDecibels(1.732), about +4.8 dB.

Example 2:
- Same as above, but now the user mutes one layer.
- The gain of the muted layer is 0.
- The power of the muted layer is 0.
- The blend power is 2.
- The blend gain is sqrt(2) = 1.414.
- The blend level is juce::Decibels::gainToDecibels(1.414), about +3.0 dB.

Example 3:
- There are three layer faders, each set at 0 dB. But two are muted. The chair is locked.
- The user moves the non-muted fader up by 3 dB.
- There is no way to keep the power constant. The locked target stays at 0 dB, while the `Σ` readout shows +3 dB in amber. Lowering the layer to 0 dB brings the blend back to the target.

Example 4: There are three layer faders, all set at 0 dB. The chair is locked.
- The blend level is juce::Decibels::gainToDecibels(sqrt(3)).
- The user mutes one layer. This changes the blend power from 3 to 2.
- The other two layer faders are adjusted so that the blend power is constant, so they must have their power multiplied by 3/2.
- This means that their gain is multiplied by sqrt(3/2).
- The layer faders are adjusted to reflect this change in gain.

Example 5: There are three layer faders, all set at 0 dB. The chair is locked.
- The user moves the chair fader down by 3 dB.
- The chair bus gain becomes −3 dB. The layer faders, the blend level and its target are unchanged; the whole chair is 3 dB quieter.

In general, muting a layer should have the same effect on other layers and on the blend level as moving the layer fader to -infinity, except that the value of the fader is remembered.

In general, unmuting a layer should have the same effect on other layers and on the blend level as moving the layer fader from -infinity to its previous value.

If all layers are muted, the blend level is -infinity. A locked chair retains its target, and its `Σ` readout shows -infinity in amber.
