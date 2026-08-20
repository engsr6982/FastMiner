# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- 重构配置文件相关代码
- 服务端侧添加花费经济显示 #11
- 优化连锁循环逻辑，队列无方块时提前退出
- 修复幽灵连锁数量 bug #12

## [0.17.0] - 2026-07-16

- 适配 LeviLamina v26.20.x

## [0.16.0] - 2026-04-07

- 适配 LeviLamina v26.10.0
- 添加匿名数据统计

## [0.15.0] - 2026-01-26

- 适配 LeviLamina v1.9.x

## [0.14.0-rc.1] - 2026-01-21

- 重写项目架构，新增客户端支持

## [0.13.0-rc.3] - 2025-11-28

- 重写部分代码, 优化性能, 修复特殊情况下可能的崩溃 #7

## [0.13.0-rc.2] - 2025-11-09

- 修复玩家连锁开关在启动时总被设置为关闭的 Bug
- 修复玩家断开连接时中断任务引发的 SEH 异常

## [0.13.0-rc.1] - 2025-11-06

- 适配 LeviLamina v1.7.0
- 重构连锁调度，优化连锁性能

## [0.12.0] - 2025-10-04

- 适配 LeviLamina v1.6.0

## [0.11.0] - 2025-09-23

### Changed

- 适配 LeviLamina v1.5.1

## [0.10.0] - 2025-09-09

### Changed

- 重构经济系统，将经济组件更改为可选组件
- 增强管理表单，支持更多配置修改
- 更新 tooth 格式版本

### Fixed

- 修复部分方块无法连锁 Bug

## [0.9.0] - 2025-7-16

### Changed

- 适配 LeviLamina v1.4.1

## [0.8.0] - 2025-6-14

### Changed

- 适配 LeviLamina v1.3.1

## [0.7.2] - 2025-6-11

### Changed

- 适配 LeviLamina v1.2.1
- 重构部分代码，优化小场景下连锁性能

## [0.7.1] - 2025-3-8

### Changed

- 适配 LeviLamina v1.2.0-rc.1
- 修复 CI/CD

## [0.7.0] - 2025-5-7

### Changed

- 适配 LeviLamina v1.2.0-rc.1

## [0.6.1] - 2025-6-11

- 适配 LeviLamina v1.1.2
- [重构部分代码，优化小场景下连锁性能](#072---2025-6-11)

## [0.6.0] - 2025-3-4

### Changed

- 适配 LeviLamina 1.1.0
- 重构部分代码

## [0.5.0-rc.2] - 2025-1-10

### Changed

- 适配 LeviLamina 1.0.0-rc.3

## [0.5.0-rc.1] - 2025-1-10

### Changed

- 适配 LeviLamina 1.0.0-rc.2

## [0.4.0] - 2024-8-25

### Added

- 兼容 “Unbreakable” nbt

## [0.3.0] - 2024-7-31

### Added

- 新增 添加方块 GUI

### Fixed

- 修复部分方块无法开启连锁 Bug

## [0.2.1] - 2024-7-29

### Fixed

- 修复连锁时刷物品 Bug

## [0.2.0] - 2024-7-26

- 优化代码

## [0.1.0] - 2024-7-26
