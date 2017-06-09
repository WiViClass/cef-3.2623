// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEVTOOLS_DEVICE_PORT_FORWARDING_CONTROLLER_H_
#define CHROME_BROWSER_DEVTOOLS_DEVICE_PORT_FORWARDING_CONTROLLER_H_

#include <map>

#include "base/macros.h"
#include "base/prefs/pref_change_registrar.h"
#include "chrome/browser/devtools/device/devtools_android_bridge.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefService;
class Profile;

class PortForwardingController {
 public:
  typedef DevToolsAndroidBridge::PortStatus PortStatus;
  typedef DevToolsAndroidBridge::PortStatusMap PortStatusMap;
  typedef DevToolsAndroidBridge::BrowserStatus BrowserStatus;
  typedef DevToolsAndroidBridge::ForwardingStatus ForwardingStatus;

  PortForwardingController(Profile* profile, DevToolsAndroidBridge* bridge);

  virtual ~PortForwardingController();

  ForwardingStatus DeviceListChanged(
      const DevToolsAndroidBridge::RemoteDevices& devices);

 private:
  class Connection;
  typedef std::map<std::string, Connection*> Registry;

  void OnPrefsChange();

  void UpdateConnections();

  DevToolsAndroidBridge* bridge_;
  PrefService* pref_service_;
  PrefChangeRegistrar pref_change_registrar_;
  Registry registry_;

  typedef std::map<int, std::string> ForwardingMap;
  ForwardingMap forwarding_map_;

  DISALLOW_COPY_AND_ASSIGN(PortForwardingController);
};

#endif  // CHROME_BROWSER_DEVTOOLS_DEVICE_PORT_FORWARDING_CONTROLLER_H_
