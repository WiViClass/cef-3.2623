// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CSSPropertyMetadata_h
#define CSSPropertyMetadata_h

#include "core/CSSPropertyNames.h"
#include "core/CoreExport.h"
#include "wtf/Allocator.h"

namespace blink {

class CORE_EXPORT CSSPropertyMetadata {
    STATIC_ONLY(CSSPropertyMetadata);
public:
    static bool isEnabledProperty(CSSPropertyID);
    static bool isInterpolableProperty(CSSPropertyID);
    static bool isInheritedProperty(CSSPropertyID);

    static void filterEnabledCSSPropertiesIntoVector(const CSSPropertyID*, size_t length, Vector<CSSPropertyID>&);
};

} // namespace blink

#endif // CSSPropertyMetadata