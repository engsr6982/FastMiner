#pragma once
#include "ll/api/Config.h"
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

using string = std::string;

namespace fm::config {


enum class SilkTouschMod : int {
    Disable = 0, // 关闭
    Always  = 2, // 无限制
    Smart   = 1  // 智能模式(根据方块决定)
};

enum class DestroyMod : int {
    Default = 0, // 相邻的6个面
    Cube    = 1  // 3x3x3=27
};

using Tools        = std::vector<string>; // 工具
using SimilarBlock = std::vector<string>; // 类似方块

struct BlockItem {
    string name;       // 名称
    string texture;    // 贴图
    int    cost{0};    // 经济
    int    limit{128}; // 上限

    DestroyMod    destroyMod{DestroyMod::Default};       // 破坏方式
    SilkTouschMod silkTouschMod{SilkTouschMod::Disable}; // 精准采集

    Tools        tools{};        // 工具
    SimilarBlock similarBlock{}; // 类似方块
};


using Blocks = std::unordered_map<string, BlockItem>;


#define DefaultWoodBlockTools                                                                                          \
    {                                                                                                                  \
        "minecraft:wooden_axe", "minecraft:stone_axe", "minecraft:iron_axe", "minecraft:diamond_axe",                  \
            "minecraft:golden_axe", "minecraft:netherite_axe"                                                          \
    }


struct Config {
    int version = 1;

    // clang-format off
    Blocks blocks  = {
        {"minecraft:acacia_log", {
            "金合欢木原木",
            "textures/blocks/log_acacia_top",
            0,
            128,
            DestroyMod::Cube,
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:ancient_debris", {
            "远古残骸",
            "textures/blocks/ancient_debris_top"
        }},
        {"minecraft:birch_log", {
            "白桦木原木",
            "textures/blocks/log_birch_top",
            0,
            128,
            DestroyMod::Default,
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:cherry_log", {
            "樱花原木",
            "textures/blocks/cherry_log_top",
            0,
            128,
            DestroyMod::Default,
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:coal_ore", {
            "煤矿石",
            {},
            {},
            {},
            {},
            {},
            {},
            {
                "minecraft:deepslate_coal_ore"
            }
        }}
    }; // 方块
    // clang-format on
};


class ConfImpl {
public:
    ConfImpl()                           = delete;
    ~ConfImpl()                          = delete;
    ConfImpl(ConfImpl const&)            = delete;
    ConfImpl& operator=(ConfImpl const&) = delete;


    static Config                                                       cfg;
    static std::unordered_map<string, std::unordered_map<string, bool>> disabledBlocks;

    static void load();
    static void save();

    static void loadDisabledBlocks();
    static void saveDisabledBlocks();
    static void saveDisabledBlockOnNewThread();

    // 玩家自定义配置
    static bool disableBlock(string const& uuid, string const& typeName);
    static bool enableBlock(string const& uuid, string const& typeName);
    static bool isBlockDisabled(string const& uuid, string const& typeName);
};


} // namespace fm::config