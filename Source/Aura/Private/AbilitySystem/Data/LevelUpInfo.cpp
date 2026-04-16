// Copyright Druid Mechanics


#include "AbilitySystem/Data/LevelUpInfo.h"

/**
 * 根据累计 XP 线性查找对应的玩家等级
 *
 * 【查找逻辑】
 * - 从 Level=1 起，将 XP 与 LevelUpInformation[Level].LevelUpRequirement 比较
 * - 若满足则升一级继续比较，不满足则停止并返回当前 Level
 * - 数组越界保护：若 Level >= Num()-1，直接返回当前 Level（已达满级）
 *
 * 【数组索引约定】
 * LevelUpInformation[1] = 升到 2 级所需的累计 XP 阈值
 * LevelUpInformation[2] = 升到 3 级所需的累计 XP 阈值
 * （Index 0 为哨兵，实际从 Index 1 开始比较）
 *
 * 调用方：AAuraCharacter::AddToXP / AAuraCharacter::LevelUp
 */
int32 ULevelUpInfo::FindLevelForXP(int32 XP) const
{
	int32 Level = 1;
	bool bSearching = true;
	while (bSearching)
	{
		// LevelUpInformation[1] = 第1级信息
		// LevelUpInformation[2] = 第1级信息
		// 若 Level 已达数组末尾，说明玩家达到最高等级，直接返回
		if (LevelUpInformation.Num() - 1 <= Level) return Level;

		// XP 满足当前 Level 的升级阈值，继续检查下一级
		if (XP >= LevelUpInformation[Level].LevelUpRequirement)
		{
			++Level;
		}
		else
		{
			// XP 不足以升到下一级，当前 Level 即为玩家等级
			bSearching = false;
		}
	}
	return Level;
}
