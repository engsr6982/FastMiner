# FastMiner

一个基于 Levilamina 的快速连锁采集 Mod(Plugin)。

FastMiner 与其它同类插件一样的采用 BFS，但不同的是，FastMiner 优化整个执行逻辑，做到更快的处理速度。

FastMiner 目前可以做到 2029 个方块，总耗时 1396ms，平均单个方块耗时 0.68ms。

## 安装

```bash
lip install github.com/engsr6982/fastminer
```

> Tip：初次使用，需输入命令 /fm 打开 GUI，开启需要连锁的方块。

## 命令

- /fm 打开设置 GUI
- /fm off 关闭连锁采集
- /fm on 开启连锁采集

## 配置文件

```json
{
  "version": 1,
  "moneys": {
    "Enable": false, // 是否启用经济系统
    "MoneyType": "LLMoney", // 经济类型 LLMoney 或 ScoreBoard
    "MoneyName": "money", // 经济名称
    "ScoreName": "" // 计分板名称
  },
  "blocks": {
    // 方块配置，键(Key) 填写方块命名空间
    "minecraft:acacia_log": {
      "name": "金合欢木原木", // 方块名称
      "cost": 0, // 每个方块消耗的经济
      "limit": 256, // 最大连锁采集数量
      "destroyMod": "Cube", // 破坏模式，支持: Default 和 Cube。 Default搜索相邻的6个面，Cube 3x3x3搜索
      "silkTouschMod": "Unlimited", // 精准采集附魔，Unlimited 无限制、Forbid 禁止精准附魔、Need 需要精准附魔
      "tools": [
        "minecraft:wooden_axe", // 工具限制，如果留空数组，则代表不限制工具类型
        "minecraft:stone_axe",
        "minecraft:iron_axe",
        "minecraft:diamond_axe",
        "minecraft:golden_axe",
        "minecraft:netherite_axe"
      ],
      "similarBlock": [] // 相似方块，填写方块命名空间，连锁时会一起采集
    }
  }
}
```
