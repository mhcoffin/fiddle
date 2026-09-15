#pragma once

#include "../RealtimeObjectPublisher.h"
#include "../AudioDiagnostics.h"
#include "../AudioProcessingGate.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace fiddle {

class PluginEditorWindow;

enum class PluginSlotRole { instrument, effect };

struct PluginSlotId {
  juce::String value;

  static PluginSlotId instrumentFor(const juce::String &stripId);

  [[nodiscard]] bool isValid() const noexcept { return value.isNotEmpty(); }
  bool operator==(const PluginSlotId &other) const noexcept {
    return value == other.value;
  }
};

enum class HostedPluginStatus { empty, loading, loaded, missing, failed };

struct PluginCompatibility {
  bool isInstrument = false;
  bool hasStereoInput = false;
  bool hasStereoOutput = false;
  bool compatible = false;
  bool requiresRuntimeValidation = false;
  juce::String reason;

  static PluginCompatibility
  fromDescription(const juce::PluginDescription &description,
                  PluginSlotRole role);

  [[nodiscard]] juce::var toJson() const;
};

struct MissingPluginState {
  juce::PluginDescription description;
  juce::MemoryBlock state;
  juce::String error;
};

/// Owns the lifecycle of one hosted processor independently of mixer-strip
/// semantics. All mutation methods are message-thread operations. The audio
/// thread accesses only a published Runtime generation and atomic flags.
class HostedPluginSlot {
public:
  struct Runtime {
    std::unique_ptr<juce::AudioProcessor> processor;
    juce::AudioBuffer<float> scratchBuffer;
    PluginRenderDiagnostics timing;
    PluginRenderDiagnostics::Snapshot latestTiming; // message thread only
    void processBlock(juce::AudioBuffer<float> &audio, juce::MidiBuffer &midi) {
      AudioProcessingGate::Render render;
      if (!render) return;
      const auto start = juce::Time::getMillisecondCounterHiRes();
      processor->processBlock(audio, midi);
      const auto end = juce::Time::getMillisecondCounterHiRes();
      timing.record(start, end, audio.getNumSamples());
      PluginRenderDiagnostics::addBlockWork(end - start);
    }
  };

  using RuntimeRead = RealtimeObjectPublisher<Runtime>::ReadGuard;
  using LoadCompletion =
      std::function<void(bool success, const juce::String &error)>;

  explicit HostedPluginSlot(PluginSlotRole role);
  ~HostedPluginSlot();

  HostedPluginSlot(const HostedPluginSlot &) = delete;
  HostedPluginSlot &operator=(const HostedPluginSlot &) = delete;

  void setId(PluginSlotId id);
  [[nodiscard]] const PluginSlotId &id() const noexcept { return id_; }
  [[nodiscard]] PluginSlotRole role() const noexcept { return role_; }
  [[nodiscard]] HostedPluginStatus status() const noexcept { return status_; }
  [[nodiscard]] static juce::String statusName(HostedPluginStatus status);

  [[nodiscard]] bool hasProcessor() const noexcept;
  [[nodiscard]] int pluginUid() const noexcept { return pluginUid_; }
  [[nodiscard]] const juce::PluginDescription &description() const noexcept {
    return description_;
  }
  [[nodiscard]] const PluginCompatibility &compatibility() const noexcept {
    return compatibility_;
  }
  [[nodiscard]] const juce::String &lastError() const noexcept {
    return lastError_;
  }
  [[nodiscard]] const std::optional<MissingPluginState> &missingState() const {
    return missingState_;
  }

  void loadPlugin(const juce::PluginDescription &description,
                  juce::AudioPluginFormatManager &formatManager,
                  double sampleRate, int blockSize,
                  LoadCompletion onComplete = nullptr);

  /// Installs an already-created processor. This is also the seam used by
  /// deterministic tests and the isolated effect host.
  bool installProcessor(const juce::PluginDescription &description,
                        std::unique_ptr<juce::AudioProcessor> processor,
                        double sampleRate, int blockSize, juce::String &error);

  void markMissing(const juce::PluginDescription &description,
                   const juce::MemoryBlock &state, const juce::String &error);
  void unload();
  void reclaimRetiredRuntimes();

  void prepareToPlay(double sampleRate, int blockSize);
  [[nodiscard]] RuntimeRead readRuntime() const noexcept;
  [[nodiscard]] juce::AudioProcessor *activeProcessor() const noexcept;

  /// Process through the hosted processor. Effect bypass preserves input;
  /// instrument bypass emits silence.
  bool processBlock(juce::AudioBuffer<float> &audio, juce::MidiBuffer &midi);
  /// Message-thread diagnostics only; never enumerates programs or captures state.
  void appendTiming(juce::Array<juce::var> &rows, const juce::String &owner,
                    const juce::String &kind, double now);

  [[nodiscard]] juce::MemoryBlock cachedState() const;
  bool captureState(juce::MemoryBlock &destination) const;
  bool applyState(const void *data, int sizeInBytes);

  struct ProgramState {
    int index = -1;
    juce::MemoryBlock processorState;

    bool operator==(const ProgramState &other) const {
      return index == other.index && processorState == other.processorState;
    }
  };

  /// Capture a fresh complete state together with the current JUCE program.
  bool captureProgramState(ProgramState &destination);
  /// Select a validated program and capture the resulting complete state.
  bool selectProgram(int programIndex, ProgramState &result);
  /// Restore both parts without recreating the processor instance.
  bool restoreProgramState(const ProgramState &state);
  bool setProgram(int programIndex);
  void refreshStateCache();

  /// Stable fingerprint of user-facing parameter values. Unlike opaque
  /// getStateInformation() data, this excludes volatile runtime data that
  /// some plug-ins serialize while idle.
  [[nodiscard]] uint64_t parameterFingerprint() const;

  void setBypassed(bool bypassed) noexcept;
  [[nodiscard]] bool isBypassed() const noexcept;

  using EditorVisibilityCallback = std::function<void()>;
  void showEditor(const juce::String &title,
                  EditorVisibilityCallback visibilityChanged = {});
  void closeEditor();
  [[nodiscard]] bool isEditorVisible() const noexcept;

  /// AudioProcessorListener callbacks set this flag without allocating or
  /// calling UI code. Consume it from the message thread.
  [[nodiscard]] bool consumeChangeNotification() noexcept;

  /// True when the pending notification came from a parameter gesture ending
  /// in the plug-in editor. Unlike generic state-change notifications, this
  /// remains a reliable user edit even during playback.
  [[nodiscard]] bool consumeExplicitEditNotification() noexcept;

  /// True when the plug-in reported non-parameter persistent state changing.
  /// Some plug-ins also emit this during playback, so callers must interpret
  /// it using the current performance context.
  [[nodiscard]] bool consumeNonParameterStateChangeNotification() noexcept;

private:
  class ChangeListener final : public juce::AudioProcessorListener {
  public:
    explicit ChangeListener(HostedPluginSlot &owner) : owner_(owner) {}

    void audioProcessorParameterChanged(juce::AudioProcessor *processor, int,
                                        float) override;
    void audioProcessorChanged(
        juce::AudioProcessor *processor,
        const juce::AudioProcessorListener::ChangeDetails &details) override;
    void audioProcessorParameterChangeGestureEnd(
        juce::AudioProcessor *processor, int parameterIndex) override;

  private:
    HostedPluginSlot &owner_;
  };

  void noteProcessorChange(juce::AudioProcessor *processor) noexcept;
  void noteExplicitProcessorEdit(juce::AudioProcessor *processor) noexcept;
  void noteNonParameterStateChange(juce::AudioProcessor *processor) noexcept;
  bool configureBusLayout(juce::AudioProcessor &processor,
                          juce::String &error) const;
  void publishRuntime(std::unique_ptr<Runtime> runtime);

  PluginSlotRole role_;
  PluginSlotId id_;
  HostedPluginStatus status_ = HostedPluginStatus::empty;
  juce::PluginDescription description_;
  PluginCompatibility compatibility_;
  juce::String lastError_;
  std::optional<MissingPluginState> missingState_;
  int pluginUid_ = 0;
  uint64_t loadGeneration_ = 0;

  std::shared_ptr<std::atomic<bool>> alive_ =
      std::make_shared<std::atomic<bool>>(true);
  std::atomic<juce::AudioProcessor *> listenerProcessor_{nullptr};
  std::atomic<bool> changeNotificationPending_{false};
  std::atomic<bool> explicitEditNotificationPending_{false};
  std::atomic<bool> nonParameterStateChangePending_{false};
  std::atomic<bool> bypassed_{false};
  ChangeListener changeListener_{*this};

  juce::AudioProcessor *messageThreadProcessor_ = nullptr;
  juce::MemoryBlock cachedState_;
  std::unique_ptr<PluginEditorWindow> editorWindow_;
  EditorVisibilityCallback editorVisibilityChanged_;
  RealtimeObjectPublisher<Runtime> runtime_;
};

} // namespace fiddle
