#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <unordered_map>
#include <string>
#include <iostream>

namespace fiddle {

class MessageRouter {
public:
    using Handler = std::function<void(const juce::var& payload)>;

    void registerHandler(const juce::String& type, Handler handler) {
        handlers_[type.toStdString()] = std::move(handler);
    }

    bool handleMessage(const juce::String& type, const juce::var& payload) const {
        auto it = handlers_.find(type.toStdString());
        if (it != handlers_.end()) {
            try {
                it->second(payload);
                return true;
            } catch (const std::exception& e) {
                std::cerr << "[MessageRouter] Exception handling message '" << type << "': " << e.what() << std::endl;
                return false;
            }
        }
        return false;
    }

private:
    std::unordered_map<std::string, Handler> handlers_;
};

} // namespace fiddle
