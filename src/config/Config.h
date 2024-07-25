#pragma once
#include "ll/api/Config.h"
#include "utils/Moneys.h"
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

    utils::MoneysConfig moneys; // 经济

    // clang-format off
    Blocks blocks  = {
        {"minecraft:acacia_log", {
            "金合欢木原木",
            0,
            128,
            DestroyMod::Cube,
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:ancient_debris", {
            "远古残骸"
        }},
        {"minecraft:birch_log", {
            "白桦木原木",
            0,
            128,
            DestroyMod::Default,
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:cherry_log", {
            "樱花原木",
            0,
            128,
            DestroyMod::Default,
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:coal_ore", {
            "煤矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:deepslate_coal_ore"
            }
        }},
        {"minecraft:copper_ore", {
            "铜矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:deepslate_copper_ore"
            }
        }},
        {"minecraft:crimson_stem", {
            "绯红菌柄",
            {},
            128,
            {},
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:dark_oak_log", {
            "深色橡木原木",
            {},
            128,
            {},
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:deepslate_coal_ore", {
            "深层煤矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:coal_ore"
            }
        }},
        {"minecraft:deepslate_copper_ore", {
            "深层铜矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:copper_ore"
            }
        }},
        {"minecraft:deepslate_diamond_ore", {
            "深层钻石矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:diamond_ore"
            }
        }},
        {"minecraft:deepslate_emerald_ore", {
            "深层绿宝石矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:emerald_ore"
            }
        }},
        {"minecraft:deepslate_gold_ore", {
            "深层金矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:gold_ore"
            }
        }},
        {"minecraft:deepslate_iron_ore", {
            "深层铁矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:iron_ore"
            }
        }},
        {"minecraft:deepslate_lapis_ore", {
            "深层青金石矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:lapis_ore"
            }
        }},
        {"minecraft:diamond_ore", {
            "钻石矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:deepslate_diamond_ore"
            }
        }},
        {"minecraft:emerald_ore", {
            "绿宝石矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:deepslate_emerald_ore"
            }
        }},
        {"minecraft:gold_ore", {
            "金矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:deepslate_gold_ore"
            }
        }},
        {"minecraft:iron_ore", {
            "铁矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:deepslate_iron_ore"
            }
        }},
        {"minecraft:jungle_log", {
            "云杉木原木",
            {},
            128,
            {},
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:lapis_ore", {
            "青金石矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:deepslate_lapis_ore"
            }
        }},
        {"minecraft:lit_deepslate_redstone_ore", {
            "深层红石矿石",
            {},
            128,
            {},
            {},
            {},
            {
                "minecraft:lit_redstone_ore",
                "minecraft:redstone_ore",
                "minecraft:deepslate_redstone_ore"
            }
        }},
        {"minecraft:lit_redstone_ore", {
            "红石矿石",
            128,
            {},
            {},
            {},
            {
                "minecraft:lit_deepslate_redstone_ore",
                "minecraft:redstone_ore",
                "minecraft:deepslate_redstone_ore"
            }
        }},
        {"minecraft:mangrove_log", {
            "红树原木",
            {},
            128,
            {},
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:nether_gold_ore", {
            "下界金矿石",
        }},
        {"minecraft:oak_log", {
            "橡木原木",
            {},
            128,
            {},
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:quartz_ore", {
            "下界石英矿石"
        }},
        {"minecraft:spruce_log", {
            "云杉木原木",
            {},
            128,
            {},
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
        }},
        {"minecraft:warped_stem", {
            "诡异菌柄",
            {},
            128,
            {},
            SilkTouschMod::Smart,
            DefaultWoodBlockTools
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
    static std::unordered_map<string, std::unordered_map<string, bool>> playerSetting;

    static void load();
    static void save();

    static void loadPlayerSetting();
    static void savePlayerSetting();
    static void savePlayerSettingOnNewThread();

    // 玩家自定义配置
    static bool disable(string const& uuid, string const& typeName);
    static bool enable(string const& uuid, string const& typeName);
    static bool isEnable(string const& uuid, string const& typeName);
    static bool setEnable(string const& uuid, string const& typeName, bool isEnable);
};


} // namespace fm::config