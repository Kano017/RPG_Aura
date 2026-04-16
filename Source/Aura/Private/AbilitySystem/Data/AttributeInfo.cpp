// Copyright Druid Mechanics


#include "AbilitySystem/Data/AttributeInfo.h"

#include "Aura/AuraLogChannels.h"

/**
 * 根据属性 Tag 在 AttributeInformation 数组中线性查找对应的元数据
 *
 * 使用 MatchesTagExact 精确匹配（不接受父子 Tag 层级匹配），
 * 确保 Attributes.Primary.Strength 不会错误命中 Attributes.Primary。
 *
 * 调用方：AttributeMenuWidgetController::BroadcastAttributeInfo
 *         在广播前会将当前属性数值填入返回的结构体副本
 */
FAuraAttributeInfo UAttributeInfo::FindAttributeInfoForTag(const FGameplayTag& AttributeTag, bool bLogNotFound) const
{
	for (const FAuraAttributeInfo& Info : AttributeInformation)
	{
		if (Info.AttributeTag.MatchesTagExact(AttributeTag))
		{
			return Info;
		}
	}

	if (bLogNotFound)
	{
		UE_LOG(LogAura, Error, TEXT("Can't find Info for AttributeTag [%s] on AttributeInfo [%s]."), *AttributeTag.ToString(),*GetNameSafe(this));
	}

	// 未找到时返回默认空结构体，调用方应检查 AttributeTag.IsValid()
	return FAuraAttributeInfo();
}
