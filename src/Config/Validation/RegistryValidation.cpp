#include "Config/Validation/RegistryValidation.h"
#include "Config/Validation/CommonValidation.h"
#include "Config/DeclaredEnum.h"

namespace gc::config::validation {
namespace {
using RegistryDwordValidator = rfl::Validator<
    std::int64_t,
    rfl::Minimum<0>
    ,
    rfl::Maximum<4294967295LL>
>;
using RegistryLogLevelValidator = rfl::Validator<
    std::int64_t,
    rfl::Minimum<0>
    ,
    rfl::Maximum<3>
>;

}
RegistryValidationResult ValidateRegistry(const ConfigDocument& document, ValidationContext& context) {
    auto& errors = context.errors;
    const auto server_octets =
        nesys_service::ParseDottedDecimalIpv4(
            document.nesys().server_ip());
    if (!server_octets)
    {
        errors.push_back({
            .path = ConfigPath{"nesys", "server_ip"},
            .code = ConfigErrorCode::invalid_value,
            .message = "expected dotted-decimal IPv4",
        });
    }

    if (!IsDeclaredEnumValue(document.registry().game().country()))
    {
        errors.push_back({
            .path = ConfigPath{"registry", "game", "country"},
            .code = ConfigErrorCode::unsupported_value,
            .message = "unsupported game country",
        });
    }

    ValidateLeaf<RegistryDwordValidator>(
        document.registry().nesys().game_kind(),
        ConfigPath{"registry", "nesys", "game_kind"},
        ConfigErrorCode::out_of_range,
        "expected a registry DWORD value",
        errors);
    ValidateLeaf<RegistryDwordValidator>(
        document.registry().nesys().event_next_time(),
        ConfigPath{"registry", "nesys", "event_next_time"},
        ConfigErrorCode::out_of_range,
        "expected a registry DWORD value",
        errors);
    ValidateLeaf<RegistryDwordValidator>(
        document.registry().nesys().condition_time(),
        ConfigPath{"registry", "nesys", "condition_time"},
        ConfigErrorCode::out_of_range,
        "expected a registry DWORD value",
        errors);
    ValidateLeaf<RegistryLogLevelValidator>(
        document.registry().nesys().log_level(),
        ConfigPath{"registry", "nesys", "log_level"},
        ConfigErrorCode::out_of_range,
        "unsupported registry log level",
        errors);
    auto derived_paths = registry_config::DeriveNesysPaths(
        document.registry().system_path());
    if (!derived_paths)
    {
        errors.push_back({
            .path = ConfigPath{"registry", "system_path"},
            .code = ConfigErrorCode::invalid_path,
            .message = derived_paths.error(),
        });
    }


    return {server_octets, std::move(derived_paths)};
}
}
