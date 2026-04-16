// Copyright Druid Mechanics


#include "AbilitySystem/Abilities/AuraBeamSpell.h"

#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"

/**
 * StoreMouseDataInfo —— 缓存鼠标命中数据，命中失败则取消技能
 *
 * 光束技能在激活初期必须确认一个有效目标，若 HitResult 未命中任何物体
 * （如玩家点击了天空或超出射程的区域），技能没有意义继续执行。
 * CancelAbility 的 bReplicateCancelAbility = true 参数确保
 * 取消行为同步到所有客户端，避免服务器取消但客户端仍播放动画的状态不一致。
 */
void UAuraBeamSpell::StoreMouseDataInfo(const FHitResult& HitResult)
{
	if (HitResult.bBlockingHit)
	{
		// 缓存命中点和命中 Actor，供后续 TraceFirstTarget 和蓝图光束特效使用
		MouseHitLocation = HitResult.ImpactPoint;
		MouseHitActor = HitResult.GetActor();
	}
	else
	{
		// 未命中有效目标，立即取消技能（会退还法力/冷却，因为 CommitAbility 尚未调用）
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
	}
}

/**
 * StoreOwnerVariables —— 缓存施法者引用
 *
 * CurrentActorInfo 是 GAS 注入的技能激活上下文，包含 PlayerController、AvatarActor 等。
 * 将常用引用缓存为成员变量（BlueprintReadWrite）：
 *   - 蓝图可直接访问，无需每次调用 GetOwner 等函数
 *   - 跨多帧的光束追踪逻辑中避免重复的接口查询开销
 * 此函数在 ActivateAbility 蓝图图表的早期节点调用，确保后续所有步骤引用可用。
 */
void UAuraBeamSpell::StoreOwnerVariables()
{
	if (CurrentActorInfo)
	{
		OwnerPlayerController = CurrentActorInfo->PlayerController.Get();
		OwnerCharacter = Cast<ACharacter>(CurrentActorInfo->AvatarActor);
	}
}

/**
 * TraceFirstTarget —— 从武器尖端向目标做球形扫描，精确定位主目标
 *
 * 【为何需要再 Trace 一次？】
 * StoreMouseDataInfo 获得的 HitResult 来自屏幕射线，命中的可能是：
 *   - 远处地面（玩家鼠标悬停在地面）
 *   - 目标角色的胶囊体
 *   - 目标角色身后的墙
 * 此函数从武器插槽（TipSocket）重新发出球形扫描，
 * 确保光束从武器尖端出发，并以武器为参照修正最终命中点，
 * 视觉上光束与武器动画完全对齐。
 *
 * 【SphereTraceSingle 参数说明】
 *   - 半径 10.f：细窄的球形扫描，兼顾精度和容错
 *   - TraceTypeQuery1：对应 ECC_Visibility，可命中可见性通道的物体（敌人、地形等）
 *   - ActorsToIgnore 含 OwnerCharacter：防止光束打到自己
 *   - EDrawDebugTrace::None：生产环境不绘制调试线（调试时可改为 ForOneFrame）
 *
 * 【死亡委托绑定】
 * 命中有效 CombatInterface 目标后，绑定其 OnDeathDelegate：
 *   - 检查 IsAlreadyBound 防止重复绑定（如技能持续期间多次调用此函数）
 *   - 目标死亡时自动回调 PrimaryTargetDied，蓝图处理光束断开逻辑
 */
void UAuraBeamSpell::TraceFirstTarget(const FVector& BeamTargetLocation)
{
	check(OwnerCharacter);
	if (OwnerCharacter->Implements<UCombatInterface>())
	{
		if (USkeletalMeshComponent* Weapon = ICombatInterface::Execute_GetWeapon(OwnerCharacter))
		{
			TArray<AActor*> ActorsToIgnore;
			ActorsToIgnore.Add(OwnerCharacter); // 排除自身，防止光束打到施法者

			FHitResult HitResult;
			// 获取武器尖端插槽的世界坐标作为扫描起点
			const FVector SocketLocation = Weapon->GetSocketLocation(FName("TipSocket"));

			// 从武器尖端向目标位置做球形扫描，半径 10 保证窄束精度
			UKismetSystemLibrary::SphereTraceSingle(
				OwnerCharacter,
				SocketLocation,
				BeamTargetLocation,
				10.f,
				TraceTypeQuery1,
				false,
				ActorsToIgnore,
				EDrawDebugTrace::None,
				HitResult,
				true); // bIgnoreSelf = true

			if (HitResult.bBlockingHit)
			{
				// 用武器视角的精确命中点覆盖鼠标视角的命中数据
				MouseHitLocation = HitResult.ImpactPoint;
				MouseHitActor = HitResult.GetActor();
			}
		}
	}

	// 绑定主目标的死亡委托（防重复绑定），目标死亡时触发 PrimaryTargetDied 蓝图事件
	if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(MouseHitActor))
	{
		if (!CombatInterface->GetOnDeathDelegate().IsAlreadyBound(this, &UAuraBeamSpell::PrimaryTargetDied))
		{
			CombatInterface->GetOnDeathDelegate().AddDynamic(this, &UAuraBeamSpell::PrimaryTargetDied);
		}
	}
}

/**
 * StoreAdditionalTargets —— 查找主目标周围的链式目标
 *
 * 【链式目标筛选流程】
 *   1. GetLivePlayersWithinRadius：以主目标为中心、850 单位为半径，
 *      查找所有存活的敌方 Actor（排除施法者和主目标自身）
 *   2. GetClosestTargets：从上述结果中取距离主目标最近的 N 个，
 *      N = min(技能等级 - 1, MaxNumShockTargets)，
 *      1 级无链式目标，每升 1 级多链接 1 个，最多 MaxNumShockTargets 个
 *
 * 【为何以"主目标位置"为中心而非施法者？】
 * 光束链式效果应在主目标周围跳跃（如闪电链），
 * 以主目标为中心才能确保链式目标在视觉上紧邻主目标，
 * 而非分散在施法者周围。
 *
 * 【死亡委托绑定】
 * 与主目标相同的防重复绑定策略，对每个链式目标绑定 AdditionalTargetDied，
 * 目标死亡时蓝图可断开对应光束并减少链式计数。
 *
 * @param OutAdditionalTargets  输出参数，蓝图读取此数组驱动链式光束粒子特效
 */
void UAuraBeamSpell::StoreAdditionalTargets(TArray<AActor*>& OutAdditionalTargets)
{
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(GetAvatarActorFromActorInfo()); // 排除施法者
	ActorsToIgnore.Add(MouseHitActor);                 // 排除已确定的主目标

	// 在主目标周围 850 单位内查找所有存活的敌方 Actor
	TArray<AActor*> OverlappingActors;
	UAuraAbilitySystemLibrary::GetLivePlayersWithinRadius(
		GetAvatarActorFromActorInfo(),
		OverlappingActors,
		ActorsToIgnore,
		850.f,
		MouseHitActor->GetActorLocation());

	// 链式数量 = 技能等级 - 1（1 级无链式），并受 MaxNumShockTargets 上限约束
	int32 NumAdditionalTargets = FMath::Min(GetAbilityLevel() - 1, MaxNumShockTargets);
	//int32 NumAdditionTargets = 5;  // 调试用固定值

	// 从所有候选目标中取距主目标最近的 N 个，提高链式视觉合理性
	UAuraAbilitySystemLibrary::GetClosestTargets(
		NumAdditionalTargets,
		OverlappingActors,
		OutAdditionalTargets,
		MouseHitActor->GetActorLocation());

	// 为每个链式目标绑定死亡委托，防止重复绑定
	for (AActor* Target : OutAdditionalTargets)
	{
		if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(Target))
		{
			if (!CombatInterface->GetOnDeathDelegate().IsAlreadyBound(this, &UAuraBeamSpell::AdditionalTargetDied))
			{
				CombatInterface->GetOnDeathDelegate().AddDynamic(this, &UAuraBeamSpell::AdditionalTargetDied);
			}
		}
	}
}
