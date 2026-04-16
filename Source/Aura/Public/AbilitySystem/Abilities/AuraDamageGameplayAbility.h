// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "AuraAbilityTypes.h"
#include "AbilitySystem/Abilities/AuraGameplayAbility.h"
#include "Interaction/CombatInterface.h"
#include "AuraDamageGameplayAbility.generated.h"

/**
 * UAuraDamageGameplayAbility —— 所有造成伤害技能的中间层基类
 *
 * 【设计意图：为何需要这一层？】
 * 并非每个技能都造成伤害（如召唤、Buff 类技能不需要），
 * 因此将伤害相关逻辑单独抽象为一层，而非堆入 UAuraGameplayAbility 基类。
 * 继承此类的技能（FireBolt、BeamSpell、ArcaneShards 等）
 * 只需在蓝图/C++ 中配置好 UPROPERTY 属性，
 * 就能通过 MakeDamageEffectParamsFromClassDefaults / CauseDamage 一行完成伤害应用，
 * 让具体子类专注于"如何施放"（投射物方向、光束目标、爆炸范围）
 * 而不必关心"如何造成伤害"（GE 构建、Tag 赋值、参数传递）。
 *
 * 【伤害流程概览】
 * CauseDamage / MakeDamageEffectParams
 *   → UAuraAbilitySystemLibrary::ApplyDamageEffect
 *     → SourceASC.ApplyGameplayEffectSpecToTarget(TargetASC)
 *       → UExecCalc_Damage::Execute_Implementation
 *         （计算最终伤害、暴击、抗性、Debuff 等）
 *
 * 【网络说明】
 * 伤害应用在服务器执行，ExecCalc 在服务器计算后通过 GE 复制到客户端。
 * 视觉效果（受击特效、飘字）由客户端监听 GameplayTag/Cue 自行触发。
 */
UCLASS()
class AURA_API UAuraDamageGameplayAbility : public UAuraGameplayAbility
{
	GENERATED_BODY()
public:

	/**
	 * CauseDamage —— 对单个目标应用伤害的快捷函数
	 *
	 * 内部直接构建 GE Spec 并调用 ASC.ApplyGameplayEffectSpecToTarget。
	 * 适合不需要投射物的近战/即时技能（如光束、近身攻击），
	 * 在技能蓝图中直接 CallFunction 即可完成一次伤害应用。
	 * 调用链：CauseDamage → MakeOutgoingGESpec → ApplyGESpecToTarget → ExecCalc_Damage
	 */
	UFUNCTION(BlueprintCallable)
	void CauseDamage(AActor* TargetActor);

	/**
	 * MakeDamageEffectParamsFromClassDefaults —— 将所有 UPROPERTY 打包为 FDamageEffectParams
	 *
	 * 【为何使用参数结构体而非直接传参？】
	 * 伤害计算涉及十余个参数（基础伤害、伤害类型、Debuff 概率/时长/强度、
	 * 击退方向/力度、死亡冲量、范围伤害半径等）。
	 * 若逐一作为函数参数传递，函数签名极为冗长，调用处易出错且难以扩展。
	 * 使用 FDamageEffectParams 封装后：
	 *   - 调用方只需传入变化的参数（TargetActor、方向覆盖等），其余取类默认值
	 *   - 投射物 Actor 拿到 Params 后持有并在碰撞时直接应用，无需反查技能对象
	 *   - 新增伤害参数只需扩展结构体，不影响所有现有调用点
	 *
	 * bOverrideKnockbackDirection / bOverrideDeathImpulse / bOverridePitch：
	 * 某些情况下（如爆炸范围伤害）需要从爆炸中心向外计算方向，
	 * 而非默认的"施法者到目标"方向，这些 Override 参数支持该场景。
	 */
	UFUNCTION(BlueprintPure)
	FDamageEffectParams MakeDamageEffectParamsFromClassDefaults(
		AActor* TargetActor = nullptr,
		FVector InRadialDamageOrigin = FVector::ZeroVector,
		bool bOverrideKnockbackDirection = false,
		FVector KnockbackDirectionOverride = FVector::ZeroVector,
		bool bOverrideDeathImpulse = false,
		FVector DeathImpulseDirectionOverride = FVector::ZeroVector,
		bool bOverridePitch = false,
		float PitchOverride = 0.f) const;

	/**
	 * GetDamageAtLevel —— 获取当前技能等级对应的伤害数值
	 *
	 * 供 UI（SpellMenu 描述文本）或蓝图节点读取当前等级伤害值，
	 * 不触发任何 GAS 逻辑，仅从 ScalableFloat 查表返回数值。
	 */
	UFUNCTION(BlueprintPure)
	float GetDamageAtLevel() const;

protected:

	/**
	 * 用于构建伤害 GE Spec 的 GameplayEffect 类
	 *
	 * 通常配置为项目的通用伤害 GE（如 GE_Damage），
	 * 该 GE 内部使用 ExecutionCalculation（ExecCalc_Damage）计算最终伤害。
	 * 具体伤害数值通过 SetByCaller Tag 在运行时注入，而非写死在 GE 里，
	 * 从而实现"一个 GE 应对所有伤害类型和数值"的数据驱动设计。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/**
	 * 伤害类型 Tag（如 Damage.Fire、Damage.Lightning）
	 *
	 * 此 Tag 作为 SetByCaller 的 Key 写入 GE Spec，
	 * ExecCalc_Damage 据此找到对应的抗性属性（如 FireResistance）进行减伤计算。
	 * 同一技能类只对应一种伤害类型，多元素攻击需使用不同技能类。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	FGameplayTag DamageType;

	/**
	 * 基础伤害值（ScalableFloat —— 可绑定 CurveTable 随技能等级自动查表）
	 *
	 * ScalableFloat 的核心优势：
	 *   - 直接填数字：所有等级使用同一固定值
	 *   - 绑定 CurveTable：技能 1 级造成 50 伤害，5 级造成 150 伤害
	 *     等级对应的曲线值在编辑器数据表中维护，无需修改代码
	 * 运行时调用 Damage.GetValueAtLevel(GetAbilityLevel()) 获取当前值。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	FScalableFloat Damage;

	/** Debuff 触发概率（0~100），如 20.f 表示 20% 概率施加状态 */
	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float DebuffChance = 20.f;

	/** Debuff 持续期间每次跳伤的伤害量 */
	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float DebuffDamage = 5.f;

	/** Debuff 跳伤频率（秒），每隔此时间触发一次 DebuffDamage */
	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float DebuffFrequency = 1.f;

	/** Debuff 总持续时长（秒），超时后状态自动移除 */
	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float DebuffDuration = 5.f;

	/**
	 * 死亡冲量大小（单位：cm/s 近似）
	 * 目标死亡时施加的物理冲量，产生"被击飞"的视觉效果。
	 * 方向默认为施法者 → 目标，可通过 Override 参数自定义。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float DeathImpulseMagnitude = 1000.f;

	/**
	 * 击退力量大小
	 * 目标存活时被命中后施加的击退冲量，方向同死亡冲量逻辑。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float KnockbackForceMagnitude = 1000.f;

	/** 击退触发概率（0~100），0 表示该技能不产生击退效果 */
	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float KnockbackChance = 0.f;

	/**
	 * 是否为范围伤害（爆炸型技能如 FireBlast 设为 true）
	 * 为 true 时，ExecCalc_Damage 根据目标到爆炸中心的距离衰减伤害，
	 * 内圈（InnerRadius）受全额伤害，外圈（OuterRadius）边缘受衰减伤害。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	bool bIsRadialDamage = false;

	/** 范围伤害内圈半径（单位：cm），内圈目标受全额伤害 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	float RadialDamageInnerRadius = 0.f;

	/** 范围伤害外圈半径（单位：cm），外圈目标受衰减后的伤害 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	float RadialDamageOuterRadius = 0.f;

	/**
	 * 从带 Tag 的 Montage 数组中随机选一个并返回
	 *
	 * 用于技能攻击动画随机化：角色持有多个攻击 Montage（如左手、右手、双手），
	 * 每次攻击随机播放其中一个，并返回其对应的 SocketTag（用于定位特效生成点）。
	 * 蓝图中 GetRandomTaggedMontageFromArray 调用后可直接驱动 PlayMontage 节点。
	 */
	UFUNCTION(BlueprintPure)
	FTaggedMontage GetRandomTaggedMontageFromArray(const TArray<FTaggedMontage>& TaggedMontages) const;
};
