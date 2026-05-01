void MainComponent::setupJsHandlers() {
  jsRouter_.registerHandler("signalReady", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    juce::String viewMode = "mixer";
    if (args.size() > 0) {
      viewMode = args[0].toString();
    }

    juce::WebBrowserComponent *targetWebComponent = &webComponent;
    bool isHistoryWindow = false;

    if (viewMode == "history" && historyWindow_) {
      targetWebComponent = &(historyWindow_->getWebView());
      historyWindowLoaded_ = true;
      isHistoryWindow = true;
      std::cerr << "[WebView] Handshake: History window ready" << std::endl;

    } else if (viewMode == "library" && libraryManagerWindow_) {
      targetWebComponent = &(libraryManagerWindow_->getWebView());
      libraryManagerWindowLoaded_ = true;
      isHistoryWindow = true; // Reuse flag to skip main-window-only init
      std::cerr << "[WebView] Handshake: Library Manager window ready"
                << std::endl;
    } else {
      webViewLoaded = true;
      std::cerr << "[WebView] Handshake: Main window ready" << std::endl;

      std::vector<std::pair<juce::String, bool>> pending;
      {
        std::lock_guard<std::mutex> lock(logMutex);
        pending.swap(logQueue);
      }
      for (const auto &item : pending) {
        pushLogMessage(item.first, item.second);
      }

      pushLogMessage("<i>Server started and listening for connections...</i>");
    }



    // Send Version
    if (auto *app = juce::JUCEApplication::getInstance()) {
      broadcastMessage("setServerVersion",
                       juce::var(app->getApplicationVersion()));
    }

    // Push current BPM
    double bpm = currentBpm_.load(std::memory_order_relaxed);
    if (bpm > 0.0) {
      juce::DynamicObject::Ptr tempoObj = new juce::DynamicObject();
      tempoObj->setProperty("bpm", bpm);
      // Hardcode initial marker to 0, future ones get correct absoluteSamples
      tempoObj->setProperty("samplePosition", (juce::int64)0);
      tempoObj->setProperty("timeSigNumerator", 4);
      tempoObj->setProperty("timeSigDenominator", 4);
      broadcastMessage("setTempo", juce::var(tempoObj.get()));
    }

    // Push channel map (port/channel → instrument) to Timeline
    {
      juce::String mapJson = masterList_.getChannelMapAsJson();
      broadcastMessage("setInstrumentMap", juce::JSON::fromString(mapJson));
    }

    // Push cached plugin list (if any prior scan exists)
    if (pluginScanner_.getPluginCount() > 0) {
      juce::String json = pluginScanner_.getPluginListAsJson();
      broadcastMessage("setPluginList", juce::JSON::fromString(json));
    }

    // Push current mixer state
    if (!isHistoryWindow) {
      pushMixerState(false);

      // Push available Lua plugins catalog
      {
        juce::Array<juce::var> pluginArr;
        for (const auto &meta : luaCatalog_.plugins()) {
          auto *obj = new juce::DynamicObject();
          obj->setProperty("name", juce::String(meta.name));
          obj->setProperty("version", juce::String(meta.version));
          obj->setProperty("author", juce::String(meta.author));
          obj->setProperty("description", juce::String(meta.description));
          obj->setProperty("filePath", juce::String(meta.filePath));
          juce::Array<juce::var> params;
          for (const auto &p : meta.params) {
            auto *pObj = new juce::DynamicObject();
            pObj->setProperty("name", juce::String(p.name));
            pObj->setProperty("type", juce::String(p.type));
            pObj->setProperty("min", p.min);
            pObj->setProperty("max", p.max);
            pObj->setProperty("default", p.defaultVal);
            juce::Array<juce::var> opts;
            for (const auto &o : p.options)
              opts.add(juce::String(o));
            pObj->setProperty("options", opts);
            params.add(juce::var(pObj));
          }
          obj->setProperty("params", params);
          pluginArr.add(juce::var(obj));
        }
        broadcastMessage("setLuaPluginCatalog", juce::var(pluginArr));
      }

      // Send Dorico instruments (with score order) so mixer can sort
      juce::String instrJson = instrumentBrowser_.getInstrumentsAsJson();
      safeCallAsync([this, instrJson]() {
        broadcastMessage("setDoricoInstruments",
                         juce::JSON::fromString(instrJson));
      });
    }

    // Restore saved main window mode.
    if (!isHistoryWindow) {
      auto mws = db_.loadWindowSettings("main");
      juce::String mode = mws.mode;
      broadcastMessage("setMainMode", juce::var(mode));
    }

    if (isHistoryWindow) {
      safeCallAsync([this]() {
        pushDagHistory();
        pushBranches();
        pushCurrentVersion();
      });
    }



    // Push branches to main window
    if (!isHistoryWindow) {
      pushBranches();
    }

    // Reveal the WebView now that it's fully loaded
    if (!isHistoryWindow) {
      initComplete_ = true;
      webComponent.setBounds(getLocalBounds());
      repaint();
    }
    return;
  });
  jsRouter_.registerHandler("setMode", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() > 0) {
      juce::String mode = args[0].toString();
      if (mode == "mixer" || mode == "setup") {
        // Save mode to window_settings for the main window
        auto ws = db_.loadWindowSettings("main");
        ws.mode = mode;
        db_.saveWindowSettings(ws);
      }
    }
    return;
  });
  jsRouter_.registerHandler("nativeLog", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() > 0)
      std::cerr << "[JS NativeLog] " << args[0].toString() << std::endl;
    return;
  });
  jsRouter_.registerHandler("requestSetupData", [this](const juce::var& payload) {
    std::cerr << "[Setup] requestSetupData called" << std::endl;

    // Send Dorico instruments list
    {
      auto t0 = std::chrono::steady_clock::now();
      juce::String instrJson = instrumentBrowser_.getInstrumentsAsJson();
      auto t1 = std::chrono::steady_clock::now();
      std::cerr << "[Setup] Built instrument JSON: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(t1 -
                                                                         t0)
                       .count()
                << "ms, " << instrJson.length() << " chars" << std::endl;
      safeCallAsync([this, instrJson]() {
        broadcastMessage("setDoricoInstruments",
                         juce::JSON::fromString(instrJson));
      });
    }

    // Push saved selections to the UI
    {
      juce::String selJson = masterList_.getSlotsAsJson();
      safeCallAsync([this, selJson]() {
        broadcastMessage("setSelectedInstruments",
                         juce::JSON::fromString(selJson));
      });
    }

    // Push channel map (port/channel → instrument) to the UI
    {
      juce::String mapJson = masterList_.getChannelMapAsJson();
      safeCallAsync([this, mapJson]() {
        broadcastMessage("setInstrumentMap", juce::JSON::fromString(mapJson));
      });
    }
    return;
  });
  jsRouter_.registerHandler("saveSelectedInstruments", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      safeCallAsync([this]() {
        broadcastMessage("setSaveResult", juce::var("Error: no data"));
      });
      return;
    }
    juce::String json = args[0].toString();
    if (masterList_.setSlotsFromJson(json)) {
      masterList_.saveToDB(db_);

      // Reconcile stable channel assignments
      bool compacted = masterList_.reconcileAssignments(db_);

      // Generate Dorico config files
      DoricoConfigGenerator generator;
      auto slots = masterList_.getSlots();
      auto assignments = DoricoConfigGenerator::expandSlots(slots);
      int numChannels = masterList_.totalSlotCount();

      auto result = generator.generateAndInstallFiles(
          assignments, numChannels, instrumentBrowser_.getInstruments());
      juce::String msg;
      if (result.wasOk()) {
        msg = "OK: Installed " + juce::String((int)assignments.size()) +
              " presets (" + juce::String(numChannels) + " channels)";

        // Rebuild channel_assignments from the sequential flat indices used by
        // the generated Dorico playback template. reconcileAssignments above
        // tries to preserve stable indices across score changes, but can
        // diverge from the template's sequential order (e.g. after reordering
        // ensemble slots). By rebuilding here we guarantee the DB always
        // matches the MIDI port/channel layout that Dorico will use.
        std::vector<ChannelAssignmentRow> newRows;
        newRows.reserve(assignments.size());
        // Track instanceNum per (entityID, isSolo) pair
        std::map<std::pair<juce::String, bool>, int> instanceCounts;
        for (int idx = 0; idx < (int)assignments.size(); ++idx) {
          const auto& a = assignments[idx];
          auto key = std::make_pair(a.entityID, a.isSolo);
          int instanceNum = ++instanceCounts[key];
          ChannelAssignmentRow row;
          row.flatIndex = idx;
          row.entityID = a.entityID;
          row.isSolo = a.isSolo;
          row.instanceNum = instanceNum;
          newRows.push_back(row);
        }
        db_.saveChannelAssignments(newRows);
        // Reload into masterList so getChannelMapAsJson reflects new order
        masterList_.reconcileAssignments(db_);
      } else {
        msg = "Error: " + result.getErrorMessage();
      }
      safeCallAsync([this, msg]() {
        broadcastMessage("setSaveResult", juce::var(msg));
      });

      if (compacted) {
        pushLogMessage("<b>[Setup]</b> ⚠️ Channel assignments were "
                       "compacted. Existing Dorico projects may need "
                       "their playback template re-applied.");
      }

      // Sync mixer strips to match updated instruments
      mixer_.syncStripsToInstruments(masterList_);

      // Update channel map for Timeline
      juce::String mapJson2 = masterList_.getChannelMapAsJson();
      safeCallAsync([this, mapJson2]() {
        broadcastMessage("setInstrumentMap", juce::JSON::fromString(mapJson2));
      });

      // Push updated mixer state to UI
      pushMixerState();
    } else {
      safeCallAsync([this]() {
        broadcastMessage("setSaveResult", juce::var("Error: Invalid JSON"));
      });
    }
    return;
  });
  jsRouter_.registerHandler("scanPlugins", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (pluginScanner_.isScanning()) {
      return;
    }
    pushLogMessage("<b>[Plugins]</b> Scanning for VST3 plugins "
                   "(incremental)...");
    pluginScanner_.scanIncrementalAsync(db_, [this]() {
      int count = pluginScanner_.getPluginCount();
      pushLogMessage("<b>[Plugins]</b> Scan complete: " + juce::String(count) +
                     " plugins found");
      juce::String json = pluginScanner_.getPluginListAsJson();
      broadcastMessage("setPluginList", juce::JSON::fromString(json));
    });
    return;
  });
  jsRouter_.registerHandler("rescanPlugins", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (pluginScanner_.isScanning()) {
      return;
    }
    pushLogMessage("<b>[Plugins]</b> Full rescan of VST3 plugins...");
    pluginScanner_.rescanAsync(db_, [this]() {
      int count = pluginScanner_.getPluginCount();
      pushLogMessage("<b>[Plugins]</b> Rescan complete: " +
                     juce::String(count) + " plugins found");
      juce::String json = pluginScanner_.getPluginListAsJson();
      broadcastMessage("setPluginList", juce::JSON::fromString(json));
    });
    return;
  });
  jsRouter_.registerHandler("addMixerStrip", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      auto action = std::make_unique<AddStripAction>(mixer_);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("duplicateStripInput", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    juce::String stripId = args[0].toString();
    safeCallAsync([this, stripId]() {
      auto action = std::make_unique<DuplicateStripAction>(mixer_, stripId);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("removeMixerStrip", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    juce::String stripId = args[0].toString();
    safeCallAsync([this, stripId]() {
      auto action = std::make_unique<RemoveStripAction>(mixer_, stripId);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("setStripLibrary", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    juce::String lib = args[1].toString();
    safeCallAsync([this, stripId, lib]() {
      juce::String oldLib;
      if (auto *s = mixer_.getStrip(stripId))
        oldLib = s->library;
      auto action =
          std::make_unique<SetLibraryAction>(mixer_, stripId, oldLib, lib);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("setStripInput", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 3) {
      return;
    }
    juce::String stripId = args[0].toString();
    int port = (int)args[1];
    int channel = (int)args[2];
    safeCallAsync([this, stripId, port, channel]() {
      int oldPort = -1, oldCh = -1;
      if (auto *s = mixer_.getStrip(stripId)) {
        oldPort = s->inputPort;
        oldCh = s->inputChannel;
      }
      auto action = std::make_unique<SetInputAction>(mixer_, stripId, oldPort,
                                                     oldCh, port, channel);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("setStripGain", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    float gainDb = static_cast<float>((double)args[1]);
    gainDb = juce::jlimit(-120.0f, 6.0f, gainDb);
    safeCallAsync([this, stripId, gainDb]() {
      float oldGain = 0.0f;
      if (auto *s = mixer_.getStrip(stripId))
        oldGain = s->gainDb.load(std::memory_order_relaxed);
      auto action =
          std::make_unique<SetGainAction>(mixer_, stripId, oldGain, gainDb);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("setStripMute", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    bool mute = (bool)args[1];
    safeCallAsync([this, stripId, mute]() {
      mixer_.setStripMute(stripId, mute);
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("setStripSolo", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    bool solo = (bool)args[1];
    safeCallAsync([this, stripId, solo]() {
      mixer_.setStripSolo(stripId, solo);
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("toggleLibraryActive", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1)
      return;
    juce::String libraryName = args[0].toString();
    safeCallAsync([this, libraryName]() {
      // Determine current state: active if ALL strips with this library are
      // active
      bool allActive = true;
      auto strips = mixer_.getAllStrips();
      for (auto *s : strips) {
        if (s->library == libraryName && !s->active) {
          allActive = false;
          break;
        }
      }
      // Toggle: if all active, deactivate; if any inactive, activate all
      bool newState = !allActive;
      auto action = std::make_unique<ToggleLibraryActiveAction>(
          mixer_, libraryName, newState);
      undoManager_.perform(std::move(action));
      saveAllStripsToDB();
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("getAnnotationRecords", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1)
      return;
    juce::String stripId = args[0].toString();
    safeCallAsync([this, stripId]() {
      auto *strip = mixer_.getStrip(stripId);
      if (!strip)
        return;
      juce::String json = strip->getAnnotationRecordsAsJson();
      auto *envelope = new juce::DynamicObject();
      envelope->setProperty("stripId", stripId);
      envelope->setProperty("records", juce::JSON::parse(json));
      broadcastMessage("setAnnotationRecords", juce::var(envelope));
    });
    return;
  });
  jsRouter_.registerHandler("clearAnnotationRecords", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1)
      return;
    juce::String stripId = args[0].toString();
    safeCallAsync([this, stripId]() {
      auto *strip = mixer_.getStrip(stripId);
      if (!strip)
        return;
      strip->clearAnnotations();
    });
    return;
  });
  jsRouter_.registerHandler("startMidiCapture", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    juce::String mode = args[1].toString(); // "incoming", "emitted", or "both"

    auto *strip = mixer_.getStrip(stripId);
    if (!strip)
      return;
    if (mode == "incoming" || mode == "both")
      strip->incomingCapture.startCapture();
    if (mode == "emitted" || mode == "both")
      strip->emittedCapture.startCapture();
    return;
  });
  jsRouter_.registerHandler("stopMidiCapture", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    juce::String mode = args[1].toString();

    auto *strip = mixer_.getStrip(stripId);
    if (!strip)
      return;
    if (mode == "incoming" || mode == "both")
      strip->incomingCapture.stopCapture();
    if (mode == "emitted" || mode == "both")
      strip->emittedCapture.stopCapture();
    return;
  });
  jsRouter_.registerHandler("getMidiCapture", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    juce::String mode = args[1].toString(); // "incoming" or "emitted"

    safeCallAsync([this, stripId, mode]() {
      auto *strip = mixer_.getStrip(stripId);
      if (!strip)
        return;
      auto *envelope = new juce::DynamicObject();
      envelope->setProperty("stripId", stripId);
      envelope->setProperty("mode", mode);
      if (mode == "incoming")
        envelope->setProperty("events", strip->incomingCapture.toVar());
      else
        envelope->setProperty("events", strip->emittedCapture.toVar());
      envelope->setProperty(
          "count",
          (int)(mode == "incoming" ? strip->incomingCapture.eventCount()
                                   : strip->emittedCapture.eventCount()));
      broadcastMessage("setMidiCapture", juce::var(envelope));
    });
    return;
  });
  jsRouter_.registerHandler("clearMidiCapture", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    juce::String mode = args[1].toString();

    auto *strip = mixer_.getStrip(stripId);
    if (!strip)
      return;
    if (mode == "incoming" || mode == "both")
      strip->incomingCapture.clear();
    if (mode == "emitted" || mode == "both")
      strip->emittedCapture.clear();
    return;
  });
  jsRouter_.registerHandler("getCaptureStripList", [this](const juce::var& payload) {
    safeCallAsync([this]() {
      juce::Array<juce::var> arr;
      for (auto *strip : mixer_.getAllStrips()) {
        // Only show strips that have an expression map assigned
        if (!strip->expressionMap)
          continue;
        auto *obj = new juce::DynamicObject();
        obj->setProperty("id", strip->id);
        obj->setProperty(
            "name", strip->library.isNotEmpty()
                        ? strip->library + " (" + strip->family + ")"
                        : strip->id);
        obj->setProperty("port", strip->inputPort + 1); // 1-based for Dorico
        obj->setProperty("channel", strip->inputChannel);
        arr.add(juce::var(obj));
      }
      broadcastMessage("setCaptureStripList", juce::var(arr));
    });
    return;
  });
  jsRouter_.registerHandler("exportCaptureFile", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String text = args[0].toString();
    juce::String defaultName = args[1].toString();

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export MIDI Capture",
        juce::File::getSpecialLocation(
            juce::File::userDesktopDirectory)
            .getChildFile(defaultName + ".midi-dump"),
        "*.midi-dump");

    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [text, chooser](const juce::FileChooser &fc) {
          auto results = fc.getResults();
          if (results.isEmpty())
            return;
          auto file = results[0];
          file.replaceWithText(text);
        });
    return;
  });
  jsRouter_.registerHandler("addStripLuaPlugin", [this](const juce::var& payload) {
    // payload: [stripId, pluginFilePath]
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    std::string pluginPath = args[1].toString().toStdString();

    // Verify the plugin is in the catalog (try full path first, then just filename)
    std::string fileName =
        std::filesystem::path(pluginPath).filename().string();
    const auto *meta = luaCatalog_.findByPath(pluginPath);
    if (!meta)
      meta = luaCatalog_.findByFileName(fileName);
    if (!meta) {
      std::cerr << "[Lua] Plugin not in catalog: " << pluginPath << std::endl;
      return;
    }
    safeCallAsync([this, stripId, fileName]() {
      auto action = std::make_unique<AddLuaPluginAction>(
          mixer_, luaCatalog_, stripId, fileName);
      undoManager_.perform(std::move(action));
      saveAllStripsToDB();
      pushMixerState();
      scheduleStateRebuild();
    });
    return;
  });
  jsRouter_.registerHandler("removeStripLuaPlugin", [this](const juce::var& payload) {
    // payload: [stripId, pluginIndex]
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    int pluginIndex = (int)args[1];

    safeCallAsync([this, stripId, pluginIndex]() {
      auto action = std::make_unique<RemoveLuaPluginAction>(
          mixer_, luaCatalog_, stripId, pluginIndex);
      undoManager_.perform(std::move(action));
      saveAllStripsToDB();
      pushMixerState();
      scheduleStateRebuild();
    });
    return;
  });
  jsRouter_.registerHandler("getLuaPluginCatalog", [this](const juce::var& payload) {
    // Re-push the catalog (e.g., after a rescan)
    juce::Array<juce::var> pluginArr;
    for (const auto &meta : luaCatalog_.plugins()) {
      auto *obj = new juce::DynamicObject();
      obj->setProperty("name", juce::String(meta.name));
      obj->setProperty("version", juce::String(meta.version));
      obj->setProperty("author", juce::String(meta.author));
      obj->setProperty("description", juce::String(meta.description));
      obj->setProperty("filePath", juce::String(meta.filePath));
      pluginArr.add(juce::var(obj));
    }
    broadcastMessage("setLuaPluginCatalog", juce::var(pluginArr));
    return;
  });
  jsRouter_.registerHandler("setStripPlugin", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    int pluginUid = (int)args[1];

    // Verify plugin exists in scanner
    if (pluginUid != 0) {
      bool found = false;
      for (const auto &d : pluginScanner_.getKnownPluginList().getTypes()) {
        if (d.uniqueId == pluginUid) {
          found = true;
          break;
        }
      }
      if (!found) {
        return;
      }
    }

    safeCallAsync([this, stripId, pluginUid]() {
      int oldUid = 0;
      if (auto *s = mixer_.getStrip(stripId))
        oldUid = s->pluginUid;
      auto action = std::make_unique<SetPluginAction>(
          mixer_, pluginScanner_, stripId, oldUid, pluginUid,
          [this]() { pushMixerState(); });
      undoManager_.perform(std::move(action));
    });
    return;
  });
  jsRouter_.registerHandler("setStripProgram", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    int progIndex = (int)args[1];
    safeCallAsync([this, stripId, progIndex]() {
      if (auto *strip = mixer_.getStrip(stripId)) {
        if (strip->pluginInstance != nullptr) {
          strip->pluginInstance->setCurrentProgram(progIndex);
          strip->refreshPluginStateCache();
          pushMixerState();
        }
      }
    });
    return;
  });
  jsRouter_.registerHandler("showStripEditor", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    juce::String stripId = args[0].toString();
    safeCallAsync([this, stripId]() {
      if (auto *s = mixer_.getStrip(stripId))
        s->showEditor();
    });
    return;
  });
  jsRouter_.registerHandler("restoreLibraryPluginState", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 4) {
      return;
    }
    juce::String stripId = args[0].toString();
    juce::String libraryId = args[1].toString();
    juce::String entityId = args[2].toString();
    int instanceNum = (int)args[3];

    safeCallAsync([this, stripId, libraryId, entityId, instanceNum]() {
      auto *strip = mixer_.getStrip(stripId);
      if (!strip || !strip->pluginInstance) {
        std::cerr << "[restoreLibraryPluginState] No strip/plugin for "
                  << stripId << std::endl;
        return;
      }

      // Load saved instruments from DB and find the matching one
      auto instruments = db_.loadLibraryInstruments(libraryId);
      std::cerr << "[restoreLibraryPluginState] libraryId=" << libraryId
                << " entityId=" << entityId
                << " instanceNum=" << instanceNum
                << " instruments=" << instruments.size() << std::endl;
      bool found = false;
      for (const auto &inst : instruments) {
        if (inst.entityId == entityId && inst.hasInstance(instanceNum)) {
          std::cerr << "[restoreLibraryPluginState] Found entity+instance, blobSize="
                    << inst.pluginState.getSize() << std::endl;
          if (inst.pluginState.getSize() > 0) {
            strip->pluginInstance->setStateInformation(
                inst.pluginState.getData(),
                static_cast<int>(inst.pluginState.getSize()));
            strip->refreshPluginStateCache();
            std::cerr << "[restoreLibraryPluginState] Restored "
                      << inst.pluginState.getSize() << " bytes for strip "
                      << stripId << std::endl;
          } else {
            std::cerr << "[restoreLibraryPluginState] No blob stored for "
                      << entityId << " #" << instanceNum << std::endl;
          }
          found = true;
          break;
        }
      }
      if (!found) {
        std::cerr << "[restoreLibraryPluginState] Entity+instance not found in library"
                  << std::endl;
      }
    });
    return;
  });
  jsRouter_.registerHandler("loadExpressionMap", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    std::string entityID = args[1].toString().toStdString();

    auto data = xmapLibrary_.load(entityID);
    if (!data) {
      return;
    }

    safeCallAsync([this, stripId, entityID, data]() {
      std::string oldEntityID;
      if (auto *s = mixer_.getStrip(stripId)) {
        if (s->expressionMap)
          oldEntityID = s->expressionMap->entityID;
      }
      auto action = std::make_unique<SetExpressionMapAction>(
          mixer_, xmapLibrary_, stripId, oldEntityID, entityID, data);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("clearExpressionMap", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    juce::String stripId = args[0].toString();
    safeCallAsync([this, stripId]() {
      std::string oldEntityID;
      if (auto *s = mixer_.getStrip(stripId)) {
        if (s->expressionMap)
          oldEntityID = s->expressionMap->entityID;
      }
      if (oldEntityID.empty())
        return; // already clear
      auto action = std::make_unique<SetExpressionMapAction>(
          mixer_, xmapLibrary_, stripId, oldEntityID, "", nullptr);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("loadExpressionMapFromFile", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    juce::String stripId = args[0].toString();

    auto chooser = std::make_shared<juce::FileChooser>(
        "Load Expression Map", juce::File{}, "*.doricolib");

    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
                         [this, stripId, chooser](const juce::FileChooser &fc) {
                           auto results = fc.getResults();
                           if (results.isEmpty())
                             return;

                           auto file = results[0];
                           auto data = std::make_shared<ExpressionMapData>();
                           if (!parseExpressionMap(file, *data))
                             return;

                           if (auto *s = mixer_.getStrip(stripId)) {
                             s->setExpressionMap(data);
                             s->expressionMapPath = file.getFullPathName();
                           }

                           safeCallAsync([this]() { pushMixerState(); });
                         });
    return;
  });
  jsRouter_.registerHandler("requestLibraries", [this](const juce::var& payload) {
    safeCallAsync([this]() {
      auto libs = db_.listLibraries();
      juce::Array<juce::var> arr;
      for (const auto &lib : libs) {
        auto *obj = new juce::DynamicObject();
        obj->setProperty("id", lib.id);
        obj->setProperty("name", lib.name);
        obj->setProperty("vendor", lib.vendor);
        obj->setProperty("variant", lib.variant);
        arr.add(juce::var(obj));
      }
      broadcastMessage("setLibraryList", juce::var(arr));
      broadcastTemplateDirty();
    });
    return;
  });
  jsRouter_.registerHandler("saveLibrary", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;

    auto *data = args[0].getDynamicObject();
    if (!data)
      return;

    fiddle::LibraryRow lib;
    lib.id = data->getProperty("id").toString();
    lib.name = data->getProperty("name").toString();
    lib.vendor = data->getProperty("vendor").toString();
    lib.variant = data->getProperty("variant").toString();

    std::vector<fiddle::LibraryInstrumentRow> instruments;
    if (auto *instArr = data->getProperty("instruments").getArray()) {
      int sortOrder = 0;
      for (const auto &item : *instArr) {
        if (auto *instObj = item.getDynamicObject()) {
          fiddle::LibraryInstrumentRow inst;
          inst.libraryId = lib.id;
          inst.sortOrder = sortOrder++;
          inst.entityId = instObj->getProperty("entityID").toString();
          inst.name = instObj->getProperty("name").toString();
          inst.family = instObj->getProperty("family").toString();
          inst.category = instObj->getProperty("category").toString();
          auto sizeStr = instObj->getProperty("size").toString();
          inst.isSolo = (sizeStr != "section");
          inst.vstPlugin = instObj->getProperty("vstPlugin").toString();
          inst.exprMap = instObj->getProperty("exprMap").toString();
          inst.note = instObj->getProperty("note").toString();
          auto instNumProp = instObj->getProperty("instanceNums");
          if (instNumProp.isArray()) {
            inst.instanceNums.clear();
            for (const auto &v : *instNumProp.getArray())
              inst.instanceNums.push_back((int)v);
            if (inst.instanceNums.empty())
              inst.instanceNums.push_back(1);
          } else {
            // Legacy: single integer
            int n = instNumProp.isVoid() ? 1 : (int)instNumProp;
            inst.instanceNums = {n};
          }
          // Capture live plugin state from the mixer strip (if linked)
          juce::String stripId = instObj->getProperty("stripId").toString();
          if (stripId.isNotEmpty()) {
            if (auto *strip = mixer_.getStrip(stripId)) {
              inst.pluginUid = strip->pluginUid;
              strip->refreshPluginStateCache();
              inst.pluginState = strip->cachedPluginState_;
            }
          }
          // Ensure pluginUid is populated from the vstPlugin string
          // even when no live strip was linked during this save.
          if (inst.pluginUid == 0 && inst.vstPlugin.isNotEmpty())
            inst.pluginUid = inst.vstPlugin.getIntValue();
          instruments.push_back(std::move(inst));
        }
      }
    }

    safeCallAsync([this, lib = std::move(lib),
                   instruments = std::move(instruments)]() {
      db_.saveLibrary(lib, instruments);

      // Broadcast updated library list
      auto libs = db_.listLibraries();
      juce::Array<juce::var> arr;
      for (const auto &l : libs) {
        auto *obj = new juce::DynamicObject();
        obj->setProperty("id", l.id);
        obj->setProperty("name", l.name);
        obj->setProperty("vendor", l.vendor);
        obj->setProperty("variant", l.variant);
        arr.add(juce::var(obj));
      }
      broadcastMessage("setLibraryList", juce::var(arr));
      broadcastTemplateDirty();
    });
    return;
  });
  jsRouter_.registerHandler("loadLibrary", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;

    juce::String libraryId = args[0].toString();
    safeCallAsync([this, libraryId]() {
      auto libs = db_.listLibraries();
      fiddle::LibraryRow foundLib;
      bool found = false;
      for (const auto &lib : libs) {
        if (lib.id == libraryId) {
          foundLib = lib;
          found = true;
          break;
        }
      }
      if (!found)
        return;

      auto instruments = db_.loadLibraryInstruments(libraryId);

      auto *result = new juce::DynamicObject();
      result->setProperty("id", foundLib.id);
      result->setProperty("name", foundLib.name);
      result->setProperty("vendor", foundLib.vendor);
      result->setProperty("variant", foundLib.variant);

      juce::Array<juce::var> instArr;
      for (const auto &inst : instruments) {
        auto *instObj = new juce::DynamicObject();
        instObj->setProperty("entityID", inst.entityId);
        instObj->setProperty("name", inst.name);
        instObj->setProperty("family", inst.family);
        instObj->setProperty("category", inst.category);
        instObj->setProperty("vstPlugin", inst.vstPlugin);
        instObj->setProperty("exprMap", inst.exprMap);
        instObj->setProperty("pluginUid", inst.pluginUid);
        instObj->setProperty("hasPluginState",
                             inst.pluginState.getSize() > 0);
        juce::Array<juce::var> iNums;
        for (int n : inst.instanceNums) iNums.add(n);
        instObj->setProperty("instanceNums", juce::var(iNums));
        instObj->setProperty("isSolo", inst.isSolo);
        instObj->setProperty("note", inst.note);
        instArr.add(juce::var(instObj));
      }
      result->setProperty("instruments", juce::var(instArr));

      broadcastMessage("setLibraryData", juce::var(result));
    });
    return;
  });
  jsRouter_.registerHandler("buildPlaybackTemplate", [this](const juce::var& payload) {
    safeCallAsync([this]() {
      std::cerr << "[BuildTemplate] Starting..." << std::endl;

      // 1. Load all libraries and their instruments
      auto libs = db_.listLibraries();
      auto allInstruments = db_.loadAllLibraryInstruments();

      // Build library name lookup
      std::map<juce::String, juce::String> libNames;
      for (const auto &lib : libs)
        libNames[lib.id] = lib.name;

      // 2. Compute union of instruments → EnsembleSlots
      //    Key: (entityId, isSolo) → max instanceNum
      struct SlotKey {
        juce::String entityId;
        bool isSolo;
        bool operator<(const SlotKey &o) const {
          if (entityId != o.entityId) return entityId < o.entityId;
          return isSolo < o.isSolo;
        }
      };
      // Track: max instanceNum per unique instrument type, plus count of
      // libraries that have each (entityId, isSolo, instanceNum) tuple.
      // The total count for a slot = number of libraries × max instanceNum.
      struct SlotInfo {
        int maxInstanceNum = 0;
        juce::String name;
        juce::String family;
        juce::String category;
        // Track which libraries have this instrument type
        std::set<juce::String> libraryIds;
      };
      std::map<SlotKey, SlotInfo> slotMap;

      // Also keep a flat lookup for strip initialization later
      // Key: (entityId, isSolo, instanceNum) → list of library instruments
      using StripKey = std::tuple<juce::String, bool, int>;
      std::map<StripKey, std::vector<fiddle::LibraryInstrumentRow>> stripLookup;

      for (const auto &inst : allInstruments) {
        SlotKey key{inst.entityId, inst.isSolo};
        auto &info = slotMap[key];
        info.maxInstanceNum = std::max(info.maxInstanceNum, inst.maxInstance());
        if (info.name.isEmpty()) {
          info.name = inst.name;
          info.family = inst.family;
          info.category = inst.category;
        }
        info.libraryIds.insert(inst.libraryId);

        // Insert one lookup entry per instance number so the strip
        // creation loop can find this row for each Dorico channel.
        for (int iNum : inst.instanceNums) {
          StripKey sk{inst.entityId, inst.isSolo, iNum};
          stripLookup[sk].push_back(inst);
        }
      }

      // Build EnsembleSlots. For each unique (entityId, isSolo), the count
      // is: numLibraries × maxInstanceNum (each library gets its own set of
      // strips for each instance).
      std::vector<MasterInstrumentList::EnsembleSlot> newSlots;

      // First collect unique entityIds to preserve order
      std::map<juce::String, std::pair<int, int>> entityCounts; // soloCount, sectionCount
      struct EntityInfo {
        juce::String name, family;
        juce::String musicXMLSoundID;
        int soloCount = 0;
        int sectionCount = 0;
      };
      std::map<juce::String, EntityInfo> entityMap;

      // Look up musicXMLSoundID from the instrument browser
      auto &browserInstruments = instrumentBrowser_.getInstruments();
      std::map<juce::String, juce::String> entityToSoundID;
      for (const auto &bi : browserInstruments)
        entityToSoundID[bi.entityID] = bi.musicXMLSoundID;

      for (const auto &[key, info] : slotMap) {
        auto &ei = entityMap[key.entityId];
        if (ei.name.isEmpty()) {
          ei.name = info.name;
          ei.family = info.family;
          ei.musicXMLSoundID = entityToSoundID[key.entityId];
        }
        // Use max instance count across libraries (not sum).
        // Multiple libraries share the same Dorico channels.
        int count = info.maxInstanceNum;
        if (key.isSolo)
          ei.soloCount += count;
        else
          ei.sectionCount += count;
      }

      for (const auto &[entityId, ei] : entityMap) {
        MasterInstrumentList::EnsembleSlot slot;
        slot.entityID = entityId;
        slot.name = ei.name;
        slot.musicXMLSoundID = ei.musicXMLSoundID;
        slot.family = ei.family;
        slot.soloCount = ei.soloCount;
        slot.sectionCount = ei.sectionCount;
        newSlots.push_back(std::move(slot));
      }

      if (newSlots.empty()) {
        broadcastMessage("buildPlaybackTemplateResult",
                         juce::var("Error: No library instruments found"));
        return;
      }

      std::cerr << "[BuildTemplate] " << newSlots.size()
                << " unique instrument types from " << libs.size()
                << " libraries" << std::endl;

      // 3. Set slots and generate Dorico config
      masterList_.setSlots(std::move(newSlots));
      masterList_.saveToDB(db_);
      // Clear graveyard so stale entries don't get reclaimed into the new template
      db_.clearGraveyard();

      DoricoConfigGenerator generator;
      auto slots = masterList_.getSlots();
      auto assignments = DoricoConfigGenerator::expandSlots(slots);
      int numChannels = masterList_.totalSlotCount();

      auto result = generator.generateAndInstallFiles(
          assignments, numChannels, browserInstruments);

      juce::String msg;
      if (result.wasOk()) {
        msg = "OK: Built template with " +
              juce::String((int)assignments.size()) + " instruments (" +
              juce::String(numChannels) + " channels)";

        // Rebuild channel assignments
        std::vector<ChannelAssignmentRow> newRows;
        newRows.reserve(assignments.size());
        std::map<std::pair<juce::String, bool>, int> instanceCounts;
        for (int idx = 0; idx < (int)assignments.size(); ++idx) {
          const auto &a = assignments[idx];
          auto aKey = std::make_pair(a.entityID, a.isSolo);
          int instanceNum = ++instanceCounts[aKey];
          ChannelAssignmentRow row;
          row.flatIndex = idx;
          row.entityID = a.entityID;
          row.isSolo = a.isSolo;
          row.instanceNum = instanceNum;
          newRows.push_back(row);
        }
        db_.saveChannelAssignments(newRows);
        masterList_.reconcileAssignments(db_);
      } else {
        msg = "Error: " + result.getErrorMessage();
      }

      // 4. Snapshot which libraries are currently active before clearing.
      //    Any library that had at least one active strip is considered
      //    "previously active" and should remain active after rebuild.
      std::set<juce::String> previouslyActiveLibs;
      for (auto *s : mixer_.getAllStrips()) {
        if (s->active && s->library.isNotEmpty())
          previouslyActiveLibs.insert(s->library);
      }

      // Clear existing strips and create fresh ones from the new template
      mixer_.clear();
      mixer_.syncStripsToInstruments(masterList_);

      // 5. Initialize strips from library data.
      // For each Dorico channel, create one strip per library that has
      // that instrument. All strips for the same channel share the same
      // inputPort/inputChannel.
      juce::String mapJson = masterList_.getChannelMapAsJson();
      auto parsed = juce::JSON::parse(mapJson);
      if (auto *mapArr = parsed.getArray()) {
        for (const auto &item : *mapArr) {
          if (auto *obj = item.getDynamicObject()) {
            int port = (int)obj->getProperty("port");
            int ch = (int)obj->getProperty("channel");
            juce::String entityId = obj->getProperty("entityID").toString();
            bool isSolo = (bool)obj->getProperty("isSolo");
            int instNum = (int)obj->getProperty("instanceNum");

            // Find the base strip (created by syncStripsToInstruments)
            auto *baseStrip = [&]() -> MixerStrip * {
              auto strips = mixer_.getAllStrips();
              for (auto *s : strips) {
                if (s->inputPort == port && s->inputChannel == ch &&
                    s->library.isEmpty())
                  return s;
              }
              return nullptr;
            }();

            // Get the slot info for this instrument type
            SlotKey sKey{entityId, isSolo};
            auto slotIt = slotMap.find(sKey);
            if (slotIt == slotMap.end()) continue;
            const auto &sInfo = slotIt->second;

            // Get sorted library IDs for deterministic order
            std::vector<juce::String> sortedLibIds(sInfo.libraryIds.begin(),
                                                    sInfo.libraryIds.end());
            std::sort(sortedLibIds.begin(), sortedLibIds.end());

            // Helper lambda to set up a strip from a library instrument
            auto initStrip = [&](MixerStrip *strip,
                                 const fiddle::LibraryInstrumentRow &inst,
                                 const juce::String &libId) {
              strip->library = libNames[libId];
              // Preserve activation for previously-active libraries;
              // new libraries default to inactive.
              strip->active = previouslyActiveLibs.count(libNames[libId]) > 0;

              if (inst.exprMap.isNotEmpty()) {
                auto xmapData = xmapLibrary_.load(
                    inst.exprMap.toStdString());
                if (xmapData)
                  strip->setExpressionMap(xmapData);
              }

              // Resolve effective plugin UID: pluginUid is set when a
              // live strip was linked during save; vstPlugin (string)
              // always holds the dropdown selection.
              int effectiveUid = inst.pluginUid;
              if (effectiveUid == 0 && inst.vstPlugin.isNotEmpty())
                effectiveUid = inst.vstPlugin.getIntValue();

              if (effectiveUid != 0) {
                strip->pluginUid = effectiveUid;
                for (const auto &d :
                     pluginScanner_.getKnownPluginList().getTypes()) {
                  if (d.uniqueId == effectiveUid) {
                    auto stateBlock = inst.pluginState;
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
            };

            // Create one strip per library for this channel
            for (int libIdx = 0; libIdx < (int)sortedLibIds.size(); ++libIdx) {
              const juce::String &libId = sortedLibIds[libIdx];

              // Find the matching library instrument
              StripKey lookupKey{entityId, isSolo, instNum};
              auto lookupIt = stripLookup.find(lookupKey);
              if (lookupIt == stripLookup.end()) continue;

              const fiddle::LibraryInstrumentRow *matchedInst = nullptr;
              for (const auto &li : lookupIt->second) {
                if (li.libraryId == libId) {
                  matchedInst = &li;
                  break;
                }
              }
              if (!matchedInst) continue;

              if (libIdx == 0 && baseStrip) {
                // First library uses the base strip
                initStrip(baseStrip, *matchedInst, libId);
              } else {
                // Additional libraries: create a new strip sharing
                // the same input
                auto newStrip = std::make_unique<MixerStrip>();
                newStrip->id = juce::Uuid().toString();
                newStrip->inputPort = port;
                newStrip->inputChannel = ch;
                newStrip->family = baseStrip ? baseStrip->family : "";
                newStrip->isSolo = isSolo;
                auto *rawPtr = newStrip.get();

                // Insert right after the base strip (or at end)
                int insertIdx = -1;
                if (baseStrip)
                  insertIdx = mixer_.stripIndex(baseStrip->id) + 1;
                else
                  insertIdx = mixer_.size();
                mixer_.insertStripAt(std::move(newStrip), insertIdx);

                initStrip(rawPtr, *matchedInst, libId);
              }
            }
          }
        }
      }

      // 6. Push state to UIs
      saveAllStripsToDB();

      juce::String mapJson2 = masterList_.getChannelMapAsJson();
      broadcastMessage("setInstrumentMap", juce::JSON::fromString(mapJson2));

      pushMixerState();

      // Push updated setup data for the Setup pane
      {
        juce::String selJson = masterList_.getSlotsAsJson();
        broadcastMessage("setSelectedInstruments",
                         juce::JSON::fromString(selJson));
      }

      broadcastMessage("buildPlaybackTemplateResult", juce::var(msg));
      std::cerr << "[BuildTemplate] " << msg << std::endl;

      // Update fingerprint so the button disables until libraries change
      lastInstalledFingerprint_ = computeLibraryFingerprint();
      broadcastTemplateDirty();
    });
    return;
  });
  jsRouter_.registerHandler("deleteLibrary", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;

    juce::String libraryId = args[0].toString();
    safeCallAsync([this, libraryId]() {
      db_.deleteLibrary(libraryId);

      auto libs = db_.listLibraries();
      juce::Array<juce::var> arr;
      for (const auto &l : libs) {
        auto *obj = new juce::DynamicObject();
        obj->setProperty("id", l.id);
        obj->setProperty("name", l.name);
        obj->setProperty("vendor", l.vendor);
        obj->setProperty("variant", l.variant);
        arr.add(juce::var(obj));
      }
      broadcastMessage("setLibraryList", juce::var(arr));
      broadcastTemplateDirty();
    });
    return;
  });
  jsRouter_.registerHandler("requestExpressionMaps", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      juce::String json = xmapLibrary_.toJson();
      broadcastMessage("setExpressionMaps", juce::JSON::parse(json));
    });
    return;
  });
  jsRouter_.registerHandler("undo", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      if (undoManager_.undo()) {
        pushMixerState();
        if (undoManager_.isAtSavePoint()) {
          stateManager_.clearDirty();
          broadcastMessage("setDirtyState", false);
          pushConfigStatus();
        }
      }
    });
    return;
  });
  jsRouter_.registerHandler("redo", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      if (undoManager_.redo()) {
        pushMixerState();
        if (undoManager_.isAtSavePoint()) {
          stateManager_.clearDirty();
          broadcastMessage("setDirtyState", false);
          pushConfigStatus();
        }
      }
    });
    return;
  });
  jsRouter_.registerHandler("requestPluginsState", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      // Always reply with the current scanning state so the UI can show the
      // spinner (or hide it) regardless of whether plugins are loaded yet.
      broadcastMessage("isScanningPlugins", pluginScanner_.isScanning());
      if (pluginScanner_.getPluginCount() > 0) {
        juce::String json = pluginScanner_.getPluginListAsJson();
        // Use broadcastMessage so all windows (main, debug, history) get it
        broadcastMessage("setPluginList", juce::JSON::fromString(json));
      }
    });
    return;
  });
  jsRouter_.registerHandler("requestMixerState", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() { pushMixerState(false); });
    return;
  });
  jsRouter_.registerHandler("getAvailableInputs", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      juce::String json = masterList_.getChannelMapAsJson();
      broadcastMessage("setAvailableInputs", juce::JSON::parse(json));
    });
    return;
  });
  jsRouter_.registerHandler("setPlaybackDelay", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() >= 1) {
      int ms = static_cast<int>(args[0]);
      safeCallAsync([this, ms]() {
        mixer_.setPlaybackDelayMs(ms);
        pushConfigStatus();
        pushLogMessage("<b>[Mixer]</b> Playback delay set to " +
                       juce::String(ms) + " ms");
      });
    }
    return;
  });
  jsRouter_.registerHandler("getPlaybackDelay", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      int ms = mixer_.getPlaybackDelayMs();
      broadcastMessage("setPlaybackDelay", juce::var(ms));
    });
    return;
  });
  jsRouter_.registerHandler("setGroupGainDelta", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    // args[0] = JSON array of strip IDs, args[1] = delta dB
    if (args.size() < 2) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    float delta = static_cast<float>((double)args[1]);
    if (!idsVar.isArray()) {
      return;
    }

    safeCallAsync([this, idsVar, delta]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (auto *s = mixer_.getStrip(sid)) {
          float old = s->gainDb.load(std::memory_order_relaxed);
          float nw = juce::jlimit(-120.0f, 6.0f, old + delta);
          subs.push_back(std::make_unique<SetGainAction>(mixer_, sid, old, nw));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group gain delta", std::move(subs), "group-gain"));
        pushMixerState();
      }
    });
    return;
  });
  jsRouter_.registerHandler("setGroupGainAbsolute", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    float gainDb =
        juce::jlimit(-120.0f, 6.0f, static_cast<float>((double)args[1]));
    if (!idsVar.isArray()) {
      return;
    }

    safeCallAsync([this, idsVar, gainDb]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (auto *s = mixer_.getStrip(sid)) {
          float old = s->gainDb.load(std::memory_order_relaxed);
          subs.push_back(
              std::make_unique<SetGainAction>(mixer_, sid, old, gainDb));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group gain absolute", std::move(subs)));
        pushMixerState();
      }
    });
    return;
  });
  jsRouter_.registerHandler("setGroupPlugin", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    int pluginUid = (int)args[1];
    if (!idsVar.isArray()) {
      return;
    }

    safeCallAsync([this, idsVar, pluginUid]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (auto *s = mixer_.getStrip(sid)) {
          int oldUid = s->pluginUid;
          subs.push_back(std::make_unique<SetPluginAction>(
              mixer_, pluginScanner_, sid, oldUid, pluginUid,
              [this]() { pushMixerState(); }));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group set plugin", std::move(subs)));
      }
    });
    return;
  });
  jsRouter_.registerHandler("setGroupLibrary", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    juce::String lib = args[1].toString();
    if (!idsVar.isArray()) {
      return;
    }

    safeCallAsync([this, idsVar, lib]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (auto *s = mixer_.getStrip(sid)) {
          subs.push_back(
              std::make_unique<SetLibraryAction>(mixer_, sid, s->library, lib));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group set library", std::move(subs)));
        pushMixerState();
      }
    });
    return;
  });
  jsRouter_.registerHandler("setGroupExpressionMap", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    std::string entityID = args[1].toString().toStdString();
    if (!idsVar.isArray()) {
      return;
    }

    auto data = entityID.empty() ? nullptr : xmapLibrary_.load(entityID);
    if (!entityID.empty() && !data) {
      return;
    }

    safeCallAsync([this, idsVar, entityID, data]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (auto *s = mixer_.getStrip(sid)) {
          std::string oldID;
          if (s->expressionMap)
            oldID = s->expressionMap->entityID;
          subs.push_back(std::make_unique<SetExpressionMapAction>(
              mixer_, xmapLibrary_, sid, oldID, entityID, data));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group set expression map", std::move(subs)));
        pushMixerState();
      }
    });
    return;
  });
  jsRouter_.registerHandler("removeGroupStrips", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    if (!idsVar.isArray()) {
      return;
    }

    safeCallAsync([this, idsVar]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (mixer_.getStrip(sid)) {
          subs.push_back(std::make_unique<RemoveStripAction>(mixer_, sid));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group remove strips", std::move(subs)));
        pushMixerState();
      }
    });
    return;
  });
  jsRouter_.registerHandler("requestBranches", [this](const juce::var& payload) {
    safeCallAsync([this]() { pushBranches(); });
    return;
  });
  jsRouter_.registerHandler("requestCurrentBranch", [this](const juce::var& payload) {
    safeCallAsync([this]() {
      if (versionStore_) {
        auto b = versionStore_->getStorage().getBranch(currentBranchId_);
        if (b) {
          broadcastMessage("setCurrentBranch",
                           juce::var(juce::String(currentBranchId_)));
          return;
        }
      }
      broadcastMessage("setCurrentBranch", juce::var(juce::String("default")));
    });
    return;
  });
  jsRouter_.registerHandler("openHistoryWindow", [this](const juce::var& payload) {
    safeCallAsync([this]() {
      if (!historyWindow_ && versionStore_) {
        historyWindow_ = std::make_unique<HistoryWindow>(createWebOptions());
        historyWindowLoaded_ = false;
        juce::String root =
            juce::WebBrowserComponent::getResourceProviderRoot();
        historyWindow_->getWebView().goToURL(root + "index.html?view=history");
      }
      if (historyWindow_) {
        historyWindow_->setVisible(true);
        historyWindow_->toFront(true);
        // pushDagHistory() will be called once the window signals ready
      }
    });
    return;
  });
  jsRouter_.registerHandler("requestDagHistory", [this](const juce::var& payload) {
    safeCallAsync([this]() { pushDagHistory(); });
    return;
  });
  jsRouter_.registerHandler("createBranch", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() > 0 && versionStore_) {
      std::string branchName = args[0].toString().toStdString();
      // Optional second arg: a specific version UUID to branch from.
      // If omitted, branches from the current head (existing behaviour).
      std::string fromVersionId =
          (args.size() > 1) ? args[1].toString().toStdString() : "";
      safeCallAsync([this, branchName, fromVersionId]() {
        std::string baseVersionId;
        if (fromVersionId.empty()) {
          // Commit current state and get the resulting branch head VersionId.
          stateManager_.commitCurrentState(mixer_, currentBranchId_);
          auto headOpt = versionStore_->getBranchHead(currentBranchId_);
          if (headOpt)
            baseVersionId = *headOpt;
        } else {
          baseVersionId = fromVersionId;
        }
        if (baseVersionId.empty()) {
          std::cerr << "[createBranch] No base version to branch from"
                    << std::endl;
          return;
        }
        auto newBranchId =
            versionStore_->createBranch(branchName, baseVersionId);
        if (newBranchId.empty()) {
          std::cerr << "[createBranch] Failed to create branch from version: "
                    << baseVersionId << std::endl;
          return;
        }
        // When branching from an explicit version ID, also load that
        // version's state into the mixer so the user lands on the new branch.
        if (!fromVersionId.empty()) {
          auto verOpt = versionStore_->getVersion(baseVersionId);
          if (verOpt) {
            auto stateOpt = versionStore_->getState(verOpt->stateHash);
            if (stateOpt) {
              mixer_.clear();
              undoManager_.clear();
              for (const auto &sh : stateOpt->stripHashes) {
                auto blobOpt = versionStore_->getStripBlob(sh);
                if (blobOpt) {
                  juce::String newId = mixer_.addStrip();
                  if (auto *strip = mixer_.getStrip(newId)) {
                    strip->library = blobOpt->library;
                    strip->family = blobOpt->family;
                    strip->isSolo = blobOpt->isSolo;
                    strip->inputPort = blobOpt->inputPort;
                    strip->inputChannel = blobOpt->inputChannel;
                    strip->pluginUid = blobOpt->pluginUid;
                    strip->gainDb.store(blobOpt->gainDb,
                                        std::memory_order_relaxed);
                    setupStripListener(*strip);
                    if (!blobOpt->expressionMapEntityId.empty()) {
                      auto xd =
                          xmapLibrary_.load(blobOpt->expressionMapEntityId);
                      if (xd)
                        strip->setExpressionMap(xd);
                    }
                    if (strip->pluginUid != 0) {
                      for (const auto &d :
                           pluginScanner_.getKnownPluginList().getTypes()) {
                        if (d.uniqueId == strip->pluginUid) {
                          juce::MemoryBlock sb(blobOpt->pluginState.data(),
                                               blobOpt->pluginState.size());
                          strip->loadPlugin(
                              d, mixer_.getFormatManager(),
                              [strip, sb](bool ok) {
                                if (ok && sb.getSize() > 0 &&
                                    strip->pluginInstance)
                                  strip->pluginInstance->setStateInformation(
                                      sb.getData(), (int)sb.getSize());
                              });
                          break;
                        }
                      }
                    }
                  }
                }
              }
              saveAllStripsToDB();
              mixer_.syncStripsToInstruments(masterList_);
              pushMixerState(false);
              scheduleStateRebuild();
            }
          }
        }
        currentBranchId_ = newBranchId;
        stateManager_.setCurrentBranchId(newBranchId);
        // Creating a branch from a detached version attaches us to the new
        // branch.
        isDetached_ = false;
        pushBranches();
        pushDagHistory();
        broadcastMessage("setCurrentBranch", juce::String(newBranchId));
        broadcastMessage("setDetachedHead", false);
      });
    }
    return;
  });
  jsRouter_.registerHandler("checkoutBranch", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() > 0 && versionStore_) {
      std::string branchId = args[0].toString().toStdString();
      safeCallAsync([this, branchId]() {
        auto b = versionStore_->getStorage().getBranch(branchId);
        if (b) {
          currentBranchId_ = branchId;
          stateManager_.setCurrentBranchId(branchId);
          auto verOpt = versionStore_->getVersion(b->second);
          if (verOpt) {
            auto stateOpt = versionStore_->getState(verOpt->stateHash);
            if (stateOpt) {
              mixer_.clear();
              undoManager_.clear();
              for (const auto &sh : stateOpt->stripHashes) {
                auto blobOpt = versionStore_->getStripBlob(sh);
                if (blobOpt) {
                  juce::String newId = mixer_.addStrip();
                  if (auto *strip = mixer_.getStrip(newId)) {
                    strip->library = blobOpt->library;
                    strip->family = blobOpt->family;
                    strip->isSolo = blobOpt->isSolo;
                    strip->inputPort = blobOpt->inputPort;
                    strip->inputChannel = blobOpt->inputChannel;
                    strip->pluginUid = blobOpt->pluginUid;
                    strip->gainDb.store(blobOpt->gainDb,
                                        std::memory_order_relaxed);

                    std::cerr << "[checkoutBranch] Loaded strip " << strip->id
                              << " gainDb=" << blobOpt->gainDb << " db"
                              << std::endl;

                    setupStripListener(*strip);

                    if (!blobOpt->expressionMapEntityId.empty()) {
                      auto xmapData =
                          xmapLibrary_.load(blobOpt->expressionMapEntityId);
                      if (xmapData)
                        strip->setExpressionMap(xmapData);
                    }

                    if (strip->pluginUid != 0) {
                      for (const auto &d :
                           pluginScanner_.getKnownPluginList().getTypes()) {
                        if (d.uniqueId == strip->pluginUid) {
                          juce::MemoryBlock stateBlock(
                              blobOpt->pluginState.data(),
                              blobOpt->pluginState.size());
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
                  }
                }
              }
              saveAllStripsToDB();
              mixer_.syncStripsToInstruments(masterList_);
              pushMixerState(false);
              scheduleStateRebuild();
              currentVersionId_ = b->second; // head of the checked-out branch
              std::cerr << "[checkoutBranch] Checked out branch id: "
                        << branchId << std::endl;
            } else {
              std::cerr
                  << "[checkoutBranch] Failed to find state for branch id: "
                  << branchId << std::endl;
            }
          } else {
            std::cerr
                << "[checkoutBranch] Failed to find version for branch id: "
                << branchId << std::endl;
          }
        } else {
          std::cerr << "[checkoutBranch] Failed to find branch id: " << branchId
                    << std::endl;
        }
        pushBranches();
        pushDagHistory();
        broadcastMessage("setCurrentBranch", juce::String(branchId));
        pushCurrentVersion();
        // Switching to a branch always clears detached state.
        isDetached_ = false;
        broadcastMessage("setDetachedHead", false);
      });
    }
    return;
  });
  jsRouter_.registerHandler("checkoutVersion", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() > 0 && versionStore_) {
      std::string versionHash = args[0].toString().toStdString();
      safeCallAsync([this, versionHash]() {
        auto verOpt = versionStore_->getVersion(versionHash);
        if (!verOpt) {
          std::cerr << "[checkoutVersion] Version not found: " << versionHash
                    << std::endl;
          return;
        }
        auto stateOpt = versionStore_->getState(verOpt->stateHash);
        if (!stateOpt) {
          std::cerr << "[checkoutVersion] State not found for: " << versionHash
                    << std::endl;
          return;
        }

        // Determine whether this version is the current HEAD of its branch.
        std::string branchId = verOpt->branchId;
        auto branchHead = versionStore_->getBranchHead(branchId);
        bool versionIsHead =
            branchHead.has_value() && *branchHead == versionHash;
        isDetached_ = !versionIsHead;

        currentVersionId_ = versionHash;

        if (!isDetached_) {
          // Normal (attached) checkout — update branch tracking.
          currentBranchId_ = branchId;
          stateManager_.setCurrentBranchId(branchId);
        }
        // Detached: leave currentBranchId_ untouched so Dorico blob stays
        // anchored to the live branch.

        mixer_.clear();
        undoManager_.clear();
        for (const auto &sh : stateOpt->stripHashes) {
          auto blobOpt = versionStore_->getStripBlob(sh);
          if (blobOpt) {
            juce::String newId = mixer_.addStrip();
            if (auto *strip = mixer_.getStrip(newId)) {
              strip->library = blobOpt->library;
              strip->family = blobOpt->family;
              strip->isSolo = blobOpt->isSolo;
              strip->active = blobOpt->active;
              strip->inputPort = blobOpt->inputPort;
              strip->inputChannel = blobOpt->inputChannel;
              strip->pluginUid = blobOpt->pluginUid;
              strip->gainDb.store(blobOpt->gainDb, std::memory_order_relaxed);
              setupStripListener(*strip);
              if (!blobOpt->expressionMapEntityId.empty()) {
                auto xd = xmapLibrary_.load(blobOpt->expressionMapEntityId);
                if (xd)
                  strip->setExpressionMap(xd);
              }
              if (strip->pluginUid != 0) {
                for (const auto &d :
                     pluginScanner_.getKnownPluginList().getTypes()) {
                  if (d.uniqueId == strip->pluginUid) {
                    juce::MemoryBlock sb(blobOpt->pluginState.data(),
                                         blobOpt->pluginState.size());
                    strip->loadPlugin(
                        d, mixer_.getFormatManager(), [strip, sb](bool ok) {
                          if (ok && sb.getSize() > 0 && strip->pluginInstance)
                            strip->pluginInstance->setStateInformation(
                                sb.getData(), (int)sb.getSize());
                        });
                    break;
                  }
                }
              }
            }
          }
        }
        saveAllStripsToDB();
        mixer_.syncStripsToInstruments(masterList_);
        pushMixerState(false);
        scheduleStateRebuild(); // no-op when detached
        pushBranches();
        pushDagHistory();
        if (!isDetached_)
          broadcastMessage("setCurrentBranch", juce::String(branchId));
        pushCurrentVersion();
        broadcastMessage("setDetachedHead", isDetached_);
      });
    }
    return;
  });
  jsRouter_.registerHandler("mergeBranch", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() >= 2 && versionStore_) {
      std::string sourceBranchId = args[0].toString().toStdString();
      std::string targetBranchId = args[1].toString().toStdString();
      safeCallAsync([this, sourceBranchId, targetBranchId]() {
        using MR = fiddle::versioning::MergeResult;
        auto result = versionStore_->merge(sourceBranchId, targetBranchId);

        auto *obj = new juce::DynamicObject();
        obj->setProperty("ok", result.kind != MR::Error);
        obj->setProperty("kind", result.kind == MR::FastForward ? "fast-forward"
                                 : result.kind == MR::ThreeWay  ? "three-way"
                                                                : "error");
        obj->setProperty("error", juce::String(result.error));

        if (result.kind != MR::Error) {
          // If the current branch was the merge target, reload the mixer with
          // the merged state.
          if (targetBranchId == currentBranchId_) {
            auto verOpt = versionStore_->getVersion(result.newHeadId);
            if (verOpt) {
              auto stateOpt = versionStore_->getState(verOpt->stateHash);
              if (stateOpt) {
                mixer_.clear();
                undoManager_.clear();
                for (const auto &sh : stateOpt->stripHashes) {
                  auto blobOpt = versionStore_->getStripBlob(sh);
                  if (blobOpt) {
                    juce::String newId = mixer_.addStrip();
                    if (auto *strip = mixer_.getStrip(newId)) {
                      strip->library = blobOpt->library;
                      strip->family = blobOpt->family;
                      strip->isSolo = blobOpt->isSolo;
                      strip->inputPort = blobOpt->inputPort;
                      strip->inputChannel = blobOpt->inputChannel;
                      strip->pluginUid = blobOpt->pluginUid;
                      strip->gainDb.store(blobOpt->gainDb,
                                          std::memory_order_relaxed);
                      setupStripListener(*strip);
                      if (!blobOpt->expressionMapEntityId.empty()) {
                        auto xd =
                            xmapLibrary_.load(blobOpt->expressionMapEntityId);
                        if (xd)
                          strip->setExpressionMap(xd);
                      }
                      if (strip->pluginUid != 0) {
                        for (const auto &d :
                             pluginScanner_.getKnownPluginList().getTypes()) {
                          if (d.uniqueId == strip->pluginUid) {
                            juce::MemoryBlock sb(blobOpt->pluginState.data(),
                                                 blobOpt->pluginState.size());
                            strip->loadPlugin(
                                d, mixer_.getFormatManager(),
                                [strip, sb](bool ok) {
                                  if (ok && sb.getSize() > 0 &&
                                      strip->pluginInstance)
                                    strip->pluginInstance->setStateInformation(
                                        sb.getData(), (int)sb.getSize());
                                });
                            break;
                          }
                        }
                      }
                    }
                  }
                }
                saveAllStripsToDB();
                mixer_.syncStripsToInstruments(masterList_);
                pushMixerState(false);
                scheduleStateRebuild();
              }
            }
          }
          pushBranches();
          pushDagHistory();
        }
        broadcastMessage("mergeResult", juce::var(obj));
      });
    }
    return;
  });
  jsRouter_.registerHandler("deleteVersion", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    std::cerr << "[IPC] deleteVersion: args.size=" << args.size()
              << " versionStore=" << (versionStore_ ? "ok" : "null")
              << std::endl;
    if (args.size() > 0 && versionStore_) {
      std::string versionHash = args[0].toString().toStdString();
      std::cerr << "[IPC] deleteVersion hash=" << versionHash << std::endl;
      safeCallAsync([this, versionHash]() {
        auto result = versionStore_->deleteVersion(versionHash);
        auto *obj = new juce::DynamicObject();
        obj->setProperty("ok", result.success);
        obj->setProperty("error", juce::String(result.error));
        if (result.success) {
          // If the deletion caused the owning branch to be auto-deleted and
          // that branch was the one we're currently on, switch to a survivor.
          if (!result.deletedBranchId.empty() &&
              result.deletedBranchId == currentBranchId_) {
            auto allBranches = versionStore_->getStorage().listBranches();
            if (!allBranches.empty()) {
              currentBranchId_ = std::get<0>(allBranches[0]);
              stateManager_.setCurrentBranchId(currentBranchId_);
              std::cerr << "[IPC] deleteVersion: branch auto-deleted; "
                           "switched to "
                        << currentBranchId_ << std::endl;
              broadcastMessage("setCurrentBranch",
                               juce::String(currentBranchId_));
            }
          }
          pushBranches();
          pushDagHistory();
        }
        broadcastMessage("deleteResult", juce::var(obj));
      });
    }
    return;
  });
  jsRouter_.registerHandler("saveConfig", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() { saveConfig(); });
    return;
  });
}

void MainComponent::handleJsMessage(const juce::String &type, const juce::var &payload) {
  if (!jsRouter_.handleMessage(type, payload)) {
    std::cerr << "Unknown JS message: " << type << std::endl;
  }
}