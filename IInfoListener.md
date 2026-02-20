In Dorico, instruments are assigned to specific MIDI channels within your VST3 plugin's ports via the Play mode interface, endpoints, and playback templates. However, there is no direct, built-in mechanism for the host to push arbitrary instrument names to the plugin via MIDI events or parameters alone. Instead, you can leverage the VST3 SDK's ChannelContext::IInfoListener interface, which allows the host (like Dorico, as a Steinberg product) to inform your plugin about contextual details, including names associated with channels or buses.

### Implementing IInfoListener in Your Plugin
To receive this information:
1. In your plugin's edit controller (IEditController implementation), inherit from and implement Steinberg::Vst::ChannelContext::IInfoListener. This is an optional extension introduced in VST3.6.5, so ensure your SDK version supports it.
2. Override the `setChannelContextInfos(Steinberg::Vst::IAttributeList* list)` method. The host calls this whenever context changes occur (e.g., when a user assigns or renames an instrument in Dorico).
3. Within this method, query the provided IAttributeList for relevant attributes using `getString()` or `getInt()`. Key attributes include:
   - `kChannelNameKey`: Retrieves the channel name as a string (e.g., "Violin 1" or whatever Dorico assigns based on the instrument/player).
   - `kChannelIndexKey`: Gets the integer index of the channel or bus.
   - `kChannelIndexNamespaceKey`: String indicating the namespace (e.g., "EventIn" for MIDI input buses, helping distinguish ports).
   - `kChannelIndexNamespaceOrderKey`: Integer order within the namespace.
   - `kChannelColorKey`: Optional integer for channel color (if you want visual cues in your plugin's UI).
   Other keys like `kChannelUIDKey` (unique ID) or `kChannelPluginLocationKey` (plugin position in the signal chain) may also be useful.
4. Parse these to map the name to your specific port/channel. For example:
   - If the namespace is MIDI-related and the index aligns with one of your 64 ports, associate the name with that port's channels.
   - Store or forward this info to your app via TCP protobufs (e.g., send a message with port, channel, and name).
5. If the list is empty or keys are missing, fall back to defaults or log it— not all hosts populate every key, but Steinberg hosts like Dorico typically do for supported contexts.
6. Retain the list if needed by calling `addRef()` on it; otherwise, it will be released after the call.

### How This Works with Dorico
- When a user adds an instrument in Setup mode, Dorico routes it to a specific endpoint (port + channel) on your plugin.
- If your plugin exposes multiple event input buses (your 64 MIDI ports), Dorico can treat them as separate contexts.
- The IInfoListener callback provides per-context updates, so for multi-timbral setups, you may receive multiple calls with different indices/namespaces corresponding to assigned instruments.
- Test in Dorico 6 to confirm—it shares the audio engine with Cubase/Nuendo, which fully support this interface for track/channel contexts. If names aren't populating as expected, check your bus setup in `getBusCount()` and `getBusInfo()` to ensure ports are labeled meaningfully (e.g., "MIDI Port 1").

### Fallback if Names Aren't Sufficient
If IInfoListener doesn't provide per-channel granularity (e.g., if Dorico sends bus-level info only), use MIDI program changes as a secondary signal:
- Dorico sends MIDI program change events (via expression maps or playback templates) to select patches on channels.
- In your plugin's processEvent, watch for Event::kProgramChangeEvent on each channel.
- Map the program number (0-127) to an internal instrument name table (e.g., GM standard: 0 = Acoustic Grand Piano).
- Since you control the protobufs, define custom mappings in your app synced with Dorico's expression maps.

### Additional Tips
- Ensure your plugin declares support for IInfoListener via `queryInterface()` in the controller.
- For debugging, log attribute values in setChannelContextInfos.
- If your plugin is purely a MIDI router to the app, keep the VST3 implementation lightweight—focus on event forwarding and context callbacks.
- Reference the VST3 SDK samples (e.g., AGain) for IInfoListener examples.

This approach should allow your plugin to receive and relay instrument names dynamically without manual user intervention. If it doesn't work in testing, consider posting on the Steinberg developer forums for Dorico-specific confirmation.