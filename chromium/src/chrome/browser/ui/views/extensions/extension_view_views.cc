// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/extensions/extension_view_views.h"

#include <utility>

#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_host.h"
#include "extensions/common/view_type.h"
#include "ui/events/event.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/widget/widget.h"

#if defined(USE_AURA)
#include "ui/base/cursor/cursor.h"
#endif

ExtensionViewViews::ExtensionViewViews(extensions::ExtensionHost* host,
                                       Browser* browser)
    : views::WebView(browser ? browser->profile() : nullptr),
      host_(host),
      browser_(browser),
      container_(nullptr) {
  SetWebContents(host_->web_contents());
}

ExtensionViewViews::~ExtensionViewViews() {
  if (parent())
    parent()->RemoveChildView(this);
}

Browser* ExtensionViewViews::GetBrowser() {
  return browser_;
}

void ExtensionViewViews::SetVisible(bool is_visible) {
  if (is_visible != visible()) {
    views::WebView::SetVisible(is_visible);

    // Also tell RenderWidgetHostView the new visibility. Despite its name, it
    // is not part of the View hierarchy and does not know about the change
    // unless we tell it.
    content::RenderWidgetHostView* host_view =
        host_->render_view_host()->GetWidget()->GetView();
    if (host_view) {
      if (is_visible)
        host_view->Show();
      else
        host_view->Hide();
    }
  }
}

gfx::NativeView ExtensionViewViews::GetNativeView() {
  return holder()->native_view();
}

void ExtensionViewViews::ResizeDueToAutoResize(
    content::WebContents* web_contents,
    const gfx::Size& new_size) {
  // Don't actually do anything with this information until we have been shown.
  // Size changes will not be honored by lower layers while we are hidden.
  if (!visible()) {
    pending_preferred_size_ = new_size;
    return;
  }

  if (new_size != GetPreferredSize())
    SetPreferredSize(new_size);
}

void ExtensionViewViews::RenderViewCreated(
    content::RenderViewHost* render_view_host) {
  extensions::ViewType host_type = host_->extension_host_type();
  if (host_type == extensions::VIEW_TYPE_EXTENSION_POPUP) {
    host_->render_view_host()->EnableAutoResize(
        gfx::Size(ExtensionPopup::kMinWidth, ExtensionPopup::kMinHeight),
        gfx::Size(ExtensionPopup::kMaxWidth, ExtensionPopup::kMaxHeight));
  }
}

void ExtensionViewViews::HandleKeyboardEvent(
    content::WebContents* source,
    const content::NativeWebKeyboardEvent& event) {
  if (browser_) {
    // Handle lower priority browser shortcuts such as Ctrl-f.
    browser_->HandleKeyboardEvent(source, event);
    return;
  }

  unhandled_keyboard_event_handler_.HandleKeyboardEvent(event,
                                                        GetFocusManager());
}

void ExtensionViewViews::DidStopLoading() {
  // We wait to show the ExtensionViewViews until it has loaded, and the view
  // has actually been created. These can happen in different orders.
  // TODO(devlin): Can they? Isn't the view created during construction?
  if (!visible() && host_->has_loaded_once()) {
    SetVisible(true);
    ResizeDueToAutoResize(web_contents(), pending_preferred_size_);
  }
}

gfx::NativeCursor ExtensionViewViews::GetCursor(const ui::MouseEvent& event) {
  return gfx::kNullCursor;
}

gfx::Size ExtensionViewViews::GetMinimumSize() const {
  // If the minimum size has never been set, returns the preferred size (same
  // behavior as views::View).
  return (minimum_size_ == gfx::Size()) ? GetPreferredSize() : minimum_size_;
}

void ExtensionViewViews::PreferredSizeChanged() {
  View::PreferredSizeChanged();
  if (container_)
    container_->OnExtensionSizeChanged(this);
}

void ExtensionViewViews::OnWebContentsAttached() {
  host_->CreateRenderViewSoon();
  SetVisible(false);
}

namespace extensions {

// static
scoped_ptr<ExtensionView> ExtensionViewHost::CreateExtensionView(
    ExtensionViewHost* host,
    Browser* browser) {
  scoped_ptr<ExtensionViewViews> view(new ExtensionViewViews(host, browser));
  // We own |view_|, so don't auto delete when it's removed from the view
  // hierarchy.
  view->set_owned_by_client();
  return std::move(view);
}

}  // namespace extensions
