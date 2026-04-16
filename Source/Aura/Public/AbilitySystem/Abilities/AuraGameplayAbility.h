// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AuraGameplayAbility.generated.h"

/**
 * UAuraGameplayAbility —— 本项目所有 Gameplay Ability 的统一基类
 *
 * 【设计意图】
 * UGameplayAbility 是 GAS 原生基类，功能丰富但与项目脱节。
 * 此基类在原生基础上做了以下扩展：
 *   1. 存储 StartupInputTag：技能被赋予（GiveAbility）时，
 *      该 Tag 写入 DynamicAbilityTags，PlayerController 通过它
 *      找到对应按键并在按下时激活技能，实现"数据驱动按键绑定"。
 *   2. 提供技能描述接口（GetDescription / GetNextLevelDescription / GetLockedDescription），
 *      供 SpellMenu UI 读取并显示富文本说明，无需在 Blueprint 重复实现。
 *   3. 提供 GetManaCost / GetCooldown 辅助函数，
 *      让 UI 在不激活技能的情况下直接读取法力消耗和冷却时间数值。
 *
 * 【技能生命周期简述】
 *   ActivateAbility → CommitAbility（消耗法力/触发冷却）
 *   → 执行技能逻辑 → EndAbility（bWasCancelled 区分正常/取消结束）
 *
 * 【网络说明】
 * GAS 技能默认在服务器激活并通过 RepNotify 同步状态到客户端。
 * 具体子类如需在客户端播放预测动画，需配置 Net Execution Policy。
 */
UCLASS()
class AURA_API UAuraGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:

	/**
	 * 技能启动时绑定的输入标签
	 *
	 * 流程：GiveAbility 赋予技能 → 该 Tag 存入 AbilitySpec.DynamicAbilityTags
	 * → UAuraAbilitySystemComponent 在输入回调中遍历所有 Spec，
	 *   匹配 DynamicAbilityTags 中含有此 Tag 的技能并激活它。
	 * 这样"技能与按键的绑定"完全由数据（Tag）决定，无需硬编码索引。
	 */
	UPROPERTY(EditDefaultsOnly, Category="Input")
	FGameplayTag StartupInputTag;

	/**
	 * 获取当前等级的技能描述（富文本格式，供 SpellMenu UI 显示）
	 * 子类应重写此函数以返回具体技能名称、伤害数值等本地化描述。
	 */
	virtual FString GetDescription(int32 Level);

	/**
	 * 获取下一等级的技能预览描述
	 * 供 SpellMenu 显示"升级后效果"提示，玩家据此决定是否花费点数升级。
	 */
	virtual FString GetNextLevelDescription(int32 Level);

	/**
	 * 获取技能未解锁时的锁定提示文本（静态函数，无需实例）
	 * 显示"需要角色达到 X 级才能解锁"，Level 来自 AbilityInfo 数据表。
	 */
	static FString GetLockedDescription(int32 Level);

protected:

	/**
	 * 获取指定等级下该技能的法力消耗值
	 *
	 * 原理：遍历绑定的 CostGameplayEffect 的 Modifiers，
	 * 找到作用于 ManaAttribute 的 Modifier 并读取其静态量值。
	 * UI 调用此函数在技能栏实时显示消耗，不触发任何 GAS 逻辑。
	 */
	float GetManaCost(float InLevel = 1.f) const;

	/**
	 * 获取指定等级下该技能的冷却时间（秒）
	 *
	 * 原理：读取绑定的 CooldownGameplayEffect 的 DurationMagnitude 静态量值。
	 * UI 调用此函数在技能栏显示冷却时长，同样不触发 GAS 逻辑。
	 */
	float GetCooldown(float InLevel = 1.f) const;
};
