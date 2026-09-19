#include "MasterLoudnessMeter.h"
#include <ebur128.h>

namespace fiddle {
void MasterLoudnessMeter::run() {
    ebur128_state *state = nullptr;
    uint64_t epoch = 0, frames = 0;
    bool dirty = false;
    const auto publish = [&] {
      if (!state || !dirty) return;
      double value;
      if (frames >= rate_ * 0.4 && ebur128_loudness_global(state, &value) == EBUR128_SUCCESS) integrated_.store(value);
      if (frames >= rate_ * 3 && ebur128_loudness_shortterm(state, &value) == EBUR128_SUCCESS) shortTerm_.store(value);
      for (int ch = 0; ch < 2; ++ch)
        if (ebur128_true_peak(state, ch, &value) == EBUR128_SUCCESS)
          truePeak_[ch].store(value > 0 ? 20 * std::log10(value) : -INFINITY);
      seconds_.store(static_cast<double>(frames) / rate_);
      dirty = false;
    };
    std::array<float, packetFrames * 2> interleaved{};
    while (!threadShouldExit()) {
      const auto request = requested_.load(std::memory_order_acquire);
      if (request != epoch) {
        if (state) ebur128_destroy(&state);
        state = ebur128_init(2, rate_, EBUR128_MODE_I | EBUR128_MODE_S | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_HISTOGRAM);
        epoch = request; frames = 0; dirty = false;
        failed_.store(!state); seconds_.store(0);
        integrated_.store(-INFINITY); shortTerm_.store(-INFINITY);
        for (auto &peak : truePeak_) peak.store(-INFINITY);
        published_.store(epoch, std::memory_order_release);
      }
      int start, count, unusedStart, unusedCount;
      fifo_.prepareToRead(1, start, count, unusedStart, unusedCount);
      if (!count) { publish(); wait(20); continue; }
      const auto &packet = packets_[start];
      if (packet.epoch == epoch && state) {
        for (int i = 0; i < packet.frames; ++i)
          for (int ch = 0; ch < 2; ++ch) {
            const auto sample = packet.audio[ch][i];
            if (!std::isfinite(sample)) failed_.store(true);
            interleaved[i * 2 + ch] = std::isfinite(sample) ? sample : 0;
          }
        if (ebur128_add_frames_float(state, interleaved.data(), packet.frames) != EBUR128_SUCCESS) failed_.store(true);
        frames += packet.frames;
        dirty = true;
        // Publish about every 100 ms during catch-up and also when the queue
        // empties, so a final partial block is never missing from the display.
        if (frames % juce::jmax(1ul, rate_ / 10) < static_cast<unsigned>(packet.frames)) {
          publish();
        }
      }
      fifo_.finishedRead(1);
    }
    if (state) ebur128_destroy(&state);
  }
} // namespace fiddle
