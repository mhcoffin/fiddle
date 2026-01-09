#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "midi_event.pb.h"
#include <google/protobuf/text_format.h>
#include <string>

MidiLoggerAudioProcessor::MidiLoggerAudioProcessor()
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
  logger = std::make_unique<juce::FileLogger>(logFile, "MidiLogger Log File");

  tcpRelay = std::make_unique<fiddle::MidiTcpRelay>();
}

MidiLoggerAudioProcessor::~MidiLoggerAudioProcessor() {}

const juce::String MidiLoggerAudioProcessor::getName() const {
  return "MidiLogger";
}

bool MidiLoggerAudioProcessor::acceptsMidi() const { return true; }

bool MidiLoggerAudioProcessor::producesMidi() const { return false; }

bool MidiLoggerAudioProcessor::isMidiEffect() const { return false; }

double MidiLoggerAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int MidiLoggerAudioProcessor::getNumPrograms() { return 1; }

int MidiLoggerAudioProcessor::getCurrentProgram() { return 0; }

void MidiLoggerAudioProcessor::setCurrentProgram(int index) {}

const juce::String MidiLoggerAudioProcessor::getProgramName(int index) {
  return {};
}

void MidiLoggerAudioProcessor::changeProgramName(int index,
                                                 const juce::String &newName) {}

void MidiLoggerAudioProcessor::prepareToPlay(double sampleRate,
                                             int samplesPerBlock) {}

void MidiLoggerAudioProcessor::releaseResources() {}

bool MidiLoggerAudioProcessor::isBusesLayoutSupported(
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

void MidiLoggerAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                            juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  auto totalNumInputChannels = getTotalNumInputChannels();
  auto totalNumOutputChannels = getTotalNumOutputChannels();

  for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    buffer.clear(i, 0, buffer.getNumSamples());

  for (const auto metadata : midiMessages) {
    auto message = metadata.getMessage();
    auto time = metadata.samplePosition;
    if (logger != nullptr) {
      fiddle::MidiEvent protoEvent;
      protoEvent.set_timestamp_samples(time);
      protoEvent.set_channel(message.getChannel());

      if (message.isNoteOn()) {
        auto *noteOn = protoEvent.mutable_note_on();
        noteOn->set_note_number(message.getNoteNumber());
        noteOn->set_velocity(message.getFloatVelocity());
      } else if (message.isNoteOff()) {
        auto *noteOff = protoEvent.mutable_note_off();
        noteOff->set_note_number(message.getNoteNumber());
        noteOff->set_velocity(message.getFloatVelocity());
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
        std::cerr << "[MidiLogger] Pushing event to relay: Ch "
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

bool MidiLoggerAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *MidiLoggerAudioProcessor::createEditor() {
  return new MidiLoggerAudioProcessorEditor(*this);
}

void MidiLoggerAudioProcessor::getStateInformation(
    juce::MemoryBlock &destData) {}

void MidiLoggerAudioProcessor::setStateInformation(const void *data,
                                                   int sizeInBytes) {}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new MidiLoggerAudioProcessor();
}
