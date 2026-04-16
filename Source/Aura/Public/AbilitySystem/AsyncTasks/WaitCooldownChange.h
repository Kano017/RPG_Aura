// Copyright Druid Mechanics

#pragma once

/*
 * WaitCooldownChange.h
 *
 * 冷却变化监听异步任务，基于 UBlueprintAsyncActionBase 实现（非 UAbilityTask 子类，
 * 但具备类似的异步节点能力，可在蓝图中作为异步节点使用）。
 *
 * 【GAS 冷却机制回顾】
 * 技能释放时，GAS 会应用一个 Duration 类型的冷却 GameplayEffect（GE）。
 * 该 GE 在应用期间向 ASC 授予（Grant）或标记（AssetTag）一个冷却标签，
 * GE 到期时标签自动移除。WaitCooldownChange 正是监听此标签的生命周期。
 *
 * 【双重监听机制】
 * 为准确捕获冷却的开始和结束，注册了两类监听：
 *
 * 1. RegisterGameplayTagEvent（NewOrRemoved）
 *    - 监听冷却 Tag 的添加（Count 从 0→1）和移除（Count 从 1→0）
 *    - Count 变为 0 时触发 CooldownEnd，广播 TimeRemaining=0 通知 UI 冷却结束
 *    - 注意：此回调仅在 Tag Count 变化时触发，不提供剩余时间，因此冷却开始用另一机制
 *
 * 2. OnActiveGameplayEffectAddedDelegateToSelf
 *    - 监听任何 GE 被应用到 ASC 的事件
 *    - OnActiveEffectAdded 检查新 GE 是否携带目标冷却 Tag（Asset Tag 或 Granted Tag）
 *    - 若匹配，查询 ASC 中所有携带该 Tag 的活跃 GE 的剩余时间，取最大值
 *    - 触发 CooldownStart，广播剩余时间给 UI（用于显示倒计时进度条）
 *
 * 【生命周期管理】
 * - WaitForCooldownChange：静态工厂函数，创建任务对象并注册监听（蓝图异步节点入口）
 * - EndTask：主动取消监听并标记对象为垃圾，在技能结束或 UI 关闭时应手动调用
 *
 * 【UI 使用方式】
 * 法术槽 Widget 在初始化时调用 WaitForCooldownChange，
 * 绑定 CooldownStart 和 CooldownEnd 代理更新技能按钮的冷却遮罩和计时文本
 */

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "GameplayTagContainer.h"
#include "ActiveGameplayEffectHandle.h"
#include "WaitCooldownChange.generated.h"

class UAbilitySystemComponent;
struct FGameplayEffectSpec;

/**
 * 冷却变化代理签名，携带剩余时间参数：
 * - CooldownStart 触发时：TimeRemaining 为冷却总时长（正值）
 * - CooldownEnd 触发时：TimeRemaining 为 0
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCooldownChangeSignature, float, TimeRemaining);

/**
 * UWaitCooldownChange
 *
 * 冷却监听异步任务。ExposedAsyncProxy 使蓝图节点暴露任务对象引用，
 * 允许调用方持有引用并在需要时手动调用 EndTask 取消监听。
 */
UCLASS(BlueprintType, meta = (ExposedAsyncProxy = "AsyncTask"))
class AURA_API UWaitCooldownChange : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
public:

	/**
	 * 冷却开始时触发
	 * TimeRemaining 参数为此次冷却的持续时长（秒），UI 据此初始化倒计时进度条
	 */
	UPROPERTY(BlueprintAssignable)
	FCooldownChangeSignature CooldownStart;

	/**
	 * 冷却结束时触发（冷却 Tag 被移除）
	 * TimeRemaining 固定为 0，UI 据此隐藏冷却遮罩并恢复技能可用状态
	 */
	UPROPERTY(BlueprintAssignable)
	FCooldownChangeSignature CooldownEnd;

	/**
	 * 蓝图异步节点工厂函数，创建并启动冷却监听任务
	 * BlueprintInternalUseOnly 确保此函数只能通过异步节点使用，不可直接调用
	 * @param AbilitySystemComponent  目标 ASC，通常为本地玩家的 ASC
	 * @param InCooldownTag           要监听的冷却标签（与技能冷却 GE 授予的 Tag 一致）
	 * @return 创建的任务对象，可通过 EndTask 主动停止
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"))
	static UWaitCooldownChange* WaitForCooldownChange(UAbilitySystemComponent* AbilitySystemComponent, const FGameplayTag& InCooldownTag);

	/**
	 * 主动结束监听并销毁任务对象
	 * 应在不再需要监听时调用（如 Widget 被销毁、技能从槽位移除）
	 * 内部移除 RegisterGameplayTagEvent 的绑定并调用 MarkAsGarbage
	 */
	UFUNCTION(BlueprintCallable)
	void EndTask();

protected:

	/** 被监听的 ASC 引用，用于注册/注销事件绑定 */
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> ASC;

	/** 被监听的冷却标签，与技能的 CooldownTag 字段对应 */
	FGameplayTag CooldownTag;

	/**
	 * 冷却 Tag Count 变化回调（由 RegisterGameplayTagEvent 触发）
	 * NewCount == 0 时表示冷却结束（Tag 完全移除），广播 CooldownEnd
	 * NewCount > 0 时为 Tag 添加，冷却开始由 OnActiveEffectAdded 处理
	 */
	void CooldownTagChanged(const FGameplayTag InCooldownTag, int32 NewCount);

	/**
	 * 任意 GE 被应用到 ASC 时触发的回调
	 * 检查新 GE 是否携带目标冷却 Tag（Asset Tags 或 Granted Tags 中任一满足即可），
	 * 若匹配则查询所有携带该 Tag 的活跃 GE 的剩余时间，取最大值后广播 CooldownStart
	 */
	void OnActiveEffectAdded(UAbilitySystemComponent* TargetASC, const FGameplayEffectSpec& SpecApplied, FActiveGameplayEffectHandle ActiveEffectHandle);
};
