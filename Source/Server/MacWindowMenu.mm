#include "MacWindowMenu.h"

#import <AppKit/AppKit.h>

namespace fiddle::mac_window_menu {
namespace {

NSMenu *installedMenu = nil;

NSMenuItem *makeItem(NSString *title, SEL action, NSString *keyEquivalent) {
  auto *item = [[NSMenuItem alloc] initWithTitle:title
                                         action:action
                                  keyEquivalent:keyEquivalent];
  [item setTarget:nil];
  return [item autorelease];
}

NSMenuItem *findWindowMenuItem(NSMenu *mainMenu) {
  for (NSMenuItem *item in [mainMenu itemArray]) {
    if ([[item title] isEqualToString:@"Window"])
      return item;
  }

  return nil;
}

} // namespace

void install() {
  uninstall();

  auto *mainMenu = [NSApp mainMenu];
  auto *windowMenuItem = findWindowMenuItem(mainMenu);
  if (windowMenuItem == nil)
    return;

  auto *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
  [windowMenu addItem:makeItem(@"Minimize", @selector(performMiniaturize:),
                               @"m")];
  [windowMenu addItem:makeItem(@"Zoom", @selector(performZoom:), @"")];
  [windowMenu addItem:[NSMenuItem separatorItem]];
  [windowMenu addItem:makeItem(@"Bring All to Front", @selector(arrangeInFront:),
                               @"")];

  [windowMenuItem setSubmenu:windowMenu];
  [NSApp setWindowsMenu:windowMenu];

  installedMenu = [windowMenu retain];
  [windowMenu release];
}

void uninstall() {
  if (installedMenu == nil)
    return;

  if ([NSApp windowsMenu] == installedMenu)
    [NSApp setWindowsMenu:nil];

  [installedMenu release];
  installedMenu = nil;
}

} // namespace fiddle::mac_window_menu
