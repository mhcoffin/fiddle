#include "midi_event.pb.h"
#include <iostream>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

using namespace juce;

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: test_load <config_path>" << std::endl;
    return 1;
  }

  String configPath = argv[1];

  fiddle::MidiEvent event;
  event.set_timestamp_samples(0);
  auto *load = event.mutable_load_config();
  load->set_config_path(configPath.toStdString());

  std::string data;
  event.SerializeToString(&data);

  StreamingSocket socket;
  if (socket.connect("127.0.0.1", 5252, 1000)) {
    uint32_t len = (uint32_t)data.size();

    // Write JUCE little-endian format length (same as
    // MemoryOutputStream::writeInt)
    uint8_t header[4];
    header[0] = len & 0xff;
    header[1] = (len >> 8) & 0xff;
    header[2] = (len >> 16) & 0xff;
    header[3] = (len >> 24) & 0xff;

    socket.write(header, 4);
    socket.write(data.data(), (int)data.size());

    std::cout << "Successfully sent LoadConfigEvent for: " << configPath
              << std::endl;
    return 0;
  } else {
    std::cerr << "Failed to connect to FiddleServer on port 5252" << std::endl;
    return 1;
  }
}
