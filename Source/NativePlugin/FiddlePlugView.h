#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"

#include <atomic>

// Forward declarations (Objective-C types can't appear in C++ headers directly)
#ifdef __OBJC__
@class NSView;
@class NSTimer;
#else
using NSView = void;
using NSTimer = void;
#endif

namespace fiddle {

class FiddleController;

/**
 * Custom IPlugView for the Fiddle VST3 plugin.
 *
 * Displays:
 * - Connection status (green/red indicator)
 * - Active config name and path
 *
 * Uses native macOS NSView via Objective-C++.
 * Non-resizable, fixed size.
 */
class FiddlePlugView : public Steinberg::IPlugView {
public:
  explicit FiddlePlugView(FiddleController *controller);
  ~FiddlePlugView();

  // --- IPlugView ---
  Steinberg::tresult PLUGIN_API
  isPlatformTypeSupported(Steinberg::FIDString type) override;
  Steinberg::tresult PLUGIN_API attached(void *parent,
                                         Steinberg::FIDString type) override;
  Steinberg::tresult PLUGIN_API removed() override;
  Steinberg::tresult PLUGIN_API onWheel(float distance) override;
  Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16 key,
                                          Steinberg::int16 keyCode,
                                          Steinberg::int16 modifiers) override;
  Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16 key,
                                        Steinberg::int16 keyCode,
                                        Steinberg::int16 modifiers) override;
  Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect *size) override;
  Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect *newSize) override;
  Steinberg::tresult PLUGIN_API onFocus(Steinberg::TBool state) override;
  Steinberg::tresult PLUGIN_API setFrame(Steinberg::IPlugFrame *frame) override;
  Steinberg::tresult PLUGIN_API canResize() override;
  Steinberg::tresult PLUGIN_API
  checkSizeConstraint(Steinberg::ViewRect *rect) override;

  // --- FUnknown ---
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                               void **obj) override;
  Steinberg::uint32 PLUGIN_API addRef() override;
  Steinberg::uint32 PLUGIN_API release() override;

  // --- Custom ---
  /// Called periodically by NSTimer to refresh the display
  void refreshDisplay();

  static constexpr int kViewWidth = 320;
  static constexpr int kViewHeight = 110;

private:
  FiddleController *controller_; // non-owning
  Steinberg::IPlugFrame *frame_ = nullptr;
  NSView *containerView_ = nullptr;
  NSTimer *refreshTimer_ = nullptr;
  std::atomic<Steinberg::uint32> refCount_{1};
};

} // namespace fiddle
