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
    Source/Server/AgentControlServer.cpp
    Source/Server/AgentControlServer.h
    Source/Server/AudioDeviceSettings.cpp
    Source/Server/RenderAheadEngine.cpp
    Source/Server/ApplicationRestart.h
    Source/Server/MainComponent.cpp
    Source/Server/ProjectRestoreService.cpp
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
    Source/Server/HostedPluginSlot.cpp
    Source/Server/HostedPluginSlot.h
    Source/Server/MasterAudioEngine.cpp
    Source/Server/MasterAudioEngine.h
    Source/Server/GroupBus.cpp
    Source/Server/GroupBus.h
    Source/Server/GroupBusActions.h
    Source/Server/GroupBusCommandService.cpp
    Source/Server/GroupBusCommandService.h
    Source/Server/GroupBusCommands.h
    Source/Server/GroupBusJsHandlers.cpp
    Source/Server/GroupBusJsHandlers.h
    Source/Server/MasterAudioCommandService.cpp
    Source/Server/MasterAudioCommandService.h
    Source/Server/MasterAudioCommands.h
    Source/Server/MasterAudioActions.h
    Source/Server/MasterAudioJsHandlers.cpp
    Source/Server/MasterAudioJsHandlers.h
    Source/Server/AudioInsertSnapshot.h
    Source/Server/StripAudioCommandService.cpp
    Source/Server/StripAudioCommandService.h
    Source/Server/StripAudioCommands.h
    Source/Server/StripAudioActions.h
    Source/Server/StripAudioJsHandlers.cpp
    Source/Server/StripAudioJsHandlers.h
    Source/Server/PluginCommands.h
    Source/Server/PluginJsHandlers.cpp
    Source/Server/PluginJsHandlers.h
    Source/Server/LibraryRoutingRepository.cpp
    Source/Server/LibraryRoutingRepository.h
    Source/Server/LibraryPatchPreviewHost.cpp
    Source/Server/LibraryPatchPreviewHost.h
    Source/Server/MixerModel.cpp
    Source/Server/MixerStrip.cpp
    Source/Server/StripAudioEngine.cpp
    Source/Server/StripAudioEngine.h
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

if(APPLE)
    target_sources(FiddleServer PRIVATE
        Source/Server/MacWindowMenu.mm
        Source/Server/MacWindowMenu.h
    )
endif()

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

# Build the Svelte UI when its sources change, then sync dist → app bundle.
# Keeping the build and copy as separate targets prevents a plain
# `cmake --build ... --target FiddleServer` from silently packaging stale UI.
set(UI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source/Server/ui")
set(UI_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source/Server/ui/dist")
set(UI_BUNDLE_DIR "$<TARGET_FILE_DIR:FiddleServer>/ui")
set(UI_BUILD_STAMP "${CMAKE_CURRENT_BINARY_DIR}/ui_build.stamp")
find_program(PNPM_EXECUTABLE pnpm)
if(PNPM_EXECUTABLE)
    file(GLOB_RECURSE UI_SOURCE_FILES CONFIGURE_DEPENDS
        "${UI_SOURCE_DIR}/src/*"
    )
    add_custom_command(
        OUTPUT "${UI_BUILD_STAMP}"
        COMMAND "${PNPM_EXECUTABLE}" run build
        COMMAND ${CMAKE_COMMAND} -E touch "${UI_BUILD_STAMP}"
        WORKING_DIRECTORY "${UI_SOURCE_DIR}"
        DEPENDS ${UI_SOURCE_FILES}
                "${UI_SOURCE_DIR}/package.json"
                "${UI_SOURCE_DIR}/pnpm-lock.yaml"
                "${UI_SOURCE_DIR}/vite.config.js"
        COMMENT "Building FiddleServer Svelte UI"
        VERBATIM
    )
    add_custom_target(FiddleServerUIBuild DEPENDS "${UI_BUILD_STAMP}")
endif()

add_custom_target(FiddleServerUI ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${UI_BUNDLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${UI_DIST_DIR}" "${UI_BUNDLE_DIR}"
    COMMENT "Syncing UI dist → FiddleServer bundle"
)
if(TARGET FiddleServerUIBuild)
    add_dependencies(FiddleServerUI FiddleServerUIBuild)
endif()
add_dependencies(FiddleServer FiddleServerUI)

# Build the local stdio MCP adapter with the official TypeScript SDK and ship
# the single bundled module inside FiddleServer.app. The adapter is launched by
# an MCP client and attaches to the running native app's loopback bridge.
set(MCP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mcp")
set(MCP_DIST_FILE "${MCP_SOURCE_DIR}/dist/fiddle-mcp.mjs")
set(MCP_BUILD_STAMP "${CMAKE_CURRENT_BINARY_DIR}/mcp_build.stamp")
set(MCP_BUNDLE_DIR "$<TARGET_FILE_DIR:FiddleServer>/../Resources/mcp")
if(PNPM_EXECUTABLE)
    file(GLOB_RECURSE MCP_SOURCE_FILES CONFIGURE_DEPENDS
        "${MCP_SOURCE_DIR}/src/*"
        "${MCP_SOURCE_DIR}/test/*"
    )
    add_custom_command(
        OUTPUT "${MCP_BUILD_STAMP}"
        COMMAND "${PNPM_EXECUTABLE}" run check
        COMMAND "${PNPM_EXECUTABLE}" run build
        COMMAND ${CMAKE_COMMAND} -E touch "${MCP_BUILD_STAMP}"
        WORKING_DIRECTORY "${MCP_SOURCE_DIR}"
        DEPENDS ${MCP_SOURCE_FILES}
                "${MCP_SOURCE_DIR}/package.json"
                "${MCP_SOURCE_DIR}/pnpm-lock.yaml"
        COMMENT "Building Fiddle MCP adapter"
        VERBATIM
    )
    add_custom_target(FiddleMcpBuild DEPENDS "${MCP_BUILD_STAMP}")
    add_custom_target(FiddleMcp ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${MCP_BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MCP_BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MCP_DIST_FILE}" "${MCP_BUNDLE_DIR}/fiddle-mcp.mjs"
        DEPENDS FiddleMcpBuild
        COMMENT "Syncing MCP adapter to FiddleServer bundle"
    )
    add_dependencies(FiddleServer FiddleMcp)
    add_test(NAME fiddle_mcp_adapter
        COMMAND "${PNPM_EXECUTABLE}" run test:ci)
    set_tests_properties(fiddle_mcp_adapter PROPERTIES
        WORKING_DIRECTORY "${MCP_SOURCE_DIR}"
        LABELS "agent;mcp;regression;stable"
    )
endif()

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

find_program(NODE_EXECUTABLE node REQUIRED)
# Discover every UI regression file, including newly added ones. These include
# pure model tests and source-contract checks; they are not browser interaction tests.
file(GLOB UI_TEST_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/Server/ui/tests/*.test.js")
foreach(ui_test IN LISTS UI_TEST_FILES)
    get_filename_component(ui_name "${ui_test}" NAME_WE)
    add_test(NAME "ui_${ui_name}" COMMAND "${NODE_EXECUTABLE}" --test "${ui_test}")
    set_tests_properties("ui_${ui_name}" PROPERTIES LABELS "ui;regression;stable")
endforeach()

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
# MidiTcpServerTest — duplicate plug-in connection diagnostics
# ==============================================================================
juce_add_console_app(MidiTcpServerTest
    PRODUCT_NAME "MidiTcpServerTest"
)

target_sources(MidiTcpServerTest PRIVATE
    Source/Server/tests/MidiTcpServerTest.cpp
    Source/Server/MidiTcpServer.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)

target_include_directories(MidiTcpServerTest PRIVATE
    Source/Server
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(MidiTcpServerTest PRIVATE
    juce::juce_core
    juce::juce_events
    libprotobuf
)
target_compile_features(MidiTcpServerTest PRIVATE cxx_std_17)
juce_generate_juce_header(MidiTcpServerTest)

add_test(NAME midi_tcp_server COMMAND MidiTcpServerTest)
set_tests_properties(midi_tcp_server PROPERTIES
    LABELS "connection;regression;stable"
)

# ==============================================================================
# AgentControlServerTest — authenticated loopback bridge for the MCP adapter
# ==============================================================================
juce_add_console_app(AgentControlServerTest
    PRODUCT_NAME "AgentControlServerTest"
)

target_sources(AgentControlServerTest PRIVATE
    Source/Server/tests/AgentControlServerTest.cpp
    Source/Server/AgentControlServer.cpp
)

target_include_directories(AgentControlServerTest PRIVATE Source/Server)
target_link_libraries(AgentControlServerTest PRIVATE
    juce::juce_core
    juce::juce_events
)
target_compile_features(AgentControlServerTest PRIVATE cxx_std_17)
juce_generate_juce_header(AgentControlServerTest)

add_test(NAME agent_control_server COMMAND AgentControlServerTest)
set_tests_properties(agent_control_server PROPERTIES
    LABELS "agent;mcp;connection;regression;stable"
)

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
# ProjectIdentityStateTest — Dorico branch/version state compatibility
# ==============================================================================
add_executable(ProjectIdentityStateTest
    Source/NativePlugin/tests/ProjectIdentityStateTest.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)

target_include_directories(ProjectIdentityStateTest PRIVATE
    Source/NativePlugin
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(ProjectIdentityStateTest PRIVATE libprotobuf)
target_compile_features(ProjectIdentityStateTest PRIVATE cxx_std_17)

add_test(NAME project_identity_state COMMAND ProjectIdentityStateTest)
set_tests_properties(project_identity_state PROPERTIES
    LABELS "persistence;versioning;regression;stable"
)

# ==============================================================================
# TcpRelaySaveTest — bounded Dorico save request/response correlation
# ==============================================================================
add_executable(TcpRelaySaveTest
    Source/NativePlugin/tests/TcpRelaySaveTest.cpp
    Source/NativePlugin/TcpRelay.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)

target_include_directories(TcpRelaySaveTest PRIVATE
    Source
    Source/NativePlugin
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(TcpRelaySaveTest PRIVATE libprotobuf)
target_compile_features(TcpRelaySaveTest PRIVATE cxx_std_17)

add_test(NAME tcp_relay_save COMMAND TcpRelaySaveTest)
set_tests_properties(tcp_relay_save PROPERTIES
    LABELS "persistence;versioning;regression;stable"
)

# ==============================================================================
# StateRestoreTest — verifies per-strip state survives restoration
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
add_executable(AudioDiagnosticsTest Source/tests/AudioDiagnosticsTest.cpp)
add_executable(RenderAheadTest Source/tests/RenderAheadTest.cpp)
target_include_directories(RenderAheadTest PRIVATE Source)
target_compile_features(RenderAheadTest PRIVATE cxx_std_17)
add_test(NAME render_ahead COMMAND RenderAheadTest)
set_tests_properties(render_ahead PROPERTIES LABELS "audio;realtime;regression;stable")
add_executable(StereoBufferOpsTest Source/tests/StereoBufferOpsTest.cpp)
add_executable(StereoBufferScalarTest Source/tests/StereoBufferOpsTest.cpp)
target_compile_definitions(StereoBufferScalarTest PRIVATE FIDDLE_DISABLE_AUDIO_SIMD=1)
foreach(stereo_test IN ITEMS StereoBufferOpsTest StereoBufferScalarTest)
    target_include_directories(${stereo_test} PRIVATE Source)
    target_compile_features(${stereo_test} PRIVATE cxx_std_17)
endforeach()
add_test(NAME stereo_buffer_simd COMMAND StereoBufferOpsTest)
add_test(NAME stereo_buffer_scalar COMMAND StereoBufferScalarTest)
set_tests_properties(stereo_buffer_simd stereo_buffer_scalar PROPERTIES LABELS "audio;realtime;regression;stable")
add_executable(StereoBufferBenchmark EXCLUDE_FROM_ALL Source/tests/StereoBufferBenchmark.cpp)
target_include_directories(StereoBufferBenchmark PRIVATE Source)
target_compile_features(StereoBufferBenchmark PRIVATE cxx_std_17)
target_include_directories(AudioDiagnosticsTest PRIVATE Source)
target_compile_features(AudioDiagnosticsTest PRIVATE cxx_std_17)
add_test(NAME audio_diagnostics COMMAND AudioDiagnosticsTest)
set_tests_properties(audio_diagnostics PROPERTIES LABELS "audio;realtime;regression;stable")

add_executable(AudioDeviceSettingsStoreTest Source/Server/tests/AudioDeviceSettingsStoreTest.cpp)
target_link_libraries(AudioDeviceSettingsStoreTest PRIVATE juce::juce_core)
target_compile_features(AudioDeviceSettingsStoreTest PRIVATE cxx_std_17)
add_test(NAME audio_device_settings_store COMMAND AudioDeviceSettingsStoreTest)
set_tests_properties(audio_device_settings_store PROPERTIES LABELS "audio;persistence;regression;stable")

add_executable(AudioDeviceSettingsTest Source/Server/tests/AudioDeviceSettingsTest.cpp Source/Server/AudioDeviceSettings.cpp)
target_link_libraries(AudioDeviceSettingsTest PRIVATE juce::juce_audio_utils)
target_compile_features(AudioDeviceSettingsTest PRIVATE cxx_std_17)
target_compile_definitions(AudioDeviceSettingsTest PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
add_test(NAME audio_device_settings COMMAND AudioDeviceSettingsTest)
set_tests_properties(audio_device_settings PROPERTIES LABELS "audio;persistence;regression;stable")

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
# StripAudioEngineTest — independent per-strip graph, gain, and isolation
# ==============================================================================
add_executable(StripAudioEngineTest
    Source/Server/tests/StripAudioEngineTest.cpp
    Source/Server/StripAudioEngine.cpp
    Source/Server/HostedPluginSlot.cpp
)

target_include_directories(StripAudioEngineTest PRIVATE Source/Server Source)
target_link_libraries(StripAudioEngineTest PRIVATE
    juce::juce_audio_processors
    juce::juce_events
    juce::juce_gui_basics
)
target_compile_definitions(StripAudioEngineTest PRIVATE JUCE_PLUGINHOST_VST3=1)
target_compile_features(StripAudioEngineTest PRIVATE cxx_std_17)

add_test(NAME strip_audio_engine COMMAND StripAudioEngineTest)

# GroupBusTest — submix gain/mute/meter behavior and published state
add_executable(GroupBusTest
    Source/Server/tests/GroupBusTest.cpp
    Source/Server/GroupBus.cpp
    Source/Server/StripAudioEngine.cpp
    Source/Server/HostedPluginSlot.cpp
)

target_include_directories(GroupBusTest PRIVATE Source/Server Source)
target_link_libraries(GroupBusTest PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_extra
)
target_compile_definitions(GroupBusTest PRIVATE JUCE_PLUGINHOST_VST3=1)
target_compile_features(GroupBusTest PRIVATE cxx_std_17)

add_test(NAME group_bus COMMAND GroupBusTest)
set_tests_properties(group_bus PROPERTIES
    LABELS "audio;graph;bus;realtime;regression;stable"
)

add_executable(GroupBusJsHandlersTest
    Source/Server/tests/GroupBusJsHandlersTest.cpp
    Source/Server/GroupBusJsHandlers.cpp
)
target_include_directories(GroupBusJsHandlersTest PRIVATE Source/Server)
target_link_libraries(GroupBusJsHandlersTest PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
)
target_compile_features(GroupBusJsHandlersTest PRIVATE cxx_std_17)
add_test(NAME group_bus_js_handlers COMMAND GroupBusJsHandlersTest)
set_tests_properties(group_bus_js_handlers PROPERTIES
    LABELS "audio;bus;ui;regression;stable"
)
set_tests_properties(strip_audio_engine PROPERTIES
    LABELS "audio;graph;mixer;realtime;regression;stable"
)

# ==============================================================================
# StripAudioJsHandlersTest — per-strip rack command and payload routing
# ==============================================================================
add_executable(StripAudioJsHandlersTest
    Source/Server/tests/StripAudioJsHandlersTest.cpp
    Source/Server/StripAudioJsHandlers.cpp
)

target_include_directories(StripAudioJsHandlersTest PRIVATE Source/Server)
target_link_libraries(StripAudioJsHandlersTest PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
)
target_compile_features(StripAudioJsHandlersTest PRIVATE cxx_std_17)

add_test(NAME strip_audio_js_handlers COMMAND StripAudioJsHandlersTest)
set_tests_properties(strip_audio_js_handlers PROPERTIES
    LABELS "audio;ui;regression;stable"
)

# ==============================================================================
# HostedPluginSlotTest — reusable host lifecycle, compatibility, and ownership
# ==============================================================================
add_executable(HostedPluginSlotTest
    Source/Server/tests/HostedPluginSlotTest.cpp
    Source/Server/HostedPluginSlot.cpp
)

target_include_directories(HostedPluginSlotTest PRIVATE Source/Server Source)
target_link_libraries(HostedPluginSlotTest PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
)
target_compile_definitions(HostedPluginSlotTest PRIVATE JUCE_PLUGINHOST_VST3=1)
target_compile_features(HostedPluginSlotTest PRIVATE cxx_std_17)

add_test(NAME hosted_plugin_slot COMMAND HostedPluginSlotTest)
set_tests_properties(hosted_plugin_slot PROPERTIES
    LABELS "audio;plugin;regression;stable"
)

# ==============================================================================
# MasterAudioEngineTest — ordered Master graph, bypass, gain, and latency
# ==============================================================================
add_executable(MasterAudioEngineTest
    Source/Server/tests/MasterAudioEngineTest.cpp
    Source/Server/MasterAudioEngine.cpp
    Source/Server/HostedPluginSlot.cpp
)

target_include_directories(MasterAudioEngineTest PRIVATE Source/Server Source)
target_link_libraries(MasterAudioEngineTest PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
)
target_compile_definitions(MasterAudioEngineTest PRIVATE JUCE_PLUGINHOST_VST3=1)
target_compile_features(MasterAudioEngineTest PRIVATE cxx_std_17)

add_test(NAME master_audio_engine COMMAND MasterAudioEngineTest)
set_tests_properties(master_audio_engine PROPERTIES
    LABELS "audio;plugin;graph;regression;stable"
)

# ==============================================================================
# MasterAudioJsHandlersTest — master panel command and payload routing
# ==============================================================================
add_executable(MasterAudioJsHandlersTest
    Source/Server/tests/MasterAudioJsHandlersTest.cpp
    Source/Server/MasterAudioJsHandlers.cpp
)

target_include_directories(MasterAudioJsHandlersTest PRIVATE Source/Server)
target_link_libraries(MasterAudioJsHandlersTest PRIVATE juce::juce_core)
target_compile_features(MasterAudioJsHandlersTest PRIVATE cxx_std_17)

add_test(NAME master_audio_js_handlers COMMAND MasterAudioJsHandlersTest)
set_tests_properties(master_audio_js_handlers PROPERTIES
    LABELS "audio;ui;regression;stable"
)

# ==============================================================================
# SqliteVersionStorageTest — durable version-state serialization and migration
# ==============================================================================
add_executable(SqliteVersionStorageTest
    Source/Server/tests/SqliteVersionStorageTest.cpp
    Source/Server/SqliteVersionStorage.cpp
)

target_include_directories(SqliteVersionStorageTest PRIVATE
    Source/Server
    "${xxhash_SOURCE_DIR}"
)
target_link_libraries(SqliteVersionStorageTest PRIVATE SQLite::SQLite3)
target_compile_features(SqliteVersionStorageTest PRIVATE cxx_std_17)

add_test(NAME sqlite_version_storage COMMAND SqliteVersionStorageTest)
set_tests_properties(sqlite_version_storage PROPERTIES
    LABELS "audio;persistence;regression;stable"
)

# ==============================================================================
# LibraryRoutingRepositoryTest — patch, chair, and layer persistence
# ==============================================================================
add_executable(LibraryRoutingRepositoryTest
    Source/Server/tests/LibraryRoutingRepositoryTest.cpp
    Source/Server/LibraryRoutingRepository.cpp
)

target_include_directories(LibraryRoutingRepositoryTest PRIVATE Source/Server)
target_link_libraries(LibraryRoutingRepositoryTest PRIVATE SQLite::SQLite3)
target_compile_features(LibraryRoutingRepositoryTest PRIVATE cxx_std_17)

add_test(NAME library_routing_repository COMMAND LibraryRoutingRepositoryTest)
set_tests_properties(library_routing_repository PROPERTIES
    LABELS "library;routing;persistence;regression;stable"
)

# ==============================================================================
# DoricoConfigGeneratorTest — sparse stable MIDI destinations in endpoint XML
# ==============================================================================
juce_add_console_app(DoricoConfigGeneratorTest
    PRODUCT_NAME "DoricoConfigGeneratorTest"
)

target_sources(DoricoConfigGeneratorTest PRIVATE
    Source/Server/tests/DoricoConfigGeneratorTest.cpp
    Source/Server/DoricoConfigGenerator.cpp
)

target_include_directories(DoricoConfigGeneratorTest PRIVATE Source/Server)
target_link_libraries(DoricoConfigGeneratorTest PRIVATE juce::juce_core)
target_compile_features(DoricoConfigGeneratorTest PRIVATE cxx_std_17)
juce_generate_juce_header(DoricoConfigGeneratorTest)

add_test(NAME dorico_config_generator COMMAND DoricoConfigGeneratorTest)
set_tests_properties(dorico_config_generator PROPERTIES
    LABELS "dorico;routing;regression;stable"
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

# Integration tests link the production mixer, command, and persistence code.
# They do not construct MainComponent, start a server, or scan installed VSTs.
juce_add_console_app(MixerIntegrationTest PRODUCT_NAME "MixerIntegrationTest")
target_sources(MixerIntegrationTest PRIVATE
    Source/Server/tests/MixerIntegrationTest.cpp
    Source/Server/LibraryPatchPreviewHost.cpp
    Source/Server/RenderAheadEngine.cpp
    Source/Server/ProjectRestoreService.cpp
    Source/Server/MixerCommandService.cpp
    Source/Server/MixerModel.cpp
    Source/Server/MixerStrip.cpp
    Source/Server/GroupBus.cpp
    Source/Server/GroupBusCommandService.cpp
    Source/Server/GroupBusJsHandlers.cpp
    Source/Server/StripAudioEngine.cpp
    Source/Server/MasterAudioEngine.cpp
    Source/Server/HostedPluginSlot.cpp
    Source/Server/RealtimeMidiScheduler.cpp
    Source/Server/NoteInspection.cpp
    Source/Server/LuaPlugin.cpp
    Source/Server/HarmonicAnalysisService.cpp
    Source/Server/HarmonicAnalyzer.cpp
    Source/Server/MasterInstrumentList.cpp
    Source/Server/ExpressionMapParser.cpp
    Source/Server/ExpressionMapLibrary.cpp
    Source/Server/ExpressionMapCommandService.cpp
    Source/Server/FiddleDatabase.cpp
    Source/Server/LibraryRoutingRepository.cpp
    Source/Server/SqliteVersionStorage.cpp
    Source/Server/VersionStore.cpp
    Source/Server/StateManager.cpp
    Source/Server/StateBlobDeserializer.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
)
target_include_directories(MixerIntegrationTest PRIVATE
    Source Source/Server "${CMAKE_CURRENT_BINARY_DIR}" "${xxhash_SOURCE_DIR}")
target_link_libraries(MixerIntegrationTest PRIVATE
    juce::juce_audio_utils juce::juce_gui_extra
    libprotobuf lua_static sol2 SQLite::SQLite3)
target_compile_definitions(MixerIntegrationTest PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_compile_features(MixerIntegrationTest PRIVATE cxx_std_20)
juce_generate_juce_header(MixerIntegrationTest)
option(FIDDLE_TEST_THREAD_SANITIZER "Instrument mixer/hosted-slot concurrency tests with TSan" OFF)
if(FIDDLE_TEST_THREAD_SANITIZER)
    foreach(safety_target MixerIntegrationTest HostedPluginSlotTest)
        target_compile_options(${safety_target} PRIVATE -fsanitize=thread -g)
        target_link_options(${safety_target} PRIVATE -fsanitize=thread)
    endforeach()
endif()
add_test(NAME mixer_integration COMMAND MixerIntegrationTest)
set_tests_properties(mixer_integration PROPERTIES
    LABELS "audio;routing;persistence;integration;regression;stable")

# One build target for local testing and CI. New *Test executables are included
juce_add_console_app(UndoManagerTest PRODUCT_NAME "UndoManagerTest")
target_sources(UndoManagerTest PRIVATE Source/Server/tests/UndoManagerTest.cpp)
target_include_directories(UndoManagerTest PRIVATE Source/Server)
target_link_libraries(UndoManagerTest PRIVATE juce::juce_core)
target_compile_features(UndoManagerTest PRIVATE cxx_std_17)
add_test(NAME undo_manager COMMAND UndoManagerTest)
set_tests_properties(undo_manager PROPERTIES LABELS "undo;regression;stable")

# One build target for local testing and CI. New *Test executables are included
# automatically, so the workflow cannot silently retain an obsolete target list.
get_property(fiddle_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
add_custom_target(FiddleTests)
foreach(test_target IN LISTS fiddle_targets)
    get_target_property(target_type "${test_target}" TYPE)
    if(target_type STREQUAL "EXECUTABLE" AND test_target MATCHES "Test$")
        add_dependencies(FiddleTests "${test_target}")
    endif()
endforeach()
if(TARGET FiddleMcpBuild)
    add_dependencies(FiddleTests FiddleMcpBuild)
endif()
get_property(fiddle_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${fiddle_tests} PROPERTIES TIMEOUT 120)
