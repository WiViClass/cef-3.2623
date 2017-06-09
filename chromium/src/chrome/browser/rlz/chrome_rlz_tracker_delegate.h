// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RLZ_CHROME_RLZ_TRACKER_DELEGATE_H_
#define CHROME_BROWSER_RLZ_CHROME_RLZ_TRACKER_DELEGATE_H_

#include "base/callback.h"
#include "base/macros.h"
#include "components/omnibox/browser/omnibox_event_global_tracker.h"
#include "components/rlz/rlz_tracker_delegate.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

class Profile;

// ChromeRLZTrackerDelegate implements RLZTrackerDelegate abstract interface
// and provides access to Chrome features.
class ChromeRLZTrackerDelegate : public rlz::RLZTrackerDelegate,
                                 public content::NotificationObserver {
 public:
  ChromeRLZTrackerDelegate();
  ~ChromeRLZTrackerDelegate() override;

  static bool IsGoogleDefaultSearch(Profile* profile);
  static bool IsGoogleHomepage(Profile* profile);
  static bool IsGoogleInStartpages(Profile* profile);

 private:
  // RLZTrackerDelegate implementation.
  void Cleanup() override;
  bool IsOnUIThread() override;
  base::SequencedWorkerPool* GetBlockingPool() override;
  net::URLRequestContextGetter* GetRequestContext() override;
  bool GetBrand(std::string* brand) override;
  bool IsBrandOrganic(const std::string& brand) override;
  bool GetReactivationBrand(std::string* brand) override;
  bool ShouldEnableZeroDelayForTesting() override;
  bool GetLanguage(base::string16* language) override;
  bool GetReferral(base::string16* referral) override;
  bool ClearReferral() override;
  void SetOmniboxSearchCallback(const base::Closure& callback) override;
  void SetHomepageSearchCallback(const base::Closure& callback) override;

  // content::NotificationObserver implementation:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  // Called when a URL is opened from the Omnibox.
  void OnURLOpenedFromOmnibox(OmniboxLog* log);

  content::NotificationRegistrar registrar_;
  base::Closure on_omnibox_search_callback_;
  base::Closure on_homepage_search_callback_;

  // Subscription for receiving callbacks that a URL was opened from the
  // omnibox.
  scoped_ptr<base::CallbackList<void(OmniboxLog*)>::Subscription>
      omnibox_url_opened_subscription_;

  DISALLOW_COPY_AND_ASSIGN(ChromeRLZTrackerDelegate);
};

#endif  // CHROME_BROWSER_RLZ_CHROME_RLZ_TRACKER_DELEGATE_H_
