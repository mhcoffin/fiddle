#import "FiddlePlugView.h"
#import "FiddleController.h"

#import <Cocoa/Cocoa.h>

#include <string>

using namespace Steinberg;

// Simple NSView subclass that holds a tag number for identification
@interface FiddleTagView : NSView
@property(nonatomic) NSInteger fiddleTag;
@end

@implementation FiddleTagView
@end

@interface FiddleTagTextField : NSTextField
@property(nonatomic) NSInteger fiddleTag;
@end

@implementation FiddleTagTextField
@end

// Helper to find subviews by fiddleTag
static NSView *findViewByTag(NSView *parent, NSInteger tag) {
  for (NSView *sub in parent.subviews) {
    if ([sub isKindOfClass:[FiddleTagView class]] &&
        ((FiddleTagView *)sub).fiddleTag == tag)
      return sub;
    if ([sub isKindOfClass:[FiddleTagTextField class]] &&
        ((FiddleTagTextField *)sub).fiddleTag == tag)
      return sub;
  }
  return nil;
}

namespace fiddle {

//----------------------------------------------------------------------
// FUnknown implementation
//----------------------------------------------------------------------
tresult PLUGIN_API FiddlePlugView::queryInterface(const TUID iid, void **obj) {
  if (FUnknownPrivate::iidEqual(iid, IPlugView::iid) ||
      FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
    addRef();
    *obj = static_cast<IPlugView *>(this);
    return kResultOk;
  }
  *obj = nullptr;
  return kNoInterface;
}

uint32 PLUGIN_API FiddlePlugView::addRef() { return ++refCount_; }

uint32 PLUGIN_API FiddlePlugView::release() {
  uint32 count = --refCount_;
  if (count == 0)
    delete this;
  return count;
}

//----------------------------------------------------------------------
// Construction
//----------------------------------------------------------------------
FiddlePlugView::FiddlePlugView(FiddleController *controller)
    : controller_(controller) {}

FiddlePlugView::~FiddlePlugView() {
  if (refreshTimer_) {
    [(NSTimer *)refreshTimer_ invalidate];
    refreshTimer_ = nullptr;
  }
  if (containerView_) {
    [(NSView *)containerView_ removeFromSuperview];
    containerView_ = nullptr;
  }
}

//----------------------------------------------------------------------
// IPlugView
//----------------------------------------------------------------------
tresult PLUGIN_API FiddlePlugView::isPlatformTypeSupported(FIDString type) {
  if (strcmp(type, kPlatformTypeNSView) == 0)
    return kResultTrue;
  return kResultFalse;
}

tresult PLUGIN_API FiddlePlugView::attached(void *parent, FIDString type) {
  if (strcmp(type, kPlatformTypeNSView) != 0)
    return kResultFalse;

  NSView *parentView = (__bridge NSView *)parent;

  // Create container view
  NSRect frame = NSMakeRect(0, 0, kViewWidth, kViewHeight);
  NSView *container = [[NSView alloc] initWithFrame:frame];
  container.wantsLayer = YES;
  container.layer.backgroundColor =
      [NSColor colorWithRed:0.12 green:0.12 blue:0.14 alpha:1.0].CGColor;

  // --- Config name (colored by connection status, fiddleTag=102) ---
  FiddleTagTextField *configNameLabel = [[FiddleTagTextField alloc]
      initWithFrame:NSMakeRect(16, kViewHeight - 32, kViewWidth - 32, 24)];
  configNameLabel.stringValue = @"No config loaded";
  configNameLabel.font = [NSFont systemFontOfSize:18
                                           weight:NSFontWeightSemibold];
  configNameLabel.textColor = [NSColor colorWithRed:0.8
                                              green:0.3
                                               blue:0.3
                                              alpha:1.0];
  configNameLabel.bezeled = NO;
  configNameLabel.drawsBackground = NO;
  configNameLabel.editable = NO;
  configNameLabel.selectable = NO;
  configNameLabel.fiddleTag = 102;
  [container addSubview:configNameLabel];

  // --- Config path (subdued, fiddleTag=103) ---
  FiddleTagTextField *configPathLabel = [[FiddleTagTextField alloc]
      initWithFrame:NSMakeRect(16, kViewHeight - 50, kViewWidth - 32, 14)];
  configPathLabel.stringValue = @"";
  configPathLabel.font = [NSFont systemFontOfSize:10
                                           weight:NSFontWeightRegular];
  configPathLabel.textColor = [NSColor colorWithRed:0.5
                                              green:0.5
                                               blue:0.55
                                              alpha:1.0];
  configPathLabel.bezeled = NO;
  configPathLabel.drawsBackground = NO;
  configPathLabel.editable = NO;
  configPathLabel.selectable = NO;
  configPathLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
  configPathLabel.fiddleTag = 103;
  [container addSubview:configPathLabel];

  [parentView addSubview:container];
  containerView_ = container;

  // Start refresh timer (500ms interval)
  FiddlePlugView *viewPtr = this;
  refreshTimer_ =
      [NSTimer scheduledTimerWithTimeInterval:0.5
                                      repeats:YES
                                        block:^(NSTimer *_Nonnull timer) {
                                          viewPtr->refreshDisplay();
                                        }];

  // Initial refresh
  refreshDisplay();

  return kResultOk;
}

tresult PLUGIN_API FiddlePlugView::removed() {
  if (refreshTimer_) {
    [(NSTimer *)refreshTimer_ invalidate];
    refreshTimer_ = nullptr;
  }
  if (containerView_) {
    [(NSView *)containerView_ removeFromSuperview];
    containerView_ = nullptr;
  }
  return kResultOk;
}

tresult PLUGIN_API FiddlePlugView::getSize(ViewRect *size) {
  if (!size)
    return kInvalidArgument;
  *size = ViewRect(0, 0, kViewWidth, kViewHeight);
  return kResultOk;
}

tresult PLUGIN_API FiddlePlugView::onSize(ViewRect * /*newSize*/) {
  return kResultOk;
}

tresult PLUGIN_API FiddlePlugView::canResize() { return kResultFalse; }

tresult PLUGIN_API FiddlePlugView::checkSizeConstraint(ViewRect *rect) {
  if (!rect)
    return kInvalidArgument;
  rect->right = rect->left + kViewWidth;
  rect->bottom = rect->top + kViewHeight;
  return kResultOk;
}

tresult PLUGIN_API FiddlePlugView::onWheel(float) { return kResultFalse; }
tresult PLUGIN_API FiddlePlugView::onKeyDown(char16, int16, int16) {
  return kResultFalse;
}
tresult PLUGIN_API FiddlePlugView::onKeyUp(char16, int16, int16) {
  return kResultFalse;
}
tresult PLUGIN_API FiddlePlugView::onFocus(TBool) { return kResultOk; }

tresult PLUGIN_API FiddlePlugView::setFrame(IPlugFrame *frame) {
  frame_ = frame;
  return kResultOk;
}

//----------------------------------------------------------------------
// Refresh display from controller state
//----------------------------------------------------------------------
void FiddlePlugView::refreshDisplay() {
  if (!containerView_ || !controller_)
    return;

  NSView *container = (NSView *)containerView_;

  // Update connection status
  bool connected = controller_->isConnected();

  // Update config name — colored by connection status
  FiddleTagTextField *configNameLabel =
      (FiddleTagTextField *)findViewByTag(container, 102);
  if (configNameLabel) {
    std::string configName = controller_->getConfigName();
    if (!configName.empty()) {
      configNameLabel.stringValue =
          [NSString stringWithUTF8String:configName.c_str()];
      configNameLabel.textColor =
          connected ? [NSColor colorWithRed:0.2 green:0.8 blue:0.3 alpha:1.0]
                    : [NSColor colorWithRed:0.9 green:0.35 blue:0.35 alpha:1.0];
    } else {
      configNameLabel.stringValue = @"No config loaded";
      configNameLabel.textColor = [NSColor colorWithRed:0.5
                                                  green:0.5
                                                   blue:0.55
                                                  alpha:1.0];
    }
  }

  // Update config path (subdued)
  FiddleTagTextField *configPathLabel =
      (FiddleTagTextField *)findViewByTag(container, 103);
  if (configPathLabel) {
    std::string configPath = controller_->getConfigPath();
    if (!configPath.empty()) {
      configPathLabel.stringValue =
          [NSString stringWithUTF8String:configPath.c_str()];
    } else {
      configPathLabel.stringValue = @"";
    }
  }
}

} // namespace fiddle
