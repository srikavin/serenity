/*
 * Copyright (c) 2023, Srikavin Ramkumar <me@srikavin.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/CORSSettingAttribute.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/urls-and-fetching.html#cors-settings-attributes
CORSSettingAttribute cors_setting_attribute_from_keyword(DeprecatedString keyword)
{
    if (keyword.is_null()) {
        // its missing value default is the No CORS state
        return CORSSettingAttribute::NoCORS;
    }
    if (keyword.is_empty() || keyword.equals_ignoring_case("anonymous"sv)) {
        return CORSSettingAttribute::Anonymous;
    }
    if (keyword.equals_ignoring_case("use-credentials"sv)) {
        return CORSSettingAttribute::UseCredentials;
    }

    // The attribute's invalid value default is the Anonymous state
    return CORSSettingAttribute::Anonymous;
}
}
