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

  // --- Title ---
  NSTextField *titleLabel = [NSTextField labelWithString:@"Fiddle"];
  titleLabel.font = [NSFont systemFontOfSize:20 weight:NSFontWeightBold];
  titleLabel.textColor = [NSColor whiteColor];
  titleLabel.frame = NSMakeRect(16, kViewHeight - 40, 200, 28);
  [container addSubview:titleLabel];

  NSTextField *subtitleLabel =
      [NSTextField labelWithString:@"VST3 → FiddleServer Relay"];
  subtitleLabel.font = [NSFont systemFontOfSize:11 weight:NSFontWeightRegular];
  subtitleLabel.textColor = [NSColor colorWithRed:0.6
                                            green:0.6
                                             blue:0.65
                                            alpha:1.0];
  subtitleLabel.frame = NSMakeRect(16, kViewHeight - 58, 300, 16);
  [container addSubview:subtitleLabel];

  // --- Connection status dot (fiddleTag=100) ---
  FiddleTagView *statusDot = [[FiddleTagView alloc]
      initWithFrame:NSMakeRect(16, kViewHeight - 84, 10, 10)];
  statusDot.wantsLayer = YES;
  statusDot.layer.cornerRadius = 5.0;
  statusDot.layer.backgroundColor =
      [NSColor colorWithRed:0.8 green:0.2 blue:0.2 alpha:1.0].CGColor;
  statusDot.fiddleTag = 100;
  [container addSubview:statusDot];

  // --- Connection status text (fiddleTag=101) ---
  FiddleTagTextField *statusLabel = [[FiddleTagTextField alloc]
      initWithFrame:NSMakeRect(32, kViewHeight - 88, 200, 18)];
  statusLabel.stringValue = @"Disconnected";
  statusLabel.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
  statusLabel.textColor = [NSColor whiteColor];
  statusLabel.bezeled = NO;
  statusLabel.drawsBackground = NO;
  statusLabel.editable = NO;
  statusLabel.selectable = NO;
  statusLabel.fiddleTag = 101;
  [container addSubview:statusLabel];

  // --- Separator ---
  NSView *separator = [[NSView alloc]
      initWithFrame:NSMakeRect(16, kViewHeight - 100, kViewWidth - 32, 1)];
  separator.wantsLayer = YES;
  separator.layer.backgroundColor =
      [NSColor colorWithRed:0.25 green:0.25 blue:0.28 alpha:1.0].CGColor;
  [container addSubview:separator];

  // --- Column headers ---
  NSTextField *chHeader = [NSTextField labelWithString:@"Ch"];
  chHeader.font = [NSFont systemFontOfSize:10 weight:NSFontWeightBold];
  chHeader.textColor = [NSColor colorWithRed:0.5 green:0.5 blue:0.55 alpha:1.0];
  chHeader.frame = NSMakeRect(16, kViewHeight - 120, 30, 14);
  [container addSubview:chHeader];

  NSTextField *progHeader = [NSTextField labelWithString:@"Instrument"];
  progHeader.font = [NSFont systemFontOfSize:10 weight:NSFontWeightBold];
  progHeader.textColor = [NSColor colorWithRed:0.5
                                         green:0.5
                                          blue:0.55
                                         alpha:1.0];
  progHeader.frame = NSMakeRect(50, kViewHeight - 120, 200, 14);
  [container addSubview:progHeader];

  // --- 16 channel rows ---
  CGFloat rowHeight = 16.0;
  CGFloat startY = kViewHeight - 140;

  for (int ch = 0; ch < 16; ++ch) {
    CGFloat y = startY - ch * rowHeight;

    // Channel number
    NSString *chStr = [NSString stringWithFormat:@"%d", ch + 1];
    NSTextField *chLabel = [NSTextField labelWithString:chStr];
    chLabel.font = [NSFont monospacedDigitSystemFontOfSize:11
                                                    weight:NSFontWeightRegular];
    chLabel.textColor = [NSColor colorWithRed:0.55
                                        green:0.55
                                         blue:0.6
                                        alpha:1.0];
    chLabel.frame = NSMakeRect(16, y, 30, 14);
    [container addSubview:chLabel];

    // Program name (fiddleTag = 200 + ch)
    FiddleTagTextField *progLabel = [[FiddleTagTextField alloc]
        initWithFrame:NSMakeRect(50, y, kViewWidth - 66, 14)];
    progLabel.stringValue = @"—";
    progLabel.font = [NSFont systemFontOfSize:11 weight:NSFontWeightRegular];
    progLabel.textColor = [NSColor whiteColor];
    progLabel.bezeled = NO;
    progLabel.drawsBackground = NO;
    progLabel.editable = NO;
    progLabel.selectable = NO;
    progLabel.fiddleTag = 200 + ch;
    [container addSubview:progLabel];
  }

  [parentView addSubview:container];
  containerView_ = container;

  // Start refresh timer (500ms interval)
  // Capture raw pointer — safe because we invalidate timer in removed()
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

  FiddleTagView *dot = (FiddleTagView *)findViewByTag(container, 100);
  if (dot) {
    dot.layer.backgroundColor =
        connected
            ? [NSColor colorWithRed:0.2 green:0.8 blue:0.3 alpha:1.0].CGColor
            : [NSColor colorWithRed:0.8 green:0.2 blue:0.2 alpha:1.0].CGColor;
  }

  FiddleTagTextField *statusLabel =
      (FiddleTagTextField *)findViewByTag(container, 101);
  if (statusLabel) {
    statusLabel.stringValue =
        connected ? @"Connected to FiddleServer" : @"Disconnected";
    statusLabel.textColor =
        connected ? [NSColor colorWithRed:0.2 green:0.8 blue:0.3 alpha:1.0]
                  : [NSColor colorWithRed:0.8 green:0.4 blue:0.4 alpha:1.0];
  }

  // Update channel program labels
  for (int ch = 0; ch < 16; ++ch) {
    FiddleTagTextField *progLabel =
        (FiddleTagTextField *)findViewByTag(container, 200 + ch);
    if (!progLabel)
      continue;

    int program = controller_->getChannelProgram(ch);
    if (program >= 0) {
      std::string name = controller_->getInstrumentName(program);
      if (name.empty())
        name = "Program " + std::to_string(program);
      progLabel.stringValue = [NSString stringWithUTF8String:name.c_str()];
      progLabel.textColor = [NSColor whiteColor];
    } else {
      progLabel.stringValue = @"—";
      progLabel.textColor = [NSColor colorWithRed:0.4
                                            green:0.4
                                             blue:0.45
                                            alpha:1.0];
    }
  }
}

} // namespace fiddle
