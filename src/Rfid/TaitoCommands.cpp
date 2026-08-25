#include "Rfid/TaitoCommands.h"

#include <algorithm>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>

namespace gc::rfid {
namespace {

[[nodiscard]] TaitoCommandResult MakeResult(
    std::span<const std::uint8_t> bytes_after_command,
    std::size_t required_parameter_count,
    bool returns_one) noexcept
{
    const auto available_parameter_count =
        std::min(bytes_after_command.size(), required_parameter_count);

    TaitoCommandResult result;
    result.consumed = 1 + available_parameter_count;
    if (available_parameter_count != required_parameter_count) {
        result.report = jvs::report::invalid_input_parameter;
        return result;
    }

    if (returns_one) {
        result.data.front() = 0x01;
        result.data_size = 1;
    }
    return result;
}

} // namespace

std::optional<TaitoCommandResult> HandleTaitoCommand(
    jvs::CommandId command,
    std::span<const std::uint8_t> bytes_after_command) noexcept
{
    switch (command.value) {
    case taito_command::query_01.value:
    case taito_command::query_03.value:
        return MakeResult(bytes_after_command, 1, true);

    case taito_command::notify_04.value:
        return MakeResult(bytes_after_command, 0, false);

    case taito_command::notify_05.value:
        return MakeResult(bytes_after_command, 2, false);

    default:
        return std::nullopt;
    }
}

} // namespace gc::rfid
