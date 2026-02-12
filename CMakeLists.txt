cmake_minimum_required(VERSION 3.22)

project(Fiddle VERSION 1.0.0)
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

# Protobuf targets in v21+ are in the protobuf:: namespace
# But since we are building from source, they might be just namespaced or local


# No separate include needed for 3.20.3 manual command


# Read version from version.txt
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" FIDDLE_VERSION)
string(STRIP "${FIDDLE_VERSION}" FIDDLE_VERSION)

# Re-configure when version.txt changes
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "version.txt")

juce_add_plugin(Fiddle
    VERSION "${FIDDLE_VERSION}"
    COMPANY_NAME "Antigravity"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_CODE "Fidl"
    FORMATS VST3
    PRODUCT_NAME "Fiddle"
)

juce_add_gui_app(FiddleServer
    VERSION "${FIDDLE_VERSION}"
    COMPANY_NAME "Antigravity"
    PRODUCT_NAME "FiddleServer"
    BUNDLE_ID "com.antigravity.fiddleserver"
)

juce_add_gui_app(FiddlePilot
    COMPANY_NAME "Antigravity"
    PRODUCT_NAME "FiddlePilot"
    BUNDLE_ID "com.antigravity.fiddlepilot"
)

target_sources(Fiddle PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.h"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/source/fobject.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/source/fdebug.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/source/updatehandler.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/source/fstring.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/thread/source/flock.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/base/funknown.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/base/ustring.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/base/coreiids.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/source/baseiids.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/public.sdk/source/vst/vstinitiids.cpp"
)

target_sources(FiddleServer PRIVATE
    Source/Server/Main.cpp
    Source/Server/MainComponent.cpp
    Source/Server/MainComponent.h
    Source/Server/DoricoConfigGenerator.cpp
    Source/Server/DoricoConfigGenerator.h
    Source/Server/DoricoSetupComponent.cpp
    Source/Server/DoricoSetupComponent.h
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
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.h"
)

# Copy expression map resource into FiddleServer app bundle
add_custom_command(TARGET FiddleServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/Fiddle_Universal.doricolib"
        "$<TARGET_FILE_DIR:FiddleServer>/../Resources/Fiddle_Universal.doricolib"
    COMMENT "Copying Fiddle_Universal.doricolib to FiddleServer bundle"
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
    "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.h"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/source/fobject.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/source/fdebug.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/source/updatehandler.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/source/fstring.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/thread/source/flock.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/base/funknown.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/base/ustring.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/base/coreiids.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/base/source/baseiids.cpp"
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK/public.sdk/source/vst/vstinitiids.cpp"
)

# Protobuf Generation (Manual for v21+)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.h" "${CMAKE_CURRENT_BINARY_DIR}/midi_event.pb.cc"
    COMMAND protoc --cpp_out="${CMAKE_CURRENT_BINARY_DIR}" -I="${CMAKE_CURRENT_SOURCE_DIR}/Source" "${CMAKE_CURRENT_SOURCE_DIR}/Source/midi_event.proto"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/midi_event.proto" protoc
)

target_link_libraries(Fiddle PRIVATE
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

target_compile_definitions(Fiddle PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_definitions(FiddleServer PRIVATE
    JUCE_WEB_BROWSER=1
)

if(APPLE OR UNIX)
    target_compile_options(FiddleServer PRIVATE -fno-strict-aliasing)
endif()

target_include_directories(Fiddle PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    Source
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK"
)

target_include_directories(FiddlePilot PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    Source
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK"
)

target_include_directories(FiddleServer PRIVATE 
    Source/Server 
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${angelscript_SOURCE_DIR}/angelscript/include"
    "${angelscript_SOURCE_DIR}/add_on/scriptstdstring"
)

juce_generate_juce_header(Fiddle)
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

