#pragma once

#include "LuaPlugin.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <type_traits>

namespace fiddle {

/// A parameter value for the UI and agents.
inline juce::var luaParamToVar(const LuaParamValue &value) {
  return std::visit([](const auto &v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, std::string>)
      return juce::var(juce::String::fromUTF8(v.c_str()));
    else if constexpr (std::is_same_v<T, std::int64_t>)
      return juce::var(static_cast<juce::int64>(v));
    else
      return juce::var(v);
  }, value);
}

/// A value from the UI, before LuaPlugin checks it against the parameter;
/// nullopt for anything that isn't a bool, number or string.
inline std::optional<LuaParamValue> luaParamFromVar(const juce::var &value) {
  if (value.isBool())
    return LuaParamValue(static_cast<bool>(value));
  if (value.isInt() || value.isInt64())
    return LuaParamValue(static_cast<std::int64_t>(static_cast<juce::int64>(value)));
  if (value.isDouble())
    return LuaParamValue(static_cast<double>(value));
  if (value.isString())
    return LuaParamValue(value.toString().toStdString());
  return std::nullopt;
}

/// A script's parameters with their current values, in declared order.
inline juce::var luaParamsToVar(const LuaPlugin &plugin) {
  juce::Array<juce::var> params;
  for (const auto &param : plugin.meta().params) {
    auto *item = new juce::DynamicObject();
    item->setProperty("name", juce::String::fromUTF8(param.name.c_str()));
    item->setProperty("label", juce::String::fromUTF8(param.label.c_str()));
    item->setProperty("type", juce::String(param.type));
    if (!param.unit.empty())
      item->setProperty("unit", juce::String::fromUTF8(param.unit.c_str()));
    if (!param.description.empty())
      item->setProperty("description", juce::String::fromUTF8(param.description.c_str()));
    if (param.type == "int" || param.type == "float") {
      item->setProperty("min", param.min);
      item->setProperty("max", param.max);
      item->setProperty("step", param.step);
      item->setProperty("control", juce::String(param.control));
    }
    if (param.type == "enum") {
      juce::Array<juce::var> options;
      for (const auto &option : param.options)
        options.add(juce::String::fromUTF8(option.c_str()));
      item->setProperty("options", options);
    }
    item->setProperty("default", luaParamToVar(param.defaultValue));
    item->setProperty("value", luaParamToVar(*plugin.paramValue(param.name)));
    params.add(juce::var(item));
  }
  return params;
}

} // namespace fiddle
