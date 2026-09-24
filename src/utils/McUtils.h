#pragma once
#include "fmt/format.h"

#include "ll/api/service/Bedrock.h"
#include <ll/api/service/ServiceManager.h>

#include "mc/common/SharedPtr.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/chunk/ChunkSource.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include <mc/deps/core/utility/optional_ref.h>
#include <mc/server/commands/Command.h>
#include <mc/server/commands/CommandContext.h>
#include <mc/server/commands/MinecraftCommands.h>
#include <mc/server/commands/PlayerCommandOrigin.h>
#include <mc/world/Minecraft.h>
#include <mc/world/actor/player/Player.h>
#include <mc/world/level/block/Block.h>
#include <mc/world/level/block/registry/BlockTypeRegistry.h>


#include <algorithm>
#include <memory>
#include <string>


namespace fm::mc_utils {


[[nodiscard]] inline Block const& getDefaultBlockState(HashedString const& type) {
    auto& blockTypeRegistry = BlockTypeRegistry::mBlockTypeRegistry();
    return blockTypeRegistry.mValue.getDefaultBlockState(type);
}

// ItemStackBase::isBlock —— v26.40 起该接口在 SDK 中不再导出，此处复原其实现。
// IDA: v1.21.133 (edu) 0x106887190 验证，原实现为：
//   mItem.counter && mItem.counter->ptr            // lock()
//     && (&item->mBlockType)->counter != nullptr
//     && (&item->mBlockType)->counter->ptr != nullptr
// 即「物品存在且其 mBlockType 弱引用未失效」，无其它副作用。
inline bool isBlock(ItemStackBase const& itemst) {
    auto item = itemst.mItem.lock();
    if (!item) return false;
    auto weak = item->mBlockType.get();
    return weak.get() != nullptr;
}

// IDA: v1.21.0
[[nodiscard]] inline bool isSneaking(Player& player) {
    return SynchedActorDataAccess::getActorFlag(player.getEntityContext(), ActorFlags::Sneaking);
}

// ItemStackBase::canDestroy
[[nodiscard]] inline bool CanDestroyBlock(ItemStackBase const& item, Block const& block) {
    auto legacy = &block.getBlockType();
    return std::find(item.mCanDestroy.begin(), item.mCanDestroy.end(), legacy) != item.mCanDestroy.end();
}

// ItemStackBase::canDestroySpecial
[[nodiscard]] inline bool CanDestroySpecial(ItemStackBase const& item, Block const& block) {
    auto it = item.mItem.get();
    if (!it) {
        return false;
    }
    return it->canDestroySpecial(block);
}

// Template function sendText, usage: sendText() or sendText<LogLevel::Success>().
enum class LogLevel : int { Normal = -1, Debug = 0, Info = 1, Warn = 2, Error = 3, Fatal = 4, Success = 5 };
inline static std::unordered_map<LogLevel, std::string> Color = {
    {LogLevel::Normal,  "§b"},
    {LogLevel::Debug,   "§7"},
    {LogLevel::Info,    "§r"},
    {LogLevel::Warn,    "§e"},
    {LogLevel::Error,   "§c"},
    {LogLevel::Fatal,   "§4"},
    {LogLevel::Success, "§a"}
};

template <typename... Args>
[[nodiscard]] inline std::string format(const std::string& fmt, Args... args) {
    try {
        return fmt::vformat(fmt, fmt::make_format_args(args...));
    } catch (...) {
        return fmt;
    }
}

#ifndef PLUGIN_NAME
#define PLUGIN_NAME "Unknown"
#endif

template <LogLevel type = LogLevel::Normal, typename... Args>
inline void sendText(Player& player, const std::string& fmt, Args&&... args) {
    player.sendMessage(format(PLUGIN_NAME + Color[type] + fmt, args...));
}
template <LogLevel type = LogLevel::Normal, typename... Args>
inline void sendText(CommandOutput& output, const std::string& fmt, Args&&... args) {
    if constexpr (type == LogLevel::Error || type == LogLevel::Fatal) {
        output.error(format(PLUGIN_NAME + Color[type] + fmt, args...));
    } else {
        output.success(format(PLUGIN_NAME + Color[type] + fmt, args...));
    }
}
template <LogLevel type = LogLevel::Normal, typename... Args>
inline void sendText(Player* player, const std::string& fmt, Args&&... args) {
    if (player) {
        return sendText<type>(*player, fmt, args...);
    } else {
        throw std::runtime_error("Failed in sendText: player is nullptr");
    }
}
template <LogLevel type = LogLevel::Normal, typename... Args>
inline void sendText(const std::string& realName, const std::string& fmt, Args&&... args) {
    auto level = ll::service::getLevel();
    if (level.has_value()) {
        return sendText<type>(level->getPlayer(realName), fmt, args...);
    } else {
        throw std::runtime_error("Failed in sendText: level is nullptr");
    }
}


} // namespace fm::mc_utils