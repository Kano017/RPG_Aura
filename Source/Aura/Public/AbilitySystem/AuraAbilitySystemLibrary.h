// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Data/CharacterClassInfo.h"
#include "AuraAbilitySystemLibrary.generated.h"

class ULootTiers;
class ULoadScreenSaveGame;
class UAbilityInfo;
class USpellMenuWidgetController;
class UAbilitySystemComponent;
class UAttributeMenuWidgetController;
class UOverlayWidgetController;
struct FWidgetControllerParams;

// ============================================================
// UAuraAbilitySystemLibrary — GAS 静态工具函数库
// ============================================================
// 设计意图：
//   本库继承自 UBlueprintFunctionLibrary，所有函数都是静态的。
//   这样设计的好处：
//     1. 蓝图可调用（UFUNCTION 标记），无需实例，任意蓝图节点可直接使用
//     2. 跨系统访问中枢：UI 层、技能层、战斗层都可能需要访问 GameMode
//        数据资产（CharacterClassInfo、AbilityInfo）或 WidgetController，
//        静态库提供统一入口，避免各系统直接持有彼此引用（防止循环依赖）
//     3. WorldContextObject 模式：通过传入 WorldContextObject（通常是 Self）
//        可以访问 UWorld，进而访问 GameMode、PlayerController 等全局对象
//   典型访问链：
//     GetOverlayWidgetController：WorldContextObject → PlayerController(0)
//       → HUD(AAuraHUD) → GetOverlayWidgetController(WCParams)
//     GetCharacterClassInfo：WorldContextObject → GameMode(AAuraGameModeBase)
//       → CharacterClassInfo 数据资产
// ============================================================

/**
 * UAuraAbilitySystemLibrary
 *
 * 全局 GAS 工具函数库，分为五个功能模块：
 *   1. 控件控制器访问（WidgetController）
 *   2. 角色属性/技能初始化（CharacterClassDefaults）
 *   3. EffectContext 扩展字段读取器（GameplayEffects Getters）
 *   4. EffectContext 扩展字段写入器（GameplayEffects Setters）
 *   5. 游戏机制工具（GameplayMechanics）
 */
UCLASS()
class AURA_API UAuraAbilitySystemLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	// ──────────────────────────────────────────────────────────
	// 控件控制器（WidgetController）
	// ──────────────────────────────────────────────────────────

	/**
	 * 构造 FWidgetControllerParams 并获取 AAuraHUD 引用的共享逻辑。
	 * 访问链：WorldContextObject → GetPlayerController(0) → GetHUD() → Cast<AAuraHUD>
	 *         → PC->GetPlayerState<AAuraPlayerState>() → 提取 ASC / AttributeSet
	 * 返回 bool 表示是否成功（PC、HUD、PS 任一为空则失败）。
	 * 这是 GetOverlayWidgetController 等三个函数的公共前置步骤，提取为独立函数避免重复代码。
	 * 仅在本地 Client 有效（GetPlayerController(0) 只返回本地玩家的 PC）。
	 */
	UFUNCTION(BlueprintPure, Category="AuraAbilitySystemLibrary|WidgetController", meta = (DefaultToSelf = "WorldContextObject"))
	static bool MakeWidgetControllerParams(const UObject* WorldContextObject, FWidgetControllerParams& OutWCParams, AAuraHUD*& OutAuraHUD);

	/**
	 * 获取覆盖层 UI 控制器（血量/法力条、物品拾取消息等）。
	 * 内部通过 MakeWidgetControllerParams 获取 HUD，再调用 AAuraHUD::GetOverlayWidgetController。
	 * AAuraHUD 采用懒加载模式：首次调用时创建控制器并缓存，后续直接返回。
	 * BlueprintPure：无副作用的查询函数，蓝图中不需要执行引脚。
	 */
	UFUNCTION(BlueprintPure, Category="AuraAbilitySystemLibrary|WidgetController", meta = (DefaultToSelf = "WorldContextObject"))
	static UOverlayWidgetController* GetOverlayWidgetController(const UObject* WorldContextObject);

	/**
	 * 获取属性菜单控制器（力量/智力等主要属性的查看/升级界面）。
	 * 同上，通过 AAuraHUD 懒加载获取。
	 */
	UFUNCTION(BlueprintPure, Category="AuraAbilitySystemLibrary|WidgetController", meta = (DefaultToSelf = "WorldContextObject"))
	static UAttributeMenuWidgetController* GetAttributeMenuWidgetController(const UObject* WorldContextObject);

	/**
	 * 获取法术菜单控制器（技能解锁、升级、装备到槽位的界面）。
	 * 法术菜单是本项目最复杂的 UI 模块，其控制器需要访问 ASC 的技能状态。
	 */
	UFUNCTION(BlueprintPure, Category="AuraAbilitySystemLibrary|WidgetController", meta = (DefaultToSelf = "WorldContextObject"))
	static USpellMenuWidgetController* GetSpellMenuWidgetController(const UObject* WorldContextObject);

	// ──────────────────────────────────────────────────────────
	// 技能系统类默认设置（CharacterClassDefaults）
	// ──────────────────────────────────────────────────────────

	/**
	 * 根据角色职业和等级，通过 GameplayEffect 初始化角色的所有属性。
	 * 初始化顺序必须严格遵守（因为次要/生命属性依赖主要属性的值）：
	 *   1. PrimaryAttributes（力量/智力等，由职业数据资产中的 GE 设置）
	 *   2. SecondaryAttributes（攻击/防御等，通过 MMC 从主要属性计算）
	 *   3. VitalAttributes（生命/法力值，从次要属性计算最大值后初始化当前值）
	 * 调用方：敌人生成时（BeginPlay）或玩家加载时。
	 * 仅在 Server 端调用（ApplyGameplayEffectSpecToSelf 是 Server 权威操作）。
	 */
	UFUNCTION(BlueprintCallable, Category="AuraAbilitySystemLibrary|CharacterClassDefaults")
	static void InitializeDefaultAttributes(const UObject* WorldContextObject, ECharacterClass CharacterClass, float Level, UAbilitySystemComponent* ASC);

	/**
	 * 从存档数据恢复玩家属性（存档加载专用路径）。
	 * 与 InitializeDefaultAttributes 的区别：
	 *   - 主要属性使用 SetByCaller GE，直接将存档中的数值写入（而非按职业/等级计算）
	 *   - 次要属性使用 Infinite 持续类型 GE（防止存档加载后因持续时间结束而重置）
	 *   - 生命/法力直接恢复，不再"重置到满"
	 */
	UFUNCTION(BlueprintCallable, Category="AuraAbilitySystemLibrary|CharacterClassDefaults")
	static void InitializeDefaultAttributesFromSaveData(const UObject* WorldContextObject, UAbilitySystemComponent* ASC, ULoadScreenSaveGame* SaveGame);

	/**
	 * 根据职业数据资产批量赋予角色技能（含所有职业共用技能 + 职业专属技能）。
	 * 两层赋予：
	 *   1. CharacterClassInfo->CommonAbilities：所有职业共用（如受击反应、近战攻击）
	 *   2. ClassDefaultInfo->StartupAbilities：该职业专属（如法师的冰锥术）
	 * 敌人技能的等级通过 ICombatInterface::GetPlayerLevel 获取（等于敌人等级）。
	 * 玩家技能通过单独的 AddCharacterAbilities 赋予，不走此函数。
	 */
	UFUNCTION(BlueprintCallable, Category="AuraAbilitySystemLibrary|CharacterClassDefaults")
	static void GiveStartupAbilities(const UObject* WorldContextObject, UAbilitySystemComponent* ASC, ECharacterClass CharacterClass);

	/**
	 * 从 GameMode 获取角色职业配置数据资产（UCharacterClassInfo）。
	 * GameMode 只存在于 Server 端，Client 调用时返回 nullptr。
	 * 调用方需注意：若在 Client 端调用，需确保有容错处理（不依赖此数据的 UI 代码除外）。
	 */
	UFUNCTION(BlueprintCallable, Category="AuraAbilitySystemLibrary|CharacterClassDefaults")
	static UCharacterClassInfo* GetCharacterClassInfo(const UObject* WorldContextObject);

	/**
	 * 从 GameMode 获取技能信息数据资产（UAbilityInfo）。
	 * UAbilityInfo 包含所有技能的元数据（图标、等级需求、技能类型、描述等），
	 * 供法术菜单 UI 和 ASC 的技能状态管理使用。
	 */
	UFUNCTION(BlueprintCallable, Category="AuraAbilitySystemLibrary|CharacterClassDefaults")
	static UAbilityInfo* GetAbilityInfo(const UObject* WorldContextObject);

	/**
	 * 从 GameMode 获取战利品掉落分级数据资产（ULootTiers）。
	 * 用于敌人死亡时决定掉落物的种类和数量。
	 */
	UFUNCTION(BlueprintCallable, Category="AuraAbilitySystemLibrary|CharacterClassDefaults", meta = (DefaultToSelf = "WorldContextObject"))
	static ULootTiers* GetLootTiers(const UObject* WorldContextObject);

	// ──────────────────────────────────────────────────────────
	// 效果上下文读取器（EffectContext Getters）
	// ──────────────────────────────────────────────────────────
	// 设计说明：
	//   GAS 的 FGameplayEffectContext 是通用基类，不包含项目特有字段。
	//   本项目扩展了 FAuraGameplayEffectContext（在 AuraAbilityTypes.h 中），
	//   增加了：是否格挡/暴击、减益数据（伤害/持续时间/频率）、伤害类型、
	//   死亡冲量、击退力、范围伤害参数等。
	//   以下读取函数统一执行 static_cast<FAuraGameplayEffectContext*>，
	//   这是安全的，因为整个项目的 GE 上下文都用此扩展类创建。
	//   提供蓝图可调用的静态包装，避免蓝图直接操作结构体指针。
	// ──────────────────────────────────────────────────────────

	/** 是否被格挡（格挡时伤害减为 0，触发格挡音效/特效） */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static bool IsBlockedHit(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 是否成功施加减益效果（ExecCalc_Damage 根据概率计算） */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static bool IsSuccessfulDebuff(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 减益效果的每次触发伤害量 */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static float GetDebuffDamage(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 减益效果的总持续时间（秒） */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static float GetDebuffDuration(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 减益效果的触发频率（每 N 秒触发一次） */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static float GetDebuffFrequency(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 伤害类型 Tag（如 Damage.Fire、Damage.Lightning），用于减益类型判断和特效分类 */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static FGameplayTag GetDamageType(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 死亡冲量向量：角色死亡时施加此冲量使尸体飞出，增强打击感 */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static FVector GetDeathImpulse(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 击退力向量：受击时施加此力使角色短暂位移（不会致死） */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static FVector GetKnockbackForce(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 是否暴击（暴击时伤害倍增，触发暴击特效） */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static bool IsCriticalHit(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 是否为范围伤害（如陨石、爆炸，以同心圆衰减伤害） */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static bool IsRadialDamage(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 范围伤害内圈半径（内圈内全额伤害） */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static float GetRadialDamageInnerRadius(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 范围伤害外圈半径（外圈边缘伤害衰减至最小） */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static float GetRadialDamageOuterRadius(const FGameplayEffectContextHandle& EffectContextHandle);

	/** 范围伤害爆炸原点坐标（用于计算目标到爆心的距离以进行伤害衰减） */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static FVector GetRadialDamageOrigin(const FGameplayEffectContextHandle& EffectContextHandle);

	// ──────────────────────────────────────────────────────────
	// 效果上下文写入器（EffectContext Setters）
	// ──────────────────────────────────────────────────────────
	// 所有 Setter 函数通过 UPARAM(ref) 接受 Handle 引用，
	// 因为 FGameplayEffectContextHandle 是包装指针的结构体，需要通过引用才能修改内部数据。
	// 调用方：ExecCalc_Damage（在 Server 端执行伤害计算时填充这些字段）
	//         和 AuraDamageGameplayAbility（在技能激活时构造 EffectContext）。
	// ──────────────────────────────────────────────────────────

	/** 设置是否格挡（由 ExecCalc_Damage 根据格挡概率计算后写入） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetIsBlockedHit(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, bool bInIsBlockedHit);

	/** 设置是否暴击（由 ExecCalc_Damage 根据暴击概率计算后写入） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetIsCriticalHit(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, bool bInIsCriticalHit);

	/** 设置减益是否成功触发（由 ExecCalc_Damage 根据 DebuffChance 计算后写入） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetIsSuccessfulDebuff(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, bool bInSuccessfulDebuff);

	/** 设置减益每次触发伤害量（从 FDamageEffectParams 传入） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetDebuffDamage(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, float InDamage);

	/** 设置减益总持续时间（从 FDamageEffectParams 传入） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetDebuffDuration(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, float InDuration);

	/** 设置减益触发频率（从 FDamageEffectParams 传入） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetDebuffFrequency(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, float InFrequency);

	/** 设置伤害类型 Tag（用于 ExecCalc_Damage 查找对应的抗性属性进行减免计算） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetDamageType(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, const FGameplayTag& InDamageType);

	/** 设置死亡冲量（技能命中时根据方向和距离计算好冲量向量后写入） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetDeathImpulse(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, const FVector& InImpulse);

	/** 设置击退力（AuraDamageGameplayAbility 在技能激活时根据配置写入） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetKnockbackForce(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, const FVector& InForce);

	/** 设置是否为范围伤害（在 ApplyDamageEffect 中从 FDamageEffectParams 读取并写入） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetIsRadialDamage(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, bool bInIsRadialDamage);

	/** 设置范围伤害内圈半径 */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetRadialDamageInnerRadius(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, float InInnerRadius);

	/** 设置范围伤害外圈半径 */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetRadialDamageOuterRadius(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, float InOuterRadius);

	/** 设置范围伤害爆炸原点（用于 ExecCalc_Damage 中计算距离衰减） */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayEffects")
	static void SetRadialDamageOrigin(UPARAM(ref) FGameplayEffectContextHandle& EffectContextHandle, const FVector& InOrigin);

	// ──────────────────────────────────────────────────────────
	// 游戏机制工具（GameplayMechanics）
	// ──────────────────────────────────────────────────────────

	/**
	 * 在球形范围内查找所有存活的 Actor（实现了 UCombatInterface 且未死亡）。
	 * 使用 OverlapMultiByObjectType（AllDynamicObjects）+ UCombatInterface::IsDead 过滤。
	 * ActorsToIgnore：排除施法者自身以及已处理过的目标。
	 * 结果存入 OutOverlappingActors（AddUnique 避免重复）。
	 * 调用方：链式闪电、范围爆炸等需要查找周围敌人的技能。
	 */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayMechanics")
	static void GetLivePlayersWithinRadius(const UObject* WorldContextObject, TArray<AActor*>& OutOverlappingActors, const TArray<AActor*>& ActorsToIgnore, float Radius, const FVector& SphereOrigin);

	/**
	 * 从给定的 Actor 数组中选出距 Origin 最近的 MaxTargets 个。
	 * 算法：贪心逐步选取（每轮找最近的，移出列表，重复），时间复杂度 O(N*MaxTargets)。
	 * 若 Actors 数量不超过 MaxTargets，直接返回全部。
	 * 调用方：链式闪电技能（选取距发射点最近的 N 个目标）。
	 */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|GameplayMechanics")
	static void GetClosestTargets(int32 MaxTargets, const TArray<AActor*>& Actors, TArray<AActor*>& OutClosestTargets, const FVector& Origin);

	/**
	 * 判断两个 Actor 是否为敌对关系（非友方）。
	 * 判断逻辑：通过 Actor Tag（"Player"/"Enemy"）区分阵营，
	 * 两者均为玩家或均为敌人时为友方，否则为敌对。
	 * 用于 GetLivePlayersWithinRadius 等函数的后续过滤，以及技能命中判断。
	 */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayMechanics")
	static bool IsNotFriend(AActor* FirstActor, AActor* SecondActor);

	/**
	 * 技能伤害效果应用的统一入口，处理完整的伤害投递流程：
	 *   1. 创建 EffectContext，设置 SourceObject、死亡冲量、击退力、范围伤害参数
	 *   2. 通过 MakeOutgoingSpec 构建 GE Spec，绑定 DamageType/DebuffChance 等 SetByCaller 数值
	 *   3. 调用 TargetASC->ApplyGameplayEffectSpecToSelf 将伤害应用到目标
	 * FDamageEffectParams 作为参数包（Parameter Object 模式）传入所有需要的数据，
	 * 避免函数参数列表过长。
	 * 返回 EffectContextHandle 供调用方（如连锁跳跃逻辑）进一步检查应用结果。
	 */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|DamageEffect")
	static FGameplayEffectContextHandle ApplyDamageEffect(const FDamageEffectParams& DamageEffectParams);

	/**
	 * 在给定扩散角范围内均匀生成 NumRotators 个朝向（FRotator）。
	 * 算法：以 Forward 方向为中心，向左旋转 Spread/2 得到起始方向，
	 *       然后以 DeltaSpread 为步长均匀分布。
	 * 用于多发弹幕类技能（如 FireBolt 多发火球扇形发射方向计算）。
	 */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayMechanics")
	static TArray<FRotator> EvenlySpacedRotators(const FVector& Forward, const FVector& Axis, float Spread, int32 NumRotators);

	/**
	 * 与 EvenlySpacedRotators 相同，但返回 FVector 方向向量而非 FRotator。
	 * 某些技能（如生成弹幕时直接设置速度方向）使用向量更方便。
	 */
	UFUNCTION(BlueprintPure, Category = "AuraAbilitySystemLibrary|GameplayMechanics")
	static TArray<FVector> EvenlyRotatedVectors(const FVector& Forward, const FVector& Axis, float Spread, int32 NumVectors);

	/**
	 * 根据敌人职业和等级查询击杀经验值奖励（从 CharacterClassInfo 数据资产中读取曲线）。
	 * 非 UFUNCTION（仅 C++ 调用）：经验值计算是纯服务器逻辑，蓝图无需访问。
	 * XPReward 存储为 FScalableFloat（曲线），支持随等级非线性增长。
	 */
	static int32 GetXPRewardForClassAndLevel(const UObject* WorldContextObject, ECharacterClass CharacterClass, int32 CharacterLevel);

	// ──────────────────────────────────────────────────────────
	// 伤害效果参数填充工具（DamageEffectParams Setters）
	// ──────────────────────────────────────────────────────────
	// FDamageEffectParams 是伤害效果的参数包，在技能蓝图中组装后传入 ApplyDamageEffect。
	// 以下函数提供对参数包中特定字段的便捷设置，尤其是需要方向计算的字段。
	// ──────────────────────────────────────────────────────────

	/**
	 * 一次性设置 FDamageEffectParams 中的所有范围伤害参数。
	 * 便于蓝图在一个节点内完成范围伤害的配置，减少连线复杂度。
	 */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|DamageEffect")
	static void SetIsRadialDamageEffectParam(UPARAM(ref) FDamageEffectParams& DamageEffectParams, bool bIsRadial, float InnerRadius, float OuterRadius, FVector Origin);

	/**
	 * 设置击退方向和力度。
	 * 若 Magnitude == 0，使用参数包中预设的 KnockbackForceMagnitude（设计时配置值）；
	 * 否则使用传入的 Magnitude 覆盖（允许运行时动态调整击退力度，如爆炸中心附近更强）。
	 * 内部自动 Normalize 方向向量，确保只有方向信息。
	 */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|DamageEffect")
	static void SetKnockbackDirection(UPARAM(ref) FDamageEffectParams& DamageEffectParams, FVector KnockbackDirection, float Magnitude = 0.f);

	/**
	 * 设置死亡冲量方向和力度。
	 * 与 SetKnockbackDirection 同理：Magnitude == 0 时使用预设值，否则使用传入值。
	 * 死亡冲量通常比击退力更大（使尸体飞出），方向由技能命中时的冲击方向决定。
	 */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|DamageEffect")
	static void SetDeathImpulseDirection(UPARAM(ref) FDamageEffectParams& DamageEffectParams, FVector ImpulseDirection, float Magnitude = 0.f);

	/**
	 * 在参数包已构建完成后，补充设置目标 ASC。
	 * 使用场景：多目标技能（如链式闪电）在找到每个目标后，
	 * 动态替换 TargetAbilitySystemComponent 然后调用 ApplyDamageEffect，
	 * 而其余参数保持不变，避免为每个目标重新构建完整参数包。
	 */
	UFUNCTION(BlueprintCallable, Category = "AuraAbilitySystemLibrary|DamageEffect")
	static void SetTargetEffectParamsASC(UPARAM(ref) FDamageEffectParams& DamageEffectParams, UAbilitySystemComponent* InASC);
};
