// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implements the Chrome Extensions Tab Capture API.

#include "chrome/browser/extensions/api/tab_capture/tab_capture_api.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/macros.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/tab_capture/offscreen_tab.h"
#include "chrome/browser/extensions/api/tab_capture/tab_capture_registry.h"
#include "chrome/browser/extensions/extension_renderer_state.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/features/feature.h"
#include "extensions/common/features/feature_provider.h"
#include "extensions/common/features/simple_feature.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/switches.h"

using extensions::api::tab_capture::MediaStreamConstraint;

namespace TabCapture = extensions::api::tab_capture;
namespace GetCapturedTabs = TabCapture::GetCapturedTabs;

namespace extensions {
namespace {

const char kCapturingSameTab[] = "Cannot capture a tab with an active stream.";
const char kFindingTabError[] = "Error finding tab to capture.";
const char kNoAudioOrVideo[] = "Capture failed. No audio or video requested.";
const char kGrantError[] =
    "Extension has not been invoked for the current page (see activeTab "
    "permission). Chrome pages cannot be captured.";

const char kNotWhitelistedForOffscreenTabApi[] =
    "Extension is not whitelisted for use of the unstable, in-development "
    "chrome.tabCapture.captureOffscreenTab API.";
const char kInvalidStartUrl[] =
    "Invalid/Missing/Malformatted starting URL for off-screen tab.";
const char kTooManyOffscreenTabs[] =
    "Extension has already started too many off-screen tabs.";
const char kCapturingSameOffscreenTab[] =
    "Cannot capture the same off-screen tab more than once.";

// Keys/values passed to renderer-side JS bindings.
const char kMediaStreamSource[] = "chromeMediaSource";
const char kMediaStreamSourceId[] = "chromeMediaSourceId";
const char kMediaStreamSourceTab[] = "tab";

// Tab Capture-specific video constraint to enable automatic resolution/rate
// throttling mode in the capture pipeline.
const char kEnableAutoThrottlingKey[] = "enableAutoThrottling";

bool OptionsSpecifyAudioOrVideo(const TabCapture::CaptureOptions& options) {
  return (options.audio && *options.audio) || (options.video && *options.video);
}

bool IsAcceptableOffscreenTabUrl(const GURL& url) {
  return url.is_valid() && (url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("data"));
}

// Add Chrome-specific source identifiers to the MediaStreamConstraints objects
// in |options| to provide references to the |target_contents| to be captured.
void AddMediaStreamSourceConstraints(content::WebContents* target_contents,
                                     TabCapture::CaptureOptions* options) {
  DCHECK(options);
  DCHECK(target_contents);

  MediaStreamConstraint* constraints_to_modify[2] = { nullptr, nullptr };

  if (options->audio && *options->audio) {
    if (!options->audio_constraints)
      options->audio_constraints.reset(new MediaStreamConstraint);
    constraints_to_modify[0] = options->audio_constraints.get();
  }

  bool enable_auto_throttling = false;
  if (options->video && *options->video) {
    if (options->video_constraints) {
      // Check for the Tab Capture-specific video constraint for enabling
      // automatic resolution/rate throttling mode in the capture pipeline.  See
      // implementation comments for content::WebContentsVideoCaptureDevice.
      base::DictionaryValue& props =
          options->video_constraints->mandatory.additional_properties;
      if (!props.GetBooleanWithoutPathExpansion(
              kEnableAutoThrottlingKey, &enable_auto_throttling)) {
        enable_auto_throttling = false;
      }
      // Remove the key from the properties to avoid an "unrecognized
      // constraint" error in the renderer.
      props.RemoveWithoutPathExpansion(kEnableAutoThrottlingKey, nullptr);
    } else {
      options->video_constraints.reset(new MediaStreamConstraint);
    }
    constraints_to_modify[1] = options->video_constraints.get();
  }

  // Format the device ID that references the target tab.
  content::RenderFrameHost* const main_frame = target_contents->GetMainFrame();
  // TODO(miu): We should instead use a "randomly generated device ID" scheme,
  // like that employed by the desktop capture API.  http://crbug.com/163100
  const std::string device_id = base::StringPrintf(
      "web-contents-media-stream://%i:%i%s",
      main_frame->GetProcess()->GetID(),
      main_frame->GetRoutingID(),
      enable_auto_throttling ? "?throttling=auto" : "");

  // Append chrome specific tab constraints.
  for (MediaStreamConstraint* msc : constraints_to_modify) {
    if (!msc)
      continue;
    base::DictionaryValue* constraint = &msc->mandatory.additional_properties;
    constraint->SetString(kMediaStreamSource, kMediaStreamSourceTab);
    constraint->SetString(kMediaStreamSourceId, device_id);
  }
}

}  // namespace

const char* const kBetaChromecastExtensionId =
    "dliochdbjfkdbacpmhlcpmleaejidimm";
const char* const kStableChromecastExtensionId =
    "boadgeojelhgndaghljhdicfkmllpafd";

// Whitelisted extensions that do not check for a browser action grant because
// they provide API's. If there are additional extension ids that need
// whitelisting and are *not* the Chromecast extension, add them to a new
// kWhitelist array.
//
// This list is also used by CastConfigDelegateChromeos to find official Cast
// extensions.
const char* const kChromecastExtensionIds[] = {
    "enhhojjnijigcajfphajepfemndkmdlo",  // Dev
    "fmfcbgogabcbclcofgocippekhfcmgfj",  // Staging
    "hfaagokkkhdbgiakmmlclaapfelnkoah",  // Canary
    kBetaChromecastExtensionId,          // Google Cast Beta
    kStableChromecastExtensionId,        // Google Cast Stable
    "hlgmmjhlnlapooncikdpiiokdjcdpjme",  // Test cast extension
};

const char* const kMediaRouterExtensionIds[] = {
    "pkedcjkdefgpdelpbcmbmeomcjbeemfm",  // Stable
    "ekpaaapppgpmolpcldedioblbkmijaca",  // Beta
};

bool TabCaptureCaptureFunction::RunSync() {
  scoped_ptr<api::tab_capture::Capture::Params> params =
      TabCapture::Capture::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params);

  // Figure out the active WebContents and retrieve the needed ids.
  Browser* target_browser = chrome::FindAnyBrowser(
      GetProfile(), include_incognito(), chrome::GetActiveDesktop());
  if (!target_browser) {
    error_ = kFindingTabError;
    return false;
  }

  content::WebContents* target_contents =
      target_browser->tab_strip_model()->GetActiveWebContents();
  if (!target_contents) {
    error_ = kFindingTabError;
    return false;
  }

  const std::string& extension_id = extension()->id();

  // Make sure either we have been granted permission to capture through an
  // extension icon click or our extension is whitelisted.
  if (!extension()->permissions_data()->HasAPIPermissionForTab(
          SessionTabHelper::IdForTab(target_contents),
          APIPermission::kTabCaptureForTab) &&
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kWhitelistedExtensionID) != extension_id &&
      !SimpleFeature::IsIdInArray(extension_id, kChromecastExtensionIds,
                                  arraysize(kChromecastExtensionIds)) &&
      !SimpleFeature::IsIdInArray(extension_id, kMediaRouterExtensionIds,
                                  arraysize(kMediaRouterExtensionIds))) {
    error_ = kGrantError;
    return false;
  }

  if (!OptionsSpecifyAudioOrVideo(params->options)) {
    error_ = kNoAudioOrVideo;
    return false;
  }

  TabCaptureRegistry* registry = TabCaptureRegistry::Get(GetProfile());
  if (!registry->AddRequest(target_contents, extension_id, false)) {
    // TODO(miu): Allow multiple consumers of single tab capture.
    // http://crbug.com/535336
    error_ = kCapturingSameTab;
    return false;
  }
  AddMediaStreamSourceConstraints(target_contents, &params->options);

  // At this point, everything is set up in the browser process.  It's now up to
  // the custom JS bindings in the extension's render process to request a
  // MediaStream using navigator.webkitGetUserMedia().  The result dictionary,
  // passed to SetResult() here, contains the extra "hidden options" that will
  // allow the Chrome platform implementation for getUserMedia() to start the
  // virtual audio/video capture devices and set up all the data flows.  The
  // custom JS bindings can be found here:
  // chrome/renderer/resources/extensions/tab_capture_custom_bindings.js
  base::DictionaryValue* result = new base::DictionaryValue();
  result->MergeDictionary(params->options.ToValue().get());
  SetResult(result);
  return true;
}

bool TabCaptureGetCapturedTabsFunction::RunSync() {
  TabCaptureRegistry* registry = TabCaptureRegistry::Get(GetProfile());
  base::ListValue* const list = new base::ListValue();
  if (registry)
    registry->GetCapturedTabs(extension()->id(), list);
  SetResult(list);
  return true;
}

bool TabCaptureCaptureOffscreenTabFunction::RunSync() {
  scoped_ptr<TabCapture::CaptureOffscreenTab::Params> params =
      TabCapture::CaptureOffscreenTab::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(params);

  // Make sure the extension is whitelisted for using this API, regardless of
  // Chrome channel.
  //
  // TODO(miu): Use _api_features.json and extensions::Feature library instead.
  // http://crbug.com/537732
  const bool is_whitelisted_extension =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kWhitelistedExtensionID) == extension()->id() ||
      SimpleFeature::IsIdInArray(extension()->id(), kChromecastExtensionIds,
                                 arraysize(kChromecastExtensionIds)) ||
      SimpleFeature::IsIdInArray(extension()->id(), kMediaRouterExtensionIds,
                                 arraysize(kMediaRouterExtensionIds));
  if (!is_whitelisted_extension) {
    error_ = kNotWhitelistedForOffscreenTabApi;
    return false;
  }

  const GURL start_url(params->start_url);
  if (!IsAcceptableOffscreenTabUrl(start_url)) {
    SetError(kInvalidStartUrl);
    return false;
  }

  if (!OptionsSpecifyAudioOrVideo(params->options)) {
    SetError(kNoAudioOrVideo);
    return false;
  }

  content::WebContents* const extension_web_contents = GetSenderWebContents();
  EXTENSION_FUNCTION_VALIDATE(extension_web_contents);
  OffscreenTab* const offscreen_tab =
      OffscreenTabsOwner::Get(extension_web_contents)->OpenNewTab(
          start_url,
          DetermineInitialSize(params->options),
          (is_whitelisted_extension && params->options.presentation_id) ?
              *params->options.presentation_id : std::string());
  if (!offscreen_tab) {
    SetError(kTooManyOffscreenTabs);
    return false;
  }

  if (!TabCaptureRegistry::Get(browser_context())->AddRequest(
          offscreen_tab->web_contents(), extension()->id(), true)) {
    // TODO(miu): Allow multiple consumers of single tab capture.
    // http://crbug.com/535336
    SetError(kCapturingSameOffscreenTab);
    return false;
  }
  AddMediaStreamSourceConstraints(offscreen_tab->web_contents(),
                                  &params->options);

  // At this point, everything is set up in the browser process.  It's now up to
  // the custom JS bindings in the extension's render process to complete the
  // request.  See the comment at end of TabCaptureCaptureFunction::RunSync()
  // for more details.
  base::DictionaryValue* const result = new base::DictionaryValue();
  result->MergeDictionary(params->options.ToValue().get());
  SetResult(result);
  return true;
}

// static
gfx::Size TabCaptureCaptureOffscreenTabFunction::DetermineInitialSize(
    const TabCapture::CaptureOptions& options) {
  static const int kDefaultWidth = 1280;
  static const int kDefaultHeight = 720;

  if (!options.video_constraints)
    return gfx::Size(kDefaultWidth, kDefaultHeight);

  gfx::Size min_size;
  int width = -1;
  int height = -1;
  const base::DictionaryValue& mandatory_properties =
      options.video_constraints->mandatory.additional_properties;
  if (mandatory_properties.GetInteger("maxWidth", &width) && width >= 0 &&
      mandatory_properties.GetInteger("maxHeight", &height) && height >= 0) {
    return gfx::Size(width, height);
  }
  if (mandatory_properties.GetInteger("minWidth", &width) && width >= 0 &&
      mandatory_properties.GetInteger("minHeight", &height) && height >= 0) {
    min_size.SetSize(width, height);
  }

  // Use optional size constraints if no mandatory ones were provided.
  if (options.video_constraints->optional) {
    const base::DictionaryValue& optional_properties =
        options.video_constraints->optional->additional_properties;
    if (optional_properties.GetInteger("maxWidth", &width) && width >= 0 &&
        optional_properties.GetInteger("maxHeight", &height) && height >= 0) {
      if (min_size.IsEmpty()) {
        return gfx::Size(width, height);
      } else {
        return gfx::Size(std::max(width, min_size.width()),
                         std::max(height, min_size.height()));
      }
    }
    if (min_size.IsEmpty() &&
        optional_properties.GetInteger("minWidth", &width) && width >= 0 &&
        optional_properties.GetInteger("minHeight", &height) && height >= 0) {
      min_size.SetSize(width, height);
    }
  }

  // No maximum size was provided, so just return the default size bounded by
  // the minimum size.
  return gfx::Size(std::max(kDefaultWidth, min_size.width()),
                   std::max(kDefaultHeight, min_size.height()));
}

}  // namespace extensions
