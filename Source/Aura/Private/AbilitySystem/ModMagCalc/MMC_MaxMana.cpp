// Copyright Druid Mechanics

/**
 * ============================================================
 * MMC_MaxMana.cpp —— 最大法力值的修改器幅度计算（ModifierMagnitudeCalculation）
 * ============================================================
 *
 * 【MMC 机制概述】
 *   本类与 MMC_MaxHealth 结构完全平行，区别仅在于：
 *   - 驱动属性：Intelligence（智力）代替 Vigor（体力）
 *   - 基础值和系数：适配法力值的平衡数值
 *
 *   关于 MMC 机制的完整说明（Infinite GE 支持、调用时机、与 ExecCalc 的对比），
 *   请参阅 MMC_MaxHealth.cpp 文件头注释，此处不再重复。
 *
 * 【触发时机】
 *   当 GAS 需要重新计算 MaxMana 属性时（例如 Intelligence 提升后），
 *   会调用 CalculateBaseMagnitude_Implementation 获取新的幅度值。
 *
 * 【输入】
 *   - Intelligence（智力）：从 Target 的 AttributeSet 实时读取
 *   - PlayerLevel：通过 ICombatInterface 接口从 GE 的 SourceObject 获取
 *
 * 【输出】
 *   返回一个 float，作为 GE Modifier 的幅度，直接加到 MaxMana 的基础值上。
 *
 * 【调用链】
 *   Intelligence 属性变化 → GAS 标记依赖属性重新计算 → CalculateBaseMagnitude_Implementation
 *   → 返回 MaxMana 幅度 → GE Modifier 更新 MaxMana → UI/逻辑响应
 */

#include "AbilitySystem/ModMagCalc/MMC_MaxMana.h"

#include "AbilitySystem/AuraAttributeSet.h"
#include "Interaction/CombatInterface.h"

// ============================================================
// 构造函数 —— 注册需要捕获的属性
// ============================================================
//
// IntDef 描述"从 Target 的 UAuraAttributeSet 实时读取 Intelligence 属性"：
//   AttributeToCapture = UAuraAttributeSet::GetIntelligenceAttribute()
//     → 通过 GAS 的属性反射系统定位 Intelligence 属性
//   AttributeSource = Target
//     → 读属性所有者自身（即施法者/敌人本身）的 Intelligence 值
//   bSnapshot = false
//     → 不快照，实时值：每当 Intelligence 提升，MaxMana 立即重算
//
// 与 MMC_MaxHealth 的 VigorDef 结构完全一致，仅属性名不同。
UMMC_MaxMana::UMMC_MaxMana()
{
	IntDef.AttributeToCapture = UAuraAttributeSet::GetIntelligenceAttribute();
	IntDef.AttributeSource = EGameplayEffectAttributeCaptureSource::Target;
	IntDef.bSnapshot = false;

	RelevantAttributesToCapture.Add(IntDef);
}

// ============================================================
// CalculateBaseMagnitude_Implementation —— MaxMana 计算公式
// ============================================================
//
// 本函数每次 MaxMana 属性被求值时调用（Infinite GE 存在期间，
// 每当依赖属性 Intelligence 或 PlayerLevel 变化时都会触发重算）。
//
// 【MaxMana 计算公式】
//   MaxMana = 50 + 2.5 × Intelligence + 15 × PlayerLevel
//
// 各项含义：
//   50                  —— 基础法力值（低于 MaxHealth 基础值 80，体现法力资源的稀缺性）
//   2.5 × Intelligence  —— 智力对法力的贡献（每点 Intelligence 提供 2.5 点 MaxMana）
//   15 × Level          —— 等级对法力的贡献（每级提供 15 点 MaxMana，高于生命的 10 点/级）
//
// 数值设计说明：
//   与 MaxHealth（80 + 2.5×Vigor + 10×Level）对比：
//   - 起点更低（50 < 80）：初期法力相对紧张，强调资源管理
//   - 等级系数更高（15 > 10）：中后期法力池随等级增长更快，适配技能规模扩大
//   - 属性系数相同（2.5）：加点效率对齐，保证属性加点的横向可比性
float UMMC_MaxMana::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// 从 GE Spec 收集 Source 和 Target 的 GameplayTag 容器
	// 用于 EvaluationParameters，使属性聚合器能正确应用基于标签的条件 Modifier
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	// 从 Target ASC 中获取实时 Intelligence 属性值
	// GetCapturedAttributeMagnitude 内部使用 IntDef 描述符在已注册的属性中查找
	float Int = 0.f;
	GetCapturedAttributeMagnitude(IntDef, Spec, EvaluationParameters, Int);
	Int = FMath::Max<float>(Int, 0.f); // 防止 Intelligence 为负导致 MaxMana 异常

	// 通过 ICombatInterface 获取角色等级
	// SourceObject 是应用此 GE 的发起对象（通常是角色自身），
	// AuraCharacter 和 AuraEnemy 均实现了 UCombatInterface，保证接口调用安全。
	// 若未实现接口（如某些非标准 Actor），等级默认为 1，保持合理基准值。
	int32 PlayerLevel = 1;
	if (Spec.GetContext().GetSourceObject()->Implements<UCombatInterface>())
	{
		PlayerLevel = ICombatInterface::Execute_GetPlayerLevel(Spec.GetContext().GetSourceObject());
	}

	// 公式：MaxMana = 50 + 2.5 × Intelligence + 15 × PlayerLevel
	// 此返回值作为 GE Modifier 的幅度，通过 Additive 操作叠加到 MaxMana 基础值
	return 50.f + 2.5f * Int + 15.f * PlayerLevel;
}
