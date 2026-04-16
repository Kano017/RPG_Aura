// Copyright Druid Mechanics


#include "AbilitySystem/Abilities/AuraSummonAbility.h"

/**
 * GetSpawnLocations —— 在角色前方扇形区域内生成 NumMinions 个地面贴合的生成坐标
 *
 * 【扇形均匀分布算法详解】
 *
 * 设角色前向为 Forward，SpawnSpread = 90°，NumMinions = 5：
 *
 *   ← 45°    前向    45° →
 *       \      |      /
 *    L4  L3   L2   L1  L0   ← 5 个方向，每间隔 18°（DeltaSpread = 90/5 = 18）
 *
 *   LeftOfSpread = Forward.RotateAngleAxis(-45°, Up)   ← 扇形最左边界方向
 *   第 i 个方向 = LeftOfSpread.RotateAngleAxis(18° * i, Up)
 *     i=0：最左，i=4：最右，均匀覆盖整个扇形
 *
 * 【距离随机化】
 * 每个方向上的距离在 [MinSpawnDistance, MaxSpawnDistance] 内随机，
 * 使小兵不会整齐排列在同一弧线上，增加召唤的自然感。
 *
 * 【地形贴合（LineTrace）】
 * 召唤可能发生在高低不平的地形上（坡道、台阶、悬崖边）。
 * 水平面计算得到的候选坐标可能悬在空中或嵌入地面，
 * 因此从候选坐标上方 400 单位向下做 LineTrace（ECC_Visibility 通道）：
 *   - 命中地面：使用 HitResult.ImpactPoint 替换候选坐标（精确贴地）
 *   - 未命中（悬空/无地面）：保留原候选坐标（容错，小兵由物理引擎落地）
 */
TArray<FVector> UAuraSummonAbility::GetSpawnLocations()
{
	// 角色前向和当前位置
	const FVector Forward = GetAvatarActorFromActorInfo()->GetActorForwardVector();
	const FVector Location = GetAvatarActorFromActorInfo()->GetActorLocation();

	// 每个小兵之间的角度间隔（将扇形等分为 NumMinions 份）
	const float DeltaSpread = SpawnSpread / NumMinions;

	// 扇形左边界方向：前向绕 Up 轴向左旋转半个扇形角度
	const FVector LeftOfSpread = Forward.RotateAngleAxis(-SpawnSpread / 2.f, FVector::UpVector);

	TArray<FVector> SpawnLocations;
	for (int32 i = 0; i < NumMinions; i++)
	{
		// 当前小兵的方向：从左边界依次向右累加 DeltaSpread * i 度
		const FVector Direction = LeftOfSpread.RotateAngleAxis(DeltaSpread * i, FVector::UpVector);

		// 在此方向上随机选取距离，得到水平面上的候选生成坐标
		FVector ChosenSpawnLocation = Location + Direction * FMath::FRandRange(MinSpawnDistance, MaxSpawnDistance);

		// 从候选点正上方 400 单位向下 LineTrace，将坐标贴合到实际地形表面
		FHitResult Hit;
		GetWorld()->LineTraceSingleByChannel(
			Hit,
			ChosenSpawnLocation + FVector(0.f, 0.f, 400.f),   // 起点：候选点上方 400
			ChosenSpawnLocation - FVector(0.f, 0.f, 400.f),   // 终点：候选点下方 400
			ECC_Visibility);

		if (Hit.bBlockingHit)
		{
			// 命中地面，使用精确的地表坐标作为最终生成点
			ChosenSpawnLocation = Hit.ImpactPoint;
		}
		SpawnLocations.Add(ChosenSpawnLocation);
	}

	return SpawnLocations;
}

/**
 * GetRandomMinionClass —— 从配置的小兵类数组中随机选取一个
 *
 * 每次调用独立随机，支持召唤不同种类小兵的混合编队。
 * 注意：调用方应确保 MinionClasses 不为空（蓝图中应做判空保护），
 * 否则 RandRange(0, -1) 行为未定义。
 */
TSubclassOf<APawn> UAuraSummonAbility::GetRandomMinionClass()
{
	const int32 Selection = FMath::RandRange(0, MinionClasses.Num() - 1);
	return MinionClasses[Selection];
}
