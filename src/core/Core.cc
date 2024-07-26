#include "Core.h"

#define logger fm::Mod::getInstance().getSelf().getLogger()


namespace core = fm::core;
using namespace core;
using namespace fm::config;
using namespace fm::utils;


struct TaskInfo {
    string     mBlockTypeName;   // 方块命名空间
    int        mDimension;       // 维度
    int        mLimit;           // 最大数量
    int        mCount;           // 当前数量
    int        mDeductDamage;    // 扣除耐久
    int        mDurabilityLevel; // 耐久等级
    ItemStack* mTool;            // 工具
    Player*    mPlayer;          // 玩家
};


ll::event::ListenerPtr          _mPlayerDestroyBlock;
ll::schedule::GameTickScheduler _mScheduler;

std::unordered_map<int, TaskInfo> mTaskList;    // 任务列表
std::unordered_set<size_t>        mRuningBlock; // 正在执行任务的方块

void nextTick(std::function<void()> func) {
    using ll::chrono_literals::operator""_tick;
    _mScheduler.add<ll::schedule::DelayTask>(1_tick, func);
}

size_t core::hash(const BlockPos& v3, const int& dim) {
    std::hash<int> hasf;
    return hasf(v3.x) ^ hasf(v3.y) ^ hasf(v3.z) ^ hasf(dim);
}
size_t core::hash(ll::event::player::PlayerDestroyBlockEvent const& ev) {
    return core::hash(ev.pos(), ev.self().getDimensionId().id);
}
int core::randomInt() {
    static std::random_device                 rd;
    static std::default_random_engine         e(rd());
    static std::uniform_int_distribution<int> dist(0, 99);
    return dist(e);
}


// Core
void core::unRegisterEvent() { ll::event::EventBus::getInstance().removeListener(_mPlayerDestroyBlock); }
void core::registerEvent() {
    _mPlayerDestroyBlock =
        ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerDestroyBlockEvent>(
            [](ll::event::player::PlayerDestroyBlockEvent& ev) {
                if (mRuningBlock.contains(hash(ev)) || ev.isCancelled()) return; // 任务正在执行

#ifdef DEBUG
                logger.warn("DestroyBlock");
#endif

                Player*       player   = &ev.self();
                const Block*  block    = &player->getDimensionBlockSource().getBlock(ev.pos());
                string const& typeName = block->getTypeName();

                if (player->getPlayerGameType() != GameType::Survival ||           // 非生存模式
                    !ConfImpl::isEnable(player->getUuid().asString(), "enable") || // 未启用
                    !ConfImpl::isEnable(player->getUuid().asString(), typeName) || // 方块未启用
                    (ConfImpl::isEnable(player->getUuid().asString(), "sneak") && !player->isSneaking()) // 未潜行
                ) {
                    return;
                }

#ifdef DEBUG
                logger.warn("Prepare Task");
#endif

                BlockPos blockPos = BlockPos(ev.pos());

                nextTick([player, block, blockPos, typeName]() {
                    if (!player->getDimensionBlockSourceConst().getBlock(blockPos).isAir()) return; // 被拦截

                    auto const& iter = ConfImpl::cfg.blocks.find(typeName);

#ifdef DEBUG
                    logger.warn("Finding block...");
#endif

                    if (iter == ConfImpl::cfg.blocks.end()) return; // 方块未配置
                    auto const& confBlock = iter->second;

#ifdef DEBUG
                    logger.warn("Finded!");
#endif

                    auto*         tool     = const_cast<ItemStack*>(&player->getSelectedItem()); // 工具
                    string const& toolType = tool->getTypeName();                                // 工具命名空间
                    auto const&   material = block->getMaterial();

                    // clang-format off
                    bool const hasSilkTouch = EnchantUtils::hasEnchant(Enchant::Type::MiningSilkTouch, *tool);
                    bool const canDestroy   = 
                        (confBlock.tools.empty() || some(confBlock.tools, toolType)) && // 未指定工具、指定工具
                        (material.isAlwaysDestroyable() || tool->canDestroy(block)) && // 可挖掘
                        (
                            (confBlock.silkTouschMod == SilkTouschMod::Forbid && !hasSilkTouch) || // 禁止精准
                            (confBlock.silkTouschMod == SilkTouschMod::Need && hasSilkTouch) || // 需要精准
                            confBlock.silkTouschMod == SilkTouschMod::Unlimited // 无限制
                        );
            // clang-format on

#ifdef DEBUG
                    logger.warn("canDestroy");
#endif
                    // __debugbreak();

                    if (!canDestroy) return;

                    int maxLimit = confBlock.limit; // 最大挖掘数量
                    if (tool->isDamageableItem()) {
                        maxLimit =
                            std::min(maxLimit, (tool->getMaxDamage() - tool->getDamageValue() - 1)); // 计算最大挖掘量

                        if (ConfImpl::cfg.moneys.Enable)
                            maxLimit = std::min(
                                maxLimit,
                                static_cast<int>(Moneys::getInstance().getMoney(player) / confBlock.cost)
                            );
                    }
                    if (maxLimit > 1) {
                        int const id = static_cast<int>(mTaskList.size()) + 1;
                        mTaskList.emplace(
                            id,
                            TaskInfo{
                                typeName,
                                player->getDimensionId().id,
                                maxLimit,
                                0,
                                0,
                                EnchantUtils::getEnchantLevel(::Enchant::Type::MiningDurability, *tool), // 耐久
                                tool,
                                player
                            }
                        );
                        miner(id, blockPos); // 执行任务
#ifdef DEBUG
                        logger.warn("Run Task");
#endif
                    }
                });
            }
        );
}

void core::miner(const int& taskID, const BlockPos stratPos) {
    static std::vector<std::tuple<int, int, int>> _dirDefault = {
        {1,  0,  0 }, // 右
        {-1, 0,  0 }, // 左
        {0,  1,  0 }, // 上
        {0,  -1, 0 }, // 下
        {0,  0,  1 }, // 前
        {0,  0,  -1}  // 后
    };
    // 3x3x3
    static std::vector<std::tuple<int, int, int>> _dirCube = {
        {1,  0,  0 }, // 右
        {-1, 0,  0 }, // 左
        {0,  1,  0 }, // 上
        {0,  -1, 0 }, // 下
        {0,  0,  1 }, // 前
        {0,  0,  -1}, // 后
        {1,  0,  1 },
        {-1, 0,  1 },
        {0,  1,  1 },
        {0,  -1, 1 },
        {1,  1,  1 },
        {-1, -1, 1 },
        {1,  -1, 1 },
        {-1, 1,  1 },
        {1,  0,  -1},
        {-1, 0,  -1},
        {0,  1,  -1},
        {0,  -1, -1},
        {1,  1,  -1},
        {-1, -1, -1},
        {1,  -1, -1},
        {-1, 1,  -1},
        {1,  1,  0 },
        {-1, -1, 0 },
        {1,  -1, 0 },
        {-1, 1,  0 }
    };
    try {
        auto&       task      = mTaskList[taskID];
        auto const& confBlock = ConfImpl::cfg.blocks[task.mBlockTypeName];

        auto& dirs = confBlock.destroyMod == DestroyMod::Cube ? _dirCube : _dirDefault;

        std::queue<std::pair<const Block*, BlockPos>> mQueue;    // 队列
        std::unordered_set<size_t>                    mSeracted; // 已搜索的方块
        std::unordered_set<size_t>                    mQueued;   // 已加入队列的方块

        BlockSource& bs  = task.mPlayer->getDimensionBlockSource();
        auto&        bus = ll::event::EventBus::getInstance();

        mQueue.push({&bs.getBlock(stratPos), stratPos}); // 插入队列

        while (task.mCount < task.mLimit && !mQueue.empty()) {
            auto& [block, pos] = mQueue.front();

            size_t const curHashed = hash(pos, task.mDimension);

            // 处理
            if (!block->isAir()) {
                mRuningBlock.insert(curHashed);

                auto ev = ll::event::player::PlayerDestroyBlockEvent(*task.mPlayer, pos);
                bus.publish(ev);

                mRuningBlock.erase(curHashed);

                if (!ev.isCancelled()) {
                    block->playerDestroy(*task.mPlayer, pos);
                    bs.removeBlock(pos);
                    task.mCount++;
                    if (task.mDurabilityLevel == 0
                        || (task.mDurabilityLevel > 0 && randomInt() < (100 / task.mDurabilityLevel + 1))) {
                        task.mDeductDamage++;
                    }
                }
            }
            mSeracted.insert(curHashed); // 标记为已搜索


            // BFS 搜索
            for (auto& [x, y, z] : dirs) {
                BlockPos     nextPos = BlockPos(pos.x + x, pos.y + y, pos.z + z);
                size_t const hashed  = hash(nextPos, task.mDimension);

                if (mSeracted.contains(hashed) || mQueued.contains(hashed))
                    continue; // 如果已经搜索过或已在队列中，跳过

                const Block*  nextBlock    = &bs.getBlock(nextPos);
                string const& nextTypeName = nextBlock->getTypeName();

                if (task.mBlockTypeName == nextTypeName || some(confBlock.similarBlock, nextTypeName)) {
                    mQueue.push({nextBlock, nextPos}); // 插入队列
                    mQueued.insert(hashed);            // 标记为已加入队列
                }
            }

            mQueue.pop(); // 出队
        }

        // 计算结果
        nextTick([taskID, task, confBlock]() {
            if (task.mCount > 0) {
                auto* pl   = task.mPlayer;
                auto* tool = task.mTool;

                short dmg = tool->getDamageValue() + task.mDeductDamage;
                short max = tool->getMaxDamage();
                if (dmg >= max) dmg = max - 1;
                tool->setDamageValue(dmg);

                auto cost = confBlock.cost * (task.mCount - 1);
                Moneys::getInstance().reduceMoney(pl, cost);

                pl->setSelectedItem(*task.mTool);
                pl->refreshInventory();

                sendText(
                    pl,
                    "连锁 {} 个方块, 消耗 {} 点耐久{}",
                    std::to_string(task.mCount),
                    std::to_string(task.mDeductDamage),
                    ConfImpl::cfg.moneys.Enable ? ", " + ConfImpl::cfg.moneys.MoneyName + std::to_string(cost) : ""
                );
            }
            mTaskList.erase(taskID);
        });
    } catch (std::exception& e) {
        mTaskList.erase(taskID);
        logger.error("Fail in running task: {}", e.what());
    }
}