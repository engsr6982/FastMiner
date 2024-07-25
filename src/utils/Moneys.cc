#include "Moneys.h"
#include "LLMoney.h"
#include "Text.h"
#include "ll/api/i18n/I18n.h"
#include "ll/api/service/Bedrock.h"
#include "mc/deps/core/mce/UUID.h"
#include "mc/world/actor/player/PlayerScoreSetFunction.h"
#include "mc/world/level/Level.h"
#include "mc/world/scores/ScoreInfo.h"
#include <mc/world/actor/player/Player.h>
#include <mc/world/scores/Objective.h>
#include <mc/world/scores/Scoreboard.h>
#include <mc/world/scores/ScoreboardId.h>
#include <memory>
#include <stdexcept>


// disable warning C4244: conversion from 'double' to 'int', possible loss of data
#pragma warning(disable : 4244)

namespace plo::utils {
using ll::i18n_literals::operator""_tr;


// ScoreBoard
ScoreBoardMoney::ScoreBoardMoney(const string& scoreName) { this->mScoreName = string(scoreName); }

void ScoreBoardMoney::setScoreName(const string& scoreName) { this->mScoreName = string(scoreName); }

int ScoreBoardMoney::getScore(Player& player) {
    Scoreboard& scoreboard = ll::service::getLevel()->getScoreboard();
    Objective*  obj        = scoreboard.getObjective(this->mScoreName);
    if (!obj) {
        sendText<Level::Error>(player, "[Moneys] 插件错误: 找不到指定的计分板: {}"_tr(this->mScoreName));
        std::runtime_error("Fail in ScoreBoardMoney::getScore: Objective not found");
        return 0;
    }
    const ScoreboardId& id = scoreboard.getScoreboardId(player);
    if (!id.isValid()) {
        scoreboard.createScoreboardId(player);
    }
    return obj->getPlayerScore(id).mScore;
}
int ScoreBoardMoney::getScore(Player* player) { return this->getScore(*player); }
int ScoreBoardMoney::getScore(mce::UUID uuid) { return getScore(ll::service::getLevel()->getPlayer(uuid)); }
int ScoreBoardMoney::getScore(const string& realName) { return getScore(ll::service::getLevel()->getPlayer(realName)); }


bool ScoreBoardMoney::setScore(Player& player, int score) {
    Scoreboard& scoreboard = ll::service::getLevel()->getScoreboard();
    Objective*  obj        = scoreboard.getObjective(this->mScoreName);
    if (!obj) {
        sendText<Level::Error>(player, "[Moneys] 插件错误: 找不到指定的计分板: "_tr(this->mScoreName));
        std::runtime_error("Fail in ScoreBoardMoney::setScore: Objective not found");
        return false;
    }
    const ScoreboardId& id = scoreboard.getScoreboardId(player);
    if (!id.isValid()) {
        scoreboard.createScoreboardId(player);
    }
    bool isSuccess = false;
    scoreboard.modifyPlayerScore(isSuccess, id, *obj, score, PlayerScoreSetFunction::Set);
    return isSuccess;
}
bool ScoreBoardMoney::setScore(Player* player, int score) { return this->setScore(*player, score); }
bool ScoreBoardMoney::setScore(mce::UUID uuid, int score) {
    return setScore(ll::service::getLevel()->getPlayer(uuid), score);
}
bool ScoreBoardMoney::setScore(const string& realName, int score) {
    return setScore(ll::service::getLevel()->getPlayer(realName), score);
}


bool ScoreBoardMoney::addScore(Player& player, int score) {
    Scoreboard& scoreboard = ll::service::getLevel()->getScoreboard();
    Objective*  obj        = scoreboard.getObjective(this->mScoreName);
    if (!obj) {
        sendText<Level::Error>(player, "[Moneys] 插件错误: 找不到指定的计分板: "_tr(this->mScoreName));
        std::runtime_error("Fail in ScoreBoardMoney::addScore: Objective not found");
        return false;
    }
    const ScoreboardId& id = scoreboard.getScoreboardId(player);
    if (!id.isValid()) {
        scoreboard.createScoreboardId(player);
    }
    bool isSuccess = false;
    scoreboard.modifyPlayerScore(isSuccess, id, *obj, score, PlayerScoreSetFunction::Add);
    return isSuccess;
}
bool ScoreBoardMoney::addScore(Player* player, int score) { return this->addScore(*player, score); }
bool ScoreBoardMoney::addScore(mce::UUID uuid, int score) {
    return addScore(ll::service::getLevel()->getPlayer(uuid), score);
}
bool ScoreBoardMoney::addScore(const string& realName, int score) {
    return addScore(ll::service::getLevel()->getPlayer(realName), score);
}


bool ScoreBoardMoney::reduceScore(Player& player, int score) {
    Scoreboard& scoreboard = ll::service::getLevel()->getScoreboard();
    Objective*  obj        = scoreboard.getObjective(this->mScoreName);
    if (!obj) {
        sendText<Level::Error>(player, "[Moneys] 插件错误: 找不到指定的计分板: "_tr(this->mScoreName));
        std::runtime_error("Fail in ScoreBoardMoney::reduceScore: Objective not found");
        return false;
    }
    const ScoreboardId& id = scoreboard.getScoreboardId(player);
    if (!id.isValid()) {
        scoreboard.createScoreboardId(player);
    }
    bool isSuccess = false;
    scoreboard.modifyPlayerScore(isSuccess, id, *obj, score, PlayerScoreSetFunction::Subtract);
    return isSuccess;
}
bool ScoreBoardMoney::reduceScore(Player* player, int score) { return this->reduceScore(*player, score); }
bool ScoreBoardMoney::reduceScore(mce::UUID uuid, int score) {
    return reduceScore(ll::service::getLevel()->getPlayer(uuid), score);
}
bool ScoreBoardMoney::reduceScore(const string& realName, int score) {
    return reduceScore(ll::service::getLevel()->getPlayer(realName), score);
}


// Moneys
Moneys& Moneys::getInstance() {
    static Moneys instance;
    return instance;
}

void Moneys::throwUnknownType(Player* player) {
    if (player) {
        sendText<Level::Error>(player, "[Moneys] 插件错误: 未知的货币类型"_tr());
    }
    std::runtime_error("Fail in Moneys::throwUnknownType: Unknown currency type");
}

bool Moneys::updateConfig(MoneysConfig config) {
    this->mIsEnable  = config.Enable;
    this->mMoneyName = config.MoneyName;
    this->mType      = config.MoneyType;
    setScoreName(config.ScoreName);
    return true;
}


long long Moneys::getMoney(Player& player) {
    switch (mType) {
    case MoneysType::ScoreBoard:
        return getScore(player);
    case MoneysType::LLMoney:
        return LLMoney_Get(player.getXuid());
    default:
        throwUnknownType(&player);
        return 0;
    }
}
long long Moneys::getMoney(Player* player) { return this->getMoney(*player); }
long long Moneys::getMoney(mce::UUID uuid) { return getMoney(ll::service::getLevel()->getPlayer(uuid)); }
long long Moneys::getMoney(const string& realName) { return getMoney(ll::service::getLevel()->getPlayer(realName)); }


bool Moneys::setMoney(Player& player, long long money) {
    switch (mType) {
    case MoneysType::ScoreBoard:
        return setScore(player, money);
    case MoneysType::LLMoney:
        return LLMoney_Set(player.getXuid(), money);
    default:
        throwUnknownType(&player);
        return false;
    }
}
bool Moneys::setMoney(Player* player, long long money) { return this->setMoney(*player, money); }
bool Moneys::setMoney(mce::UUID uuid, long long money) {
    return setMoney(ll::service::getLevel()->getPlayer(uuid), money);
}
bool Moneys::setMoney(const string& realName, long long money) {
    return setMoney(ll::service::getLevel()->getPlayer(realName), money);
}


bool Moneys::addMoney(Player& player, long long money) {
    if (!mIsEnable) return true; // 未启用则不限制
    switch (mType) {
    case MoneysType::ScoreBoard:
        return addScore(player, money);
    case MoneysType::LLMoney:
        return LLMoney_Add(player.getXuid(), money);
    default:
        throwUnknownType(&player);
        return false;
    }
}
bool Moneys::addMoney(Player* player, long long money) { return this->addMoney(*player, money); }
bool Moneys::addMoney(mce::UUID uuid, long long money) {
    return addMoney(ll::service::getLevel()->getPlayer(uuid), money);
}
bool Moneys::addMoney(const string& realName, long long money) {
    return addMoney(ll::service::getLevel()->getPlayer(realName), money);
}


bool Moneys::reduceMoney(Player& player, long long money) {
    if (!mIsEnable) return true;     // 未启用则不限制
    if (getMoney(player) >= money) { // 防止玩家余额不足
        switch (mType) {
        case MoneysType::ScoreBoard:
            return reduceScore(player, money);
        case MoneysType::LLMoney:
            return LLMoney_Reduce(player.getXuid(), money);
        default:
            throwUnknownType(&player);
            return false;
        }
    }
    // 封装提示信息
    sendMoneySpendTip(player, money);
    return false;
}
bool Moneys::reduceMoney(Player* player, long long money) { return this->reduceMoney(*player, money); }
bool Moneys::reduceMoney(mce::UUID uuid, long long money) {
    return reduceMoney(ll::service::getLevel()->getPlayer(uuid), money);
}
bool Moneys::reduceMoney(const string& realName, long long money) {
    return reduceMoney(ll::service::getLevel()->getPlayer(realName), money);
}


string Moneys::getMoneySpendTipStr(Player& player, long long money) {
    long long currentMoney = mIsEnable ? getMoney(player) : 0;
    string    prefix       = "\n§l[§uTip§r§l]§r ";

    if (mIsEnable)
        return prefix
             + "此操作消耗§6{0}§r:§e{1}§r | 当前§6{2}§r:§d{3}§r | 剩余§6{4}§r:§s{5}§r | {6}"_tr(
                   mMoneyName,
                   money,
                   mMoneyName,
                   currentMoney,
                   mMoneyName,
                   currentMoney - money,
                   currentMoney >= money ? "§6{}§r§a充足§r"_tr(mMoneyName) : "§6{}§r§c不足§r"_tr(mMoneyName)
             );
    else return prefix + "经济系统未启用，此操作不消耗§6{0}§r"_tr(mMoneyName);
}
string Moneys::getMoneySpendTipStr(Player* player, long long money) { return getMoneySpendTipStr(*player, money); }
string Moneys::getMoneySpendTipStr(mce::UUID uuid, long long money) {
    return getMoneySpendTipStr(ll::service::getLevel()->getPlayer(uuid), money);
}
string Moneys::getMoneySpendTipStr(const string& realName, long long money) {
    return getMoneySpendTipStr(ll::service::getLevel()->getPlayer(realName), money);
}

void Moneys::sendMoneySpendTip(Player& player, long long money) {
    sendText<Level::Error>(
        player,
        "[Moneys] 操作失败，此操作需要{0}:{1}，当前{2}:{3}"_tr(mMoneyName, money, mMoneyName, getMoney(player))
    );
}

} // namespace plo::utils
