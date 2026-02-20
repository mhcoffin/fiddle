import socket
import struct

# The protobuf message for MidiEvent with load_config 
# `string config_path = 1;` inside `LoadConfigEvent`
# `LoadConfigEvent load_config = 4;` inside `MidiEvent`

path = "/Users/mhc/fiddle/Orchestra_Template.yaml"
path_bytes = path.encode('utf-8')

# Build the embedded LoadConfigEvent message (tag 1: string)
# tag = (1 << 3) | 2 = 8 | 2 = 10 (0x0A)
load_config_msg = struct.pack("B", 0x0A) + struct.pack("B", len(path_bytes)) + path_bytes

# Build the parent MidiEvent message (tag 4: embedded message)
# tag = (4 << 3) | 2 = 32 | 2 = 34 (0x22)
midi_event_msg = struct.pack("B", 0x22) + struct.pack("B", len(load_config_msg)) + load_config_msg

length = len(midi_event_msg)

# Header is a little-endian 32-bit integer length, which JUCE uses for MemoryOutputStream::writeInt()
header = struct.pack(">I", length)

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect(('127.0.0.1', 5252))
    s.sendall(header + midi_event_msg)
    print("Injected raw Protobuf LoadConfigEvent over TCP successfully.")
