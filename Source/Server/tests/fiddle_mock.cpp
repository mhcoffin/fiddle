/**
 * fiddle-mock — CLI tool for testing FiddleServer's state save/load path
 * without running Dorico.
 *
 * Commands:
 *   fiddle-mock save <file>     Read current state blob, write to file
 *   fiddle-mock load <file>     Send state blob to FiddleServer via TCP
 *   fiddle-mock inspect <file>  Print blob contents in human-readable form
 *
 * Protocols used:
 *   - Save: reads ~/Library/Caches/Fiddle/fiddle_state.bin (file-based
 *     "shared memory")
 *   - Load: connects to localhost:5252 and sends a protobuf MidiEvent
 *     with RestoreStateEvent containing the blob
 *   - Inspect: parses the binary blob format defined by StateManager
 */

#include "midi_event.pb.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------------------
// Utility: resolve the state file path
// ---------------------------------------------------------------------------

static std::string getStateFilePath() {
  const char *home = std::getenv("HOME");
  if (!home)
    home = "/tmp";
  return std::string(home) + "/Library/Caches/Fiddle/fiddle_state.bin";
}

// ---------------------------------------------------------------------------
// Utility: read a binary file into a vector
// ---------------------------------------------------------------------------

static bool readFile(const std::string &path, std::vector<uint8_t> &out) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    std::cerr << "Error: cannot open " << path << std::endl;
    return false;
  }
  ifs.seekg(0, std::ios::end);
  auto size = ifs.tellg();
  ifs.seekg(0, std::ios::beg);
  out.resize((size_t)size);
  ifs.read(reinterpret_cast<char *>(out.data()), size);
  return true;
}

static bool writeFile(const std::string &path,
                      const std::vector<uint8_t> &data) {
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs) {
    std::cerr << "Error: cannot write " << path << std::endl;
    return false;
  }
  ofs.write(reinterpret_cast<const char *>(data.data()), data.size());
  return true;
}

// ---------------------------------------------------------------------------
// SAVE: read state blob from shared memory file, write to output file
// ---------------------------------------------------------------------------

static int doSave(const std::string &outFile) {
  std::string stateFile = getStateFilePath();
  std::vector<uint8_t> data;
  if (!readFile(stateFile, data)) {
    std::cerr << "Error: cannot read " << stateFile << std::endl;
    std::cerr << "Is FiddleServer running?" << std::endl;
    return 1;
  }
  if (data.empty()) {
    std::cerr << "Error: state file is empty" << std::endl;
    return 1;
  }
  if (!writeFile(outFile, data)) {
    return 1;
  }
  std::cout << "Saved " << data.size() << " bytes to " << outFile << std::endl;
  return 0;
}

// ---------------------------------------------------------------------------
// INSPECT: parse and print blob contents
// ---------------------------------------------------------------------------

static int doInspect(const std::string &inFile) {
  std::vector<uint8_t> data;
  if (!readFile(inFile, data))
    return 1;

  if (data.size() < 12) {
    std::cerr << "Error: blob too small (" << data.size() << " bytes)"
              << std::endl;
    return 1;
  }

  const uint8_t *p = data.data();
  size_t offset = 0;
  size_t size = data.size();

  auto readU32 = [&]() -> uint32_t {
    if (offset + 4 > size)
      return 0;
    uint32_t val;
    std::memcpy(&val, p + offset, 4);
    offset += 4;
    return val;
  };

  auto readU8 = [&]() -> uint8_t {
    if (offset + 1 > size)
      return 0;
    return p[offset++];
  };

  auto readString = [&](uint32_t len) -> std::string {
    if (offset + len > size)
      return "<truncated>";
    std::string s(reinterpret_cast<const char *>(p + offset), len);
    offset += len;
    return s;
  };

  // Header
  uint32_t magic = readU32();
  uint32_t version = readU32();
  uint32_t totalSize = readU32();

  std::cout << "=== State Blob ===" << std::endl;
  std::cout << "Total size: " << data.size() << " bytes" << std::endl;
  std::cout << "Magic:      0x" << std::hex << magic << std::dec;
  if (magic == 0x46444C53)
    std::cout << " (FDLS ✓)";
  else
    std::cout << " (UNEXPECTED)";
  std::cout << std::endl;
  std::cout << "Version:    " << version << std::endl;
  std::cout << "Payload:    " << totalSize << " bytes" << std::endl;

  if (magic != 0x46444C53) {
    std::cerr << "Error: bad magic number" << std::endl;
    return 1;
  }
  if (version < 1 || version > 3) {
    std::cerr << "Warning: unknown blob version " << version << std::endl;
  }

  // Config name
  uint32_t cfgNameLen = readU32();
  std::string cfgName = readString(cfgNameLen);
  std::cout << "Config:     \"" << cfgName << "\"" << std::endl;

  // Config version (v2+)
  std::string cfgVersion;
  if (version >= 2) {
    uint32_t cfgVerLen = readU32();
    cfgVersion = readString(cfgVerLen);
    std::cout << "Version:    \"" << cfgVersion << "\"" << std::endl;
  }

  // Dirty flag
  uint8_t dirty = readU8();
  std::cout << "Dirty:      " << (dirty ? "yes" : "no") << std::endl;

  if (version >= 3) {
    uint32_t hashLen = readU32();
    std::string hash = readString(hashLen);
    std::cout << "StateHash:  " << hash << std::endl;

    uint32_t ancestorCount = readU32();
    std::cout << "Ancestors:  " << ancestorCount << std::endl;
    for (uint32_t i = 0; i < ancestorCount; ++i) {
      uint32_t ancestorLen = readU32();
      std::string ancestorHash = readString(ancestorLen);
      std::cout << "  -> " << ancestorHash << std::endl;
    }
  }

  // Strips
  uint32_t stripCount = readU32();
  std::cout << "Strips:     " << stripCount << std::endl;
  std::cout << std::endl;

  for (uint32_t i = 0; i < stripCount && offset < size; ++i) {
    uint32_t jsonLen = readU32();
    std::string jsonStr = readString(jsonLen);

    std::cout << "  Strip " << (i + 1) << ":" << std::endl;
    std::cout << "    JSON (" << jsonLen << " bytes): " << jsonStr << std::endl;

    // Plugin blob
    uint32_t blobSize = readU32();
    if (blobSize > 0 && offset + blobSize <= size) {
      offset += blobSize; // Skip binary plugin data
    }
    std::cout << "    Plugin state: " << blobSize << " bytes" << std::endl;
  }

  return 0;
}

static int doGenerate(const std::string &outFile) {
  std::vector<uint8_t> data;

  auto appendU32 = [&](uint32_t val) {
    uint8_t bytes[4];
    std::memcpy(bytes, &val, 4);
    data.insert(data.end(), bytes, bytes + 4);
  };
  auto appendU8 = [&](uint8_t val) { data.push_back(val); };
  auto appendStr = [&](const std::string &s) {
    appendU32((uint32_t)s.size());
    data.insert(data.end(), s.begin(), s.end());
  };

  appendU32(0x46444C53); // Magic
  appendU32(3);          // Version
  appendU32(0);          // Size placeholder
  appendStr("default");  // Config
  appendStr("mock123");  // Config Version
  appendU8(1);           // Dirty

  // Dummy V3 extensions: StateHash, Ancestors
  std::string fakeHash = "4e3fccbdc41645e7bc40b3beff909405";
  appendStr(fakeHash);
  appendU32(0); // 0 Ancestors

  appendU32(0); // 0 Strips

  // Patch size
  uint32_t total = (uint32_t)data.size();
  std::memcpy(data.data() + 8, &total, 4);

  if (!writeFile(outFile, data))
    return 1;
  std::cout << "Generated dummy v3 blob: " << total << " bytes" << std::endl;
  return 0;
}

// ---------------------------------------------------------------------------
// LOAD: send state blob to FiddleServer via TCP
// ---------------------------------------------------------------------------

static int doLoad(const std::string &inFile, const std::string &configName,
                  int port) {
  std::vector<uint8_t> data;
  if (!readFile(inFile, data))
    return 1;

  // Build a RestoreStateEvent protobuf message
  fiddle::MidiEvent msg;
  msg.set_timestamp_samples(0);
  auto *restore = msg.mutable_restore_state();
  restore->set_config_name(configName);
  restore->set_state_blob(data.data(), data.size());

  std::string serialized;
  if (!msg.SerializeToString(&serialized)) {
    std::cerr << "Error: failed to serialize protobuf" << std::endl;
    return 1;
  }

  // Connect to FiddleServer
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    std::cerr << "Error: socket() failed" << std::endl;
    return 1;
  }

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    std::cerr << "Error: cannot connect to localhost:" << port << std::endl;
    std::cerr << "Is FiddleServer running?" << std::endl;
    close(sock);
    return 1;
  }

  // MidiTcpServer wire format: 4-byte little-endian size + payload
  uint32_t payloadSize = htonl((uint32_t)serialized.size());
  if (write(sock, &payloadSize, 4) != 4 ||
      write(sock, serialized.data(), serialized.size()) !=
          (ssize_t)serialized.size()) {
    std::cerr << "Error: write failed" << std::endl;
    close(sock);
    return 1;
  }

  // Give the server a moment to process, then close
  usleep(100000); // 100ms
  close(sock);

  std::cout << "Sent " << data.size() << " byte blob to FiddleServer (config=\""
            << configName << "\", proto=" << serialized.size() << " bytes)"
            << std::endl;
  return 0;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

static void printUsage(const char *prog) {
  std::cout << "Usage:" << std::endl;
  std::cout << "  " << prog << " save <file>              "
            << "Read current state blob, write to file" << std::endl;
  std::cout << "  " << prog << " load <file> [config] [port]  "
            << "Send state blob to FiddleServer" << std::endl;
  std::cout << "  " << prog << " inspect <file>           "
            << "Print blob contents" << std::endl;
  std::cout << std::endl;
  std::cout << "Options for 'load':" << std::endl;
  std::cout << "  config  Config name (default: \"default\")" << std::endl;
  std::cout << "  port    TCP port (default: 5252)" << std::endl;
}

int main(int argc, char *argv[]) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  if (argc < 3) {
    printUsage(argv[0]);
    return 1;
  }

  std::string cmd = argv[1];
  std::string file = argv[2];

  int result;
  if (cmd == "save") {
    result = doSave(file);
  } else if (cmd == "inspect") {
    result = doInspect(file);
  } else if (cmd == "generate") {
    result = doGenerate(file);
  } else if (cmd == "load") {
    std::string config = (argc > 3) ? argv[3] : "default";
    int port = (argc > 4) ? std::atoi(argv[4]) : 5252;
    result = doLoad(file, config, port);
  } else {
    std::cerr << "Unknown command: " << cmd << std::endl;
    printUsage(argv[0]);
    result = 1;
  }

  google::protobuf::ShutdownProtobufLibrary();
  return result;
}
