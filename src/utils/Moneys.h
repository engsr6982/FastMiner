#pragma once
#include "Utils.h"
#include "mc/deps/core/mce/UUID.h"
#include "mc/world/actor/player/Player.h"

using string = std::string;

namespace plo::utils {

enum class MoneysType : int { Unknown = -1, LLMoney = 0, ScoreBoard = 1 };

struct MoneysConfig {
    bool       Enable    = false;
    MoneysType MoneyType = MoneysType::LLMoney;
    string     MoneyName = "money";
    string     ScoreName = "";
};

class ScoreBoardMoney {
private:
    string mScoreName;

public:
    ScoreBoardMoney(const string& scoreName = "");

    void setScoreName(const string& scoreName);

    int getScore(Player* player);
    int getScore(Player& player);
    int getScore(mce::UUID uuid);
    int getScore(const string& realName);

    bool setScore(Player* player, int score);
    bool setScore(Player& player, int score);
    bool setScore(mce::UUID uuid, int score);
    bool setScore(const string& realName, int score);

    bool addScore(Player* player, int score);
    bool addScore(Player& player, int score);
    bool addScore(mce::UUID uuid, int score);
    bool addScore(const string& realName, int score);

    bool reduceScore(Player* player, int score);
    bool reduceScore(Player& player, int score);
    bool reduceScore(mce::UUID uuid, int score);
    bool reduceScore(const string& realName, int score);
};


// 继承ScoreBoardMoney
class Moneys : public ScoreBoardMoney {
private:
    bool       mIsEnable = false;
    MoneysType mType     = MoneysType::Unknown; // 经济类型
    string     mMoneyName;                      // 货币名称

    Moneys()                         = default;
    Moneys(const Moneys&)            = delete;
    Moneys& operator=(const Moneys&) = delete;

    void throwUnknownType(Player* player = nullptr);

public:
    static Moneys& getInstance();
    // 单例模式，服务器启动后请调用此方法初始化
    bool updateConfig(MoneysConfig config);

    long long getMoney(Player& player);
    long long getMoney(Player* player);
    long long getMoney(mce::UUID uuid);
    long long getMoney(const string& realName);

    bool setMoney(Player& player, long long money);
    bool setMoney(Player* player, long long money);
    bool setMoney(mce::UUID uuid, long long money);
    bool setMoney(const string& realName, long long money);

    bool addMoney(Player& player, long long money);
    bool addMoney(Player* player, long long money);
    bool addMoney(mce::UUID uuid, long long money);
    bool addMoney(const string& realName, long long money);

    // ! 此 API 已封装经济不足提示，无需手动发送
    bool reduceMoney(Player& player, long long money);
    bool reduceMoney(Player* player, long long money);
    bool reduceMoney(mce::UUID uuid, long long money);
    bool reduceMoney(const string& realName, long long money);

    string getMoneySpendTipStr(Player& player, long long money);
    string getMoneySpendTipStr(Player* player, long long money);
    string getMoneySpendTipStr(mce::UUID uuid, long long money);
    string getMoneySpendTipStr(const string& realName, long long money);

    void sendMoneySpendTip(Player& player, long long money); // 发送经济不足提示
};

} // namespace plo::utils
