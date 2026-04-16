// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AuraGameplayAbility.h"
#include "AuraSummonAbility.generated.h"

/**
 * UAuraSummonAbility —— 召唤类技能基类
 *
 * 【与伤害技能的层次差异】
 * 召唤技能不造成直接伤害，因此继承自 UAuraGameplayAbility 而非 UAuraDamageGameplayAbility。
 * 被召唤的小兵自身持有 ASC 和伤害能力，与召唤技能完全解耦。
 *
 * 【召唤技能流程概览】
 *   1. ActivateAbility（蓝图图表）：检查当前场景中是否还有存活小兵
 *      → 若已有小兵则直接杀死旧小兵（调用 KillMinion）
 *   2. CommitAbility：消耗法力/触发冷却
 *   3. 播放施法动画
 *   4. 在动画事件点调用 GetSpawnLocations 计算生成位置数组
 *   5. 在蓝图中循环 SpawnActor，对每个位置用 GetRandomMinionClass 随机选一个小兵类
 *   6. EndAbility
 *
 * 【位置生成算法】
 * GetSpawnLocations 在角色前方扇形区域内均匀分布生成点：
 *   - 扇形以角色前向为中轴，左右各张开 SpawnSpread/2 度
 *   - NumMinions 个生成点将扇形等分为 NumMinions 份，每份取一个方向
 *   - 每个方向上的距离在 [MinSpawnDistance, MaxSpawnDistance] 内随机
 *   - 最后对每个候选点做垂直 LineTrace，贴合地形表面，防止生成在空中或地下
 *
 * 【网络说明】
 * 召唤的小兵 Actor 在服务器生成后复制到所有客户端，是标准的服务器权威 Actor。
 * GetSpawnLocations 仅在服务器调用，返回的位置数组传给蓝图的 SpawnActor 节点。
 */
UCLASS()
class AURA_API UAuraSummonAbility : public UAuraGameplayAbility
{
	GENERATED_BODY()
public:

	/**
	 * GetSpawnLocations —— 计算所有小兵的生成位置
	 *
	 * 算法步骤（详见 .cpp 中的逐行注释）：
	 *   1. 取角色前向向量，将其向左旋转 SpawnSpread/2 度得到扇形左边界方向
	 *   2. 每个小兵依次在左边界基础上累加 DeltaSpread（= SpawnSpread / NumMinions）度
	 *      得到均匀分布在扇形内的 N 个方向
	 *   3. 每个方向上随机选一个距离，得到初步的水平面候选坐标
	 *   4. 从候选坐标上方 400 单位向下 LineTrace，命中地面则贴合地形，
	 *      未命中（如悬崖边缘）也将候选坐标直接加入结果（容错处理）
	 *
	 * @return  NumMinions 个世界坐标，蓝图遍历此数组逐一 SpawnActor
	 */
	UFUNCTION(BlueprintCallable)
	TArray<FVector> GetSpawnLocations();

	/**
	 * GetRandomMinionClass —— 从 MinionClasses 数组中随机选取一个小兵类
	 *
	 * 支持在单次召唤中生成不同种类的小兵（如骷髅弓箭手、骷髅战士混合召唤）。
	 * 每次调用独立随机，蓝图循环时每个位置可能生成不同类型。
	 */
	UFUNCTION(BlueprintPure, Category="Summoning")
	TSubclassOf<APawn> GetRandomMinionClass();

	/** 单次召唤的小兵总数，同时决定 GetSpawnLocations 返回的位置数量 */
	UPROPERTY(EditDefaultsOnly, Category = "Summoning")
	int32 NumMinions = 5;

	/**
	 * 可召唤的小兵类数组（支持多种混合）
	 * 配置多个小兵蓝图类后，GetRandomMinionClass 从中随机选取，
	 * 实现召唤多样性而无需为每种组合创建单独的技能类。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Summoning")
	TArray<TSubclassOf<APawn>> MinionClasses;

	/** 生成点距角色的最小距离（单位：cm），防止小兵生成在角色身体里 */
	UPROPERTY(EditDefaultsOnly, Category = "Summoning")
	float MinSpawnDistance = 50.f;

	/** 生成点距角色的最大距离（单位：cm），控制召唤扩散范围 */
	UPROPERTY(EditDefaultsOnly, Category = "Summoning")
	float MaxSpawnDistance = 250.f;

	/**
	 * 召唤扇形的总张角（单位：度）
	 * 90.f 表示以角色前向为轴，左右各 45 度的 90 度扇形。
	 * 增大此值使小兵更分散（如 180 表示角色正面半圆），
	 * 减小此值使小兵集中在正前方（如 30 表示紧密前方锥形）。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Summoning")
	float SpawnSpread = 90.f;



};
