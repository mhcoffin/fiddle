#pragma once
#include "HostMidiBlock.h"
#include "../RealtimeSpscQueue.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <thread>

namespace fiddle {
// Capture exists only in the separately identified MIDI Probe build. Files,
// allocation and worker lifecycle are confined to activation/deactivation.
class MidiProbeCapture {
  struct Record {
    HostMidiInput input;
    uint64_t block = 0, dropped = 0, invalid = 0;
    int64_t host = 0;
    double sampleRate = 0;
    uint32_t contextFlags = 0;
    int32_t samples = 0;
    bool context = false, isInput = false, playing = false;
    size_t inputs = 0;
  };
  RealtimeSpscQueue<Record, 32769> queue_;
  std::atomic<bool> running_{true};
  std::atomic<uint64_t> lost_{0};
  uint64_t block_ = 0, accepted_ = 0;
  std::ofstream file_;
  std::filesystem::path path_;
  std::thread worker_;
  static constexpr uint64_t maxRecords = 250000;

  void push(const Record &record) noexcept {
    if (accepted_ >= maxRecords || !queue_.tryPush(record)) ++lost_;
    else ++accepted_;
  }
  void write(const Record &r) {
    if (!r.isInput) {
      file_ << "{\"kind\":\"block\",\"block\":" << r.block
            << ",\"host_samples\":" << r.host << ",\"has_context\":" << r.context
            << ",\"context_flags\":" << r.contextFlags << ",\"sample_rate\":" << r.sampleRate
            << ",\"playing\":" << r.playing << ",\"inputs\":" << r.inputs
            << ",\"samples\":" << r.samples << ",\"input_dropped\":" << r.dropped
            << ",\"input_invalid\":" << r.invalid << "}\n";
      return;
    }
    const auto &i = r.input;
    file_ << "{\"kind\":\"input\",\"block\":" << r.block
          << ",\"sequence\":" << i.sequence << ",\"source\":" << i.source
          << ",\"source_index\":" << i.sourceIndex << ",\"point_index\":" << i.pointIndex
          << ",\"parameter_id\":" << i.parameterId << ",\"raw_type\":" << i.rawType
          << ",\"normalized\":" << i.normalized << ",\"raw_value\":";
    if (std::isfinite(i.rawValue)) file_ << i.rawValue; else file_ << "null";
    file_ << ",\"valid\":" << i.valid << ",\"offset\":" << i.midi.timestampSamples
          << ",\"host_samples\":" << i.midi.hostSamplePosition
          << ",\"port\":" << i.midi.port << ",\"channel\":" << i.midi.channel
          << ",\"type\":" << static_cast<int>(i.midi.type)
          << ",\"data1\":" << i.midi.data1 << ",\"data2\":" << i.midi.data2 << "}\n";
  }
  void run() {
    Record record;
    for (;;) {
      while (queue_.tryPop(record)) write(record);
      file_.flush();
      if (!running_.load(std::memory_order_acquire)) {
        // Producer is quiescent before shutdown; drain anything published
        // between the preceding empty check and the shutdown observation.
        while (queue_.tryPop(record)) write(record);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    file_ << "{\"kind\":\"end\",\"capture_dropped\":" << lost_.load()
          << ",\"blocks\":" << block_ << "}\n";
    file_.flush();
  }
public:
  explicit MidiProbeCapture(double sampleRate) {
    static std::atomic<uint64_t> serial{0};
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path();
    std::filesystem::path directory;
    for (int attempt = 0; attempt < 100; ++attempt) {
      directory = root / ("fiddle-midi-probe-" + std::to_string(stamp) + "-" + std::to_string(serial++));
      if (std::filesystem::create_directory(directory)) break;
      directory.clear();
    }
    if (directory.empty()) throw std::runtime_error("Cannot create unique MIDI probe directory");
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
    path_ = directory / "capture.jsonl";
    file_.open(path_, std::ios::out);
    if (!file_) throw std::runtime_error("Cannot open MIDI probe trace");
    file_ << std::setprecision(17) << std::boolalpha
          << "{\"kind\":\"header\",\"schema\":1,\"capture_only\":true,\"sample_rate\":" << sampleRate
          << ",\"tie_policy\":\"parameters-before-events; source order within ties\",\"capacity\":" << HostMidiBlock::capacity
          << ",\"max_records\":" << maxRecords << "}\n";
    file_.flush();
    worker_ = std::thread([this] { run(); });
  }
  ~MidiProbeCapture() {
    running_.store(false, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
  }
  const std::filesystem::path &path() const noexcept { return path_; }
  void capture(const HostMidiBlock &block, const Steinberg::Vst::ProcessData &data, double sampleRate) noexcept {
    Record record;
    record.block = block_++; record.host = HostMidiBlock::hostPosition(data);
    record.samples = data.numSamples; record.sampleRate = sampleRate;
    record.context = data.processContext != nullptr;
    record.contextFlags = data.processContext ? data.processContext->state : 0;
    record.playing = (record.contextFlags & Steinberg::Vst::ProcessContext::kPlaying) != 0;
    record.inputs = block.count;
    record.dropped = block.dropped; record.invalid = block.invalid;
    push(record);
    record.isInput = true;
    for (size_t i = 0; i < block.count; ++i) {
      record.input = block.inputs[i]; push(record);
    }
  }
};
} // namespace fiddle
