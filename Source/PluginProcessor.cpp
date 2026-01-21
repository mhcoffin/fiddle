#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "midi_event.pb.h"
#include <google/protobuf/text_format.h>
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
  juce::File logFile("/tmp/juce_midi_log.txt");
  logger = std::make_unique<juce::FileLogger>(logFile, "Fiddle Log File");

  tcpRelay = std::make_unique<fiddle::MidiTcpRelay>();
}

FiddleAudioProcessor::~FiddleAudioProcessor() {}

const juce::String FiddleAudioProcessor::getName() const { return "Fiddle"; }

bool FiddleAudioProcessor::acceptsMidi() const { return true; }

bool FiddleAudioProcessor::producesMidi() const { return false; }

bool FiddleAudioProcessor::isMidiEffect() const { return false; }

double FiddleAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int FiddleAudioProcessor::getNumPrograms() { return 1; }

int FiddleAudioProcessor::getCurrentProgram() { return 0; }

void FiddleAudioProcessor::setCurrentProgram(int index) {}

const juce::String FiddleAudioProcessor::getProgramName(int index) {
  return {};
}

void FiddleAudioProcessor::changeProgramName(int index,
                                             const juce::String &newName) {}

void FiddleAudioProcessor::prepareToPlay(double sampleRate,
                                         int samplesPerBlock) {}

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
  auto totalNumInputChannels = getTotalNumInputChannels();
  auto totalNumOutputChannels = getTotalNumOutputChannels();

  for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

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
      std::cerr << "[Fiddle] Transport START detected at host sample "
                << hostSamples << std::endl;
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
    auto message = metadata.getMessage();
    auto time = metadata.samplePosition;
    if (logger != nullptr) {
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
      } else if (message.isNoteOff()) {
        auto *noteOff = protoEvent.mutable_note_off();
        noteOff->set_note_number(message.getNoteNumber());
        noteOff->set_velocity(message.getVelocity());
      } else if (message.isController()) {
        auto *cc = protoEvent.mutable_cc();
        cc->set_controller_number(message.getControllerNumber());
        cc->set_controller_value(message.getControllerValue());
      } else if (message.isPitchWheel()) {
        auto *pb = protoEvent.mutable_pitch_bend();
        pb->set_value(message.getPitchWheelValue());
      } else if (message.isProgramChange()) {
        auto *pc = protoEvent.mutable_program_change();
        pc->set_program_number(message.getProgramChangeNumber());
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

      std::string output;
      if (google::protobuf::TextFormat::PrintToString(protoEvent, &output)) {
        logger->logMessage(output);
      }

      if (tcpRelay != nullptr) {
        std::cerr << "[Fiddle] Pushing event to relay: Ch "
                  << protoEvent.channel() << " Type: "
                  << (message.isNoteOn()    ? "NoteOn"
                      : message.isNoteOff() ? "NoteOff"
                                            : "Other")
                  << std::endl;
        tcpRelay->pushMessage(protoEvent);
      }
    }
  }
}

bool FiddleAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *FiddleAudioProcessor::createEditor() {
  return new FiddleAudioProcessorEditor(*this);
}

void FiddleAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {}

void FiddleAudioProcessor::setStateInformation(const void *data,
                                               int sizeInBytes) {}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new FiddleAudioProcessor();
}
