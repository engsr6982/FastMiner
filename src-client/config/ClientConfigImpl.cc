#include "ClientConfigImpl.h"
#include "config/StaticGlobalConfigHost.h"

#include "ll/api/Config.h"

#include <filesystem>

#include "mc/deps/core/string/HashedString.h"
#include "mc/world/item/VanillaItemNames.h"
#include <mc/world/level/block/VanillaBlockTypeIds.h>
#include <tuple>


namespace fm::client {

constexpr std::string_view kFileName = "ClientConfig.json";

ll::Expected<> ClientConfigImpl::load(const std::filesystem::path& baseDir) {
    namespace fs = std::filesystem;

    auto path = baseDir / kFileName;
    if (!fs::exists(path) || !ll::config::loadConfig(model, path)) {
        save(baseDir);
    }
    return {};
}

ll::Expected<> ClientConfigImpl::save(const std::filesystem::path& baseDir) {
    auto path = baseDir / kFileName;
    ll::config::saveConfig(model, path);
    return {};
}

void ClientConfigImpl::buildDefault() {
    model.blockDefault.name = "全局默认配置";

    model.overrides.clear();
    model.overrides = {
        // clang-format off
        // 树木类
        {VanillaBlockTypeIds::AcaciaLog(), BlockOverride{
            .name = "金合欢木原木",
            .destroyMode = DestroyMode::Cube,
        }},
        {VanillaBlockTypeIds::BirchLog(), BlockOverride{
            .name = "白桦木原木",
        }},
        {VanillaBlockTypeIds::CherryLog(), BlockOverride{
            .name = "樱花原木",
            .destroyMode = DestroyMode::Cube,
        }},
        {VanillaBlockTypeIds::DarkOakLog(), BlockOverride{
            .name = "深色橡木原木",
        }},
        {VanillaBlockTypeIds::JungleLog(), BlockOverride{
            .name = "丛林原木",
        }},
        {VanillaBlockTypeIds::MangroveLog(), BlockOverride{
            .name = "红树原木",
        }},
        {VanillaBlockTypeIds::SpruceLog(), BlockOverride{
            .name = "云杉木原木",
        }},
        {VanillaBlockTypeIds::OakLog(), BlockOverride{
            .name = "橡木原木",
        }},
        {VanillaBlockTypeIds::WarpedStem(), BlockOverride{
            .name = "诡异菌柄",
        }},
        {VanillaBlockTypeIds::CrimsonStem(), BlockOverride{
            .name = "绯红菌柄",
        }},

        // 矿石类
        {VanillaBlockTypeIds::AncientDebris(), BlockOverride{
            .name = "远古残骸"
        }},
        {VanillaBlockTypeIds::CoalOre(), BlockOverride{
            .name = "煤矿石",
            .similarBlock = {
                VanillaBlockTypeIds::DeepslateCoalOre()
            }
        }},
        {VanillaBlockTypeIds::CopperOre(), BlockOverride{
            .name = "铜矿石",
            .similarBlock = {
                VanillaBlockTypeIds::DeepslateCopperOre()
            }
        }},
        {VanillaBlockTypeIds::DiamondOre(), BlockOverride{
            .name = "钻石矿石",
            .similarBlock = {
                VanillaBlockTypeIds::DeepslateDiamondOre()
            }
        }},
        {VanillaBlockTypeIds::EmeraldOre(), BlockOverride{
            .name = "绿宝石矿石",
            .similarBlock = {
                VanillaBlockTypeIds::DeepslateEmeraldOre()
            }
        }},
        {VanillaBlockTypeIds::GoldOre(), BlockOverride{
            .name = "金矿石",
            .similarBlock = {
                VanillaBlockTypeIds::DeepslateGoldOre()
            }
        }},
        {VanillaBlockTypeIds::IronOre(), BlockOverride{
            .name = "铁矿石",
            .similarBlock = {
                VanillaBlockTypeIds::DeepslateIronOre()
            }
        }},
        {VanillaBlockTypeIds::LapisOre(), BlockOverride{
            .name = "青金石矿石",
            .similarBlock = {
                VanillaBlockTypeIds::DeepslateLapisOre()
            }
        }},
        {VanillaBlockTypeIds::LitRedstoneOre(), BlockOverride{
            .name = "红石矿石",
            .similarBlock = {
                VanillaBlockTypeIds::LitDeepslateRedstoneOre(),
                VanillaBlockTypeIds::RedstoneOre(),
                VanillaBlockTypeIds::DeepslateRedstoneOre()
            }
        }},
        {VanillaBlockTypeIds::NetherGoldOre(), BlockOverride{
            .name = "下界金矿石",
        }},
        {VanillaBlockTypeIds::QuartzOre(), BlockOverride{
            .name = "下界石英矿石"
        }},
        {VanillaBlockTypeIds::DeepslateCoalOre(), BlockOverride{
            .name = "深层煤矿石",
            .similarBlock = {
                VanillaBlockTypeIds::CoalOre()
            }
        }},
        {VanillaBlockTypeIds::DeepslateCopperOre(), BlockOverride{
            .name = "深层铜矿石",
            .similarBlock = {
                VanillaBlockTypeIds::CopperOre()
            }
        }},
        {VanillaBlockTypeIds::DeepslateDiamondOre(), BlockOverride{
            .name = "深层钻石矿石",
            .similarBlock = {
                VanillaBlockTypeIds::DiamondOre()
            }
        }},
        {VanillaBlockTypeIds::DeepslateEmeraldOre(), BlockOverride{
            .name = "深层绿宝石矿石",
            .similarBlock = {
                VanillaBlockTypeIds::EmeraldOre()
            }
        }},
        {VanillaBlockTypeIds::DeepslateGoldOre(), BlockOverride{
            .name = "深层金矿石",
            .similarBlock = {
                VanillaBlockTypeIds::GoldOre()
            }
        }},
        {VanillaBlockTypeIds::DeepslateIronOre(), BlockOverride{
            .name = "深层铁矿石",
            .similarBlock = {
                VanillaBlockTypeIds::IronOre()
            }
        }},
        {VanillaBlockTypeIds::DeepslateLapisOre(), BlockOverride{
            .name = "深层青金石矿石",
            .similarBlock = {
                VanillaBlockTypeIds::LapisOre()
            }
        }},
        {VanillaBlockTypeIds::LitDeepslateRedstoneOre(), BlockOverride{
            .name = "深层红石矿石",
            .similarBlock = {
                VanillaBlockTypeIds::LitRedstoneOre(),
                VanillaBlockTypeIds::RedstoneOre(),
                VanillaBlockTypeIds::DeepslateRedstoneOre()
            }
        }}
        // clang-format on
    };

    // 批量右键默认白名单：木->下界合金 斧/锹/锄 + 作物种子/茎（甘蔗/竹子）
    // 物品类型-律取 VanillaItemNames 常量，避免硬编码物品 ID
    namespace VIN  = VanillaItemNames;
    model.useItems = {
        // 斧
        VIN::WoodenAxe(),
        VIN::StoneAxe(),
        VIN::IronAxe(),
        VIN::GoldenAxe(),
        VIN::DiamondAxe(),
        VIN::NetheriteAxe(),
        // 锹
        VIN::WoodenShovel(),
        VIN::StoneShovel(),
        VIN::IronShovel(),
        VIN::GoldenShovel(),
        VIN::DiamondShovel(),
        VIN::NetheriteShovel(),
        // 锄
        VIN::WoodenHoe(),
        VIN::StoneHoe(),
        VIN::IronHoe(),
        VIN::GoldenHoe(),
        VIN::DiamondHoe(),
        VIN::NetheriteHoe(),
        // 作物 / 种子
        VIN::SugarCane(),     // 甘蔗
        VIN::WheatSeeds(),    // 小麦种子
        VIN::PumpkinSeeds(),  // 南瓜种子
        VIN::MelonSeeds(),    // 西瓜种子
        VIN::BeetrootSeeds(), // 甜菜根种子
        VIN::Carrot(),        // 胡萝卜
        VIN::Potato(),        // 土豆

        VIN::BoneMeal(), // 骨粉
    };
}

void ClientConfigImpl::buildRuntimeMap() {
    runtimeConfigMap.clear();
    for (auto& [type, ov] : model.overrides) {
        runtimeConfigMap.emplace(getBlockIdCached(type), buildRuntimeSingleBlockConfig(ov));
    }

    default_ = buildRuntimeSingleBlockConfig(model.blockDefault);
}

RuntimeSingleBlockConfigPtr ClientConfigImpl::buildRuntimeSingleBlockConfig(SingleBlockConfig single) {
    auto rtConfig   = std::make_shared<RuntimeSingleBlockConfig>(single);
    rtConfig->limit = single.limit;
    rtConfig->similarBlock.reserve(single.similarBlock.size());
    for (auto& block : single.similarBlock) {
        rtConfig->similarBlock.emplace(getBlockIdCached(block));
    }
    return rtConfig;
}

RuntimeSingleBlockConfigPtr ClientConfigImpl::getDefault() { return default_; }


void ClientConfigImpl::addSimilarBlock(std::string const& blockType, std::string const& similarBlockType) {
    auto iter = model.overrides.find(blockType);
    if (iter == model.overrides.end()) {
        return;
    }
    if (iter->second.similarBlock.insert(similarBlockType).second) {
        (void)StaticGlobalConfigHost::save();
        if (auto ptr = getRuntimeSingleBlockConfig(blockType)) {
            ptr->similarBlock.insert(getBlockIdCached(similarBlockType));
        }
    }
}
void ClientConfigImpl::removeSimilarBlock(std::string const& blockType, std::string const& similarBlockType) {
    auto iter = model.overrides.find(blockType);
    if (iter == model.overrides.end()) {
        return;
    }
    if (iter->second.similarBlock.erase(similarBlockType)) {
        (void)StaticGlobalConfigHost::save();
        if (auto ptr = getRuntimeSingleBlockConfig(blockType)) {
            ptr->similarBlock.erase(getBlockIdCached(similarBlockType));
        }
    }
}
void ClientConfigImpl::updateBlockConfig(std::string const& oldType, std::string const& newType, BlockOverride config) {
    if (oldType == newType) {
        auto iter = model.overrides.find(oldType);
        if (iter == model.overrides.end()) {
            return;
        }
        iter->second = std::move(config);
        (void)StaticGlobalConfigHost::save();

        if (auto ptr = getRuntimeSingleBlockConfig(oldType)) {
            ptr->rawConfig = iter->second;
            ptr->limit     = iter->second.limit;
        }
        return;
    }

    removeBlockConfig(oldType);
    addBlockConfig(newType, std::move(config));
}
void ClientConfigImpl::addBlockConfig(std::string const& blockType, BlockOverride config) {
    auto result = model.overrides.emplace(blockType, std::move(config));
    if (result.second) {
        (void)StaticGlobalConfigHost::save();
        if (auto ptr = buildRuntimeSingleBlockConfig(result.first->second)) {
            runtimeConfigMap.emplace(getBlockIdCached(blockType), ptr);
        }
    }
}
void ClientConfigImpl::removeBlockConfig(std::string const& blockType) {
    auto iter = model.overrides.find(blockType);
    if (iter == model.overrides.end()) {
        return;
    }
    model.overrides.erase(iter);
    (void)StaticGlobalConfigHost::save();
    runtimeConfigMap.erase(getBlockIdCached(blockType));
}

} // namespace fm::client