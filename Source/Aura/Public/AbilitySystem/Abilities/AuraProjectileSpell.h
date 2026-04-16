// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraDamageGameplayAbility.h"
#include "AuraProjectileSpell.generated.h"

class AAuraProjectile;
class UGameplayEffect;
struct FGameplayTag;

/**
 * UAuraProjectileSpell —— 所有投射物法术的基类
 *
 * 【技能完整流程】
 *   1. 玩家按下技能键 → PlayerController 通过 WaitForTargetDataUnderMouse Task
 *      发送 TargetData（鼠标射线命中的 FHitResult）到服务器
 *   2. ActivateAbility 在客户端/服务器均被调用（取决于 Net Execution Policy），
 *      蓝图图表监听 TargetData 到达事件
 *   3. 服务器接收 TargetData 后调用 SpawnProjectile，在武器骨骼插槽处生成投射物
 *   4. 生成的 AAuraProjectile 在世界中飞行，碰到敌人后：
 *      调用 UAuraAbilitySystemLibrary::ApplyDamageEffect(DamageEffectParams)
 *      → 完整走一遍 ExecCalc_Damage 流程（含 Debuff、击退、死亡冲量）
 *
 * 【与 CauseDamage 的区别】
 *   CauseDamage 是"即时"伤害（直接对目标 ASC 应用 GE），
 *   投射物法术是"延迟"伤害（伤害参数随投射物飞行，命中时才触发）。
 *   因此此类使用 MakeDamageEffectParamsFromClassDefaults 打包参数后
 *   存入投射物，而非直接调用 CauseDamage。
 *
 * 【网络说明】
 *   SpawnProjectile 内有 HasAuthority() 检查，确保投射物仅在服务器生成。
 *   服务器生成后通过 Actor 复制同步到所有客户端，客户端看到的是复制副本。
 */
UCLASS()
class AURA_API UAuraProjectileSpell : public UAuraDamageGameplayAbility
{
	GENERATED_BODY()


protected:

	/**
	 * ActivateAbility —— 技能激活入口（此处调用 Super 后由蓝图图表接管）
	 *
	 * C++ 侧仅调用 Super::ActivateAbility 完成基础初始化（CommitAbility 检查等）。
	 * 实际的目标数据等待、动画播放、SpawnProjectile 调用均在蓝图图表中实现，
	 * 这是 GAS 中"C++ 搭骨架，Blueprint 填肉"的典型模式。
	 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/**
	 * SpawnProjectile —— 在武器插槽处朝目标方向生成投射物（仅服务器执行）
	 *
	 * @param ProjectileTargetLocation  目标位置（来自 TargetData 的 HitResult.ImpactPoint）
	 * @param SocketTag                 武器骨骼插槽标签（由 GetCombatSocketLocation 解析为世界坐标）
	 * @param bOverridePitch            是否覆盖投射角度（某些法术需要水平飞行而非对准目标高度）
	 * @param PitchOverride             覆盖的 Pitch 值（如 0.f 强制水平，-45.f 斜向上抛）
	 *
	 * 生成流程：
	 *   1. 通过 ICombatInterface::GetCombatSocketLocation 获取插槽世界坐标作为出生点
	 *   2. 计算插槽 → 目标的旋转作为投射物朝向（可 Override Pitch）
	 *   3. SpawnActorDeferred：先分配对象、设置 DamageEffectParams，再 FinishSpawning
	 *      延迟生成确保投射物在首帧 BeginPlay 前就持有完整的伤害参数，
	 *      避免 BeginPlay 中访问空参数导致的时序问题
	 */
	UFUNCTION(BlueprintCallable, Category = "Projectile")
	void SpawnProjectile(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag, bool bOverridePitch = false, float PitchOverride = 0.f);

	/**
	 * 要生成的投射物 Actor 类
	 * 配置为 BP_AuraFireBolt 等具体投射物蓝图，
	 * 该蓝图负责飞行轨迹、碰撞检测、命中特效、以及应用 DamageEffectParams。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<AAuraProjectile> ProjectileClass;

	/**
	 * 单次施法生成的投射物数量
	 * AuraFireBolt 据此在多个不同角度生成多发火球，
	 * 角度分散逻辑在蓝图中通过循环调用 SpawnProjectile 实现。
	 */
	UPROPERTY(EditDefaultsOnly)
	int32 NumProjectiles = 5;
};
