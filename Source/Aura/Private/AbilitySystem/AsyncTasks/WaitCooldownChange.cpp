// Copyright Druid Mechanics


#include "AbilitySystem/AsyncTasks/WaitCooldownChange.h"
#include "AbilitySystemComponent.h"

/**
 * 创建并启动冷却监听任务（蓝图异步节点入口）
 *
 * 【双重监听注册】
 * 1. RegisterGameplayTagEvent：监听冷却 Tag 的 NewOrRemoved 事件
 *    → Count 从 1 降到 0 时（Tag 移除）触发 CooldownTagChanged，广播 CooldownEnd
 *
 * 2. OnActiveGameplayEffectAddedDelegateToSelf：监听任意 GE 被应用的事件
 *    → OnActiveEffectAdded 过滤出携带冷却 Tag 的 GE，查询剩余时间并广播 CooldownStart
 *
 * 参数无效时立即 EndTask 并返回 nullptr，避免空指针监听泄漏
 */
UWaitCooldownChange* UWaitCooldownChange::WaitForCooldownChange(UAbilitySystemComponent* AbilitySystemComponent, const FGameplayTag& InCooldownTag)
{
	UWaitCooldownChange* WaitCooldownChange = NewObject<UWaitCooldownChange>();
	WaitCooldownChange->ASC = AbilitySystemComponent;
	WaitCooldownChange->CooldownTag = InCooldownTag;

	if (!IsValid(AbilitySystemComponent) || !InCooldownTag.IsValid())
	{
		WaitCooldownChange->EndTask();
		return nullptr;
	}

	// 用于得知冷却何时结束（冷却标签被移除时，Count 变为 0）
	AbilitySystemComponent->RegisterGameplayTagEvent(
		InCooldownTag,
		EGameplayTagEventType::NewOrRemoved).AddUObject(
			WaitCooldownChange,
			&UWaitCooldownChange::CooldownTagChanged);

	// 用于得知冷却效果何时被应用（监听所有新 GE，在回调中过滤目标冷却 Tag）
	AbilitySystemComponent->OnActiveGameplayEffectAddedDelegateToSelf.AddUObject(WaitCooldownChange, &UWaitCooldownChange::OnActiveEffectAdded);

	return WaitCooldownChange;
}

/**
 * 主动结束并销毁任务
 *
 * 注销 GameplayTag 事件绑定（防止 ASC 持续持有悬空引用），
 * 然后通知 GC 系统本对象可以被回收
 * 注意：OnActiveGameplayEffectAddedDelegateToSelf 的绑定随对象销毁自动清理
 */
void UWaitCooldownChange::EndTask()
{
	if (!IsValid(ASC)) return;
	ASC->RegisterGameplayTagEvent(CooldownTag, EGameplayTagEventType::NewOrRemoved).RemoveAll(this);

	SetReadyToDestroy();
	MarkAsGarbage();
}

/**
 * 冷却 Tag Count 变化回调
 *
 * NewCount == 0：冷却 GE 已到期，标签被完全移除，冷却结束
 * → 广播 CooldownEnd（TimeRemaining=0），UI 隐藏冷却遮罩，恢复技能可点击状态
 *
 * NewCount > 0：标签被添加（冷却开始），由 OnActiveEffectAdded 负责处理，此处不重复处理
 */
void UWaitCooldownChange::CooldownTagChanged(const FGameplayTag InCooldownTag, int32 NewCount)
{
	if (NewCount == 0)
	{
		// 冷却结束，通知 UI 清除冷却显示
		CooldownEnd.Broadcast(0.f);
	}
}

/**
 * 任意 GE 被应用时的过滤回调
 *
 * 【冷却 Tag 的两种挂载方式】
 * - Asset Tags（SpecApplied.GetAllAssetTags）：GE 本身定义的标签，不授予给 Owner
 * - Granted Tags（SpecApplied.GetAllGrantedTags）：GE 应用时授予给 Owner ASC 的标签
 * 两者之一包含冷却 Tag 即视为目标冷却 GE
 *
 * 【查询剩余时间】
 * 使用 MakeQuery_MatchAnyOwningTags 构建查询条件，GetActiveEffectsTimeRemaining
 * 返回所有匹配 GE 的剩余时间数组（理论上只有一个，但取最大值以应对边缘情况）
 *
 * 广播 CooldownStart（TimeRemaining=最大剩余时间），UI 据此初始化倒计时进度条
 */
void UWaitCooldownChange::OnActiveEffectAdded(UAbilitySystemComponent* TargetASC, const FGameplayEffectSpec& SpecApplied, FActiveGameplayEffectHandle ActiveEffectHandle)
{
	FGameplayTagContainer AssetTags;
	SpecApplied.GetAllAssetTags(AssetTags);

	FGameplayTagContainer GrantedTags;
	SpecApplied.GetAllGrantedTags(GrantedTags);

	// 检查新应用的 GE 是否携带目标冷却 Tag（无论是 Asset Tag 还是 Granted Tag）
	if (AssetTags.HasTagExact(CooldownTag) || GrantedTags.HasTagExact(CooldownTag))
	{
		// 查询 ASC 中所有携带此冷却 Tag 的活跃 GE 的剩余时间
		FGameplayEffectQuery GameplayEffectQuery = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(CooldownTag.GetSingleTagContainer());
		TArray<float> TimesRemaining = ASC->GetActiveEffectsTimeRemaining(GameplayEffectQuery);
		if (TimesRemaining.Num() > 0)
		{
			// 取所有匹配 GE 中的最大剩余时间（通常只有一个冷却 GE，但防御性处理多个的情况）
			float TimeRemaining = TimesRemaining[0];
			for (int32 i = 0; i < TimesRemaining.Num(); i++)
			{
				if (TimesRemaining[i] > TimeRemaining)
				{
					TimeRemaining = TimesRemaining[i];
				}
			}

			// 广播冷却开始事件，携带最大剩余时间，UI 据此设置进度条满值
			CooldownStart.Broadcast(TimeRemaining);
		}
	}
}
