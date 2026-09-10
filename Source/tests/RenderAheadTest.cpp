#include "AudioStreamRing.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <thread>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
using Ring = fiddle::AudioStreamRing;
using Plan = fiddle::RenderAheadPlan;

std::unique_ptr<Ring> makeRing(int block = 1024) {
  auto r = std::make_unique<Ring>();
  r->sampleRate = 44100; r->blockSize = uint32_t(block);
  r->targetFrames = Ring::reserveFrames(r->sampleRate, r->blockSize);
  r->writeFrame.store(r->targetFrames);
  r->magic.store(Ring::magicValue); r->active.store(1);
  return r;
}

// Discrete-event simulation uses the production ring, consumer and planning
// logic. Processing costs are virtual; no sleeps, real device or vendor needed.
void simulate(int block, int hostBlock, double stallBlocks, bool expectUnderrun) {
  auto r = makeRing(block);
  const auto period = 1000.0 * block / r->sampleRate;
  const auto hostPeriod = 1000.0 * hostBlock / r->sampleRate;
  std::vector<float> left(size_t(std::max(block, hostBlock))), right(left.size());
  std::vector<float> outLeft(size_t(hostBlock), 0), outRight(size_t(hostBlock), 0);
  float *outputs[]{outLeft.data(), outRight.data()};
  double now = 0, nextHost = 0, finish = 1e20;
  bool busy = false, stalled = false;
  Plan pending;
  uint64_t underruns = 0, skipped = 0, refills = 0, lastUnderrun = 0;
  for (int event = 0; event < 20000 && r->readFrame.load() < 44100 * 3; ++event) {
    if (finish <= nextHost) {
      now = finish;
      for (int i = 0; i < block; ++i)
        left[size_t(i)] = right[size_t(i)] = float(pending.startFrame + uint64_t(i) + 1);
      CHECK(r->push(pending.startFrame, left.data(), right.data(), uint32_t(block)));
      skipped += pending.skippedFrames;
      busy = false; finish = 1e20;
    } else {
      now = nextHost; nextHost += hostPeriod;
      const auto read = r->readFrame.load();
      const auto result = r->pull(outputs, 2, hostBlock, 44100, 1000 + now);
      CHECK(result != Ring::PullResult::unavailable);
      if (result == Ring::PullResult::underrun) {
        ++underruns; lastUnderrun = read;
        for (auto value : outLeft) CHECK(value == 0);
      } else {
        for (int i = 0; i < hostBlock; ++i) {
          const auto frame = read + uint64_t(i);
          const float expected = frame < r->targetFrames ? 0.0f : float(frame + 1);
          CHECK(outLeft[size_t(i)] == expected); // no stale/repeated/shifted samples
        }
      }
    }
    if (!busy) {
      const auto plan = Plan::next(*r);
      if (plan.render) {
        CHECK(std::abs(plan.presentationTimeMs - (1000 + 1000.0 * plan.startFrame / 44100)) < 0.002);
        CHECK(plan.startFrame + uint64_t(block) <= r->readFrame.load() + r->targetFrames);
        pending = plan; busy = true;
        double cost = period * 0.55;
        if (!stalled && plan.startFrame >= 22050) { cost = period * stallBlocks; stalled = true; }
        finish = now + cost;
        if (now != nextHost - hostPeriod) ++refills;
      }
    }
  }
  CHECK(stalled && refills > 0);
  CHECK(r->readFrame.load() >= 44100 * 3);
  CHECK((underruns > 0) == expectUnderrun);
  if (expectUnderrun) CHECK(skipped > 0 && lastUnderrun < 44100); // recovered for remaining two seconds
  else CHECK(skipped == 0);
}

void boundaries() {
  CHECK(Ring::reserveFrames(44100, 512) == 3072);
  CHECK(Ring::reserveFrames(44100, 1024) == 3072);
  CHECK(Ring::reserveFrames(96000, 1024) == 6144);
  CHECK(Ring::reserveFrames(44100, 0) == 0);
  CHECK(Ring::reserveFrames(44100, 8192) == 0);
  auto r = makeRing();
  CHECK(!Plan::next(*r).render); // wait for the consumer's presentation clock
  float left[512]{}, right[512]{}; float *output[]{left, right};
  CHECK(r->pull(output, 2, 512, 48000, 1000) == Ring::PullResult::unavailable);
  CHECK(r->readFrame.load() == 0); // mismatch cannot drain the stream
  CHECK(r->pull(output, 2, 512, 44100, 1000) == Ring::PullResult::audio);
  CHECK(!Plan::next(*r).render); // don't overfill for a half-block deficit
  CHECK(r->pull(output, 2, 512, 44100, 1011.609977) == Ring::PullResult::audio);
  CHECK(Plan::next(*r).render);
  r->clockSequence.store(3);
  CHECK(!Plan::next(*r).render); // odd/torn clock snapshot: retry later, never spin
  r->active.store(0);
  CHECK(r->pull(output, 2, 512, 44100, 1023) == Ring::PullResult::unavailable);
}

void concurrentCopies() {
  auto r = makeRing(512);
  // Disable priming for this sequence-number transfer test.
  r->writeFrame.store(0);
  constexpr uint64_t total = 512 * 2000;
  std::atomic<bool> done{false};
  std::thread producer([&] {
    std::array<float, 512> samples;
    for (uint64_t frame = 0; frame < total; frame += 512) {
      for (uint64_t i = 0; i < 512; ++i) samples[i] = float(frame + i + 1);
      while (!r->push(frame, samples.data(), samples.data(), 512)) std::this_thread::yield();
    }
    done.store(true);
  });
  std::array<float, 128> a{}, b{}; float *out[]{a.data(), b.data()};
  while (r->readFrame.load() < total) {
    const auto frame = r->readFrame.load();
    // Avoid intentional starvation in this test; virtual tests cover it above.
    if (r->writeFrame.load() < frame + 128) { std::this_thread::yield(); continue; }
    CHECK(r->pull(out, 2, 128, 44100, 1000 + frame * 1000.0 / 44100) == Ring::PullResult::audio);
    for (uint64_t i = 0; i < 128; ++i) CHECK(a[i] == float(frame + i + 1) && b[i] == a[i]);
  }
  producer.join(); CHECK(done.load());
}

int main() {
  try {
    boundaries();
    simulate(512, 512, 2.0, false);
    simulate(1024, 512, 1.4, false);
    simulate(1024, 256, 1.4, false);
    simulate(1024, 1024, 1.4, false);
    simulate(1024, 512, 6.0, true);
    simulate(512, 16, 12.0, true);
    concurrentCopies();
    std::cout << "Render ahead: reserve, catch-up, variable host blocks, starvation and concurrent ring copies passed\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
