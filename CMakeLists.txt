cmake_minimum_required(VERSION 3.22)

project(Fiddle VERSION 1.0.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()


# Fetch JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.0
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

# Fetch Lua 5.4 source and build as a static library
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
endif()

# Build Lua as a static library (no CMakeLists.txt in official Lua)
file(GLOB LUA_CORE_SOURCES "${lua_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM LUA_CORE_SOURCES "${lua_SOURCE_DIR}/src/lua.c")   # standalone interpreter
list(REMOVE_ITEM LUA_CORE_SOURCES "${lua_SOURCE_DIR}/src/luac.c")  # compiler
add_library(lua_static STATIC ${LUA_CORE_SOURCES})
target_include_directories(lua_static PUBLIC "${lua_SOURCE_DIR}/src")
# Build Lua as C (not C++) — sol2 handles the extern "C" wrapping
target_compile_definitions(lua_static PRIVATE LUA_USE_MACOSX)

# Fetch sol2 (header-only C++/Lua binding)
# Use develop branch — v3.3.1 has a known optional_implementation.hpp
# bug with clang 18+ (ThePhD/sol2#1657).
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG develop
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(sol2)

# Find system SQLite3 (ships with macOS)
find_package(SQLite3 REQUIRED)

# Fetch the full VST3 SDK (JUCE's bundled copy lacks public.sdk/source/main/)
FetchContent_Declare(
    vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.7.12_build_20
    GIT_SHALLOW TRUE
)
# Don't use FetchContent_MakeAvailable — we don't want its CMake targets,
# just the source files. Use FetchContent_Populate instead.
FetchContent_GetProperties(vst3sdk)
if(NOT vst3sdk_POPULATED)
    FetchContent_Populate(vst3sdk)
endif()

# Fetch xxHash (header-only usage via XXH_INLINE_ALL)
FetchContent_Declare(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.2
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(xxhash)
if(NOT xxhash_POPULATED)
    FetchContent_Populate(xxhash)
endif()

# Protobuf targets in v21+ are in the protobuf:: namespace
# But since we are building from source, they might be just namespaced or local


# No separate include needed for 3.20.3 manual command


# Read version from version.txt
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" FIDDLE_VERSION)
string(STRIP "${FIDDLE_VERSION}" FIDDLE_VERSION)

# Re-configure when version.txt changes
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "version.txt")



juce_add_gui_app(FiddleServer
    VERSION "${FIDDLE_VERSION}"
    COMPANY_NAME "Antigravity"
    PRODUCT_NAME "FiddleServer"
    BUNDLE_ID "com.antigravity.fiddleserver"
    ICON_BIG "Source/Server/Resources/fiddle_icon.png"
)




target_sources(FiddleServer PRIVATE
    Source/Server/Main.cpp
    Source/Server/ApplicationRestart.cpp
    Source/Server/ApplicationRestart.h
    Source/Server/MainComponent.cpp
    Source/Server/ExpressionMapCommandService.cpp
    Source/Server/ExpressionMapCommandService.h
    Source/Server/ExpressionMapCommands.h
    Source/Server/ExpressionMapJsHandlers.cpp
    Source/Server/ExpressionMapJsHandlers.h
    Source/Server/MixerCommandService.cpp
    Source/Server/MixerCommandService.h
    Source/Server/MixerCommands.h
    Source/Server/MixerJsHandlers.cpp
    Source/Server/MixerJsHandlers.h
    Source/Server/PluginCommandService.cpp
    Source/Server/PluginCommandService.h
    Source/Server/PluginCommands.h
    Source/Server/PluginJsHandlers.cpp
    Source/Server/PluginJsHandlers.h
    Source/Server/MixerModel.cpp
    Source/Server/MixerStrip.cpp
    Source/Server/NoteInspection.cpp
    Source/Server/NoteInspection.h
    Source/Server/RealtimeMidiScheduler.cpp
    Source/Server/RealtimeMidiScheduler.h
    Source/RealtimeObjectPublisher.h
    Source/Server/MainComponent.h
    Source/Server/WebViewBridge.cpp
    Source/Server/WebViewBridge.h
    Source/Server/DoricoConfigGenerator.cpp
    Source/Server/DoricoConfigGenerator.h
    Source/Server/DoricoInstrumentBrowser.cpp
    Source/Server/DoricoInstrumentBrowser.h
    Source/Server/MasterInstrumentList.cpp
    Source/Server/MasterInstrumentList.h
    Source/Server/MidiTcpServer.cpp
    Source/Server/MidiTcpServer.h
    Source/Server/JsTestBridge.cpp
    Source/Server/JsTestBridge.h
    Source/Server/NoteStreamTracker.h
    Source/Server/SubnoteGenerator.h
    Source/Server/PluginScanner.h
    Source/Server/PluginEditorWindow.h
    Source/Server/MixerStrip.h
    Source/Server/MixerModel.h
    Source/Server/LuaPlugin.cpp
    Source/Server/LuaPlugin.h
    Source/Server/LuaAnnotator.h
    Source/Server/HarmonicState.h
    Source/Server/HarmonicAnalyzer.cpp
    Source/Server/HarmonicAnalyzer.h
    Source/Server/HarmonicAnalysisService.cpp
    Source/Server/HarmonicAnalysisService.h
    Source/Server/TonalContext.h
    Source/Server/AnnotatorChain.h
    Source/Server/FiddleConfig.h
    Source/Server/ExpressionMapParser.cpp
    Source/Server/ExpressionMapParser.h
    Source/Server/ExpressionMapLibrary.cpp
    Source/Server/ExpressionMapLibrary.h
    Source/Server/UndoManager.h
    Source/Server/UndoActions.h
    Source/Server/FiddleDatabase.cpp
    Source/Server/SqliteVersionStorage.h
    Source/Server/SqliteVersionStorage.cpp
    Source/Server/VersionTypes.h
    Source/Server/VersionStoreInterface.h
    Source/Server/VersionStore.h
    Source/Server/VersionStore.cpp
    Source/Server/StateManager.h
    Source/Server/StateManager.cpp
    Source/Server/StateBlobTypes.h
    Source/Server/StateBlobDeserializer.cpp
    Source/StateSharedMemory.h
    Source/Server/ConfigChooserWindow.h

    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.h"
)

# Copy expression map resource into FiddleServer app bundle
add_custom_command(TARGET FiddleServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/Fiddle_Universal.doricolib"
        "$<TARGET_FILE_DIR:FiddleServer>/../Resources/Fiddle_Universal.doricolib"
    COMMENT "Copying Fiddle_Universal.doricolib to FiddleServer bundle"
)


# Protobuf Generation (Manual for v21+)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
    COMMAND protoc --cpp_out="${CMAKE_CURRENT_BINARY_DIR}" -I="${CMAKE_CURRENT_SOURCE_DIR}/Source" "${CMAKE_CURRENT_SOURCE_DIR}/Source/midi_event.proto"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/midi_event.proto" protoc
)



target_link_libraries(FiddleServer PRIVATE
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_data_structures
    juce::juce_core
    juce::juce_audio_processors
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    libprotobuf
    lua_static
    sol2
    SQLite::SQLite3
)




target_compile_definitions(FiddleServer PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_PLUGINHOST_VST3=1
)

if(APPLE OR UNIX)
    target_compile_options(FiddleServer PRIVATE -fno-strict-aliasing)
endif()




target_include_directories(FiddleServer PRIVATE 
    Source/Server 
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${lua_SOURCE_DIR}/src"
    "${xxhash_SOURCE_DIR}"
)


juce_generate_juce_header(FiddleServer)


# Post-build copy for scripts and UI
add_custom_command(TARGET FiddleServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    "$<TARGET_FILE_DIR:FiddleServer>/scripts"
)

# Sync UI dist → app bundle. This custom target runs every build, so
# JS/CSS-only changes are picked up without touching C++ sources.
set(UI_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source/Server/ui/dist")
set(UI_BUNDLE_DIR "$<TARGET_FILE_DIR:FiddleServer>/ui")
set(UI_STAMP "${CMAKE_CURRENT_BINARY_DIR}/ui_copy.stamp")

add_custom_target(FiddleServerUI ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${UI_BUNDLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${UI_DIST_DIR}" "${UI_BUNDLE_DIR}"
    COMMENT "Syncing UI dist → FiddleServer bundle"
)
add_dependencies(FiddleServer FiddleServerUI)

# ==============================================================================
# FiddleNative — Native VST3 plugin with per-channel program changes
# ==============================================================================
set(VST3_SDK "${vst3sdk_SOURCE_DIR}")

add_library(FiddleNative MODULE
    # Our plugin sources
    Source/NativePlugin/FiddleFactory.cpp
    Source/NativePlugin/FiddleProcessor.cpp
    Source/NativePlugin/FiddleController.cpp
    Source/NativePlugin/FiddlePlugView.mm
    Source/NativePlugin/TcpRelay.cpp

    # Generated protobuf
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"

    # VST3 SDK sources needed for a native plugin
    "${VST3_SDK}/base/source/fobject.cpp"
    "${VST3_SDK}/base/source/fdebug.cpp"
    "${VST3_SDK}/base/source/updatehandler.cpp"
    "${VST3_SDK}/base/source/fstring.cpp"
    "${VST3_SDK}/base/source/fbuffer.cpp"
    "${VST3_SDK}/base/source/timer.cpp"
    "${VST3_SDK}/base/thread/source/flock.cpp"
    "${VST3_SDK}/pluginterfaces/base/funknown.cpp"
    "${VST3_SDK}/pluginterfaces/base/ustring.cpp"
    "${VST3_SDK}/pluginterfaces/base/coreiids.cpp"
    "${VST3_SDK}/base/source/baseiids.cpp"
    "${VST3_SDK}/public.sdk/source/vst/vstinitiids.cpp"
    "${VST3_SDK}/public.sdk/source/vst/vstcomponent.cpp"
    "${VST3_SDK}/public.sdk/source/vst/vstcomponentbase.cpp"
    "${VST3_SDK}/public.sdk/source/vst/vstaudioeffect.cpp"
    "${VST3_SDK}/public.sdk/source/vst/vstbus.cpp"
    "${VST3_SDK}/public.sdk/source/vst/vsteditcontroller.cpp"
    "${VST3_SDK}/public.sdk/source/vst/vstparameters.cpp"
    "${VST3_SDK}/public.sdk/source/common/pluginview.cpp"
    "${VST3_SDK}/public.sdk/source/common/commonstringconvert.cpp"
    "${VST3_SDK}/public.sdk/source/common/commoniids.cpp"
    "${VST3_SDK}/public.sdk/source/main/pluginfactory.cpp"
    "${VST3_SDK}/public.sdk/source/main/macmain.cpp"
)

target_include_directories(FiddleNative PRIVATE
    "${VST3_SDK}"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "Source/NativePlugin"
)

target_link_libraries(FiddleNative PRIVATE
    libprotobuf
)

target_compile_definitions(FiddleNative PRIVATE
    DEVELOPMENT=0
    RELEASE=1
)

target_compile_features(FiddleNative PRIVATE cxx_std_17)

if(APPLE)
    # Configure as a macOS .vst3 bundle
    set_target_properties(FiddleNative PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "vst3"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Fiddle"
        MACOSX_BUNDLE_BUNDLE_VERSION "${FIDDLE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${FIDDLE_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.antigravity.fiddle"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Source/NativePlugin/Info.plist.in"
        OUTPUT_NAME "Fiddle"
        PREFIX ""
        SUFFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/FiddleNative_artefacts/VST3"
    )

    target_link_libraries(FiddleNative PRIVATE
        "-framework CoreFoundation"
        "-framework AppKit"
    )

    # Copy moduleinfo.json into the bundle's Resources
    set(FIDDLE_VST3_BUNDLE "${CMAKE_BINARY_DIR}/FiddleNative_artefacts/VST3/Fiddle.vst3")
    add_custom_command(TARGET FiddleNative POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FIDDLE_VST3_BUNDLE}/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/Source/NativePlugin/moduleinfo.json"
            "${FIDDLE_VST3_BUNDLE}/Contents/Resources/moduleinfo.json"
        COMMENT "Copying moduleinfo.json into FiddleNative bundle"
    )
endif()

# ==============================================================================
# ExpressionMapTest — test executable for expression map parser
# ==============================================================================
juce_add_console_app(ExpressionMapTest
    PRODUCT_NAME "ExpressionMapTest"
)

target_sources(ExpressionMapTest PRIVATE
    Source/Server/tests/ExpressionMapParserTest.cpp
    Source/Server/tests/ExpressionMapAnnotatorTest.cpp
    Source/Server/ExpressionMapParser.cpp
    Source/Server/NoteInspection.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)

target_include_directories(ExpressionMapTest PRIVATE
    Source/Server
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(ExpressionMapTest PRIVATE
    juce::juce_core
    juce::juce_events
    libprotobuf
)

target_compile_features(ExpressionMapTest PRIVATE cxx_std_17)
juce_generate_juce_header(ExpressionMapTest)
add_test(NAME expression_maps COMMAND ExpressionMapTest)
set_tests_properties(expression_maps PROPERTIES
    LABELS stable
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# ==============================================================================
# VersionStoreTest — test executable for version DAG algorithms
# ==============================================================================
add_executable(VersionStoreTest
    Source/Server/tests/VersionStoreTest.cpp
    Source/Server/VersionStore.cpp
)

target_include_directories(VersionStoreTest PRIVATE
    Source/Server
    Source/Server/tests
    "${xxhash_SOURCE_DIR}"
)

target_compile_features(VersionStoreTest PRIVATE cxx_std_17)
add_test(NAME version_store COMMAND VersionStoreTest)
set_tests_properties(version_store PROPERTIES LABELS stable)

# ==============================================================================
# GainMathTest — test executable for power-model gain arithmetic
# ==============================================================================
add_executable(GainMathTest
    Source/Server/tests/GainMathTest.cpp
)

target_include_directories(GainMathTest PRIVATE
    Source/Server
)

target_compile_features(GainMathTest PRIVATE cxx_std_17)
add_test(NAME gain_math COMMAND GainMathTest)
set_tests_properties(gain_math PROPERTIES LABELS stable)

# ==============================================================================
# MixerJsHandlersTest — typed mixer command routing and payload validation
# ==============================================================================
add_executable(MixerJsHandlersTest
    Source/Server/tests/MixerJsHandlersTest.cpp
    Source/Server/MixerJsHandlers.cpp
)

target_include_directories(MixerJsHandlersTest PRIVATE
    Source/Server
)

target_link_libraries(MixerJsHandlersTest PRIVATE juce::juce_core)
target_compile_features(MixerJsHandlersTest PRIVATE cxx_std_17)

add_test(NAME mixer_js_handlers COMMAND MixerJsHandlersTest)
set_tests_properties(mixer_js_handlers PROPERTIES
    LABELS "mixer;regression;stable"
)

# ==============================================================================
# PluginJsHandlersTest — plugin catalog, assignment, and payload routing
# ==============================================================================
add_executable(PluginJsHandlersTest
    Source/Server/tests/PluginJsHandlersTest.cpp
    Source/Server/PluginJsHandlers.cpp
)

target_include_directories(PluginJsHandlersTest PRIVATE
    Source/Server
)

target_link_libraries(PluginJsHandlersTest PRIVATE juce::juce_core)
target_compile_features(PluginJsHandlersTest PRIVATE cxx_std_17)

add_test(NAME plugin_js_handlers COMMAND PluginJsHandlersTest)
set_tests_properties(plugin_js_handlers PROPERTIES
    LABELS "plugin;regression;stable"
)

# ==============================================================================
# ApplicationRestartTest — app-bundle discovery and safe helper invocation
# ==============================================================================
add_executable(ApplicationRestartTest
    Source/Server/tests/ApplicationRestartTest.cpp
    Source/Server/ApplicationRestart.cpp
)

target_include_directories(ApplicationRestartTest PRIVATE
    Source/Server
)

target_link_libraries(ApplicationRestartTest PRIVATE juce::juce_core)
target_compile_features(ApplicationRestartTest PRIVATE cxx_std_17)

add_test(NAME application_restart COMMAND ApplicationRestartTest)
set_tests_properties(application_restart PROPERTIES
    LABELS "application;regression;stable"
)

# ==============================================================================
# ExpressionMapJsHandlersTest — map catalog, assignment, and file routing
# ==============================================================================
add_executable(ExpressionMapJsHandlersTest
    Source/Server/tests/ExpressionMapJsHandlersTest.cpp
    Source/Server/ExpressionMapJsHandlers.cpp
)

target_include_directories(ExpressionMapJsHandlersTest PRIVATE
    Source/Server
)

target_link_libraries(ExpressionMapJsHandlersTest PRIVATE juce::juce_core)
target_compile_features(ExpressionMapJsHandlersTest PRIVATE cxx_std_17)

add_test(NAME expression_map_js_handlers COMMAND ExpressionMapJsHandlersTest)
set_tests_properties(expression_map_js_handlers PROPERTIES
    LABELS "expression-map;regression;stable"
)

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME mixer_ui_selection
        COMMAND "${NODE_EXECUTABLE}" --test
                "${CMAKE_CURRENT_SOURCE_DIR}/Source/Server/ui/tests/mixerSelection.test.js"
    )
    set_tests_properties(mixer_ui_selection PROPERTIES
        LABELS "mixer;ui;regression;stable"
    )

    add_test(NAME ui_preferences
        COMMAND "${NODE_EXECUTABLE}" --test
                "${CMAKE_CURRENT_SOURCE_DIR}/Source/Server/ui/tests/uiPreferences.test.js"
    )
    set_tests_properties(ui_preferences PROPERTIES
        LABELS "ui;accessibility;regression;stable"
    )

    add_test(NAME note_inspection_ui
        COMMAND "${NODE_EXECUTABLE}" --test
                "${CMAKE_CURRENT_SOURCE_DIR}/Source/Server/ui/tests/noteInspection.test.js"
    )
    set_tests_properties(note_inspection_ui PROPERTIES
        LABELS "ui;inspection;regression;stable"
    )

    add_test(NAME expression_map_picker_ui
        COMMAND "${NODE_EXECUTABLE}" --test
                "${CMAKE_CURRENT_SOURCE_DIR}/Source/Server/ui/tests/expressionMapCatalog.test.js"
    )
    set_tests_properties(expression_map_picker_ui PROPERTIES
        LABELS "ui;expression-map;regression;stable"
    )
endif()

# ==============================================================================
# HarmonicAnalyzerTest — test executable for HMM harmonic analysis engine
# ==============================================================================
add_executable(HarmonicAnalyzerTest
    Source/Server/tests/HarmonicAnalyzerTest.cpp
    Source/Server/HarmonicAnalyzer.cpp
)

target_include_directories(HarmonicAnalyzerTest PRIVATE
    Source/Server
)

target_compile_features(HarmonicAnalyzerTest PRIVATE cxx_std_20)
add_test(NAME harmonic_analyzer COMMAND HarmonicAnalyzerTest)
set_tests_properties(harmonic_analyzer PROPERTIES
    LABELS "harmonic;known-failure"
    WILL_FAIL TRUE
)

# ==============================================================================
# TransportTest — transport lifecycle and MIDI channel-state regression tests
# ==============================================================================
add_executable(TransportTest
    Source/Server/tests/TransportTest.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)

target_include_directories(TransportTest PRIVATE
    Source/Server
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(TransportTest PRIVATE
    juce::juce_core
    juce::juce_events
    libprotobuf
)

target_compile_features(TransportTest PRIVATE cxx_std_17)
add_test(NAME transport COMMAND TransportTest)
set_tests_properties(transport PROPERTIES LABELS stable)

# ==============================================================================
# ProgramStateReplayTest — verifies zero-based replay port numbering
# ==============================================================================
add_executable(ProgramStateReplayTest
    Source/NativePlugin/tests/ProgramStateReplayTest.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)

target_include_directories(ProgramStateReplayTest PRIVATE
    Source/NativePlugin
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(ProgramStateReplayTest PRIVATE libprotobuf)
target_compile_features(ProgramStateReplayTest PRIVATE cxx_std_17)

add_test(NAME program_state_replay COMMAND ProgramStateReplayTest)
set_tests_properties(program_state_replay PROPERTIES
    LABELS "regression;stable"
)

# ==============================================================================
# StateRestoreTest — verifies strip activation survives state restoration
# ==============================================================================
add_executable(StateRestoreTest
    Source/Server/tests/StateRestoreTest.cpp
    Source/Server/StateBlobDeserializer.cpp
)

target_include_directories(StateRestoreTest PRIVATE
    Source/Server
)

target_link_libraries(StateRestoreTest PRIVATE juce::juce_core)

target_compile_features(StateRestoreTest PRIVATE cxx_std_17)

add_test(NAME state_restore COMMAND StateRestoreTest)
set_tests_properties(state_restore PROPERTIES
    LABELS "regression;stable"
)

# ==============================================================================
# RealtimeQueueTest — bounded SPSC/MPSC handoff and concurrent delivery
# ==============================================================================
add_executable(RealtimeQueueTest
    Source/tests/RealtimeQueueTest.cpp
)

target_include_directories(RealtimeQueueTest PRIVATE Source)
target_compile_features(RealtimeQueueTest PRIVATE cxx_std_17)

add_test(NAME realtime_queues COMMAND RealtimeQueueTest)
set_tests_properties(realtime_queues PROPERTIES
    LABELS "realtime;regression;stable"
)

# ==============================================================================
# RealtimeMidiSchedulerTest — block timing, clear, and panic semantics
# ==============================================================================
add_executable(RealtimeMidiSchedulerTest
    Source/Server/tests/RealtimeMidiSchedulerTest.cpp
    Source/Server/RealtimeMidiScheduler.cpp
)

target_include_directories(RealtimeMidiSchedulerTest PRIVATE
    Source
    Source/Server
)
target_link_libraries(RealtimeMidiSchedulerTest PRIVATE juce::juce_audio_basics)
target_compile_features(RealtimeMidiSchedulerTest PRIVATE cxx_std_17)

add_test(NAME realtime_midi_scheduler COMMAND RealtimeMidiSchedulerTest)
set_tests_properties(realtime_midi_scheduler PROPERTIES
    LABELS "realtime;regression;stable"
)

# ==============================================================================
# AudioGraphSpikeTest — offline JUCE graph topology, latency, and realtime tests
# ==============================================================================
add_executable(AudioGraphSpikeTest
    Source/Server/tests/AudioGraphSpikeTest.cpp
)

target_link_libraries(AudioGraphSpikeTest PRIVATE
    juce::juce_audio_processors
    juce::juce_events
)
target_compile_features(AudioGraphSpikeTest PRIVATE cxx_std_17)

add_test(NAME audio_graph_spike COMMAND AudioGraphSpikeTest)
set_tests_properties(audio_graph_spike PROPERTIES
    LABELS "audio;realtime;regression;stable"
)

# ==============================================================================
# FiddleMock — CLI tool for testing Dorico save/load without Dorico
# ==============================================================================
add_executable(FiddleMock
    Source/Server/tests/fiddle_mock.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)

target_include_directories(FiddleMock PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"   # for midi_event.pb.h
)

target_link_libraries(FiddleMock PRIVATE
    libprotobuf
)

target_compile_features(FiddleMock PRIVATE cxx_std_17)
