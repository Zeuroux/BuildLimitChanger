#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "../dimension_hook.hpp"

class ConfigManager {
public:
    static ConfigManager& getInstance();

    void init(const std::string& folderPath);

    [[nodiscard]] const std::string& getConfigFolderPath() const;
    [[nodiscard]] DimensionHeightRange getDimension(std::string_view name, int16_t defMin, int16_t defMax);
    [[nodiscard]] int getHookVersionOverride();
private:
    ConfigManager() = default;
    ConfigManager(const ConfigManager&)            = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    struct Section {
        std::string name;
        int16_t     min{};
        int16_t     max{};
    };

    static constexpr const char* k_filename = "config.ini";
    static constexpr const char* k_header =
        "# Config Instructions\n"
        "# --------------------------\n"
        "# hook_version:\n"
        "#   Optional override (0, 1, or 2).\n"
        "#   Leave unset unless you understand the implications.\n"
        "#\n"
        "# min:\n"
        "#   Minimum build height.\n"
        "#   Must be divisible by 16; otherwise, it will be adjusted to the nearest valid value.\n"
        "#   Must not go below -2048; otherwise, it will be set to -2048.\n"
        "#   Modifying this may affect world generation and can cause corruption or crashes.\n"
        "#   Ignored on versions 1.16.40 and below.\n"
        "#\n"
        "# max:\n"
        "#   Maximum build height.\n"
        "#   Must be divisible by 16; otherwise, it will be adjusted to the nearest valid value.\n"
        "#   Must not go above 2048; otherwise, it will be set to 2048.\n"
        "#   Setting this below the default may affect world generation or cause crashes.\n"
        "#   Cannot exceed 256 on versions 1.16.40 and below.\n"
        "#   Note: In Minecraft 1.16.40, there is a vanilla game bug where building above\n"
        "#   height 256 causes a crash, even if the configured max is 256.\n"
        "#   This is a Minecraft issue, not caused by this mod.\n"
        "\n"
        "# Extra important notes\n"
        "# --------------------------\n"
        "# This does not work on realms\n"
        "# This does not work on servers, unless the server has this mod installed\n"
        "# This does not work on multiplayer, unless the host and the other players has this mod installed\n"
        "# Server and client must have the same build limit, if they dont it might cause weird glitches\n"
        "# For mobs to spawn above the nether the spawning platform must have a roof because the nether mobs are \"cave spawns\"\n"
        "\n";

    std::string m_folderPath;
    int m_hookVersion{-1};
    std::vector<Section> m_sections;
    
    [[nodiscard]] std::string configFilePath() const;
    [[nodiscard]] Section* findSection(std::string_view name);
    [[nodiscard]] const Section* findSection(std::string_view name) const;
    [[nodiscard]] bool sanitiseSections();
    void refreshIfNeeded();
    void refreshLastWriteTime();
    void load();
    void save();
    void applyDefaults();
};
