// Copyright Druid Mechanics

#pragma once

/*
 * AttributeInfo.h
 *
 * 属性信息数据资产，为属性菜单 UI 提供每个属性的本地化名称、描述和当前数值。
 *
 * 【架构职责】
 * - 将"属性 Tag"映射为可读的 FText 名称与描述（支持本地化）
 * - AttributeValue 在运行时由 AttributeMenuWidgetController 从 AttributeSet 读取后填入
 * - 本资产仅描述静态元数据；属性的实际运行时值不持久化存储在此资产中
 *
 * 【访问方式】
 * - 挂载在 AuraGameModeBase 上（实际通过 AuraPlayerController 或 WidgetController 访问）
 * - AttributeMenuWidgetController 调用 FindAttributeInfoForTag 获取每条属性的展示信息
 * - 通过 UAuraAbilitySystemLibrary::GetAttributeInfo 全局访问
 */

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AttributeInfo.generated.h"

/**
 * 单条属性的元数据描述
 * AttributeValue 为运行时动态字段，由 WidgetController 在广播前填充
 */
USTRUCT(BlueprintType)
struct FAuraAttributeInfo
{
	GENERATED_BODY()

	/**
	 * 属性标识 Tag，对应 AuraGameplayTags 中的 Attributes.xxx 层级
	 * 例如：Attributes.Primary.Strength、Attributes.Secondary.MaxHealth
	 * 用于在 FindAttributeInfoForTag 中精确匹配（MatchesTagExact）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTag AttributeTag = FGameplayTag();

	/**
	 * 属性的本地化显示名称，在属性面板左侧显示
	 * 示例："力量"、"智力"、"最大生命值"
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FText AttributeName = FText();

	/**
	 * 属性的本地化描述文本，用于 Tooltip 或悬停提示
	 * 向玩家解释该属性的游戏效果（例如"力量提升物理伤害和护甲"）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FText AttributeDescription = FText();

	/**
	 * 属性的当前数值（运行时动态字段，不在资产中配置）
	 * AttributeMenuWidgetController 从 AuraAttributeSet 读取实际数值后填入此字段，
	 * 再将完整结构体广播给 UI Widget
	 */
	UPROPERTY(BlueprintReadOnly)
	float AttributeValue = 0.f;
};

/**
 * UAttributeInfo
 *
 * 属性信息数据资产，在蓝图编辑器中配置后通过 AuraGameModeBase 或 PlayerController 挂载。
 */
UCLASS()
class AURA_API UAttributeInfo : public UDataAsset
{
	GENERATED_BODY()
public:

	/**
	 * 根据属性 Tag 查找对应的属性元数据（精确匹配，不含子 Tag）
	 * @param AttributeTag   要查找的属性标识 Tag
	 * @param bLogNotFound   找不到时是否输出错误日志
	 * @return 找到则返回结构体副本；未找到则返回默认空结构体
	 */
	FAuraAttributeInfo FindAttributeInfoForTag(const FGameplayTag& AttributeTag, bool bLogNotFound = false) const;

	/** 所有属性的元数据数组，涵盖主属性、次要属性和生命属性三个层级 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FAuraAttributeInfo> AttributeInformation;
};
