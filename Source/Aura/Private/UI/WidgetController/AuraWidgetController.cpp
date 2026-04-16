// Copyright Druid Mechanics


#include "UI/WidgetController/AuraWidgetController.h"

#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Data/AbilityInfo.h"

/**
 * 将 FWidgetControllerParams 中打包的四个依赖解包，分别赋给成员变量。
 * 此后子类可通过 GetAuraPC/PS/ASC/AS 获取 Cast 后的派生类指针。
 */
void UAuraWidgetController::SetWidgetControllerParams(const FWidgetControllerParams& WCParams)
{
	PlayerController = WCParams.PlayerController;
	PlayerState = WCParams.PlayerState;
	AbilitySystemComponent = WCParams.AbilitySystemComponent;
	AttributeSet = WCParams.AttributeSet;
}

/**
 * 基类默认实现为空。
 * 子类（OverlayWidgetController / AttributeMenuWidgetController 等）
 * 重写此函数，在 Widget 首次显示时"主动推送"当前属性快照。
 */
void UAuraWidgetController::BroadcastInitialValues()
{

}

/**
 * 基类默认实现为空。
 * 子类重写此函数，向 ASC / PlayerState 注册事件监听，
 * 确保后续数据变化时能自动通知 Widget 刷新。
 */
void UAuraWidgetController::BindCallbacksToDependencies()
{

}

/**
 * 遍历 ASC 中已注册的所有技能 Spec，逐一广播技能信息给 Widget。
 *
 * 【工作流程】
 *   1. 若 bStartupAbilitiesGiven == false，说明初始技能还未赋予，直接返回，
 *      避免广播不完整数据（此情况由 AbilitiesGivenDelegate 延迟触发）。
 *   2. 构造 FForEachAbility 委托，内部 Lambda：
 *      a. 从 AbilityInfo DataAsset 中按技能 Tag 查找显示配置
 *      b. 补充 InputTag（技能绑定到哪个按键）和 StatusTag（锁定/可解锁/已解锁/已装备）
 *      c. 通过 AbilityInfoDelegate 广播给所有绑定的 Widget
 *   3. 调用 ForEachAbility 遍历全部技能 Spec
 */
void UAuraWidgetController::BroadcastAbilityInfo()
{
	// 初始技能尚未全部赋予，延迟到 AbilitiesGivenDelegate 触发后再广播
	if (!GetAuraASC()->bStartupAbilitiesGiven) return;

	FForEachAbility BroadcastDelegate;
	BroadcastDelegate.BindLambda([this](const FGameplayAbilitySpec& AbilitySpec)
	{
		// 从 DataAsset 查找该技能的 UI 配置（图标/名称/描述等）
		FAuraAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(AuraAbilitySystemComponent->GetAbilityTagFromSpec(AbilitySpec));
		// 补充运行时数据：当前绑定的输入键和技能解锁状态
		Info.InputTag = AuraAbilitySystemComponent->GetInputTagFromSpec(AbilitySpec);
		Info.StatusTag = AuraAbilitySystemComponent->GetStatusFromSpec(AbilitySpec);
		AbilityInfoDelegate.Broadcast(Info);
	});
	GetAuraASC()->ForEachAbility(BroadcastDelegate);
}

/**
 * 懒加载 Getter：首次调用时将基类 PlayerController 指针 Cast 到 AAuraPlayerController，
 * 结果缓存到 AuraPlayerController 成员变量，后续调用直接返回缓存，避免重复 Cast。
 */
AAuraPlayerController* UAuraWidgetController::GetAuraPC()
{
	if (AuraPlayerController == nullptr)
	{
		AuraPlayerController = Cast<AAuraPlayerController>(PlayerController);
	}
	return AuraPlayerController;
}

/**
 * 懒加载 Getter：将 PlayerState 指针 Cast 到 AAuraPlayerState 并缓存。
 * AAuraPlayerState 持有等级、XP、技能点等玩家持久数据，是 WidgetController 的重要数据源。
 */
AAuraPlayerState* UAuraWidgetController::GetAuraPS()
{
	if (AuraPlayerState == nullptr)
	{
		AuraPlayerState = Cast<AAuraPlayerState>(PlayerState);
	}
	return AuraPlayerState;
}

/**
 * 懒加载 Getter：将 AbilitySystemComponent 指针 Cast 到 UAuraAbilitySystemComponent 并缓存。
 * UAuraAbilitySystemComponent 提供技能状态查询、技能遍历、ServerRPC 等扩展功能。
 */
UAuraAbilitySystemComponent* UAuraWidgetController::GetAuraASC()
{
	if (AuraAbilitySystemComponent == nullptr)
	{
		AuraAbilitySystemComponent = Cast<UAuraAbilitySystemComponent>(AbilitySystemComponent);
	}
	return AuraAbilitySystemComponent;
}

/**
 * 懒加载 Getter：将 AttributeSet 指针 Cast 到 UAuraAttributeSet 并缓存。
 * UAuraAttributeSet 暴露具体属性的 Get 方法和 TagsToAttributes 映射表，
 * 供 WidgetController 查询当前属性值。
 */
UAuraAttributeSet* UAuraWidgetController::GetAuraAS()
{
	if (AuraAttributeSet == nullptr)
	{
		AuraAttributeSet = Cast<UAuraAttributeSet>(AttributeSet);
	}
	return AuraAttributeSet;
}
