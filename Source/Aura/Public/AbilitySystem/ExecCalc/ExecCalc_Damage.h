// Copyright Druid Mechanics

#pragma once

/**
 * ============================================================
 * UExecCalc_Damage —— GAS 伤害执行计算器（ExecutionCalculation）
 * ============================================================
 *
 * 【职责】
 *   本类是整个伤害系统的核心计算单元，负责将一次技能/攻击产生的
 *   原始伤害经过多重修正（抗性、格挡、护甲、暴击、径向衰减）后，
 *   写入目标的元属性 IncomingDamage，从而触发后续的生命值扣除流程。
 *
 * 【触发时机】
 *   当一个 GameplayEffect（GE）的 Executions 数组中引用了本类时，
 *   GAS 在应用该 GE 时（Apply 阶段）会自动调用 Execute_Implementation。
 *   只有 Instant / Has Duration 类型的 GE 才会执行 ExecutionCalculation；
 *   Infinite GE 不支持（应改用 ModifierMagnitudeCalculation）。
 *
 * 【输入】
 *   - Spec 中通过 SetByCaller 设置的各伤害类型原始值（Fire/Lightning/Arcane/Physical）
 *   - Spec 中通过 SetByCaller 设置的减益参数（Chance/Damage/Duration/Frequency）
 *   - Source / Target 在 RelevantAttributesToCapture 中注册的属性快照或实时值
 *   - FAuraGameplayEffectContext 中携带的附加信息（是否径向伤害、爆炸半径等）
 *
 * 【输出】
 *   通过 OutExecutionOutput.AddOutputModifier 将最终伤害以加法方式写入
 *   UAuraAttributeSet::IncomingDamage（元属性），该属性的 PostGameplayEffectExecute
 *   会将其换算为真实的 Health 扣减，并在扣减完成后清零。
 *
 * 【调用链】
 *   技能蓝图/C++ → 应用伤害 GE → GAS 框架 → Execute_Implementation（此处）
 *     → DetermineDebuff（减益判定）
 *     → UAuraAttributeSet::PostGameplayEffectExecute（写入 Health）
 *     → AAuraCharacterBase::OnDeath / HitReact 等表现层回调
 */

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "ExecCalc_Damage.generated.h"

/**
 * UExecCalc_Damage
 *
 * 继承自 UGameplayEffectExecutionCalculation。
 * GAS 框架保证在同一帧、同一 GE 应用时只调用一次 Execute_Implementation，
 * 且该函数是 const 的——所有状态均通过参数传入传出，不依赖成员变量。
 */
UCLASS()
class AURA_API UExecCalc_Damage : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()
public:

	/** 构造函数：向 RelevantAttributesToCapture 注册所有需要读取的属性捕获定义 */
	UExecCalc_Damage();

	/**
	 * DetermineDebuff —— 减益触发判定
	 *
	 * 遍历本次伤害涉及的所有伤害类型，对每种有实际伤害值的类型：
	 *   1. 读取攻击方的减益触发概率（SetByCaller：Debuff_Chance）
	 *   2. 读取防御方对应属性的抗性值（用于折减触发概率）
	 *   3. 随机判定是否触发减益
	 *   4. 若触发，将减益参数（伤害/持续/频率/类型）写入 EffectContext，
	 *      供后续 AuraAbilitySystemComponent 读取并动态创建减益 GE
	 *
	 * 设计说明：减益判定之所以放在 ExecCalc 而不是独立 GE，是因为需要
	 * 同时访问 Source 属性（Debuff_Chance）和 Target 属性（Resistance），
	 * 而 ExecutionCalculation 是 GAS 中唯一能同时读取双方属性的机制。
	 *
	 * @param ExecutionParams   GAS 执行参数，含 Source/Target ASC 及属性快照
	 * @param Spec              当前 GE Spec，含 SetByCaller 伤害值和减益参数
	 * @param EvaluationParameters  聚合器求值参数（含 Source/Target 标签）
	 * @param InTagsToDefs      GameplayTag → 属性捕获定义的映射表，用于按标签查属性
	 */
	void DetermineDebuff(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	                     const FGameplayEffectSpec& Spec,
	                     FAggregatorEvaluateParameters EvaluationParameters,
	                     const TMap<FGameplayTag, FGameplayEffectAttributeCaptureDefinition>& InTagsToDefs) const;

	/**
	 * Execute_Implementation —— 伤害计算主入口（GAS 框架回调）
	 *
	 * 完整伤害计算流程（详见 .cpp 文件各阶段注释）：
	 *   阶段①  建立标签→属性捕获定义映射表
	 *   阶段②  获取 Source/Target Avatar 及等级
	 *   阶段③  调用 DetermineDebuff 进行减益判定
	 *   阶段④  遍历伤害类型，按抗性折算原始伤害，处理径向衰减
	 *   阶段⑤  格挡判定（成功则伤害减半）
	 *   阶段⑥  护甲穿透 + 有效护甲计算
	 *   阶段⑦  暴击判定（成功则伤害翻倍并叠加暴击加成）
	 *   阶段⑧  将最终伤害写入 IncomingDamage 元属性
	 *
	 * @param ExecutionParams      GAS 注入的执行参数（只读）
	 * @param OutExecutionOutput   计算结果输出对象，调用 AddOutputModifier 写入结果
	 */
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
