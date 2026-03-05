#pragma once

#include <juce_core/juce_core.h>

namespace fiddle {

/// Format an ISO-8601 timestamp string into a smart abbreviated form.
/// - Today:      "5:03 PM"
/// - Yesterday:  "Yesterday 5:03 PM"
/// - This week:  "Mon 5:03 PM"
/// - This year:  "Feb 20, 5:03 PM"
/// - Older:      "Feb 20, 2025"
/// Returns the original string unchanged if it can't be parsed.
inline juce::String formatTimestamp(const juce::String &isoTimestamp) {
  if (isoTimestamp.isEmpty())
    return {};

  // Parse ISO-8601: "2026-02-26T13:03:42.994-08:00"
  // juce::Time::fromISO8601 handles this format.
  auto parsed = juce::Time::fromISO8601(isoTimestamp);
  if (parsed.toMilliseconds() == 0)
    return isoTimestamp; // Failed to parse, return as-is

  auto now = juce::Time::getCurrentTime();

  // Get local time components
  int pYear = parsed.getYear();
  int pMonth = parsed.getMonth();
  int pDay = parsed.getDayOfMonth();

  int nYear = now.getYear();
  int nMonth = now.getMonth();
  int nDay = now.getDayOfMonth();

  int hour12 = parsed.getHours() > 12   ? parsed.getHours() - 12
               : parsed.getHours() == 0 ? 12
                                        : parsed.getHours();
  juce::String ampm = parsed.getHours() >= 12 ? "PM" : "AM";
  juce::String mins = juce::String(parsed.getMinutes()).paddedLeft('0', 2);
  juce::String secs = juce::String(parsed.getSeconds()).paddedLeft('0', 2);

  // Format time: with seconds for today, without for older
  juce::String timePart = juce::String(hour12) + ":" + mins + " " + ampm;

  // Same day? Include seconds for precision
  if (pYear == nYear && pMonth == nMonth && pDay == nDay) {
    return juce::String(hour12) + ":" + mins + ":" + secs + " " + ampm;
  }

  // Yesterday?
  auto yesterday = now - juce::RelativeTime::days(1);
  if (pYear == yesterday.getYear() && pMonth == yesterday.getMonth() &&
      pDay == yesterday.getDayOfMonth()) {
    return "Yesterday " + timePart;
  }

  // Within the last 7 days?
  auto weekAgo = now - juce::RelativeTime::days(7);
  if (parsed > weekAgo) {
    static const char *dayNames[] = {"Sun", "Mon", "Tue", "Wed",
                                     "Thu", "Fri", "Sat"};
    return juce::String(dayNames[parsed.getDayOfWeek()]) + " " + timePart;
  }

  // Month names
  static const char *monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  // Same year?
  if (pYear == nYear) {
    return juce::String(monthNames[pMonth]) + " " + juce::String(pDay) + ", " +
           timePart;
  }

  // Older
  return juce::String(monthNames[pMonth]) + " " + juce::String(pDay) + ", " +
         juce::String(pYear);
}

} // namespace fiddle
