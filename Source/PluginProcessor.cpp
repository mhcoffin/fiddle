#include "PluginProcessor.h"
#include "InstrumentNames.h"
#include "PluginEditor.h"
#include "Vst3Extensions.h"
#include "midi_event.pb.h"
#include <fstream>
#include <iostream>
#include <string>

FiddleAudioProcessor::FiddleAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(
          BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
      )
#endif
{
  // Create Parameters with Legacy IDs if possible, or string IDs matching
  // Vst3Extensions expectations Note: JUCE hashing might result in different
  // IDs. For robustness, we'd need to match mapping. For now, assume "1001"
  // works or that the Host is smart enough if we name them "Bank MSB".

  // Bank MSB (CC 0)
  addParameter(bankMSBParam = new juce::AudioParameterInt(
                   juce::ParameterID("1001", 1), "Bank MSB", 0, 127, 0));

  // Bank LSB (CC 32)
  addParameter(bankLSBParam = new juce::AudioParameterInt(
                   juce::ParameterID("1002", 1), "Bank LSB", 0, 127, 0));

  vst3Extensions = std::make_unique<fiddle::FiddleVST3Extensions>(*this);
  tcpRelay = std::make_unique<fiddle::MidiTcpRelay>();
}

FiddleAudioProcessor::~FiddleAudioProcessor() {}

juce::VST3ClientExtensions *FiddleAudioProcessor::getVST3ClientExtensions() {
  return vst3Extensions.get();
}
// ... (getName, etc)

// Listener removed to enable raw MIDI test
void FiddleAudioProcessor::parameterValueChanged(int parameterIndex,
                                                 float newValue) {}
void FiddleAudioProcessor::parameterGestureChanged(int parameterIndex,
                                                   bool gestureIsStarting) {}

const juce::String FiddleAudioProcessor::getName() const { return "Fiddle"; }

bool FiddleAudioProcessor::acceptsMidi() const { return true; }

bool FiddleAudioProcessor::producesMidi() const { return false; }

bool FiddleAudioProcessor::isMidiEffect() const { return false; }

double FiddleAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int FiddleAudioProcessor::getNumPrograms() { return 128; }

int FiddleAudioProcessor::getCurrentProgram() { return 0; }

// Capture VST3 Program Changes (if host uses this instead of MIDI)
// Capture VST3 Program Changes (if host uses this instead of MIDI)
void FiddleAudioProcessor::setCurrentProgram(int index) {
  if (tcpRelay != nullptr && tcpRelay->isConnected()) {
    int channel = nextProgramChangeChannel;
    if (nextProgramChangeChannel < 16)
      nextProgramChangeChannel++;

    // Send Debug Log
    fiddle::MidiEvent debugEvent;
    debugEvent.set_timestamp_samples(0);
    debugEvent.mutable_other()->set_description(
        "Debug: VST3 setCurrentProgram(" + std::to_string(index) + ") -> Ch " +
        std::to_string(channel));
    tcpRelay->pushMessage(debugEvent);

    fiddle::MidiEvent protoEvent;
    protoEvent.set_timestamp_samples(0);
    protoEvent.set_channel(channel);
    auto *pc = protoEvent.mutable_program_change();
    pc->set_program_number(index);
    tcpRelay->pushMessage(protoEvent);
  }
}

const juce::String FiddleAudioProcessor::getProgramName(int index) {
  if (index >= 0 && index < 128)
    return "Program " + juce::String(index + 1);
  return {};
}

void FiddleAudioProcessor::changeProgramName(int index,
                                             const juce::String &newName) {}

void FiddleAudioProcessor::updateTrackProperties(
    const juce::AudioProcessor::TrackProperties &properties) {
  if (tcpRelay != nullptr && tcpRelay->isConnected() &&
      properties.name.has_value() && !properties.name->isEmpty()) {

    juce::String name = *properties.name;

    // Send Debug Log
    fiddle::MidiEvent debugEvent;
    debugEvent.set_timestamp_samples(0);
    debugEvent.mutable_other()->set_description(
        "Debug: updateTrackProperties Name='" + name.toStdString() + "'");
    tcpRelay->pushMessage(debugEvent);

    // Send Context Update
    fiddle::MidiEvent event;
    event.set_timestamp_samples(0);
    juce::String msg = "ContextUpdate: TrackName='" + name + "'";
    event.mutable_other()->set_description(msg.toStdString());
    tcpRelay->pushMessage(event);
  }
}

void FiddleAudioProcessor::prepareToPlay(double sampleRate,
                                         int samplesPerBlock) {
  // Reset channel counter so next setCurrentProgram sequence starts at ch 1
  nextProgramChangeChannel = 1;

  // Report the playback delay to the host (read from active_config.txt,
  // written by FiddleServer). This allows Dorico to compensate cursor position.
  cachedSampleRate_ = sampleRate;
  lastKnownDelayMs_ = getActiveServerDelay();
  setLatencySamples(static_cast<int>(sampleRate * lastKnownDelayMs_ / 1000.0));

  // Poll for delay changes every 1 second
  startTimer(1000);
}

void FiddleAudioProcessor::releaseResources() {}

bool FiddleAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
#if JucePlugin_IsMidiEffect
  juce::ignoreUnused(layouts);
  return true;
#else
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

#if !JucePlugin_IsSynth
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif

  return true;
#endif
}

void FiddleAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                        juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  // Pull audio from FiddleServer via lock-free shared memory
  audioSharedMemory_.pullAudio(buffer);

  // DEBUG LOGGING
  static std::ofstream debugFile;
  static bool debugFileOpened = false;
  if (!debugFileOpened) {
    debugFile.open("/tmp/fiddle_plugin_debug.log", std::ios::app);
    debugFileOpened = true;
    debugFile << "--- Plugin Process Block Started ---" << std::endl;
  }

  // Calculate peak amplitude to verify audio is arriving
  float peak = 0.0f;
  for (int c = 0; c < buffer.getNumChannels(); ++c) {
    if (auto *channelData = buffer.getReadPointer(c)) {
      for (int i = 0; i < buffer.getNumSamples(); ++i) {
        peak = std::max(peak, std::abs(channelData[i]));
      }
    }
  }

  static int logCounter = 0;
  if (debugFile.is_open() && ++logCounter % 50 == 0) {
    if (audioSharedMemory_.isReady()) {
      debugFile << "[Audio] Block size: " << buffer.getNumSamples()
                << " | SharedMem Ready: YES"
                << " | Peak Amp: " << peak << std::endl;
    } else {
      auto *map = audioSharedMemory_.getMemoryMap();
      auto file = audioSharedMemory_.getMapFile();
      bool fileExists = file.existsAsFile();
      bool hasDataPtr = map != nullptr && map->getData() != nullptr;

      uint64_t magic = 0;
      if (hasDataPtr) {
        auto *state =
            reinterpret_cast<fiddle::AudioSharedMemory::SharedState *>(
                map->getData());
        magic = state->magic.load(std::memory_order_acquire);
      }

      debugFile << "[Audio] SharedMem Ready: NO"
                << " | File Exists: " << (fileExists ? "YES" : "NO")
                << " | Map Ptr OK: " << (hasDataPtr ? "YES" : "NO")
                << " | Magic: 0x" << std::hex << magic << std::dec
                << " | Path: " << file.getFullPathName().toStdString()
                << std::endl;
    }
  }

  for (const auto metadata : midiMessages) {
    auto message = metadata.getMessage();

    // Log to file
    if (debugFile.is_open()) {
      const uint8_t *raw = message.getRawData();
      int len = message.getRawDataSize();

      debugFile << "Event: "
                << (message.isNoteOn()          ? "NoteOn"
                    : message.isNoteOff()       ? "NoteOff"
                    : message.isController()    ? "CC"
                    : message.isProgramChange() ? "PC"
                                                : "Other")
                << " Ch:" << message.getChannel()
                << " B1:" << (len > 1 ? (int)raw[1] : -1)
                << " B2:" << (len > 2 ? (int)raw[2] : -1) << std::endl;
    }
  }

  // Detect Transport Start and capture Host Position
  juce::Optional<juce::AudioPlayHead::PositionInfo> positionInfo;
  if (auto *playHead = getPlayHead()) {
    positionInfo = playHead->getPosition();
  }

  if (positionInfo.hasValue()) {
    bool isPlaying = positionInfo->getIsPlaying();
    auto hostSamplesOpt = positionInfo->getTimeInSamples();
    int64_t hostSamples = hostSamplesOpt.hasValue() ? *hostSamplesOpt : 0;

    if (isPlaying && !wasPlaying) {
      fiddle::MidiEvent transportEvent;
      transportEvent.set_timestamp_samples(0);
      transportEvent.set_host_sample_position((uint64_t)hostSamples);

      auto *transport = transportEvent.mutable_transport();
      transport->set_type(fiddle::MidiEvent_TransportEvent_Type_START);
      transport->set_host_sample_position((uint64_t)hostSamples);

      if (tcpRelay != nullptr) {
        tcpRelay->pushMessage(transportEvent);
      }
    }
    wasPlaying = isPlaying;
  }

  for (const auto metadata : midiMessages) {
    if (tcpRelay != nullptr) {
      auto message = metadata.getMessage();
      auto time = metadata.samplePosition;

      fiddle::MidiEvent protoEvent;
      protoEvent.set_timestamp_samples(time);
      protoEvent.set_channel(message.getChannel());

      if (positionInfo.hasValue()) {
        auto hostSamplesOpt = positionInfo->getTimeInSamples();
        int64_t hostSamples = hostSamplesOpt.hasValue() ? *hostSamplesOpt : 0;
        protoEvent.set_host_sample_position((uint64_t)(hostSamples + time));
      }

      if (message.isNoteOn()) {
        auto *noteOn = protoEvent.mutable_note_on();
        noteOn->set_note_number(message.getNoteNumber());
        noteOn->set_velocity(message.getVelocity());

        // (Test hack removed)
      } else if (message.isNoteOff()) {
        auto *noteOff = protoEvent.mutable_note_off();
        noteOff->set_note_number(message.getNoteNumber());
        noteOff->set_velocity(message.getVelocity());
      } else if (message.isController()) {
        auto *cc = protoEvent.mutable_cc();
        cc->set_controller_number(message.getControllerNumber());
        cc->set_controller_value(message.getControllerValue());

        // Track Bank Select messages for instrument detection
        int channelIndex = message.getChannel() - 1; // Convert to 0-based
        if (channelIndex >= 0 && channelIndex < 16) {
          if (message.getControllerNumber() == 0) {
            // Bank Select MSB (CC #0)
            channelStates[channelIndex].bankMSB = message.getControllerValue();
            DBG("[MIDI] Ch " + juce::String(message.getChannel()) +
                " Bank MSB = " + juce::String(message.getControllerValue()));
          } else if (message.getControllerNumber() == 32) {
            // Bank Select LSB (CC #32)
            channelStates[channelIndex].bankLSB = message.getControllerValue();
            DBG("[MIDI] Ch " + juce::String(message.getChannel()) +
                " Bank LSB = " + juce::String(message.getControllerValue()));
          }
        }
      } else if (message.isPitchWheel()) {
        auto *pb = protoEvent.mutable_pitch_bend();
        pb->set_value(message.getPitchWheelValue());
      } else if (message.isProgramChange()) {
        auto *pc = protoEvent.mutable_program_change();
        pc->set_program_number(message.getProgramChangeNumber());

        // Debug Log for MIDI PC
        fiddle::MidiEvent debugEvent;
        debugEvent.set_timestamp_samples(time);
        debugEvent.mutable_other()->set_description(
            "Debug: MIDI ProgramChange Ch" +
            std::to_string(message.getChannel()) + " Val" +
            std::to_string(message.getProgramChangeNumber()));
        tcpRelay->pushMessage(debugEvent);

        // Track program change and send instrument name update
        int channelIndex = message.getChannel() - 1; // Convert to 0-based
        if (channelIndex >= 0 && channelIndex < 16) {
          int program = message.getProgramChangeNumber();
          channelStates[channelIndex].program = program;

          DBG("[MIDI] Ch " + juce::String(message.getChannel()) +
              " Program Change = " + juce::String(program));

          // Look up instrument name using current bank and program
          juce::String instrumentName = fiddle::getInstrumentName(
              channelStates[channelIndex].bankMSB,
              channelStates[channelIndex].bankLSB, program);

          DBG("[MIDI] Instrument name: " + instrumentName);

          // Only send update if name changed
          if (instrumentName != channelStates[channelIndex].instrumentName) {
            channelStates[channelIndex].instrumentName = instrumentName;

            DBG("[MIDI] Sending ContextUpdate for Ch " +
                juce::String(message.getChannel()));

            // Send instrument name update to UI
            fiddle::MidiEvent contextEvent;
            juce::String contextInfo =
                "ContextUpdate: Index=" + juce::String(channelIndex) +
                ", Name='" + instrumentName + "'" + ", Namespace='MIDI'";
            contextEvent.mutable_other()->set_description(
                contextInfo.toStdString());
            tcpRelay->pushMessage(contextEvent);
          }
        }
      } else if (message.isAftertouch()) {
        auto *at = protoEvent.mutable_aftertouch();
        at->set_note_number(message.getNoteNumber());
        at->set_value(message.getAfterTouchValue());
      } else if (message.isChannelPressure()) {
        auto *cp = protoEvent.mutable_channel_pressure();
        cp->set_value(message.getChannelPressureValue());
      } else if (message.isSysEx()) {
        auto *sx = protoEvent.mutable_sys_ex();
        sx->set_data(message.getSysExData(), message.getSysExDataSize());
      } else {
        auto *other = protoEvent.mutable_other();
        other->set_description(message.getDescription().toStdString());
      }

      tcpRelay->pushMessage(protoEvent);
    }
  }
}

//==============================================================================
void FiddleAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
  // Save the current config path into the VST Host's project file
  auto xml = std::make_unique<juce::XmlElement>("FiddleState");
  xml->setAttribute("ConfigPath", currentConfigPath);
  copyXmlToBinary(*xml, destData);
}

void FiddleAudioProcessor::setStateInformation(const void *data,
                                               int sizeInBytes) {
  // Load the config path saved by the VST Host
  std::unique_ptr<juce::XmlElement> xmlState(
      getXmlFromBinary(data, sizeInBytes));
  if (xmlState != nullptr) {
    if (xmlState->hasTagName("FiddleState")) {
      juce::String savedPath = xmlState->getStringAttribute("ConfigPath", "");
      if (savedPath.isNotEmpty()) {
        setConfigPath(savedPath);
      }
    }
  }
}

void FiddleAudioProcessor::setConfigPath(const juce::String &path) {
  currentConfigPath = path;

  // Update relay so it announces this path on (re)connect
  if (tcpRelay != nullptr)
    tcpRelay->setConfigPath(path);

  // Fire IPC msg to FiddleServer immediately if connected
  if (tcpRelay != nullptr && tcpRelay->isConnected()) {
    fiddle::MidiEvent configEvent;
    configEvent.set_timestamp_samples(0);
    configEvent.mutable_load_config()->set_config_path(path.toStdString());
    tcpRelay->pushMessage(configEvent);
  }
}

bool FiddleAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *FiddleAudioProcessor::createEditor() {
  return new FiddleAudioProcessorEditor(*this);
}

// Test methods for debugging
int FiddleAudioProcessor::sendTestProgramChange() {
  if (tcpRelay != nullptr) {
    bool connected = tcpRelay->isConnected();
    if (connected) {
      fiddle::MidiEvent testEvent;
      testEvent.set_timestamp_samples(1000);
      testEvent.set_channel(1);
      auto *pc = testEvent.mutable_program_change();
      pc->set_program_number(40); // Violin
      tcpRelay->pushMessage(testEvent);
      return 0; // Success
    } else {
      return 1; // Disconnected
    }
  }
  return 2; // Null
}

int FiddleAudioProcessor::sendTestContextUpdate() {
  if (tcpRelay != nullptr) {
    bool connected = tcpRelay->isConnected();
    if (connected) {
      fiddle::MidiEvent contextEvent;
      contextEvent.set_timestamp_samples(1000);
      juce::String contextInfo =
          "ContextUpdate: Index=0, Name='TEST VIOLIN', Namespace='TEST'";
      contextEvent.mutable_other()->set_description(contextInfo.toStdString());
      tcpRelay->pushMessage(contextEvent);
      return 0; // Success
    } else {
      return 1; // Disconnected
    }
  }
  return 2; // Null
}

//==============================================================================
// function moved to top

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new FiddleAudioProcessor();
}
