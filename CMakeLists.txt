cmake_minimum_required(VERSION 3.22)

project(MidiLogger VERSION 1.0.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()


# Fetch JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG master # Use master for the latest version
)
FetchContent_MakeAvailable(JUCE)

# Fetch Protobuf
FetchContent_Declare(
    protobuf
    URL https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-all-21.12.tar.gz
    PATCH_COMMAND sed -i "" "s/cmake_minimum_required(VERSION 3.5)/cmake_minimum_required(VERSION 3.10)/" CMakeLists.txt
)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "Install protobuf" FORCE)
set(protobuf_BUILD_LIBPROTOC ON CACHE BOOL "Build libprotoc" FORCE)
FetchContent_MakeAvailable(protobuf)

FetchContent_Declare(
    angelscript
    URL https://www.angelcode.com/angelscript/sdk/files/angelscript_2.37.0.zip
    SOURCE_SUBDIR angelscript/projects/cmake
    PATCH_COMMAND sed -i "" "s/cmake_minimum_required(VERSION 3.5)/cmake_minimum_required(VERSION 3.10)/" angelscript/projects/cmake/CMakeLists.txt
)
FetchContent_MakeAvailable(angelscript)

# Protobuf targets in v21+ are in the protobuf:: namespace
# But since we are building from source, they might be just namespaced or local


# No separate include needed for 3.20.3 manual command


juce_add_plugin(MidiLogger
    COMPANY_NAME "Antigravity"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_CODE "MLog"
    FORMATS VST3
    PRODUCT_NAME "MidiLogger"
)

juce_add_gui_app(FiddleServer
    COMPANY_NAME "Antigravity"
    PRODUCT_NAME "FiddleServer"
    BUNDLE_ID "com.antigravity.fiddleserver"
)

juce_add_gui_app(FiddlePilot
    COMPANY_NAME "Antigravity"
    PRODUCT_NAME "FiddlePilot"
    BUNDLE_ID "com.antigravity.fiddlepilot"
)

target_sources(MidiLogger PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)

target_sources(FiddleServer PRIVATE
    Source/Server/Main.cpp
    Source/Server/MainComponent.cpp
    Source/Server/MainComponent.h
    Source/Server/MidiTcpServer.cpp
    Source/Server/MidiTcpServer.h
    Source/Server/NoteStreamTracker.h
    Source/Server/SubnoteGenerator.h
    Source/Server/ScriptEngine.cpp
    Source/Server/ScriptEngine.h
    Source/Server/ScriptBindings.cpp
    Source/Server/ScriptBindings.h
    "${angelscript_SOURCE_DIR}/add_on/scriptstdstring/scriptstdstring.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)

target_sources(FiddlePilot PRIVATE
    Source/Pilot/Main.cpp
    Source/Pilot/MainComponent.cpp
    Source/Pilot/MainComponent.h
    Source/Pilot/MidiPlayer.h
    Source/Pilot/PluginHost.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)

# Protobuf Generation (Manual for v21+)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
    COMMAND protoc --cpp_out="${CMAKE_CURRENT_BINARY_DIR}" -I="${CMAKE_CURRENT_SOURCE_DIR}/Source" "${CMAKE_CURRENT_SOURCE_DIR}/Source/midi_event.proto"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/midi_event.proto" protoc
)

target_link_libraries(MidiLogger PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_core
    libprotobuf
)

target_link_libraries(FiddleServer PRIVATE
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_data_structures
    juce::juce_core
    libprotobuf
    angelscript
)

target_link_libraries(FiddlePilot PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_core
    libprotobuf
)

target_compile_definitions(MidiLogger PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_definitions(FiddleServer PRIVATE
    JUCE_WEB_BROWSER=1
)

if(APPLE OR UNIX)
    target_compile_options(FiddleServer PRIVATE -fno-strict-aliasing)
endif()

target_include_directories(MidiLogger PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    Source
)

target_include_directories(FiddlePilot PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    Source
)

target_include_directories(FiddleServer PRIVATE 
    Source/Server 
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${angelscript_SOURCE_DIR}/angelscript/include"
    "${angelscript_SOURCE_DIR}/add_on/scriptstdstring"
)

juce_generate_juce_header(MidiLogger)
juce_generate_juce_header(FiddleServer)
juce_generate_juce_header(FiddlePilot)

# Post-build copy for scripts and UI
add_custom_command(TARGET FiddleServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    "$<TARGET_FILE_DIR:FiddleServer>/scripts"
)

add_custom_command(TARGET FiddleServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Server/ui/dist"
    "$<TARGET_FILE_DIR:FiddleServer>/ui"
)
