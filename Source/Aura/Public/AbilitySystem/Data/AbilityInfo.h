// Copyright Druid Mechanics

#pragma once

/*
 * AbilityInfo.h
 *
 * 技能信息数据资产，存储游戏中所有可学习技能的静态元数据。
 *
 * 【架构职责】
 * - 本资产作为"技能字典"：每条 FAuraAbilityInfo 描述一个技能的图标、冷却标签、等级需求等静态信息
 * - 动态状态（InputTag、StatusTag）由 ASC 在运行时填充，不存储在此资产中
 * - 挂载在 AuraGameModeBase 上，通过 UAuraAbilitySystemLibrary::GetAbilityInfo 全局访问
 *
 * 【主要消费者】
 * - USpellMenuWidgetController：遍历 AbilityInformation 数组构建技能树 UI
 * - UOverlayWidgetController：查找已装备技能的图标和冷却信息
 * - FindAbilityInfoForTag：以 AbilityTag 为键快速查找对应的技能元数据
 */

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AbilityInfo.generated.h"

class UGameplayAbility;

/**
 * 单个技能的静态描述信息
 * 部分字段（InputTag、StatusTag）为运行时动态字段，不在资产中配置，由 WidgetController 填入后传给 UI
 */
USTRUCT(BlueprintType)
struct FAuraAbilityInfo
{
	GENERATED_BODY()

	/**
	 * 技能的唯一标识 Tag，对应 AuraGameplayTags 中的 Abilities.xxx 层级
	 * 用于在 FindAbilityInfoForTag 中查找技能信息，也用于 ASC 内部索引技能规格
	 * 示例：Abilities.Fire.FireBolt
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTag AbilityTag = FGameplayTag();

	/**
	 * 此技能当前绑定的输入槽 Tag（运行时字段，不在资产中配置）
	 * 由 SpellMenuWidgetController 或 OverlayWidgetController 从 ASC 读取后填入，
	 * 对应 InputTag.Spell1 / InputTag.Spell2 / InputTag.Spell3 / InputTag.Spell4 等
	 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag InputTag = FGameplayTag();

	/**
	 * 技能的当前解锁状态 Tag（运行时字段，不在资产中配置）
	 * 由 ASC 维护，可能的值：
	 *   Abilities.Status.Locked（未解锁）
	 *   Abilities.Status.Eligible（达到等级要求，可解锁）
	 *   Abilities.Status.Unlocked（已解锁，可装备）
	 *   Abilities.Status.Equipped（已装备到输入槽）
	 */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag StatusTag = FGameplayTag();

	/**
	 * 冷却 GameplayEffect 授予的 Tag，在资产中配置
	 * 冷却 GE 应用时此 Tag 被添加到 ASC，GE 到期时移除。
	 * WaitCooldownChange 异步任务监听此 Tag 的 NewOrRemoved 事件，
	 * 触发 CooldownStart / CooldownEnd 代理通知 UI 更新冷却显示
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTag CooldownTag = FGameplayTag();

	/**
	 * 技能类型 Tag，决定技能可装备到哪类槽位，在资产中配置
	 * 可能的值：
	 *   Abilities.Type.Offensive（进攻型，可放入法术槽 Spell1~4）
	 *   Abilities.Type.Passive（被动型，可放入被动槽）
	 *   Abilities.Type.None（无类型，如职业专属技能，不可手动装备）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTag AbilityType = FGameplayTag();

	/**
	 * 技能图标，在技能树 UI 和法术槽按钮中显示
	 * SpellMenuWidgetController 广播 AbilityInfo 时由 UI 读取此字段
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<const UTexture2D> Icon = nullptr;

	/**
	 * 技能按钮背景材质，不同元素系技能使用不同背景色以区分系别
	 * UI 控件将此材质赋给背景 Image 的 Dynamic Material Instance
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<const UMaterialInterface> BackgroundMaterial = nullptr;

	/**
	 * 解锁此技能所需的最低玩家等级
	 * ASC 在每次升级时遍历所有技能，将满足条件的技能状态从 Locked 改为 Eligible
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	int32 LevelRequirement = 1;

	/**
	 * 对应的 GameplayAbility 类，用于在赋予技能时实例化
	 * AuraAbilitySystemComponent 通过此字段 GiveAbility，
	 * 同时也可通过 GetAbilityLevel 查询已赋予实例的当前等级
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UGameplayAbility> Ability;
};

/**
 * UAbilityInfo
 *
 * 技能信息数据资产，在蓝图编辑器中配置后挂载到 AuraGameModeBase。
 * 通过 UAuraAbilitySystemLibrary::GetAbilityInfo 全局访问。
 */
UCLASS()
class AURA_API UAbilityInfo : public UDataAsset
{
	GENERATED_BODY()
public:

	/** 所有技能的元数据数组，每个元素对应一个可学习技能的完整静态描述 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AbilityInformation")
	TArray<FAuraAbilityInfo> AbilityInformation;

	/**
	 * 根据 AbilityTag 线性查找对应的技能信息
	 * @param AbilityTag    要查找的技能标识 Tag
	 * @param bLogNotFound  找不到时是否输出错误日志（调试时置 true）
	 * @return 找到则返回对应结构体副本；未找到则返回默认空结构体
	 */
	FAuraAbilityInfo FindAbilityInfoForTag(const FGameplayTag& AbilityTag, bool bLogNotFound = false) const;
};
