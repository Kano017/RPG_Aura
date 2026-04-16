// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraDamageGameplayAbility.h"
#include "AuraBeamSpell.generated.h"

/**
 * UAuraBeamSpell —— 链式光束技能基类（如 Electrocute 闪电链）
 *
 * 【光束技能流程概览】
 *   1. 玩家按下技能键 → ActivateAbility（蓝图图表）
 *   2. 蓝图等待鼠标目标数据（WaitForTargetDataUnderMouse Task）
 *   3. 收到 HitResult 后调用 StoreMouseDataInfo 缓存命中位置和目标
 *      → 若未命中有效目标则 CancelAbility 立即取消
 *   4. 调用 StoreOwnerVariables 缓存 PlayerController 和角色引用
 *      （后续 Tick 中频繁使用，缓存避免每帧重新查找）
 *   5. 播放施法动画，在动画事件点调用 TraceFirstTarget：
 *      从武器插槽向目标做球形扫描，精确确定主目标和命中位置
 *   6. 调用 StoreAdditionalTargets 查找主目标周围的链式目标
 *   7. 蓝图通过 Timer 每隔固定时间对所有目标调用 CauseDamage，
 *      同时播放光束粒子特效连接各目标
 *   8. 主目标/链式目标死亡时触发 PrimaryTargetDied / AdditionalTargetDied 蓝图事件，
 *      蓝图据此断开对应光束、将光束转移到新目标或结束技能
 *
 * 【与投射物技能的核心区别】
 *   投射物技能：一次生成、飞行、命中时伤害
 *   光束技能：持续存在、每帧/每 Tick 更新目标位置、周期性造成伤害
 *   因此光束技能需要缓存目标引用，并监听目标死亡委托以动态调整
 *
 * 【网络说明】
 *   光束的逻辑（伤害 Timer、目标追踪）在服务器执行。
 *   光束的视觉特效（粒子系统连线）由客户端在蓝图中处理，
 *   通过 BlueprintReadWrite 属性在 C++ 与蓝图间共享状态。
 */
UCLASS()
class AURA_API UAuraBeamSpell : public UAuraDamageGameplayAbility
{
	GENERATED_BODY()
public:

	/**
	 * StoreMouseDataInfo —— 缓存鼠标射线命中结果
	 *
	 * 从 WaitForTargetDataUnderMouse Task 获得 HitResult 后调用。
	 * 若命中则存储命中点坐标（MouseHitLocation）和命中 Actor（MouseHitActor）；
	 * 若未命中任何物体（bBlockingHit = false）则立即取消技能，
	 * 防止在空地施放光束导致后续逻辑空指针。
	 */
	UFUNCTION(BlueprintCallable)
	void StoreMouseDataInfo(const FHitResult& HitResult);

	/**
	 * StoreOwnerVariables —— 缓存施法者的 PlayerController 和 Character 引用
	 *
	 * 光束技能持续期间需要频繁访问 PlayerController（获取鼠标位置更新光束终点）
	 * 和 Character（获取武器插槽位置更新光束起点）。
	 * 在技能激活时一次性缓存，避免每 Tick 通过 CurrentActorInfo 重复 Cast 查找。
	 */
	UFUNCTION(BlueprintCallable)
	void StoreOwnerVariables();

	/**
	 * TraceFirstTarget —— 从武器插槽向目标做球形扫描，精确定位主目标
	 *
	 * 鼠标点击获得的 HitResult 可能命中地面或远处物体，
	 * 此函数从武器尖端（TipSocket）做一次球形 Trace 到目标位置，
	 * 更新 MouseHitLocation 和 MouseHitActor 为最近的有效命中。
	 * 同时绑定主目标的死亡委托（OnDeathDelegate），
	 * 目标死亡时自动触发 PrimaryTargetDied 蓝图事件。
	 *
	 * @param BeamTargetLocation  光束目标位置（来自 StoreMouseDataInfo 的 MouseHitLocation）
	 */
	UFUNCTION(BlueprintCallable)
	void TraceFirstTarget(const FVector& BeamTargetLocation);

	/**
	 * StoreAdditionalTargets —— 查找并缓存主目标周围的链式目标
	 *
	 * 以主目标为中心，在 850 单位半径内查找所有存活的敌对 Actor，
	 * 排除施法者自身和主目标，取最近的若干个作为链式目标。
	 * 链式目标数量 = min(技能等级 - 1, MaxNumShockTargets)：
	 *   1 级只有主目标，2 级新增 1 个链式，以此类推，直到上限。
	 * 对所有链式目标同样绑定死亡委托（AdditionalTargetDied），
	 * 目标死亡后蓝图可选择将光束转移到新目标。
	 *
	 * @param OutAdditionalTargets  输出的链式目标数组（蓝图驱动光束粒子连接这些目标）
	 */
	UFUNCTION(BlueprintCallable)
	void StoreAdditionalTargets(TArray<AActor*>& OutAdditionalTargets);

	/**
	 * PrimaryTargetDied —— 主目标死亡时的蓝图事件（蓝图实现）
	 *
	 * 由 TraceFirstTarget 绑定到主目标的 OnDeathDelegate。
	 * 蓝图实现中通常：销毁主光束粒子 → 尝试寻找新主目标 → 或直接 EndAbility。
	 */
	UFUNCTION(BlueprintImplementableEvent)
	void PrimaryTargetDied(AActor* DeadActor);

	/**
	 * AdditionalTargetDied —— 链式目标死亡时的蓝图事件（蓝图实现）
	 *
	 * 由 StoreAdditionalTargets 绑定到每个链式目标的 OnDeathDelegate。
	 * 蓝图实现中通常：销毁对应链式光束粒子，减少当前链式数量计数。
	 */
	UFUNCTION(BlueprintImplementableEvent)
	void AdditionalTargetDied(AActor* DeadActor);

protected:

	/** 缓存鼠标射线命中的世界坐标（光束终点，每帧由蓝图更新以追踪移动目标） */
	UPROPERTY(BlueprintReadWrite, Category = "Beam")
	FVector MouseHitLocation;

	/** 缓存鼠标命中的主目标 Actor（空指针安全：命中失败时 CancelAbility） */
	UPROPERTY(BlueprintReadWrite, Category = "Beam")
	TObjectPtr<AActor> MouseHitActor;

	/** 缓存施法者的 PlayerController，用于每帧查询鼠标世界位置更新光束方向 */
	UPROPERTY(BlueprintReadWrite, Category = "Beam")
	TObjectPtr<APlayerController> OwnerPlayerController;

	/** 缓存施法者角色，用于获取武器插槽位置（光束起点） */
	UPROPERTY(BlueprintReadWrite, Category = "Beam")
	TObjectPtr<ACharacter> OwnerCharacter;

	/**
	 * 最大链式目标数量上限
	 * 实际链式数 = min(技能等级 - 1, MaxNumShockTargets)，
	 * 此值防止高等级时链式数量无限增长导致性能问题。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Beam")
	int32 MaxNumShockTargets = 5;
};
