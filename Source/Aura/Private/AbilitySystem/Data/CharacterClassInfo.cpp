// Copyright Druid Mechanics


#include "AbilitySystem/Data/CharacterClassInfo.h"

/**
 * 根据职业枚举查找对应的默认信息结构体
 *
 * 使用 FindChecked 而非 Find：若枚举对应条目在数据资产中未配置，
 * 会直接触发 check 断言并崩溃，方便在开发阶段及早发现配置遗漏。
 * 调用方：UAuraAbilitySystemLibrary::InitializeDefaultAttributes
 */
FCharacterClassDefaultInfo UCharacterClassInfo::GetClassDefaultInfo(ECharacterClass CharacterClass)
{
	return CharacterClassInformation.FindChecked(CharacterClass);
}
