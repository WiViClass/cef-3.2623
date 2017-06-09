// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/history_menu_cocoa_controller.h"

#include "chrome/app/chrome_command_ids.h"  // IDC_HISTORY_MENU
#import "chrome/browser/app_controller_mac.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_live_tab_context.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/host_desktop.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/sessions/core/tab_restore_service.h"
#import "ui/base/cocoa/cocoa_base_utils.h"
#include "ui/base/window_open_disposition.h"

using content::OpenURLParams;
using content::Referrer;

@implementation HistoryMenuCocoaController

- (id)initWithBridge:(HistoryMenuBridge*)bridge {
  if ((self = [super init])) {
    bridge_ = bridge;
    DCHECK(bridge_);
  }
  return self;
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem {
  AppController* controller = [NSApp delegate];
  return ![controller keyWindowIsModal];
}

// Open the URL of the given history item in the current tab.
- (void)openURLForItem:(const HistoryMenuBridge::HistoryItem*)node {
  // If this item can be restored using TabRestoreService, do so. Otherwise,
  // just load the URL.
  sessions::TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(bridge_->profile());
  if (node->session_id && service) {
    Browser* browser = chrome::FindTabbedBrowser(bridge_->profile(), false,
        chrome::HOST_DESKTOP_TYPE_NATIVE);
    BrowserLiveTabContext* context =
        browser ? browser->live_tab_context() : NULL;
    service->RestoreEntryById(context, node->session_id,
        chrome::HOST_DESKTOP_TYPE_NATIVE, UNKNOWN);
  } else {
    DCHECK(node->url.is_valid());
    WindowOpenDisposition disposition =
        ui::WindowOpenDispositionFromNSEvent([NSApp currentEvent]);
    chrome::NavigateParams params(bridge_->profile(), node->url,
        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = disposition;
    chrome::Navigate(&params);
  }
}

- (IBAction)openHistoryMenuItem:(id)sender {
  const HistoryMenuBridge::HistoryItem* item =
      bridge_->HistoryItemForMenuItem(sender);
  [self openURLForItem:item];
}

@end  // HistoryMenuCocoaController
