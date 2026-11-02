#pragma once

#include "DoricoPlaybackOptions.h"
#include "midi_event.pb.h"
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace fiddle {

/// Dynamics of notes from a bridge that pins Dorico's playback options
/// (kPinnedPlaybackDimension). Each value the bridge sent (CC1 or velocity) is
/// read back as Dorico's dynamic level and re-expressed on the layer's own
/// curve, as Dorico would have sent it to that layer's instrument directly.
/// See experiments/dynamics-curve/results-01.md.
struct PinnedDynamics {
  /// Velocity Dorico sends when a switch carries dynamics on a controller
  /// only (measured: 100 whatever the dynamic).
  static constexpr int kControllerDynamicsVelocity = 100;

  /// The layer's value for a bridge value, with `delta` levels added.
  static int layerValue(const DoricoDynamics &layer, int bridgeValue, double delta = 0.0) {
    return layer.midiValue(bridgeDynamicLevel(bridgeValue) + delta);
  }

  /// The bridge value for the note's starting dynamic: CC1 for notes whose
  /// bridge switch carries dynamics on CC1, else the note velocity.
  static int startValue(const fiddle::Note &note) {
    if (note.dynamics_mode() == fiddle::Note::CC) {
      const auto it = note.cc_automation().find(1);
      if (it != note.cc_automation().end() && it->second.points_size() > 0)
        return static_cast<int>(it->second.points(0).value());
    }
    return static_cast<int>(note.start_velocity());
  }

  /// Re-express the note's dynamics for a layer: the velocity (when the layer
  /// takes dynamics on velocity, else Dorico's fixed controller-dynamics
  /// velocity, scaled by `velocityFactor`) and, for each of `controllers`,
  /// the note's CC1 points (or its starting dynamic) as during-note CCs. The
  /// note's articulations add their level, as Dorico adds them per note.
  static void apply(fiddle::Note &note, const DoricoDynamics &layer,
                    const std::set<std::string> &techniques, bool velocityDynamics,
                    const std::vector<int> &controllers, float velocityFactor,
                    double sampleRate) {
    const double delta = layer.articulationDelta(techniques);
    const int start = startValue(note);
    for (const int controller : controllers) {
      const auto it = note.cc_automation().find(1);
      const bool lane = note.dynamics_mode() == fiddle::Note::CC &&
                        it != note.cc_automation().end() && it->second.points_size() > 0;
      const int count = lane ? it->second.points_size() : 1;
      for (int i = 0; i < count; ++i) {
        auto *inst = note.add_during_note();
        inst->set_type(fiddle::MidiInstruction::CC);
        inst->set_param1(controller);
        if (lane) {
          const auto &point = it->second.points(i);
          inst->set_param2(layerValue(layer, static_cast<int>(point.value()), delta));
          inst->set_delay_ms(static_cast<int>(point.offset_samples() * 1000.0 /
                                              std::max(1.0, sampleRate)));
        } else {
          inst->set_param2(layerValue(layer, start, delta));
        }
      }
    }
    const int velocity = velocityDynamics ? layerValue(layer, start, delta)
                                          : kControllerDynamicsVelocity;
    note.set_start_velocity(static_cast<uint32_t>(
        std::clamp(static_cast<int>(velocity * velocityFactor), 1, 127)));
  }
};

} // namespace fiddle
