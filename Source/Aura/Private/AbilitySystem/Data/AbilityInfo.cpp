// Copyright Druid Mechanics


#include "AbilitySystem/Data/AbilityInfo.h"

#include "Aura/AuraLogChannels.h"

/**
 * 根据 AbilityTag 在 AbilityInformation 数组中线性查找对应的技能元数据
 *
 * 查找策略：遍历数组，使用 == 精确匹配 Tag（非层级匹配）
 * 返回值：值类型副本（非引用），调用方可直接修改（填入 InputTag/StatusTag 后广播给 UI）
 *
 * 找不到的常见原因：
 * 1. 数据资产中未为该 AbilityTag 添加条目
 * 2. Tag 字符串拼写错误（大小写敏感）
 */
FAuraAbilityInfo UAbilityInfo::FindAbilityInfoForTag(const FGameplayTag& AbilityTag, bool bLogNotFound) const
{
	for (const FAuraAbilityInfo& Info : AbilityInformation)
	{
		if (Info.AbilityTag == AbilityTag)
		{
			return Info;
		}
	}

	if (bLogNotFound)
	{
		UE_LOG(LogAura, Error, TEXT("Can't find info for AbilityTag [%s] on AbilityInfo [%s]"), *AbilityTag.ToString(), *GetNameSafe(this));
	}

	// 未找到时返回默认空结构体（AbilityTag 为空），调用方应检查 AbilityTag.IsValid()
	return FAuraAbilityInfo();
}
