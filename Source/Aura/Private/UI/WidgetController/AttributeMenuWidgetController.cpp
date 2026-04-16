// Copyright Druid Mechanics


#include "UI/WidgetController/AttributeMenuWidgetController.h"

#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Data/AttributeInfo.h"
#include "Player/AuraPlayerState.h"

/**
 * 为属性菜单中所有属性注册动态更新监听，同时订阅属性点变化。
 *
 * 【遍历 TagsToAttributes 的意义】
 *   TagsToAttributes 是 UAuraAttributeSet 中定义的 TMap<FGameplayTag, TStaticFuncPtr<FGameplayAttribute()>>，
 *   它将每个属性 GameplayTag 映射到对应的属性静态 Getter 函数指针。
 *   遍历这张表，可以动态地为所有属性注册监听，无需硬编码每个属性名，
 *   新增属性时只需在 AttributeSet 和 DataAsset 中配置，无需修改此处代码。
 *
 * 【Lambda 捕获注意】
 *   Lambda 捕获 [this, Pair] 而非 [this, &Pair]，
 *   因为 Pair 是循环变量，捕获引用会导致循环结束后引用悬空。
 *   捕获值拷贝可确保 Lambda 持有正确的 Key/Value。
 */
void UAttributeMenuWidgetController::BindCallbacksToDependencies()
{
	check(AttributeInfo);

	// 遍历所有属性 Tag，为每个属性注册 GAS 属性变化委托
	for (auto& Pair : GetAuraAS()->TagsToAttributes)
	{
		// Pair.Value() 调用函数指针获得 FGameplayAttribute，
		// GetGameplayAttributeValueChangeDelegate 返回该属性专用的多播委托
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Pair.Value()).AddLambda(
		[this, Pair](const FOnAttributeChangeData& Data)
		{
			// 属性数值变化时，广播该属性的完整信息（名称+新值）给 Widget
			BroadcastAttributeInfo(Pair.Key, Pair.Value());
		}
	);
	}

	// 订阅属性点变化（升级时获得属性点，消费时减少）
	// 让 Widget 上的"剩余属性点"数字实时更新，并控制升级按钮的可用性
	GetAuraPS()->OnAttributePointsChangedDelegate.AddLambda(
		[this](int32 Points)
		{
			AttributePointsChangedDelegate.Broadcast(Points);
		}
	);
}

/**
 * 属性菜单打开时推送所有属性的当前值快照，以及当前剩余属性点数。
 *
 * 遍历 TagsToAttributes 中的全部属性条目，每个属性调用一次 BroadcastAttributeInfo，
 * Widget 依次接收并填充属性列表行（名称 + 数值）。
 * 最后广播当前属性点数，Widget 据此决定升级按钮是否可用。
 */
void UAttributeMenuWidgetController::BroadcastInitialValues()
{
	UAuraAttributeSet* AS = CastChecked<UAuraAttributeSet>(AttributeSet);
	check(AttributeInfo);

	// 遍历所有属性，每条属性单独广播一次（Widget 根据 Tag 匹配对应的 UI 行）
	for (auto& Pair : AS->TagsToAttributes)
	{
		BroadcastAttributeInfo(Pair.Key, Pair.Value());
	}

	// 广播当前可用属性点数，Widget 据此初始化升级按钮状态
	AttributePointsChangedDelegate.Broadcast(GetAuraPS()->GetAttributePoints());
}

/**
 * 玩家点击升级按钮时由蓝图调用。
 * 转发给 AuraASC 执行 ServerRPC，服务器完成属性升级后，
 * 属性变化会通过 GAS 复制系统自动触发 BindCallbacksToDependencies 中注册的 Lambda，
 * 从而更新 Widget 显示值——整个流程是异步的，Widget 无需等待确认。
 */
void UAttributeMenuWidgetController::UpgradeAttribute(const FGameplayTag& AttributeTag)
{
	UAuraAbilitySystemComponent* AuraASC = CastChecked<UAuraAbilitySystemComponent>(AbilitySystemComponent);
	AuraASC->UpgradeAttribute(AttributeTag);
}

/**
 * 广播单个属性的完整 UI 信息。
 *
 * 【数据合并流程】
 *   1. 从 AttributeInfo DataAsset 按 Tag 查找静态配置（属性名/描述等）
 *   2. 调用 Attribute.GetNumericValue(AttributeSet) 读取属性当前数值
 *      GetNumericValue 内部根据 FGameplayAttribute 的偏移量，
 *      从 AttributeSet 对象中读取对应成员变量的浮点数值
 *   3. 将数值填入 Info.AttributeValue 后广播
 * Widget 收到 FAuraAttributeInfo 后只需展示 AttributeName 和 AttributeValue，
 * 完全不需要了解 GAS 属性系统的内部结构。
 */
void UAttributeMenuWidgetController::BroadcastAttributeInfo(const FGameplayTag& AttributeTag, const FGameplayAttribute& Attribute) const
{
	// 从 DataAsset 取出名称/描述等静态显示配置
	FAuraAttributeInfo Info = AttributeInfo->FindAttributeInfoForTag(AttributeTag);
	// 从 AttributeSet 读取该属性的实际当前数值（基础值+修改值的最终结果）
	Info.AttributeValue = Attribute.GetNumericValue(AttributeSet);
	AttributeInfoDelegate.Broadcast(Info);
}
