// Copyright Druid Mechanics

/**
 * ============================================================
 * MMC_MaxHealth.cpp —— 最大生命值的修改器幅度计算（ModifierMagnitudeCalculation）
 * ============================================================
 *
 * 【MMC 机制概述】
 *   ModifierMagnitudeCalculation（MMC）是 GAS 中用于 GameplayEffect Modifier
 *   幅度计算的自定义类，继承自 UGameplayModMagnitudeCalculation。
 *
 *   与 ExecCalc（ExecutionCalculation）的区别：
 *   ┌────────────────┬──────────────────────────┬──────────────────────────┐
 *   │ 特性           │ MMC                      │ ExecCalc                 │
 *   ├────────────────┼──────────────────────────┼──────────────────────────┤
 *   │ 适用 GE 类型   │ Instant / Duration /     │ 仅 Instant / Duration    │
 *   │                │ Infinite（全支持）        │（不支持 Infinite）        │
 *   │ 修改属性数量   │ 只能影响单个属性          │ 可同时读写多个属性        │
 *   │ 调用时机       │ 每次属性被查询时重新计算  │ GE 应用时执行一次         │
 *   │ 典型用途       │ 次要属性（MaxHealth 等）  │ 伤害、治疗等复杂计算      │
 *   └────────────────┴──────────────────────────┴──────────────────────────┘
 *
 *   MaxHealth 使用 Infinite 类型 GE 持续存在，因此必须用 MMC 而非 ExecCalc。
 *
 * 【触发时机】
 *   当 GAS 需要重新计算 MaxHealth 属性时（例如 Vigor 提升后），
 *   会调用 CalculateBaseMagnitude_Implementation 获取新的幅度值，
 *   并通过 GE Modifier 的加法/乘法运算更新最终属性值。
 *
 * 【输入】
 *   - Vigor（体力）：从 Target 的 AttributeSet 实时读取
 *   - PlayerLevel：通过 ICombatInterface 接口从 GE 的 SourceObject 获取
 *
 * 【输出】
 *   返回一个 float，作为 GE Modifier 的幅度，直接加到 MaxHealth 的基础值上。
 *
 * 【调用链】
 *   Vigor 属性变化 → GAS 标记依赖属性重新计算 → CalculateBaseMagnitude_Implementation
 *   → 返回 MaxHealth 幅度 → GE Modifier 更新 MaxHealth → UI/逻辑响应
 */

#include "AbilitySystem/ModMagCalc/MMC_MaxHealth.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "Interaction/CombatInterface.h"

// ============================================================
// 构造函数 —— 注册需要捕获的属性
// ============================================================
//
// VigorDef 描述"从 Target 的 UAuraAttributeSet 实时读取 Vigor 属性"：
//   AttributeToCapture = UAuraAttributeSet::GetVigorAttribute()
//     → 通过 GAS 的属性反射系统定位 Vigor 属性
//   AttributeSource = Target
//     → 读防御方（即属性所有者自身）的 Vigor 值
//   bSnapshot = false
//     → 不快照，每次计算时读当前实时值
//       当 Vigor 提升时，MaxHealth 会立即重新计算反映新值
//
// 注册到 RelevantAttributesToCapture 后，GAS 才会在计算时提供此属性的值；
// 未注册则 GetCapturedAttributeMagnitude 返回 false 且值为 0。
UMMC_MaxHealth::UMMC_MaxHealth()
{
	VigorDef.AttributeToCapture = UAuraAttributeSet::GetVigorAttribute();
	VigorDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
	VigorDef.bSnapshot = false;

	RelevantAttributesToCapture.Add(VigorDef);
}

// ============================================================
// CalculateBaseMagnitude_Implementation —— MaxHealth 计算公式
// ============================================================
//
// 本函数每次 MaxHealth 属性被求值时调用（Infinite GE 存在期间，
// 每当依赖属性 Vigor 或 PlayerLevel 变化时都会触发重算）。
//
// 【MaxHealth 计算公式】
//   MaxHealth = 80 + 2.5 × Vigor + 10 × PlayerLevel
//
// 各项含义：
//   80          —— 基础生命值（所有角色的起点，等级 1、Vigor 0 时的最小值）
//   2.5 × Vigor —— 体力对生命的贡献（每点 Vigor 提供 2.5 点 MaxHealth）
//   10 × Level  —— 等级对生命的贡献（每级提供 10 点 MaxHealth）
//
// 设计说明：
//   使用线性公式而非曲线表，因为 MaxHealth 的增长需要"可预期性"——
//   玩家加点前能看到明确的收益数字。如需非线性增长，可改用 ScalableFloat
//   绑定 CurveTable 以支持按等级查表（MMC_MaxMana 同理）。
float UMMC_MaxHealth::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// 从 GE Spec 收集 Source 和 Target 的 GameplayTag 容器
	// 用于 EvaluationParameters，使属性聚合器能正确应用基于标签的条件 Modifier
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	// 从 Target ASC 中获取实时 Vigor 属性值
	// GetCapturedAttributeMagnitude 内部使用 VigorDef 描述符在已注册的属性快照/实时值中查找
	float Vigor = 0.f;
	GetCapturedAttributeMagnitude(VigorDef, Spec, EvaluationParameters, Vigor);
	Vigor = FMath::Max<float>(Vigor, 0.f); // 防止 Vigor 为负导致 MaxHealth 异常

	// 通过 ICombatInterface 获取角色等级
	// SourceObject 是应用此 GE 的发起对象（通常是角色自身的 AuraCharacter/AuraEnemy），
	// 两者均实现了 UCombatInterface，保证接口调用安全。
	// 若未实现接口（如某些非标准 Actor），等级默认为 1，保持合理基准值。
	int32 PlayerLevel = 1;
	if (Spec.GetContext().GetSourceObject()->Implements<UCombatInterface>())
	{
		PlayerLevel = ICombatInterface::Execute_GetPlayerLevel(Spec.GetContext().GetSourceObject());
	}

	// 公式：MaxHealth = 80 + 2.5 × Vigor + 10 × PlayerLevel
	// 此返回值作为 GE Modifier 的幅度，通过 Additive 操作叠加到 MaxHealth 基础值
	return 80.f + 2.5f * Vigor + 10.f * PlayerLevel;
}
