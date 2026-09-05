#pragma once
#include "Config/ConfigError.h"

namespace gc::config::validation {
struct ValidationContext final {
    ConfigErrors& errors;
};
}
