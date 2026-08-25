#pragma once

#include "Input/Types/InputTypes.h"

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::rfid
{
    class FeatureSettings final
    {
    public:
        [[nodiscard]] input::PhysicalKey card_read_key() const noexcept
        {
            return card_read_key_;
        }

        [[nodiscard]] bool testmode_storage_redirect_enabled() const noexcept
        {
            return testmode_storage_redirect_enabled_;
        }

    private:
        FeatureSettings(
            input::PhysicalKey card_read_key,
            bool redirect_enabled) noexcept
            : card_read_key_(card_read_key),
              testmode_storage_redirect_enabled_(redirect_enabled)
        {
        }

        friend class gc::config::ConfigCompiler;
        input::PhysicalKey card_read_key_{};
        bool testmode_storage_redirect_enabled_{};
    };
} // namespace gc::rfid
