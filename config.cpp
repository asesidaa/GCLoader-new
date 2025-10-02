#include "config.h"
#include <filesystem>
#include <fstream>
#include "rfl/toml.hpp"
#include "rfl/json.hpp"

ConfigManager::ConfigManager()
{
    const auto configPath = std::filesystem::current_path () / "config.toml";
    if (!std::filesystem::exists(configPath))
    {
        PLOG_ERROR << "Config file not found: " << configPath.c_str() << std::endl;
        throw std::runtime_error("Config file not found");
    }

    std::ifstream configFile(configPath);
    if (!configFile.is_open())
    {
        PLOG_ERROR << "Failed to open config file: " << configPath.c_str() << std::endl;
        throw std::runtime_error("Failed to open config file");
    }

    auto result = rfl::toml::read<InputConfig>(configFile);
    if (result)
    {
        config = result.value();
        PLOG_DEBUG << "Config file parsed successfully" << std::endl;
        PLOG_DEBUG << "Loaded: " << rfl::json::write(config) << std::endl;
        return;
    }

    auto error = result.error();
    PLOG_ERROR << "Failed to parse config file: " << error.what() << std::endl;
    throw std::runtime_error("Failed to parse config file: " + error.what());
}
