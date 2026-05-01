void MainComponent::initExpressionMaps() {
  // Scan for expression maps in Dorico directories
    addInitMessage("Scanning expression maps...");
    xmapLibrary_.scanDefaultDirectories();
    safeCallAsync([this]() { initInstrumentBrowser(); });
}

void MainComponent::initInstrumentBrowser() {
  // Load Dorico instrument browser
    addInitMessage("Loading Dorico instruments...");
    if (instrumentBrowser_.loadFromDorico()) {
      pushLogMessage(
          "<b>[Setup]</b> Loaded " +
          juce::String((int)instrumentBrowser_.getInstruments().size()) +
          " instruments from Dorico");
    } else {
      pushLogMessage("<b>[Setup]</b> Could not load Dorico instruments", true);
    }
    safeCallAsync([this]() { initPlaceholder(); });
}

void MainComponent::initPlaceholder() {
  // Ensemble is now loaded from DB after db_.open() (init step 7+).
    // This step is kept as a placeholder for the splash screen message.
    addInitMessage("Loading instrument selections...");
    safeCallAsync([this]() { initLuaPlugins(); });
}

void MainComponent::initLuaPlugins() {
  addInitMessage("Initializing Lua plugin system...");
    // Scan bundled plugins (in app bundle next to executable)
    auto exeFile = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile);
    auto bundledDir = exeFile.getParentDirectory()
                          .getChildFile("scripts")
                          .getChildFile("plugins");
    if (bundledDir.isDirectory()) {
      luaCatalog_.scanDirectory(bundledDir.getFullPathName().toStdString());
    }
    // Scan user plugins directory
    luaCatalog_.scanDefaultDirectory();
    std::cerr << "[Init] Lua plugin system ready — "
              << luaCatalog_.plugins().size() << " plugins found"
              << std::endl;
    safeCallAsync([this]() { initExpressionMapLibrary(); });
}

void MainComponent::initExpressionMapLibrary() {
  // Load Expression Map from .doricolib
    addInitMessage("Loading expression map...");
    juce::File exeFile =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File doricolibFile =
        exeFile.getSiblingFile("Fiddle_Universal.doricolib");
    if (!doricolibFile.exists()) {
      doricolibFile = exeFile.getParentDirectory().getSiblingFile(
          "Resources/Fiddle_Universal.doricolib");
    }
    if (!doricolibFile.exists()) {
      juce::File projectRoot = exeFile;
      for (int i = 0; i < 10; ++i) {
        if (projectRoot.getChildFile("Source").isDirectory())
          break;
        projectRoot = projectRoot.getParentDirectory();
      }
      doricolibFile =
          projectRoot.getChildFile("resources/Fiddle_Universal.doricolib");
    }

    if (doricolibFile.exists()) {
      if (expressionMap.loadFromDoricolib(doricolibFile)) {
        pushLogMessage("<b>[ExpressionMap]</b> Loaded from " +
                       doricolibFile.getFileName());
        noteTracker.setExpressionMap(&expressionMap);
      } else {
        pushLogMessage("<b>[ExpressionMap]</b> Failed to parse " +
                           doricolibFile.getFileName(),
                       true);
      }
    } else {
      pushLogMessage("<b>[ExpressionMap]</b> Could not find "
                     "Fiddle_Universal.doricolib",
                     true);
    }
    // Wire metronome tracker unconditionally — prominence annotations
    // work even without an expression map.
    noteTracker.setMetronomeTracker(&metronomeTracker_);
    safeCallAsync([this]() { initMidiServer(); });
}

void MainComponent::initMidiServer() {
  // Set up note/MIDI callbacks, server, and start listening
    addInitMessage("Starting MIDI server...");
    {
      // Start the harmonic analysis service (HMM-based key/chord detection)
      // and wire it to the mixer. The service is owned by MainComponent;
      // MixerModel holds a raw ptr.
      harmonicService_.setBpm(currentBpm_.load(std::memory_order_relaxed));
      harmonicService_.setPlaybackDelayMs(mixer_.getPlaybackDelayMs());
      harmonicService_.buildDefaultEmissions();

      // Load trained transition matrix from bundle resources.
      {
        auto bundleDir = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getParentDirectory();
        auto transFile = bundleDir.getChildFile("resources/hmm/cpe_transitions.bin");
        if (!transFile.existsAsFile()) {
          // Also check project resources dir (for dev builds).
          // From MacOS dir, project root is 6 levels up:
          // MacOS → Contents → .app → Debug → FiddleServer_artefacts → build → project
          auto projectRoot = bundleDir;
          for (int i = 0; i < 6; ++i)
            projectRoot = projectRoot.getParentDirectory();
          auto projectResources = projectRoot.getChildFile("resources/hmm/cpe_transitions.bin");
          if (projectResources.existsAsFile())
            transFile = projectResources;
        }

        if (transFile.existsAsFile()) {
          juce::MemoryBlock data;
          transFile.loadFileAsData(data);
          if (harmonicService_.loadTransitionMatrix(data.getData(), data.getSize()))
            std::cerr << "[Init] Loaded transition matrix: " << transFile.getFullPathName() << std::endl;
          else
            std::cerr << "[Init] WARNING: Failed to parse transition matrix" << std::endl;
        } else {
          std::cerr << "[Init] No transition matrix found; using emission-only mode" << std::endl;
        }
      }

      mixer_.setHarmonicService(&harmonicService_);
      harmonicService_.start();
      std::cerr << "[Init] HarmonicAnalysisService started" << std::endl;

      // Wire metronome tempo tracker callback.
      // When the tracker detects a tempo change from the click track,
      // update the global BPM and broadcast to the UI.
      metronomeTracker_.onTempoChanged =
          [this](double bpm, int tsNum, int tsDen, uint64_t samplePos) {
        currentBpm_.store(bpm, std::memory_order_relaxed);
        harmonicService_.setBpm(bpm);

        juce::DynamicObject::Ptr tempoObj = new juce::DynamicObject();
        tempoObj->setProperty("bpm", bpm);
        tempoObj->setProperty("timeSigNumerator", tsNum);
        tempoObj->setProperty("timeSigDenominator", tsDen);
        tempoObj->setProperty("samplePosition", (juce::int64)samplePos);
        tempoObj->setProperty("source", juce::String("metronome"));
        juce::var tempoVar(tempoObj.get());
        safeCallAsync([this, tempoVar]() {
          broadcastMessage("setTempo", tempoVar);
        });
      };

      auto noteToJson = [](const fiddle::Note &n) {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("id", (juce::int64)n.id());
        obj->setProperty("noteNumber", (int)n.note_number());
        obj->setProperty("channel", (int)n.channel());
        obj->setProperty("port", (int)n.port());
        obj->setProperty("startVelocity", (int)n.start_velocity());
        obj->setProperty("startSample", (juce::int64)n.start_sample());
        obj->setProperty("durationSamples", (juce::int64)n.duration_samples());

        // Compute end velocity for UI display:
        // VELOCITY mode (short notes) → same as start velocity
        // CC mode (sustained notes) → last CC1 value from automation
        int endVel = (int)n.start_velocity();
        if (n.dynamics_mode() == fiddle::Note::CC) {
          auto it = n.cc_automation().find(1); // CC1
          if (it != n.cc_automation().end() && it->second.points_size() > 0) {
            endVel =
                (int)it->second.points(it->second.points_size() - 1).value();
          }
        }
        obj->setProperty("endVelocity", endVel);

        juce::DynamicObject::Ptr dims = new juce::DynamicObject();
        for (auto const &it : n.notation_dimensions()) {
          dims->setProperty(juce::String(it.first), it.second);
        }
        obj->setProperty("dimensions", dims.get());

        juce::DynamicObject::Ptr techs = new juce::DynamicObject();
        for (auto const &it : n.notation_techniques()) {
          techs->setProperty(juce::String(it.first), juce::String(it.second));
        }
        obj->setProperty("techniques", techs.get());

        juce::DynamicObject::Ptr defaults = new juce::DynamicObject();
        for (auto const &it : n.notation_is_default()) {
          defaults->setProperty(juce::String(it.first), it.second);
        }
        obj->setProperty("notation_is_default", defaults.get());

        obj->setProperty("beatProminence", (int)n.beat_prominence());

        return juce::JSON::toString(juce::var(obj.get()), true);
      };

      auto midiEventToJson = [](const fiddle::MidiEvent &event,
                                uint64_t absoluteSamples, int oldCCVal) {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("type", (int)event.event_case());
        obj->setProperty("channel", (int)event.channel());
        obj->setProperty("port", (int)event.port());
        obj->setProperty("timestamp", (juce::int64)absoluteSamples);

        if (event.has_note_on()) {
          obj->setProperty("note", (int)event.note_on().note_number());
          obj->setProperty("velocity", (int)event.note_on().velocity());
        } else if (event.has_note_off()) {
          obj->setProperty("note", (int)event.note_off().note_number());
          obj->setProperty("velocity", (int)event.note_off().velocity());
        } else if (event.has_cc()) {
          obj->setProperty("cc", (int)event.cc().controller_number());
          obj->setProperty("value", (int)event.cc().controller_value());
          if (oldCCVal >= 0)
            obj->setProperty("oldValue", oldCCVal);
        } else if (event.has_program_change()) {
          obj->setProperty("program",
                           (int)event.program_change().program_number());
        } else if (event.has_other()) {
          obj->setProperty("description",
                           juce::String(event.other().description()));
        } else if (event.has_transport()) {
          obj->setProperty("transportType", (int)event.transport().type());
        }

        return juce::JSON::toString(juce::var(obj.get()));
      };

      noteTracker.uiLogger = [this](const juce::String &msg) {
        pushLogMessage(msg);
      };

      noteTracker.setCallbacks(
          {[this, noteToJson](const fiddle::Note &n) {
             pushLogMessage("<b>[Tracker]</b> Note ON: " +
                            juce::String((juce::int64)n.id()) + " (Ch " +
                            juce::String((int)n.channel()) + ")");
             subnoteGenerator.onNoteStarted(n);

             double triggerTimeMs = juce::Time::getMillisecondCounterHiRes() +
                                    mixer_.getPlaybackDelayMs() - 40.0;
             // Route through annotator-aware path.
             // n.port() from Dorico is 0-based (matches strip inputPort).
             // n.channel() from Dorico is 1-based; strip inputChannel is
             // 0-based, so subtract 1.
             
             // Feed the harmonic analysis service.
             harmonicService_.onNoteEvent(
                 (int)n.note_number(), true, (int)n.start_velocity(),
                 juce::Time::getMillisecondCounterHiRes());

             int notePort = (int)n.port();
             // std::cerr << "[MainComponent] Routing Note ON (port=" << notePort
             //           << ", ch=" << (int)n.channel() - 1 << ")" << std::endl;
             // Mutable copy so annotators can populate pre_note/post_note
             fiddle::Note mutableNote(n);
             mixer_.routeAnnotatedNoteOn(notePort, (int)n.channel() - 1,
                                         mutableNote, triggerTimeMs);

             juce::var noteVar = juce::JSON::fromString(noteToJson(n));
             juce::DynamicObject::Ptr obj = new juce::DynamicObject();
             obj->setProperty("noteData", noteVar);
             obj->setProperty("status", juce::String("started"));
             safeCallAsync([this, obj]() {
               broadcastMessage("updateNoteState", juce::var(obj.get()));
             });
           },
           [this, noteToJson](const fiddle::Note &n) {
             pushLogMessage("<b>[Tracker]</b> Note OFF: " +
                            juce::String((juce::int64)n.id()));
             subnoteGenerator.onNoteEnded(n);

             double triggerTimeMs = juce::Time::getMillisecondCounterHiRes() +
                                    mixer_.getPlaybackDelayMs() - 40.0;
             // Route through annotator-aware path.
             
             // Feed the harmonic analysis service.
             harmonicService_.onNoteEvent(
                 (int)n.note_number(), false, 0,
                 juce::Time::getMillisecondCounterHiRes());

             int noteOffPort = (int)n.port();
             // std::cerr << "[MainComponent] Routing Note OFF (port=" << noteOffPort
             //           << ", ch=" << (int)n.channel() - 1 << ")" << std::endl;
             fiddle::Note mutableNote(n);
             mixer_.routeAnnotatedNoteOff(noteOffPort, (int)n.channel() - 1,
                                          mutableNote, triggerTimeMs);

             juce::var noteVar = juce::JSON::fromString(noteToJson(n));
             juce::DynamicObject::Ptr obj = new juce::DynamicObject();
             obj->setProperty("noteData", noteVar);
             obj->setProperty("status", juce::String("ended"));
             safeCallAsync([this, obj]() {
               broadcastMessage("updateNoteState", juce::var(obj.get()));
             });
           },
           [this, noteToJson](const fiddle::Note &n) {
             juce::var noteVar = juce::JSON::fromString(noteToJson(n));
             juce::DynamicObject::Ptr obj = new juce::DynamicObject();
             obj->setProperty("noteData", noteVar);
             obj->setProperty("status", juce::String("updated"));
             safeCallAsync([this, obj]() {
               broadcastMessage("updateNoteState", juce::var(obj.get()));
             });
           },
           [this, midiEventToJson](const fiddle::MidiEvent &event,
                                   uint64_t absoluteSamples, int oldCCVal) {
             // On transport stop, release all latched keyswitches
             if (event.has_transport() &&
                 event.transport().type() ==
                     fiddle::MidiEvent_TransportEvent_Type_STOP) {
               double triggerTimeMs =
                   juce::Time::getMillisecondCounterHiRes() +
                   mixer_.getPlaybackDelayMs() - 40.0;
               mixer_.releaseAllKeyswitches(triggerTimeMs);
               harmonicService_.onTransportStop();
             }

             // Live tempo from FiddleNative's ProcessContext.
             // Forward to the classifier so beat-duration weighting is accurate.
             if (event.has_tempo() && event.tempo().bpm() > 0.0) {
               double bpm = event.tempo().bpm();
               currentBpm_.store(bpm, std::memory_order_relaxed);
               harmonicService_.setBpm(bpm);
               std::cerr << "[Tempo] BPM=" << bpm
                         << " timeSig=" << event.tempo().time_sig_numerator()
                         << "/" << event.tempo().time_sig_denominator()
                         << std::endl;

               // Broadcast to the UI so the Timeline can align its beat grid.
               juce::DynamicObject::Ptr tempoObj = new juce::DynamicObject();
               tempoObj->setProperty("bpm", bpm);
               tempoObj->setProperty("timeSigNumerator",
                   event.tempo().time_sig_numerator());
               tempoObj->setProperty("timeSigDenominator",
                   event.tempo().time_sig_denominator());
               // absoluteSamples gives the exact sample position so the UI can
               // draw the marker at the correct x on the timeline.
               tempoObj->setProperty("samplePosition",
                   (juce::int64)absoluteSamples);
               juce::var tempoVar(tempoObj.get());
               safeCallAsync([this, tempoVar]() {
                 broadcastMessage("setTempo", tempoVar);
               });
             }

             juce::var eventVar = juce::JSON::fromString(
                 midiEventToJson(event, absoluteSamples, oldCCVal));
             safeCallAsync([this, eventVar]() {
               broadcastMessage("pushMidiEvent", eventVar);
             });
           }});

      subnoteGenerator.setCallbacks(
          {[this](const fiddle::Subnote &s) {
             pushSubnoteToWebView(s);
             safeCallAsync([this, id = s.id()]() {
               juce::DynamicObject::Ptr nd = new juce::DynamicObject();
               nd->setProperty("id", (juce::int64)id);
               juce::DynamicObject::Ptr obj = new juce::DynamicObject();
               obj->setProperty("noteData", juce::var(nd.get()));
               obj->setProperty("status", juce::String("subnote"));
               broadcastMessage("updateNoteState", juce::var(obj.get()));
             });
           },
           [this, noteToJson](const fiddle::Note &n) {
             pushLogMessage("<b>[Watchdog]</b> Note Timed Out: " +
                            juce::String((juce::int64)n.id()));
             juce::var noteVar = juce::JSON::fromString(noteToJson(n));
             juce::DynamicObject::Ptr obj = new juce::DynamicObject();
             obj->setProperty("noteData", noteVar);
             obj->setProperty("status", juce::String("ended"));
             safeCallAsync([this, obj]() {
               broadcastMessage("updateNoteState", juce::var(obj.get()));
             });
           }});

      server = std::make_unique<fiddle::MidiTcpServer>();
      server->onMessageReceived([this](const fiddle::MidiEvent &event) {
        // Force a log to the UI so we can see the flow
        pushLogMessage("<b>[Server]</b> Received Event Case: " +
                       juce::String((int)event.event_case()) +
                       " Ch: " + juce::String(event.channel()));

        // ── Metronome interception ────────────────────────────────────
        // The reserved metronome channel is port 0, channel 0 (0-based).
        // In the protobuf, channel is 1-based, so channel()==1 is ch 0.
        bool isMetronomeChannel =
            ((int)event.port() == kMetronomePort &&
             (int)event.channel() == kMetronomeChannel + 1);

        // ── Reset metronome tracker on transport stop ──────────────────
        // Transport events arrive on channel 0 (default), not on the
        // metronome channel, so this must be checked before the
        // channel-specific block.
        if (event.has_transport() &&
            event.transport().type() ==
                fiddle::MidiEvent_TransportEvent_Type_STOP) {
          metronomeTracker_.reset();
        }

        if (isMetronomeChannel) {
          // Feed note-on events to the tempo tracker
          if (event.has_note_on() && event.note_on().velocity() > 0) {
            uint64_t samplePos =
                event.has_host_sample_position()
                    ? event.host_sample_position()
                    : event.timestamp_samples();
            int noteNum = (int)event.note_on().note_number();
            metronomeTracker_.onBeat(noteNum, samplePos);

            // Broadcast each beat position to the UI so the Timeline
            // can use actual beat positions for its grid.
            bool isDownbeat = (noteNum == metronomeTracker_.downbeatNote);
            juce::DynamicObject::Ptr beatObj = new juce::DynamicObject();
            beatObj->setProperty("samplePosition",
                                 (juce::int64)samplePos);
            beatObj->setProperty("isDownbeat", isDownbeat);
            beatObj->setProperty("noteNumber", noteNum);
            beatObj->setProperty("velocity",
                                 (int)event.note_on().velocity());
            juce::var beatVar(beatObj.get());
            safeCallAsync([this, beatVar]() {
              broadcastMessage("metronomeBeat", beatVar);
            });
          }

          // Still push to WebView so click notes are visible in Timeline,
          // but do NOT route through noteTracker or mixer.
          pushEventToWebView(event);
          return; // Skip normal processing
        }

        noteTracker.processEvent(event);
        pushEventToWebView(event);

        // Record raw incoming MIDI for capture/comparison
        {
          double nowMs = juce::Time::getMillisecondCounterHiRes();
          int port = (int)event.port();
          int ch0 = (int)event.channel() - 1; // 0-based for strip matching
          if (event.has_note_on()) {
            mixer_.recordIncomingMidi(
                port, ch0, CapturedMidiEvent::NoteOn,
                (int)event.note_on().note_number(),
                (int)event.note_on().velocity(), nowMs);
          } else if (event.has_note_off()) {
            mixer_.recordIncomingMidi(
                port, ch0, CapturedMidiEvent::NoteOff,
                (int)event.note_off().note_number(),
                (int)event.note_off().velocity(), nowMs);
          } else if (event.has_cc()) {
            mixer_.recordIncomingMidi(
                port, ch0, CapturedMidiEvent::CC,
                (int)event.cc().controller_number(),
                (int)event.cc().controller_value(), nowMs);
          } else if (event.has_program_change()) {
            mixer_.recordIncomingMidi(
                port, ch0, CapturedMidiEvent::ProgramChange,
                (int)event.program_change().program_number(), 0, nowMs);
          }
        }

        // Forward CC events to VST plugins
        if (event.has_cc()) {
          int ch = event.channel();
          // event.port() from Dorico is 0-based (matches strip inputPort directly).
          // event.channel() from Dorico is 1-based; subtract 1 for strip inputChannel.
          int port = (int)event.port();
          int ccNum = event.cc().controller_number();
          int ccVal = event.cc().controller_value();

          // CC 120 (All Sound Off) or CC 123 (All Notes Off) → panic all strips
          if (ccNum == 120 || ccNum == 123) {
            mixer_.allNotesOff();
            std::cerr << "[MainComponent] Panic: CC " << ccNum
                      << " → allNotesOff on all strips" << std::endl;
          } else {
            // Route CC through annotator-aware path.
            // ch from Dorico is 1-based; JUCE controllerEvent also uses 1-16.
            juce::MidiMessage ccMsg =
                juce::MidiMessage::controllerEvent(ch, ccNum, ccVal);
            double triggerTimeMs =
                juce::Time::getMillisecondCounterHiRes() +
                mixer_.getPlaybackDelayMs() - 40.0;
            mixer_.routeAnnotatedCC(port, ch - 1, event, ccMsg,
                                    triggerTimeMs);
          }

          // ch is 1-based from protobuf — log it directly (no +1 needed)
          juce::String logMsg = "<b>[CC]</b> Ch " + juce::String(ch) +
                                " CC" + juce::String(ccNum) + " = " +
                                juce::String(ccVal);
          auto *dim = expressionMap.getDimensionForCC(ccNum);
          if (dim) {
            logMsg += " (" + dim->name;
            auto techIt = dim->techniques.find(ccVal);
            if (techIt != dim->techniques.end())
              logMsg += ": " + techIt->second;
            else
              logMsg += ": unknown value";
            logMsg += ")";
          }
          pushLogMessage(logMsg);
        }

        // Track program changes → instrument names for the UI
        if (event.has_program_change()) {
          int channel = event.channel();
          int program = event.program_change().program_number();
          std::string name =
              instrumentMapper_.handleProgramChange(channel, program);
          if (!name.empty()) {
            juce::DynamicObject::Ptr ci = new juce::DynamicObject();
            ci->setProperty("channel", channel);
            ci->setProperty("name", juce::String(name));
            safeCallAsync([this, ci]() {
              broadcastMessage("setChannelInstrument", juce::var(ci.get()));
            });
          }
        }

        // Dynamic Server Load Command (from VST Host)
        if (event.has_load_config()) {
          safeCallAsync([this]() {
            pushLogMessage("<b>[Host]</b> Plugin connected");
            pushConfigStatus();
          });
        }

        // Restore full state from Dorico (setStateInformation)
        if (event.has_restore_state()) {
          auto configName = juce::String(event.restore_state().config_name());
          auto &blobData = event.restore_state().state_blob();

          safeCallAsync([this, configName, blobData]() {
            auto restored =
                StateManager::deserializeBlob(blobData.data(), blobData.size());
            if (!restored) {
              pushLogMessage("<span style=\"color: red;\"><b>[Restore]</b> "
                             "Failed to deserialize state blob</span>");
              return;
            }

            pushLogMessage("<b>[Restore]</b> Restoring state from Dorico: '" +
                           configName + "' (" +
                           juce::String(restored->strips.size()) + " strips)");

            // Commit imported state to current branch
            // (strip-level data is committed below via saveAllStripsToDB)

            // Clear and rebuild mixer from blob
            mixer_.clear();
            undoManager_.clear();

            for (auto &rs : restored->strips) {
              juce::String newId = mixer_.addStrip();
              if (auto *strip = mixer_.getStrip(newId)) {
                strip->id = rs.id;
                strip->library = rs.library;
                strip->family = rs.family;
                strip->isSolo = rs.isSolo;
                strip->inputPort = rs.inputPort;
                strip->inputChannel = rs.inputChannel;
                strip->pluginUid = rs.pluginUid;
                strip->gainDb.store(rs.gainDb, std::memory_order_relaxed);
                setupStripListener(*strip);

                // Restore expression map
                if (!rs.expressionMapEntityID.empty()) {
                  auto xmapData = xmapLibrary_.load(rs.expressionMapEntityID);
                  if (xmapData)
                    strip->setExpressionMap(xmapData);
                }

                // Load plugin
                if (strip->pluginUid != 0) {
                  for (const auto &d :
                       pluginScanner_.getKnownPluginList().getTypes()) {
                    if (d.uniqueId == strip->pluginUid) {
                      auto stateBlock = rs.pluginState;
                      strip->loadPlugin(
                          d, mixer_.getFormatManager(),
                          [strip, stateBlock](bool success) {
                            if (success && stateBlock.getSize() > 0 &&
                                strip->pluginInstance) {
                              strip->pluginInstance->setStateInformation(
                                  stateBlock.getData(),
                                  (int)stateBlock.getSize());
                              strip->refreshPluginStateCache();
                            }
                          });
                      break;
                    }
                  }
                }

                // Restore Lua plugins
                for (const auto &fileName : rs.luaPluginFileNames) {
                  auto resolved = luaCatalog_.resolvePluginPath(fileName);
                  if (!resolved.empty()) {
                    auto plugin = std::make_shared<LuaPlugin>(resolved);
                    if (plugin->load())
                      strip->addLuaPlugin(std::move(plugin));
                  }
                }
              }
            }

            saveAllStripsToDB();
            pushMixerState(false);
            mixer_.syncStripsToInstruments(masterList_);
            pushMixerState(false);
            scheduleStateRebuild();

            pushLogMessage("<b>[Restore]</b> State restored successfully");
          });
        }

        lastSampleTime = event.timestamp_samples();
        lastSystemTime = juce::Time::getMillisecondCounter();
      });

      server->onConnectionChanged([this](bool connected, juce::String host) {
        safeCallAsync([this, connected, host]() {
          broadcastMessage("setConnectionState", connected);
          if (connected) {
            pushConfigStatus();
            pushLogMessage("<span style=\"color: #03dac6\">[Connected: " +
                           host + "]</span>");
          } else {
            pushLogMessage(
                "<span style=\"color: #cf6679\">[Disconnected]</span>");
          }
        });
      });

      server->onRawActivity([this](juce::String msg) {
        safeCallAsync(
            [this, msg]() { pushLogMessage("<small>" + msg + "</small>"); });
      });

      server->startThread();

      startTimer(20); // 20ms tick for subnotes
    }
    safeCallAsync([this]() { initAudioDevice(); });
}

void MainComponent::initAudioDevice() {
  // Initialize audio device for driving VST3 plugins
    addInitMessage("Initializing audio device...");
    {
      juce::String err = deviceManager.initialiseWithDefaultDevices(0, 2);
      if (err.isNotEmpty()) {
        std::cerr << "[Audio] Failed to initialize device manager: " << err
                  << std::endl;
      } else {
        deviceManager.addAudioCallback(this);
      }
    }
    safeCallAsync([this]() { initDatabase(); });
}

void MainComponent::initDatabase() {
  addInitMessage("Opening database...");

    // Open SQLite database
    auto dbFile = FiddleConfig::getAppDataDir().getChildFile("fiddle.db");
    db_.open(dbFile);

    // Create the VersionStore backed by the database's SqliteVersionStorage.
    // This must happen immediately after db_.open() so that all downstream
    // code (pushDagHistory, pushBranches, stateManager) can use it.
    if (auto *storage = db_.getVersionStorage()) {
      versionStore_ = std::make_unique<versioning::VersionStore>(*storage);

      // On first run, the branches table is empty — seed with an empty root.
      if (versionStore_->getStorage().listBranches().empty()) {
        versionStore_->initializeEmpty();
        std::cerr << "[MainComponent] VersionStore seeded with empty root"
                  << std::endl;
      } else {
        std::cerr << "[MainComponent] VersionStore loaded ("
                  << versionStore_->getStorage().listBranches().size()
                  << " branches)" << std::endl;
      }

      // ── Dedup guard: remove stale duplicate-named branches ───────────────
      // This can accumulate during development if the DB is partially reset
      // without wiping the branches table. For each duplicate name, keep the
      // branch whose head version is most recent (highest created_at); delete
      // the others.
      {
        auto allBranches = versionStore_->getStorage().listBranches();
        // Map name → (branchId, headId, createdAt)
        std::map<std::string, std::tuple<versioning::BranchId,
                                         versioning::VersionId, std::string>>
            bestByName;

        for (const auto &[bid, name, headId] : allBranches) {
          std::string headCreatedAt;
          if (auto ver = versionStore_->getStorage().getVersion(headId))
            headCreatedAt = ver->createdAt;

          auto it = bestByName.find(name);
          if (it == bestByName.end()) {
            bestByName[name] = {bid, headId, headCreatedAt};
          } else {
            // Keep whichever head is newer
            const std::string &existingTs = std::get<2>(it->second);
            if (headCreatedAt > existingTs) {
              // Current entry is older — delete it
              std::cerr << "[MainComponent] Dedup: removing stale branch '"
                        << name << "' id=" << std::get<0>(it->second)
                        << std::endl;
              versionStore_->deleteBranch(std::get<0>(it->second));
              it->second = {bid, headId, headCreatedAt};
            } else {
              // New entry is older — delete it
              std::cerr << "[MainComponent] Dedup: removing stale branch '"
                        << name << "' id=" << bid << std::endl;
              versionStore_->deleteBranch(bid);
            }
          }
        }
      }

      // ── Orphan guard: remove branches whose head version is claimed by a
      // different branch. This catches the case where a stale branch record
      // points to a version whose branchId is already owned by another branch.
      {
        auto allBranches = versionStore_->getStorage().listBranches();
        for (const auto &[bid, name, headId] : allBranches) {
          auto ver = versionStore_->getStorage().getVersion(headId);
          if (ver && !ver->branchId.empty() && ver->branchId != bid) {
            std::cerr << "[MainComponent] Orphan branch '" << name
                      << "' (id=" << bid << "): head version belongs to branch "
                      << ver->branchId << " — removing record only."
                      << std::endl;
            // Only delete the branch record; its versions belong to another
            // branch and must not be removed.
            versionStore_->getStorage().deleteBranch(bid);
          }
        }
      }

      // Wire the version store into the state manager so it embeds ancestor
      // hashes in the Dorico blob.
      stateManager_.setVersionStore(versionStore_.get());

      // Track the current branch (default to first branch, i.e. "Main").
      auto allBranches = versionStore_->getStorage().listBranches();
      if (!allBranches.empty()) {
        currentBranchId_ = std::get<0>(allBranches[0]);
        stateManager_.setCurrentBranchId(currentBranchId_);

        // Initialise currentVersionId_ so the History window can highlight
        // the loaded version immediately on first open.
        auto headOpt = versionStore_->getBranchHead(currentBranchId_);
        if (headOpt)
          currentVersionId_ = *headOpt;
      }
    } else {
      std::cerr << "[MainComponent] WARNING: VersionStore not available — "
                   "DAG history will be disabled"
                << std::endl;
    }

    // Now that the DB is open, restore debug window geometry + visibility.
    if (debugWindow_) {
      auto dws = db_.loadWindowSettings("debug");
      std::cerr << "[DebugWindow] Loaded settings: visible="
                << (dws.visible ? "true" : "false") << std::endl;
      if (dws.width > 0 && dws.height > 0) {
        debugWindow_->restoreGeometry(dws.x, dws.y, dws.width, dws.height,
                                      dws.visible);
      }
      // If no saved geometry, window stays hidden (constructor default).
    }

    // Restore history window if it was previously visible.
    // Deferred to the message loop so the main WebView is fully initialised.
    {
      auto hws = db_.loadWindowSettings("history");
      if (hws.visible) {
        safeCallAsync([this, hws]() {
          if (!historyWindow_ && versionStore_) {
            historyWindow_ =
                std::make_unique<HistoryWindow>(createWebOptions());
            historyWindowLoaded_ = false;
            juce::String root =
                juce::WebBrowserComponent::getResourceProviderRoot();
            historyWindow_->getWebView().goToURL(root +
                                                 "index.html?view=history");
            if (hws.width > 0 && hws.height > 0) {
              historyWindow_->restoreGeometry(hws.x, hws.y, hws.width,
                                              hws.height, true);
            } else {
              historyWindow_->setVisible(true);
            }
            std::cerr << "[HistoryWindow] Restored from saved settings"
                      << std::endl;
          }
        });
      }
    }


    // Restore library manager window if it was previously visible.
    {
      auto lws = db_.loadWindowSettings("library");
      if (lws.visible) {
        safeCallAsync([this, lws]() {
          if (!libraryManagerWindow_) {
            libraryManagerWindow_ =
                std::make_unique<LibraryManagerWindow>(createWebOptions());
            libraryManagerWindowLoaded_ = false;
            juce::String root =
                juce::WebBrowserComponent::getResourceProviderRoot();
            libraryManagerWindow_->getWebView().goToURL(
                root + "index.html?view=library");
            libraryManagerWindow_->setGeometrySaver(
                [this]() { saveLibraryManagerWindowGeometry(); });
            if (lws.width > 0 && lws.height > 0) {
              libraryManagerWindow_->restoreGeometry(lws.x, lws.y, lws.width,
                                                     lws.height, true);
            } else {
              libraryManagerWindow_->setVisible(true);
            }
            std::cerr << "[LibraryManager] Restored from saved settings"
                      << std::endl;
          }
        });
      }
    }

    // Auto-scan for plugins on startup (incremental — uses plugin_cache).
    std::cerr << "[Startup] Auto-scanning for VST3 plugins..." << std::endl;

    // Now that the DB is open, restore main window geometry.
    {
      auto bounds = restoreMainWindowGeometry();
      if (auto *topLevel = getTopLevelComponent()) {
        topLevel->setBounds(bounds);
      }
    }

    // Load ensemble from DB (with one-time migration from legacy JSON).
    if (masterList_.loadFromDB(db_)) {
      // Load stable channel assignments (first run: assigns sequentially)
      masterList_.reconcileAssignments(db_);
      pushLogMessage("<b>[Setup]</b> Loaded " +
                     juce::String(masterList_.size()) + " saved instruments");
    }

    // Auto-scan for plugins on startup (incremental — uses plugin_cache).
    std::cerr << "[Startup] Auto-scanning for VST3 plugins..." << std::endl;
    pluginScanner_.scanIncrementalAsync(db_, [this]() {
      int count = pluginScanner_.getPluginCount();
      std::cerr << "[Startup] Plugin scan complete: " << count
                << " plugins found" << std::endl;
      juce::String json = pluginScanner_.getPluginListAsJson();
      broadcastMessage("setPluginList", juce::JSON::fromString(json));
    });

    // Initialize shadow state manager (creates shared memory file)
    stateManager_.initialize();
    // Set config name to the current branch name for Dorico blob compat.
    if (versionStore_ && !currentBranchId_.empty()) {
      auto branchOpt = versionStore_->getStorage().getBranch(currentBranchId_);
      if (branchOpt)
        stateManager_.setConfigName(juce::String(branchOpt->first));
      else
        stateManager_.setConfigName("default");
    } else {
      stateManager_.setConfigName("default");
    }

    // Pre-cache config_status for immediate push to clients.
    if (server) {
      fiddle::MidiEvent msg;
      msg.set_timestamp_samples(0);
      auto *cs = msg.mutable_config_status();
      cs->set_config_name(stateManager_.getConfigName().toStdString());
      cs->set_config_version("");
      cs->set_dirty(false);
      cs->set_delay_ms(1000); // default, updated later
      server->setCachedConfigStatus(msg);
    }
    safeCallAsync([this]() { initPluginsAndStrips(); });
}

void MainComponent::initPluginsAndStrips() {
  // Load plugins and strips from database
    addInitMessage("Loading plugins...");

    // Load persisted plugin listener capabilities
    listenerCapableUids_ = db_.loadListenerCapableUids();
    if (!listenerCapableUids_.empty()) {
      std::cerr << "[MainComponent] Loaded " << listenerCapableUids_.size()
                << " listener-capable plugin UIDs from DB" << std::endl;
    }

    // Normal load from SQLite
    loadStripsFromDB();

    // Ensure mixer strips exist for all ensemble instruments
    mixer_.syncStripsToInstruments(masterList_);

    pushMixerState(false);

    // Build initial shadow state blob
    scheduleStateRebuild();

    // Cache config_status so new client connections get it immediately
    pushConfigStatus();

    // Wait for the WebView to signal it's ready before dismissing splash
    addInitMessage("Preparing interface...");
    return;

  default:
    
  } // switch
}