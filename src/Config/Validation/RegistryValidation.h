#pragma once
#include "Config/ConfigDocument.h"
#include "Config/RegistryConfig.h"
#include "Config/Validation/ValidationContext.h"
#include "Nesys/Network/NesysNetworkConfig.h"

namespace gc::config::validation {
struct RegistryValidationResult final {
    std::optional<nesys_service::Ipv4Octets> server_octets;
    std::expected<registry_config::DerivedNesysPaths, std::string> derived_paths;
};
[[nodiscard]] RegistryValidationResult ValidateRegistry(const ConfigDocument&, ValidationContext&);
}
