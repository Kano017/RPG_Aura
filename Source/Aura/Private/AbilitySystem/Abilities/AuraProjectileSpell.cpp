// Copyright Druid Mechanics


#include "AbilitySystem/Abilities/AuraProjectileSpell.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Actor/AuraProjectile.h"
#include "Interaction/CombatInterface.h"

/**
 * ActivateAbility —— 技能激活时的 C++ 入口
 *
 * 此处仅调用 Super 完成 GAS 内部的基础激活流程：
 *   - 检查 CanActivateAbility（标签要求、消耗检查等）
 *   - 触发 AbilityActivated 委托供外部监听
 * 真正的技能逻辑（等待鼠标目标数据、播放施法动画、
 * 调用 SpawnProjectile）均在蓝图的事件图表中实现。
 *
 * 这种 C++/Blueprint 分工的设计意图：
 *   - C++ 提供可复用的工具函数（SpawnProjectile）
 *   - Blueprint 控制技能的时序流程（等待 → 播放动画 → 生成 → 结束）
 *     让策划/TA 无需修改 C++ 即可调整技能节奏
 */
void UAuraProjectileSpell::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                           const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                           const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);



}

/**
 * SpawnProjectile —— 在武器插槽处生成朝向目标的投射物（仅服务器执行）
 *
 * 【权威检查】
 * 函数首行检查 HasAuthority()，非服务器直接返回。
 * 原因：投射物 Actor 需要服务器生成后复制到客户端，
 * 若客户端也生成一个则会出现"幽灵投射物"（本地预测副本）与
 * 服务器复制副本的双重冲突，产生视觉错误和伤害重复。
 *
 * 【插槽位置获取】
 * ICombatInterface::Execute_GetCombatSocketLocation 通过 SocketTag 查找
 * 角色骨骼网格上对应插槽的世界坐标（如手腕插槽、法杖尖端插槽）。
 * 使用接口而非直接访问 Mesh，保证对玩家角色和敌人角色通用。
 *
 * 【旋转计算与 Pitch 覆盖】
 * 默认旋转 = 插槽位置 → 目标位置的方向。
 * bOverridePitch 允许锁定俯仰角，例如 FireBolt 多发弹散射时
 * 每发弹的水平角度不同（左右扇形），但 Pitch 保持一致（平射）。
 *
 * 【延迟生成（SpawnActorDeferred / FinishSpawning）的必要性】
 * 普通 SpawnActor 在生成时立即调用 BeginPlay，此时若设置 DamageEffectParams
 * 则为时已晚（BeginPlay 已在空参数状态下执行）。
 * SpawnActorDeferred 先创建对象但挂起 BeginPlay，
 * 手动设置好 DamageEffectParams 后再调用 FinishSpawning 触发 BeginPlay，
 * 确保投射物在首帧就持有完整伤害数据。
 */
void UAuraProjectileSpell::SpawnProjectile(const FVector& ProjectileTargetLocation, const FGameplayTag& SocketTag, bool bOverridePitch, float PitchOverride)
{
	// 仅服务器有权生成投射物，客户端通过网络复制看到结果
	const bool bIsServer = GetAvatarActorFromActorInfo()->HasAuthority();
	if (!bIsServer) return;

	// 获取武器/角色骨骼插槽的世界坐标作为投射物生成点
	const FVector SocketLocation = ICombatInterface::Execute_GetCombatSocketLocation(
		GetAvatarActorFromActorInfo(),
		SocketTag);

	// 计算生成点指向目标的旋转；bOverridePitch 时替换俯仰角（如强制水平飞行）
	FRotator Rotation = (ProjectileTargetLocation - SocketLocation).Rotation();
	if (bOverridePitch)
	{
		Rotation.Pitch = PitchOverride;
	}

	FTransform SpawnTransform;
	SpawnTransform.SetLocation(SocketLocation);
	SpawnTransform.SetRotation(Rotation.Quaternion());

	// 延迟生成：先分配投射物对象，挂起 BeginPlay
	AAuraProjectile* Projectile = GetWorld()->SpawnActorDeferred<AAuraProjectile>(
		ProjectileClass,
		SpawnTransform,
		GetOwningActorFromActorInfo(),          // Owner（技能持有者）
		Cast<APawn>(GetOwningActorFromActorInfo()), // Instigator（用于伤害溯源）
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn); // 始终生成，不因碰撞阻止

	// 在 BeginPlay 前将完整的伤害参数注入投射物
	// 投射物飞行期间持有这份参数，碰到目标后直接调用 ApplyDamageEffect
	Projectile->DamageEffectParams = MakeDamageEffectParamsFromClassDefaults();

	// 触发 BeginPlay，投射物正式激活（启动飞行组件、开始碰撞检测）
	Projectile->FinishSpawning(SpawnTransform);
}
