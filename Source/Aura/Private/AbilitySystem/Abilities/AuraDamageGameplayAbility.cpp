// Copyright Druid Mechanics


#include "AbilitySystem/Abilities/AuraDamageGameplayAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"

/**
 * CauseDamage —— 对单个目标应用一次即时伤害
 *
 * 适用场景：技能直接命中目标（无投射物飞行），如近战打击、即时光束点击。
 *
 * 执行步骤：
 *   1. MakeOutgoingGameplayEffectSpec：以 DamageEffectClass 和当前技能等级
 *      创建一个 GE Spec（含施法者信息、技能标签等上下文）
 *   2. Damage.GetValueAtLevel：从 ScalableFloat 查表获取当前等级伤害值
 *   3. AssignTagSetByCallerMagnitude：将伤害值注入 Spec 的 SetByCaller 字典，
 *      Key 为 DamageType Tag（如 Damage.Fire），ExecCalc 据此找到对应抗性计算
 *   4. ApplyGameplayEffectSpecToTarget：服务器将 Spec 应用到目标 ASC，
 *      触发 ExecCalc_Damage 执行最终伤害、Debuff、击退等完整计算流程
 *
 * 【注意】此函数仅在服务器调用有效（ASC 修改需要服务器权威）。
 */
void UAuraDamageGameplayAbility::CauseDamage(AActor* TargetActor)
{
	FGameplayEffectSpecHandle DamageSpecHandle = MakeOutgoingGameplayEffectSpec(DamageEffectClass, 1.f);
	const float ScaledDamage = Damage.GetValueAtLevel(GetAbilityLevel());
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(DamageSpecHandle, DamageType, ScaledDamage);
	GetAbilitySystemComponentFromActorInfo()->ApplyGameplayEffectSpecToTarget(*DamageSpecHandle.Data.Get(), UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor));
}

/**
 * MakeDamageEffectParamsFromClassDefaults —— 将所有伤害 UPROPERTY 打包为 FDamageEffectParams
 *
 * 【核心逻辑分段说明】
 *
 * ① 基础字段填充：
 *    - WorldContextObject：提供世界上下文，用于 GetWorld() 调用
 *    - SourceASC / TargetASC：伤害从 Source 流向 Target，ExecCalc 需要双方 ASC
 *    - BaseDamage：ScalableFloat 按当前技能等级查表得到的基础伤害数值
 *    - DamageType / Debuff 系列：直接从类成员复制
 *
 * ② 方向向量计算（仅 TargetActor 有效时）：
 *    默认方向 = 施法者位置 → 目标位置（归一化向量）
 *    此方向同时用于 KnockbackForce（击退）和 DeathImpulse（死亡冲量）。
 *    bOverridePitch 允许调整俯仰角（如投射物需要向上抛出时 PitchOverride = -45°）。
 *
 * ③ 方向覆盖（Override）：
 *    范围爆炸技能（FireBlast）需要从爆炸中心向外辐射击退，
 *    此时传入 bOverrideKnockbackDirection = true，并提供自定义方向向量。
 *    Override 方向同样支持 PitchOverride 修正。
 *
 * ④ 范围伤害（RadialDamage）：
 *    bIsRadialDamage 为 true 时填充 Origin 和内外圈半径，
 *    ExecCalc_Damage 根据目标到 Origin 的距离在内外圈之间线性衰减伤害。
 */
FDamageEffectParams UAuraDamageGameplayAbility::MakeDamageEffectParamsFromClassDefaults(AActor* TargetActor,
	FVector InRadialDamageOrigin, bool bOverrideKnockbackDirection, FVector KnockbackDirectionOverride,
	bool bOverrideDeathImpulse, FVector DeathImpulseDirectionOverride, bool bOverridePitch, float PitchOverride) const
{
	FDamageEffectParams Params;
	Params.WorldContextObject = GetAvatarActorFromActorInfo();
	Params.DamageGameplayEffectClass = DamageEffectClass;
	Params.SourceAbilitySystemComponent = GetAbilitySystemComponentFromActorInfo();
	Params.TargetAbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	Params.BaseDamage = Damage.GetValueAtLevel(GetAbilityLevel());
	Params.AbilityLevel = GetAbilityLevel();
	Params.DamageType = DamageType;
	Params.DebuffChance = DebuffChance;
	Params.DebuffDamage = DebuffDamage;
	Params.DebuffDuration = DebuffDuration;
	Params.DebuffFrequency = DebuffFrequency;
	Params.DeathImpulseMagnitude = DeathImpulseMagnitude;
	Params.KnockbackForceMagnitude = KnockbackForceMagnitude;
	Params.KnockbackChance = KnockbackChance;

	if (IsValid(TargetActor))
	{
		// 计算施法者指向目标的旋转，Pitch 可选覆盖（如水平击退时强制 Pitch = 0）
		FRotator Rotation = (TargetActor->GetActorLocation() - GetAvatarActorFromActorInfo()->GetActorLocation()).Rotation();
		if (bOverridePitch)
		{
			Rotation.Pitch = PitchOverride;
		}
		const FVector ToTarget = Rotation.Vector();
		// 未被覆盖时，使用"施法者→目标"方向作为击退和死亡冲量的默认方向
		if (!bOverrideKnockbackDirection)
		{
			Params.KnockbackForce = ToTarget * KnockbackForceMagnitude;
		}
		if (!bOverrideDeathImpulse)
		{
			Params.DeathImpulse = ToTarget * DeathImpulseMagnitude;
		}
	}


	if (bOverrideKnockbackDirection)
	{
		// 使用自定义击退方向（如爆炸中心向外辐射），先归一化再乘以力量大小
		KnockbackDirectionOverride.Normalize();
		Params.KnockbackForce = KnockbackDirectionOverride * KnockbackForceMagnitude;
		if (bOverridePitch)
		{
			FRotator KnockbackRotation = KnockbackDirectionOverride.Rotation();
			KnockbackRotation.Pitch = PitchOverride;
			Params.KnockbackForce = KnockbackRotation.Vector() * KnockbackForceMagnitude;
		}
	}

	if (bOverrideDeathImpulse)
	{
		// 使用自定义死亡冲量方向，同样支持 Pitch 修正
		DeathImpulseDirectionOverride.Normalize();
		Params.DeathImpulse = DeathImpulseDirectionOverride * DeathImpulseMagnitude;
		if (bOverridePitch)
		{
			FRotator DeathImpulseRotation = DeathImpulseDirectionOverride.Rotation();
			DeathImpulseRotation.Pitch = PitchOverride;
			Params.DeathImpulse = DeathImpulseRotation.Vector() * DeathImpulseMagnitude;
		}
	}

	if (bIsRadialDamage)
	{
		// 范围伤害：填入爆炸原点和内外圈半径，ExecCalc 据此衰减边缘目标的伤害
		Params.bIsRadialDamage = bIsRadialDamage;
		Params.RadialDamageOrigin = InRadialDamageOrigin;
		Params.RadialDamageInnerRadius = RadialDamageInnerRadius;
		Params.RadialDamageOuterRadius = RadialDamageOuterRadius;
	}
	return Params;
}

/**
 * GetDamageAtLevel —— 查询当前技能等级对应的伤害数值
 *
 * 直接委托 ScalableFloat.GetValueAtLevel，从绑定的 CurveTable 取值。
 * 供 SpellMenu 描述文本（GetDescription）等 UI 调用，展示"当前伤害：120"。
 */
float UAuraDamageGameplayAbility::GetDamageAtLevel() const
{
	return Damage.GetValueAtLevel(GetAbilityLevel());
}

/**
 * GetRandomTaggedMontageFromArray —— 从 Montage 数组中随机选取一个
 *
 * FTaggedMontage 同时持有 Montage 资产和对应的 SocketTag（骨骼插槽标签），
 * 调用方获取返回值后：
 *   - 用 Montage 驱动 PlayMontageAndWait 播放攻击动画
 *   - 用 SocketTag 调用 GetCombatSocketLocation 确定特效/投射物生成点
 * 随机选取使同一技能每次攻击动画不同，提升战斗表现多样性。
 */
FTaggedMontage UAuraDamageGameplayAbility::GetRandomTaggedMontageFromArray(const TArray<FTaggedMontage>& TaggedMontages) const
{
	if (TaggedMontages.Num() > 0)
	{
		const int32 Selection = FMath::RandRange(0, TaggedMontages.Num() - 1);
		return TaggedMontages[Selection];
	}

	return FTaggedMontage();
}
