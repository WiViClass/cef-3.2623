// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/foreign_session_helper.h"

#include <jni.h>
#include <stddef.h>

#include "base/android/jni_string.h"
#include "base/prefs/pref_service.h"
#include "base/prefs/scoped_user_pref_update.h"
#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile_android.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/android/tab_model/tab_model.h"
#include "chrome/browser/ui/android/tab_model/tab_model_list.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "components/browser_sync/browser/profile_sync_service.h"
#include "components/sync_sessions/open_tabs_ui_delegate.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/web_contents.h"
#include "jni/ForeignSessionHelper_jni.h"

using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;
using base::android::AttachCurrentThread;
using base::android::ConvertUTF16ToJavaString;
using base::android::ConvertUTF8ToJavaString;
using base::android::ConvertJavaStringToUTF8;
using sync_driver::OpenTabsUIDelegate;
using sync_driver::SyncedSession;

namespace {

OpenTabsUIDelegate* GetOpenTabsUIDelegate(Profile* profile) {
  ProfileSyncService* service = ProfileSyncServiceFactory::GetInstance()->
      GetForProfile(profile);

  // Only return the delegate if it exists and it is done syncing sessions.
  if (!service || !service->IsSyncActive())
    return NULL;

  return service->GetOpenTabsUIDelegate();
}

bool ShouldSkipTab(const sessions::SessionTab& session_tab) {
    if (session_tab.navigations.empty())
      return true;

    int selected_index = session_tab.normalized_navigation_index();
    const sessions::SerializedNavigationEntry& current_navigation =
        session_tab.navigations.at(selected_index);

    if (current_navigation.virtual_url().is_empty())
      return true;

    return false;
}

bool ShouldSkipWindow(const sessions::SessionWindow& window) {
  for (std::vector<sessions::SessionTab*>::const_iterator tab_it =
           window.tabs.begin(); tab_it != window.tabs.end(); ++tab_it) {
    const sessions::SessionTab &session_tab = **tab_it;
    if (!ShouldSkipTab(session_tab))
      return false;
  }
  return true;
}

bool ShouldSkipSession(const sync_driver::SyncedSession& session) {
  for (SyncedSession::SyncedWindowMap::const_iterator it =
      session.windows.begin(); it != session.windows.end(); ++it) {
    const sessions::SessionWindow  &window = *(it->second);
    if (!ShouldSkipWindow(window))
      return false;
  }
  return true;
}

void CopyTabToJava(
    JNIEnv* env,
    const sessions::SessionTab& tab,
    ScopedJavaLocalRef<jobject>& j_window) {
  int selected_index = tab.normalized_navigation_index();
  DCHECK_GE(selected_index, 0);
  DCHECK_LT(selected_index, static_cast<int>(tab.navigations.size()));

  const sessions::SerializedNavigationEntry& current_navigation =
      tab.navigations.at(selected_index);

  GURL tab_url = current_navigation.virtual_url();

  Java_ForeignSessionHelper_pushTab(
      env, j_window.obj(),
      ConvertUTF8ToJavaString(env, tab_url.spec()).obj(),
      ConvertUTF16ToJavaString(env, current_navigation.title()).obj(),
      tab.timestamp.ToJavaTime(),
      tab.tab_id.id());
}

void CopyWindowToJava(
    JNIEnv* env,
    const sessions::SessionWindow& window,
    ScopedJavaLocalRef<jobject>& j_window) {
  for (std::vector<sessions::SessionTab*>::const_iterator tab_it =
           window.tabs.begin(); tab_it != window.tabs.end(); ++tab_it) {
    const sessions::SessionTab &session_tab = **tab_it;

    if (ShouldSkipTab(session_tab))
      return;

    CopyTabToJava(env, session_tab, j_window);
  }
}

void CopySessionToJava(
    JNIEnv* env,
    const SyncedSession& session,
    ScopedJavaLocalRef<jobject>& j_session) {
  for (SyncedSession::SyncedWindowMap::const_iterator it =
      session.windows.begin(); it != session.windows.end(); ++it) {
    const sessions::SessionWindow &window = *(it->second);

    if (ShouldSkipWindow(window))
      continue;

    ScopedJavaLocalRef<jobject> last_pushed_window;
    last_pushed_window.Reset(
        Java_ForeignSessionHelper_pushWindow(
            env, j_session.obj(),
            window.timestamp.ToJavaTime(),
            window.window_id.id()));

    CopyWindowToJava(env, window, last_pushed_window);
  }
}

}  // namespace

static jlong Init(JNIEnv* env,
                  const JavaParamRef<jclass>& clazz,
                  const JavaParamRef<jobject>& profile) {
  ForeignSessionHelper* foreign_session_helper = new ForeignSessionHelper(
      ProfileAndroid::FromProfileAndroid(profile));
  return reinterpret_cast<intptr_t>(foreign_session_helper);
}

ForeignSessionHelper::ForeignSessionHelper(Profile* profile)
    : profile_(profile), scoped_observer_(this) {
  ProfileSyncService* service = ProfileSyncServiceFactory::GetInstance()->
      GetForProfile(profile);

  // NOTE: The ProfileSyncService can be null in tests.
  if (service)
    scoped_observer_.Add(service);
}

ForeignSessionHelper::~ForeignSessionHelper() {
}

void ForeignSessionHelper::Destroy(JNIEnv* env,
                                   const JavaParamRef<jobject>& obj) {
  delete this;
}

jboolean ForeignSessionHelper::IsTabSyncEnabled(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  ProfileSyncService* service = ProfileSyncServiceFactory::GetInstance()->
      GetForProfile(profile_);
  return service && service->GetActiveDataTypes().Has(syncer::PROXY_TABS);
}

void ForeignSessionHelper::TriggerSessionSync(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  ProfileSyncService* service = ProfileSyncServiceFactory::GetInstance()->
      GetForProfile(profile_);
  if (!service)
    return;

  const syncer::ModelTypeSet types(syncer::SESSIONS);
  service->TriggerRefresh(types);
}

void ForeignSessionHelper::SetOnForeignSessionCallback(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& callback) {
  callback_.Reset(env, callback);
}

void ForeignSessionHelper::FireForeignSessionCallback() {
  if (callback_.is_null())
    return;

  JNIEnv* env = AttachCurrentThread();
  Java_ForeignSessionCallback_onUpdated(env, callback_.obj());
}

void ForeignSessionHelper::OnSyncConfigurationCompleted() {
  FireForeignSessionCallback();
}

void ForeignSessionHelper::OnForeignSessionUpdated() {
  FireForeignSessionCallback();
}

jboolean ForeignSessionHelper::GetForeignSessions(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& result) {
  OpenTabsUIDelegate* open_tabs = GetOpenTabsUIDelegate(profile_);
  if (!open_tabs)
    return false;

  std::vector<const sync_driver::SyncedSession*> sessions;
  if (!open_tabs->GetAllForeignSessions(&sessions))
    return false;

  // Use a pref to keep track of sessions that were collapsed by the user.
  // To prevent the pref from accumulating stale sessions, clear it each time
  // and only add back sessions that are still current.
  DictionaryPrefUpdate pref_update(profile_->GetPrefs(),
                                   prefs::kNtpCollapsedForeignSessions);
  base::DictionaryValue* pref_collapsed_sessions = pref_update.Get();
  scoped_ptr<base::DictionaryValue> collapsed_sessions(
      pref_collapsed_sessions->DeepCopy());
  pref_collapsed_sessions->Clear();

  ScopedJavaLocalRef<jobject> last_pushed_session;

  // Note: we don't own the SyncedSessions themselves.
  for (size_t i = 0; i < sessions.size(); ++i) {
    const sync_driver::SyncedSession& session = *(sessions[i]);
    if (ShouldSkipSession(session))
      continue;

    const bool is_collapsed = collapsed_sessions->HasKey(session.session_tag);

    if (is_collapsed)
      pref_collapsed_sessions->SetBoolean(session.session_tag, true);

    last_pushed_session.Reset(
        Java_ForeignSessionHelper_pushSession(
            env,
            result,
            ConvertUTF8ToJavaString(env, session.session_tag).obj(),
            ConvertUTF8ToJavaString(env, session.session_name).obj(),
            session.device_type,
            session.modified_time.ToJavaTime()));

    const std::string group_name =
        base::FieldTrialList::FindFullName("TabSyncByRecency");
    if (group_name == "Enabled") {
      // Create a custom window with tabs from all windows included and ordered
      // by recency (GetForeignSessionTabs will do ordering automatically).
      std::vector<const sessions::SessionTab*> tabs;
      open_tabs->GetForeignSessionTabs(session.session_tag, &tabs);
      ScopedJavaLocalRef<jobject> last_pushed_window(
          Java_ForeignSessionHelper_pushWindow(
              env, last_pushed_session.obj(),
              session.modified_time.ToJavaTime(), 0));
      for (const sessions::SessionTab* tab : tabs) {
         if (ShouldSkipTab(*tab))
           continue;
         CopyTabToJava(env, *tab, last_pushed_window);
      }
    } else {
      // Push the full session, with tabs ordered by visual position.
      CopySessionToJava(env, session, last_pushed_session);
    }
  }

  return true;
}

jboolean ForeignSessionHelper::OpenForeignSessionTab(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& j_tab,
    const JavaParamRef<jstring>& session_tag,
    jint session_tab_id,
    jint j_disposition) {
  OpenTabsUIDelegate* open_tabs = GetOpenTabsUIDelegate(profile_);
  if (!open_tabs) {
    LOG(ERROR) << "Null OpenTabsUIDelegate returned.";
    return false;
  }

  const sessions::SessionTab* session_tab;

  if (!open_tabs->GetForeignTab(ConvertJavaStringToUTF8(env, session_tag),
                                session_tab_id,
                                &session_tab)) {
    LOG(ERROR) << "Failed to load foreign tab.";
    return false;
  }

  if (session_tab->navigations.empty()) {
    LOG(ERROR) << "Foreign tab no longer has valid navigations.";
    return false;
  }

  TabAndroid* tab_android = TabAndroid::GetNativeTab(env, j_tab);
  if (!tab_android)
    return false;
  content::WebContents* web_contents = tab_android->web_contents();
  if (!web_contents)
    return false;

  WindowOpenDisposition disposition =
      static_cast<WindowOpenDisposition>(j_disposition);
  SessionRestore::RestoreForeignSessionTab(web_contents,
                                           *session_tab,
                                           disposition);

  return true;
}

void ForeignSessionHelper::DeleteForeignSession(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& session_tag) {
  OpenTabsUIDelegate* open_tabs = GetOpenTabsUIDelegate(profile_);
  if (open_tabs)
    open_tabs->DeleteForeignSession(ConvertJavaStringToUTF8(env, session_tag));
}

// static
bool ForeignSessionHelper::RegisterForeignSessionHelper(JNIEnv* env) {
  return RegisterNativesImpl(env);
}
