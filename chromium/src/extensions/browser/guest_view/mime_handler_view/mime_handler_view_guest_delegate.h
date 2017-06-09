// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_GUEST_VIEW_MIME_HANDLER_VIEW_MIME_HANDLER_VIEW_GUEST_DELEGATE_H_
#define EXTENSIONS_BROWSER_GUEST_VIEW_MIME_HANDLER_VIEW_MIME_HANDLER_VIEW_GUEST_DELEGATE_H_

#include "base/macros.h"
#include "content/public/browser/web_contents.h"

namespace content {
struct ContextMenuParams;
class RenderWidgetHost;
}  // namespace content

namespace extensions {

class MimeHandlerViewGuest;

// A delegate class of MimeHandlerViewGuest that are not a part of chrome.
class MimeHandlerViewGuestDelegate {
 public:
  MimeHandlerViewGuestDelegate() {}
  virtual ~MimeHandlerViewGuestDelegate() {}
  
  // Provides an opportunity to supply a custom view implementation.
  virtual void OverrideWebContentsCreateParams(
      content::WebContents::CreateParams* params) {}

  // Called when a guest is attached or detached.
  virtual bool OnGuestAttached(content::WebContentsView* guest_view,
                               content::WebContentsView* parent_view);
  virtual bool OnGuestDetached(content::WebContentsView* guest_view,
                               content::WebContentsView* parent_view);

  // Called to create the view for the widget.
  virtual bool CreateViewForWidget(
      content::WebContentsView* guest_view,
      content::RenderWidgetHost* render_widget_host);

  // Handles context menu, or returns false if unhandled.
  virtual bool HandleContextMenu(content::WebContents* web_contents,
                                 const content::ContextMenuParams& params);

  // Request to change the zoom level of the top level page containing
  // this view.
  virtual void ChangeZoom(bool zoom_in) {}

 private:
  DISALLOW_COPY_AND_ASSIGN(MimeHandlerViewGuestDelegate);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_GUEST_VIEW_MIME_HANDLER_VIEW_MIME_HANDLER_VIEW_GUEST_DELEGATE_H_
