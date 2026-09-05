#pragma once
// SPDX-License-Identifier: CC0-1.0


#include <string>

namespace gc::audio {

struct AsioControlPanelRequest {
    std::string driver_name;
};

} // namespace gc::audio
