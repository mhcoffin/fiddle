#pragma once

#include "FiddleController.h"
#include "RealtimeMidiEvent.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace fiddle {

// Both the shipping bridge and the isolated probe use this exact adapter.
// VST3 supplies separate parameter queues and an event list, not a total order.
// Preserve each source's order at equal offsets; parameters precede events at
// ties. Never claim this tie policy is an observed cross-list host ordering.
struct HostMidiInput {
  RealtimeMidiEvent midi;
  double rawValue = 0;
  uint32_t parameterId = 0;
  int32_t source = 0; // 0 = parameter, 1 = event list
  int32_t sourceIndex = 0;
  int32_t pointIndex = 0;
  int32_t rawType = -1;
  uint32_t sequence = 0; // original enumeration, before sample-offset merge
  bool normalized = false;
  bool valid = false;
};

class HostMidiBlock {
public:
  static constexpr size_t capacity = 16384;
  std::array<HostMidiInput, capacity> inputs{};
  size_t count = 0;
  uint64_t dropped = 0, invalid = 0;

  static int midi7(double value) noexcept {
    if (!std::isfinite(value) || value < 0 || value > 1) return -1;
    return static_cast<int>(value * 127.0 + 0.5);
  }

  static int64_t hostPosition(const Steinberg::Vst::ProcessData &data) noexcept {
    // projectTimeSamples is always valid when a ProcessContext exists; the
    // music-valid flag applies to projectTimeMusic, not this sample clock.
    return data.processContext ? data.processContext->projectTimeSamples : 0;
  }

  void collect(Steinberg::Vst::ProcessData &data) noexcept {
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    count = 0; dropped = 0; invalid = 0;
    size_t attempts = 0;
    const auto host = hostPosition(data);
    const auto append = [&](HostMidiInput input) {
      input.sequence = static_cast<uint32_t>(count);
      const auto offset = input.midi.timestampSamples;
      if (offset < 0 || offset >= std::max(1, data.numSamples)) input.valid = false;
      // The existing wire timestamp is unsigned. Keep the original signed host
      // position in diagnostic metadata; saturate preroll only at this boundary.
      const auto absolute = host > std::numeric_limits<int64_t>::max() - std::max(0, offset)
          ? std::numeric_limits<int64_t>::max() : host + std::max(0, offset);
      input.midi.hostSamplePosition = static_cast<uint64_t>(std::max<int64_t>(0, absolute));
      if (!input.valid) ++invalid;
      inputs[count++] = input;
    };
    if (data.inputParameterChanges) {
      const auto total = data.inputParameterChanges->getParameterCount();
      const auto queues = std::min<int32>(total, capacity);
      if (total > queues) dropped += total - queues; // at least this many inputs
      for (int32 q = 0; q < queues; ++q) {
        auto *queue = data.inputParameterChanges->getParameterData(q);
        if (!queue) { ++invalid; continue; }
        const auto id = queue->getParameterId();
        const auto points = queue->getPointCount();
        for (int32 p = 0; p < points; ++p) {
          if (attempts++ >= capacity) { dropped += points - p; break; }
          HostMidiInput input;
          input.parameterId = id; input.sourceIndex = q; input.pointIndex = p;
          input.normalized = true;
          if (queue->getPoint(p, input.midi.timestampSamples, input.rawValue) != kResultOk) {
            ++invalid; continue;
          }
          const auto value = midi7(input.rawValue);
          int logical = -1;
          if (id >= FiddleController::kProgramParamBase && id < FiddleController::kProgramParamBase + FiddleController::kNumChannels) {
            logical = id - FiddleController::kProgramParamBase;
            input.midi.type = RealtimeMidiEventType::ProgramChange;
            input.midi.data1 = value;
          } else if (id >= FiddleController::kCCParamBase && id < FiddleController::kCCParamBase + FiddleController::kNumSupportedCCs * FiddleController::kNumChannels) {
            const auto index = id - FiddleController::kCCParamBase;
            logical = index % FiddleController::kNumChannels;
            input.midi.type = RealtimeMidiEventType::ControlChange;
            input.midi.data1 = FiddleController::kSupportedCCs[index / FiddleController::kNumChannels];
            input.midi.data2 = value;
          }
          input.valid = logical >= 0 && value >= 0;
          input.midi.port = logical >= 0 ? logical / 16 : -1;
          input.midi.channel = logical >= 0 ? logical % 16 + 1 : -1;
          append(input);
        }
      }
    }
    if (data.inputEvents) {
      const auto total = data.inputEvents->getEventCount();
      for (int32 i = 0; i < total; ++i) {
        if (attempts++ >= capacity) { dropped += total - i; break; }
        Event event{};
        if (data.inputEvents->getEvent(i, event) != kResultOk) { ++invalid; continue; }
        HostMidiInput input;
        input.source = 1; input.sourceIndex = i; input.rawType = event.type;
        auto &out = input.midi;
        out.timestampSamples = event.sampleOffset; out.port = event.busIndex;
        input.valid = out.port >= 0 && out.port < FiddleController::kNumPorts;
        switch (event.type) {
        case Event::kNoteOnEvent:
          out.type = RealtimeMidiEventType::NoteOn;
          out.channel = event.noteOn.channel + 1; out.data1 = event.noteOn.pitch;
          input.rawValue = event.noteOn.velocity; input.normalized = true;
          out.data2 = midi7(input.rawValue);
          break;
        case Event::kNoteOffEvent:
          out.type = RealtimeMidiEventType::NoteOff;
          out.channel = event.noteOff.channel + 1; out.data1 = event.noteOff.pitch;
          input.rawValue = event.noteOff.velocity; input.normalized = true;
          out.data2 = midi7(input.rawValue);
          break;
        case Event::kPolyPressureEvent:
          out.type = RealtimeMidiEventType::PolyPressure;
          out.channel = event.polyPressure.channel + 1; out.data1 = event.polyPressure.pitch;
          input.rawValue = event.polyPressure.pressure; input.normalized = true;
          out.data2 = midi7(input.rawValue);
          break;
        case Event::kLegacyMIDICCOutEvent: {
          const auto &cc = event.midiCCOut;
          out.channel = cc.channel + 1;
          out.data1 = cc.controlNumber; out.data2 = cc.value;
          if (cc.controlNumber <= 127) out.type = RealtimeMidiEventType::ControlChange;
          else if (cc.controlNumber == 128) { out.type = RealtimeMidiEventType::ChannelPressure; out.data1 = cc.value; }
          else if (cc.controlNumber == 129) { out.type = RealtimeMidiEventType::PitchBend; out.data1 = cc.value | (cc.value2 << 7); }
          else if (cc.controlNumber == 130) { out.type = RealtimeMidiEventType::ProgramChange; out.data1 = cc.value; }
          else input.valid = false;
          break;
        }
        default: input.valid = false; break;
        }
        input.valid = input.valid && out.channel >= 1 && out.channel <= 16 && out.data1 >= 0 &&
            out.data1 <= (out.type == RealtimeMidiEventType::PitchBend ? 16383 : 127) && out.data2 >= 0 && out.data2 <= 127;
        append(input);
      }
    }
    std::sort(inputs.begin(), inputs.begin() + count, [](const auto &a, const auto &b) {
      if (a.midi.timestampSamples != b.midi.timestampSamples) return a.midi.timestampSamples < b.midi.timestampSamples;
      return a.sequence < b.sequence;
    });
  }
};
} // namespace fiddle
