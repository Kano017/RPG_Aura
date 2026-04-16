// Copyright Druid Mechanics

#pragma once

/*
 * LevelUpInfo.h
 *
 * 升级系统数据资产，定义玩家每个等级的升级阈值和奖励。
 *
 * 【累计 XP 设计】
 * LevelUpRequirement 存储的是达到该等级所需的"总经验值"（累计值），而非增量。
 * 例如：
 *   Index 1 → LevelUpRequirement = 300   （达到 2 级需要累计 300 XP）
 *   Index 2 → LevelUpRequirement = 900   （达到 3 级需要累计 900 XP）
 *   Index 3 → LevelUpRequirement = 1800  （达到 4 级需要累计 1800 XP）
 *
 * 【数组索引约定】
 * Index 0 通常留空（1 级不需要任何 XP），实际升级阈值从 Index 1 开始，
 * FindLevelForXP 内部从 Level=1 开始与 LevelUpInformation[Level] 比较，
 * 即 LevelUpInformation[1] 对应"升到 2 级所需的 XP"。
 *
 * 【访问方式】
 * - 挂载在 AuraPlayerState 上，通过 AuraPlayerState::LevelUpInfo 访问
 * - AAuraCharacter 升级时调用 FindLevelForXP 确定当前等级
 */

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LevelUpInfo.generated.h"

/**
 * 单个等级的升级配置数据
 * 数组下标 N 的条目描述"累计达到 N 级升到 N+1 级"所需的条件和奖励
 */
USTRUCT(BlueprintType)
struct FAuraLevelUpInfo
{
	GENERATED_BODY()

	/**
	 * 达到对应等级所需的累计总 XP（不是增量）
	 * 例如 Index 2 的值为 900，表示玩家总 XP >= 900 时即为 3 级
	 */
	UPROPERTY(EditDefaultsOnly)
	int32 LevelUpRequirement = 0;

	/**
	 * 升到此等级时奖励的属性点数量
	 * 玩家用属性点在属性面板中手动加点（Strength / Intelligence 等）
	 */
	UPROPERTY(EditDefaultsOnly)
	int32 AttributePointAward = 1;

	/**
	 * 升到此等级时奖励的技能点数量
	 * 玩家用技能点在法术菜单中解锁或升级技能
	 */
	UPROPERTY(EditDefaultsOnly)
	int32 SpellPointAward = 1;
};

/**
 * ULevelUpInfo
 *
 * 升级信息数据资产，在蓝图编辑器中配置后挂载到 AuraPlayerState。
 */
UCLASS()
class AURA_API ULevelUpInfo : public UDataAsset
{
	GENERATED_BODY()
public:

	/**
	 * 所有等级的升级配置数组
	 * 数组长度决定游戏最高等级上限，Index 0 通常为哨兵值（LevelUpRequirement=0）
	 */
	UPROPERTY(EditDefaultsOnly)
	TArray<FAuraLevelUpInfo> LevelUpInformation;

	/**
	 * 根据当前累计 XP 计算对应的玩家等级（线性顺序查找）
	 *
	 * 算法：从 Level=1 开始，若 XP >= LevelUpInformation[Level].LevelUpRequirement 则 Level++，
	 * 直到 XP 不满足下一级要求或已达数组末尾。
	 *
	 * @param XP  玩家当前累计总经验值
	 * @return    对应的游戏等级（最小为 1）
	 */
	int32 FindLevelForXP(int32 XP) const;
};
