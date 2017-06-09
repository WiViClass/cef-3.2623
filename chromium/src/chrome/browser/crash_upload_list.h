// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CRASH_UPLOAD_LIST_H_
#define CHROME_BROWSER_CRASH_UPLOAD_LIST_H_

#include "base/memory/ref_counted.h"
#include "components/upload_list/crash_upload_list.h"

// Factory that creates the platform-specific implementation of the crash
// upload list with the given callback delegate.
scoped_refptr<CrashUploadList> CreateCrashUploadList(
    UploadList::Delegate* delegate);

#endif  // CHROME_BROWSER_CRASH_UPLOAD_LIST_H_
