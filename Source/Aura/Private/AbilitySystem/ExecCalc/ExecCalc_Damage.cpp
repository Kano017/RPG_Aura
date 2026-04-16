// Copyright Druid Mechanics

/**
 * ============================================================
 * ExecCalc_Damage.cpp —— 伤害执行计算器实现
 * ============================================================
 *
 * 本文件实现 UExecCalc_Damage 的完整伤害管线。
 * 关于架构与触发时机，请参阅对应头文件 ExecCalc_Damage.h 的文件注释。
 *
 * 【核心依赖】
 *   - AuraAttributeSet    ：提供所有属性的静态 Getter（用于构造捕获定义）
 *   - AuraGameplayTags    ：全局单例，持有伤害类型→抗性/减益的映射关系
 *   - CharacterClassInfo  ：数据资产，存储按等级缩放的曲线表系数
 *   - AuraAbilitySystemLibrary ：工具函数库，读写 EffectContext 中的自定义字段
 *   - FAuraGameplayEffectContext：扩展的 GE 上下文，携带暴击/格挡/减益/径向伤害信息
 */

#include "AbilitySystem/ExecCalc/ExecCalc_Damage.h"

#include "AbilitySystemComponent.h"
#include "AuraAbilityTypes.h"
#include "AuraGameplayTags.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "Camera/CameraShakeSourceActor.h"
#include "Interaction/CombatInterface.h"
#include "Kismet/GameplayStatics.h"

// ============================================================
// AuraDamageStatics —— 属性捕获定义的集中管理结构体
// ============================================================
//
// 【设计目的】
//   GAS 的 ExecutionCalculation 在读取 Source/Target 属性之前，
//   必须先通过 FGameplayEffectAttributeCaptureDefinition 描述"要捕获哪个属性、
//   从哪方捕获、是否快照"，然后在构造函数中注册到 RelevantAttributesToCapture。
//
//   将所有捕获定义集中在此结构体，避免在多处重复声明，也方便统一维护。
//
// 【DECLARE_ATTRIBUTE_CAPTUREDEF 宏】
//   展开后声明两个成员：
//     FProperty*                            <Name>Property  （属性的反射指针）
//     FGameplayEffectAttributeCaptureDefinition <Name>Def   （完整捕获定义）
//
// 【DEFINE_ATTRIBUTE_CAPTUREDEF 宏参数解析】
//   DEFINE_ATTRIBUTE_CAPTUREDEF(AttributeSetClass, AttributeName, CaptureSource, bSnapshot)
//
//   参数①  UAuraAttributeSet  —— 属性所在的 AttributeSet 类，用于定位属性
//   参数②  Armor / BlockChance … —— 属性名，宏内部会调用 GetArmorAttribute() 等静态方法
//   参数③  Source / Target   —— 从攻击方（Source）还是防御方（Target）读取
//             · Source：ArmorPenetration、CriticalHitChance、CriticalHitDamage
//               → 穿甲/暴击是攻击方的能力，应读攻击方的属性
//             · Target：Armor、BlockChance、CriticalHitResistance、各元素抗性
//               → 防御类属性读防御方
//   参数④  false（不快照）    —— 使用 GE 应用时（命中时）的实时属性值，而非技能施放时的快照
//             · 护甲等防御属性用实时值，让防御装备的即时提升立即生效
//             · 注：攻击方的 ArmorPenetration/暴击等此处也用 false（实时值），
//               这意味着施放到命中之间若属性变化会影响结果；
//               若改为 true（快照），则以施放时刻的属性为准。
//               本项目选择统一用实时值，简化设计。
struct AuraDamageStatics
{
	// ---- 防御方属性（Target）----
	DECLARE_ATTRIBUTE_CAPTUREDEF(Armor);              // 护甲：减少入射伤害的百分比
	DECLARE_ATTRIBUTE_CAPTUREDEF(BlockChance);        // 格挡几率：成功格挡使伤害减半

	// ---- 攻击方属性（Source）----
	DECLARE_ATTRIBUTE_CAPTUREDEF(ArmorPenetration);   // 护甲穿透：无视目标一定比例的护甲
	DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalHitChance);  // 暴击率：触发暴击的基础概率
	DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalHitDamage);  // 暴击伤害加成：暴击时额外叠加的固定值

	// ---- 防御方属性（Target）----
	DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalHitResistance); // 暴击抗性：降低攻击方暴击率

	// ---- 元素抗性（Target）—— 各元素伤害类型的减伤百分比 ----
	DECLARE_ATTRIBUTE_CAPTUREDEF(FireResistance);
	DECLARE_ATTRIBUTE_CAPTUREDEF(LightningResistance);
	DECLARE_ATTRIBUTE_CAPTUREDEF(ArcaneResistance);
	DECLARE_ATTRIBUTE_CAPTUREDEF(PhysicalResistance);

	AuraDamageStatics()
	{
		// Target 防御属性（命中时读实时值）
		DEFINE_ATTRIBUTE_CAPTUREDEF(UAuraAttributeSet, Armor, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UAuraAttributeSet, BlockChance, Target, false);

		// Source 攻击属性（命中时读实时值）
		DEFINE_ATTRIBUTE_CAPTUREDEF(UAuraAttributeSet, ArmorPenetration, Source, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UAuraAttributeSet, CriticalHitChance, Source, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UAuraAttributeSet, CriticalHitResistance, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UAuraAttributeSet, CriticalHitDamage, Source, false);

		// Target 元素抗性（命中时读实时值）
		DEFINE_ATTRIBUTE_CAPTUREDEF(UAuraAttributeSet, FireResistance, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UAuraAttributeSet, LightningResistance, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UAuraAttributeSet, ArcaneResistance, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UAuraAttributeSet, PhysicalResistance, Target, false);
	}
};

/**
 * DamageStatics() —— 捕获定义的全局单例访问函数
 *
 * 使用函数内静态变量（Meyer's Singleton），保证：
 *   1. 延迟初始化（首次调用时才构造，避免全局初始化顺序问题）
 *   2. 线程安全（C++11 保证局部静态变量的初始化是线程安全的）
 *   3. 零拷贝（返回 const 引用，不产生额外开销）
 */
static const AuraDamageStatics& DamageStatics()
{
	static AuraDamageStatics DStatics;
	return DStatics;
}

// ============================================================
// 构造函数 —— 向 GAS 注册所有需要捕获的属性
// ============================================================
//
// RelevantAttributesToCapture 是父类 UGameplayEffectExecutionCalculation 的数组成员。
// GAS 框架在 GE 应用之前会遍历此数组，提前从 Source/Target 的 ASC 中
// 采集对应属性的聚合值（已叠加所有 GameplayModifier），存储为快照或实时引用。
// 若此处未注册，Execute_Implementation 中调用 AttemptCalculateCapturedAttributeMagnitude
// 将返回 false，属性值保持为 0。
UExecCalc_Damage::UExecCalc_Damage()
{
	// 防御方属性
	RelevantAttributesToCapture.Add(DamageStatics().ArmorDef);
	RelevantAttributesToCapture.Add(DamageStatics().BlockChanceDef);

	// 攻击方属性
	RelevantAttributesToCapture.Add(DamageStatics().ArmorPenetrationDef);
	RelevantAttributesToCapture.Add(DamageStatics().CriticalHitChanceDef);
	RelevantAttributesToCapture.Add(DamageStatics().CriticalHitResistanceDef);
	RelevantAttributesToCapture.Add(DamageStatics().CriticalHitDamageDef);

	// 防御方元素抗性
	RelevantAttributesToCapture.Add(DamageStatics().FireResistanceDef);
	RelevantAttributesToCapture.Add(DamageStatics().LightningResistanceDef);
	RelevantAttributesToCapture.Add(DamageStatics().ArcaneResistanceDef);
	RelevantAttributesToCapture.Add(DamageStatics().PhysicalResistanceDef);
}

// ============================================================
// DetermineDebuff —— 减益触发判定
// ============================================================
//
// 【调用时机】在 Execute_Implementation 的最开头调用，先于伤害计算。
// 【设计原因】减益是独立于伤害数值之外的附加效果，但其触发概率受
//   目标抗性影响（同种元素抗性既减伤又降低减益触发率），因此需要在
//   这里读取双方属性做概率判定。
//
// 【减益触发公式】
//   公式：有效减益概率 = 攻击方减益概率 × (100 - 目标抗性) / 100
//   随机数 [1, 100] < 有效减益概率 则触发
//
// 【触发后的流程】
//   将减益参数写入 EffectContextHandle（FAuraGameplayEffectContext 的扩展字段），
//   由 UAuraAbilitySystemComponent 在 ApplyGameplayEffectToSelf 之后读取，
//   动态创建并应用对应的减益 GE（持续伤害/控制效果等）。
//   这种"在 ExecCalc 中判定、在 ASC 中应用"的两段式设计，
//   避免了在 ExecutionCalculation 中直接调用 ApplyGameplayEffect 的复杂性。
void UExecCalc_Damage::DetermineDebuff(const FGameplayEffectCustomExecutionParameters& ExecutionParams, const FGameplayEffectSpec& Spec, FAggregatorEvaluateParameters EvaluationParameters,
					 const TMap<FGameplayTag, FGameplayEffectAttributeCaptureDefinition>& InTagsToDefs) const
{
	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();

	// 遍历所有"伤害类型→减益类型"映射对
	// DamageTypesToDebuffs 定义在 FAuraGameplayTags 中，例如：
	//   FireDamage → Debuff_Burn，LightningDamage → Debuff_Stun 等
	for (TTuple<FGameplayTag, FGameplayTag> Pair : GameplayTags.DamageTypesToDebuffs)
	{
		const FGameplayTag& DamageType = Pair.Key;
		const FGameplayTag& DebuffType = Pair.Value;

		// 从 GE Spec 读取本次攻击在该伤害类型上设置的原始伤害值
		// 默认返回 -1.0f 表示"未设置此伤害类型"（本次攻击不包含该元素）
		const float TypeDamage = Spec.GetSetByCallerMagnitude(DamageType, false, -1.f);

		// 只对"有实际伤害值"的类型判定减益，使用 -0.5 作为容差而非 0，
		// 避免浮点精度问题将合法的 0 伤害误判为"未设置"
		if (TypeDamage > -.5f) // 浮点精度误差的0.5容差值
		{
			// 读取攻击方设置的减益触发基础概率（通过 SetByCaller 写入 GE Spec）
			const float SourceDebuffChance = Spec.GetSetByCallerMagnitude(GameplayTags.Debuff_Chance, false, -1.f);

			// 读取目标对应元素的抗性值（同种元素抗性同时抵抗伤害和减益）
			float TargetDebuffResistance = 0.f;
			const FGameplayTag& ResistanceTag = GameplayTags.DamageTypesToResistances[DamageType];
			ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(InTagsToDefs[ResistanceTag], EvaluationParameters, TargetDebuffResistance);
			TargetDebuffResistance = FMath::Max<float>(TargetDebuffResistance, 0.f); // 抗性不能为负

			// 公式：有效减益概率 = 攻击方减益概率 × (100 - 目标抗性) / 100
			// 目标抗性越高，减益越难触发（与伤害减免使用相同的抗性值，设计上保持一致）
			const float EffectiveDebuffChance = SourceDebuffChance * ( 100 - TargetDebuffResistance ) / 100.f;

			// 随机判定：[1, 100] 内的随机整数 < 有效概率则触发
			const bool bDebuff = FMath::RandRange(1, 100) < EffectiveDebuffChance;
			if (bDebuff)
			{
				// 获取 GE 上下文（FAuraGameplayEffectContext）的句柄
				// EffectContext 随 GE 传播，是跨系统传递自定义数据的标准通道
				FGameplayEffectContextHandle ContextHandle = Spec.GetContext();

				// 将"减益成功触发"标记写入 EffectContext，
				// AAuraAbilitySystemComponent 会检查此标记来决定是否应用减益 GE
				UAuraAbilitySystemLibrary::SetIsSuccessfulDebuff(ContextHandle, true);

				// 记录触发减益的伤害类型（决定应用哪种减益效果：灼烧/感电/秘法干扰等）
				UAuraAbilitySystemLibrary::SetDamageType(ContextHandle, DamageType);

				// 从 GE Spec 读取减益的具体参数（由技能蓝图通过 SetByCaller 传入）
				const float DebuffDamage = Spec.GetSetByCallerMagnitude(GameplayTags.Debuff_Damage, false, -1.f);       // 减益每次触发的伤害量
				const float DebuffDuration = Spec.GetSetByCallerMagnitude(GameplayTags.Debuff_Duration, false, -1.f);   // 减益持续时间（秒）
				const float DebuffFrequency = Spec.GetSetByCallerMagnitude(GameplayTags.Debuff_Frequency, false, -1.f); // 减益触发频率（次/秒）

				// 将所有减益参数写入 EffectContext，供后续动态创建减益 GE 时使用
				UAuraAbilitySystemLibrary::SetDebuffDamage(ContextHandle, DebuffDamage);
				UAuraAbilitySystemLibrary::SetDebuffDuration(ContextHandle, DebuffDuration);
				UAuraAbilitySystemLibrary::SetDebuffFrequency(ContextHandle, DebuffFrequency);
			}
		}
	}
}

// ============================================================
// Execute_Implementation —— 伤害计算主函数（GAS 框架回调）
// ============================================================
//
// 本函数是整个伤害管线的核心，按顺序执行以下 8 个计算阶段：
//   ① 建立标签→属性捕获定义的运行时映射表
//   ② 获取 Source/Target Avatar Actor 及各自等级
//   ③ 减益触发判定（DetermineDebuff）
//   ④ 遍历伤害类型：原始伤害 × (1 - 抗性/100)，处理径向衰减
//   ⑤ 格挡判定：成功则伤害 ÷ 2
//   ⑥ 护甲穿透 → 有效护甲 → 护甲减伤
//   ⑦ 暴击判定：成功则伤害 × 2 + 暴击加成
//   ⑧ 将最终伤害写入 IncomingDamage 元属性
void UExecCalc_Damage::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
                                              FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	// ============================================================
	// 阶段① 建立 GameplayTag → 属性捕获定义 的映射表
	// ============================================================
	//
	// 将 GameplayTag（如 Attributes_Secondary_Armor）与对应的
	// FGameplayEffectAttributeCaptureDefinition（ArmorDef）关联，
	// 后续可通过标签动态查找捕获定义，而无需硬编码属性名。
	// 这是支持"数据驱动：标签驱动伤害类型→抗性属性"的关键设计。
	TMap<FGameplayTag, FGameplayEffectAttributeCaptureDefinition> TagsToCaptureDefs;
	const FAuraGameplayTags& Tags = FAuraGameplayTags::Get();

	TagsToCaptureDefs.Add(Tags.Attributes_Secondary_Armor,                 DamageStatics().ArmorDef);
	TagsToCaptureDefs.Add(Tags.Attributes_Secondary_BlockChance,           DamageStatics().BlockChanceDef);
	TagsToCaptureDefs.Add(Tags.Attributes_Secondary_ArmorPenetration,      DamageStatics().ArmorPenetrationDef);
	TagsToCaptureDefs.Add(Tags.Attributes_Secondary_CriticalHitChance,     DamageStatics().CriticalHitChanceDef);
	TagsToCaptureDefs.Add(Tags.Attributes_Secondary_CriticalHitResistance, DamageStatics().CriticalHitResistanceDef);
	TagsToCaptureDefs.Add(Tags.Attributes_Secondary_CriticalHitDamage,     DamageStatics().CriticalHitDamageDef);

	TagsToCaptureDefs.Add(Tags.Attributes_Resistance_Arcane,    DamageStatics().ArcaneResistanceDef);
	TagsToCaptureDefs.Add(Tags.Attributes_Resistance_Fire,      DamageStatics().FireResistanceDef);
	TagsToCaptureDefs.Add(Tags.Attributes_Resistance_Lightning, DamageStatics().LightningResistanceDef);
	TagsToCaptureDefs.Add(Tags.Attributes_Resistance_Physical,  DamageStatics().PhysicalResistanceDef);

	// ============================================================
	// 阶段② 获取 Source/Target 的 ASC、Avatar Actor 和角色等级
	// ============================================================
	//
	// Source：施放技能的角色（攻击方）
	// Target：被技能命中的角色（防御方）
	// 等级通过 UCombatInterface::Execute_GetPlayerLevel 接口统一获取，
	// 玩家和敌人均实现此接口，使计算代码无需区分角色类型。
	const UAbilitySystemComponent* SourceASC = ExecutionParams.GetSourceAbilitySystemComponent();
	const UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();

	AActor* SourceAvatar = SourceASC ? SourceASC->GetAvatarActor() : nullptr;
	AActor* TargetAvatar = TargetASC ? TargetASC->GetAvatarActor() : nullptr;

	// 等级用于从曲线表（CurveTable）查找按等级缩放的系数
	// 默认等级 1 确保无 CombatInterface 的 Actor（如陷阱）也有合理基准值
	int32 SourcePlayerLevel = 1;
	if (SourceAvatar->Implements<UCombatInterface>())
	{
		SourcePlayerLevel = ICombatInterface::Execute_GetPlayerLevel(SourceAvatar);
	}
	int32 TargetPlayerLevel = 1;
	if (TargetAvatar->Implements<UCombatInterface>())
	{
		TargetPlayerLevel = ICombatInterface::Execute_GetPlayerLevel(TargetAvatar);
	}

	// GE Spec 包含 SetByCaller 伤害值、减益参数、EffectContext 等全部输入数据
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	FGameplayEffectContextHandle EffectContextHandle = Spec.GetContext();

	// 构建聚合器求值参数，用于属性计算时过滤条件型 Modifier（基于标签的 Modifier 条件）
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();
	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	// ============================================================
	// 阶段③ 减益触发判定（详见 DetermineDebuff 注释）
	// ============================================================
	// 在伤害数值计算之前先判定减益，是因为减益判定结果写入 EffectContext，
	// 不影响伤害值本身，逻辑上独立，放在最前面保持代码清晰。
	DetermineDebuff(ExecutionParams, Spec, EvaluationParameters, TagsToCaptureDefs);

	// ============================================================
	// 阶段④ 遍历所有伤害类型，应用抗性后累加总伤害
	// ============================================================
	//
	// 公式：实际伤害（单类型）= 原始伤害 × (100 - 抗性%) / 100
	// 抗性值 Clamp 在 [0, 100]：0 = 无减免，100 = 完全免疫，不允许负值（否则变成增伤）
	//
	// 多元素伤害（如同时有火和雷）分别按各自抗性计算后相加，
	// 使玩家通过提升某一元素抗性可以有针对性地抵御特定技能。
	float Damage = 0.f;
	for (const TTuple<FGameplayTag, FGameplayTag>& Pair  : FAuraGameplayTags::Get().DamageTypesToResistances)
	{
		const FGameplayTag DamageTypeTag = Pair.Key;    // 例如：Damage.Type.Fire
		const FGameplayTag ResistanceTag = Pair.Value;  // 例如：Attributes.Resistance.Fire

		// 确保映射表中有该抗性标签对应的捕获定义（Debug 阶段检查，防止漏注册）
		checkf(TagsToCaptureDefs.Contains(ResistanceTag), TEXT("TagsToCaptureDefs doesn't contain Tag: [%s] in ExecCalc_Damage"), *ResistanceTag.ToString());
		const FGameplayEffectAttributeCaptureDefinition CaptureDef = TagsToCaptureDefs[ResistanceTag];

		// 读取 GE Spec 中 SetByCaller 设置的该伤害类型原始值
		// 若本次攻击不包含此元素（未 SetByCaller），返回 0 并跳过，避免不必要计算
		float DamageTypeValue = Spec.GetSetByCallerMagnitude(Pair.Key, false);
		if (DamageTypeValue <= 0.f)
		{
			continue;
		}

		// 读取目标对该元素的抗性值（实时值，非快照）
		float Resistance = 0.f;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(CaptureDef, EvaluationParameters, Resistance);
		Resistance = FMath::Clamp(Resistance, 0.f, 100.f); // 钳制到 [0, 100]，避免负抗性变增伤

		// 公式：实际伤害 = 原始伤害 × (100 - 抗性) / 100
		// 抗性 100 → 实际伤害为 0（完全免疫）
		// 抗性 0   → 实际伤害 = 原始伤害（无减免）
		DamageTypeValue *= ( 100.f - Resistance ) / 100.f;

		// ---- 径向伤害（Radial Damage）衰减处理 ----
		// 径向伤害使用 UE 的 ApplyRadialDamageWithFalloff 计算距离衰减，
		// 该函数走 TakeDamage 路径，因此需要通过 OnDamageDelegate 桥接回 GAS 管线。
		//
		// 完整流程（注释中的 * 标记为在 AuraCharacterBase 中实现的部分）：
		//   1. AuraCharacterBase 中重写 TakeDamage，在其中广播 OnDamageDelegate
		//   2. 此处绑定 Lambda 到 OnDamageDelegate，接收实际衰减后的伤害值
		//   3. 调用 ApplyRadialDamageWithFalloff，UE 内部对目标调用 TakeDamage
		//   4. TakeDamage 广播，Lambda 被触发，用衰减后的值覆盖 DamageTypeValue
		//   5. 最终 DamageTypeValue 已包含距离衰减，加入总伤害
		if (UAuraAbilitySystemLibrary::IsRadialDamage(EffectContextHandle))
		{
			// 1. 在 AuraCharacterBase 中重写 TakeDamage。*
			// 2. 创建委托 OnDamageDelegate，在 TakeDamage 中广播受到的伤害。*
			// 3. 在此处将 lambda 绑定到受害者的 OnDamageDelegate。*
			// 4. 调用 UGameplayStatics::ApplyRadialDamageWithFalloff 造成伤害（这将导致对受害者调用 TakeDamage，
			//		进而广播 OnDamageDelegate）。
			// 5. 在 Lambda 中，将 DamageTypeValue 设置为从广播中收到的伤害值。*

			if (ICombatInterface* CombatInterface = Cast<ICombatInterface>(TargetAvatar))
			{
				// Lambda 捕获 DamageTypeValue 的引用，在 TakeDamage 广播时写入衰减后的值
				CombatInterface->GetOnDamageSignature().AddLambda([&](float DamageAmount)
				{
					DamageTypeValue = DamageAmount;
				});
			}
			// 调用 UE 内置径向伤害函数，参数含：
			//   BaseDamage = DamageTypeValue（未衰减的中心伤害）
			//   MinimumDamage = 0（边缘最低伤害为 0，即可完全无伤）
			//   Origin = 爆炸中心点（从 EffectContext 读取）
			//   InnerRadius = 完全伤害半径内无衰减
			//   OuterRadius = 超出此半径伤害为 0
			//   DamageFalloff = 1.0（线性衰减）
			UGameplayStatics::ApplyRadialDamageWithFalloff(
				TargetAvatar,
				DamageTypeValue,
				0.f,
				UAuraAbilitySystemLibrary::GetRadialDamageOrigin(EffectContextHandle),
				UAuraAbilitySystemLibrary::GetRadialDamageInnerRadius(EffectContextHandle),
				UAuraAbilitySystemLibrary::GetRadialDamageOuterRadius(EffectContextHandle),
				1.f,
				UDamageType::StaticClass(),
				TArray<AActor*>(),
				SourceAvatar,
				nullptr);
		}

		// 将该元素实际伤害累加到总伤害池
		Damage += DamageTypeValue;
	}

	// ============================================================
	// 阶段⑤ 格挡判定
	// ============================================================
	//
	// 公式：成功格挡概率 = TargetBlockChance（百分比，直接与 [1,100] 随机数比较）
	// 格挡是防御方的主动防御机制：成功格挡使本次伤害减半，
	// 同时通过 EffectContext 通知表现层（如格挡特效、UI 反馈）。

	float TargetBlockChance = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().BlockChanceDef, EvaluationParameters, TargetBlockChance);
	TargetBlockChance = FMath::Max<float>(TargetBlockChance, 0.f); // 格挡率不能为负

	// 随机判定：[1, 100] < 格挡率 则格挡成功
	const bool bBlocked = FMath::RandRange(1, 100) < TargetBlockChance;

	// 将格挡结果写入 EffectContext，供 AttributeSet::PostGameplayEffectExecute 和
	// 表现层（格挡音效/特效）读取
	UAuraAbilitySystemLibrary::SetIsBlockedHit(EffectContextHandle, bBlocked);

	// 公式：格挡后伤害 = 格挡成功 ? 伤害 ÷ 2 : 伤害（不变）
	Damage = bBlocked ? Damage / 2.f : Damage;

	// ============================================================
	// 阶段⑥ 护甲穿透 → 有效护甲 → 护甲减伤
	// ============================================================
	//
	// 【三步计算逻辑】
	//   步骤 A：读取目标护甲值和攻击方穿甲值
	//   步骤 B：用曲线表按等级查出穿甲系数，计算"有效护甲"（穿甲降低护甲效果）
	//   步骤 C：用曲线表按等级查出护甲系数，计算护甲减伤百分比

	// 步骤 A：读取防御/攻击属性
	float TargetArmor = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().ArmorDef, EvaluationParameters, TargetArmor);
	TargetArmor = FMath::Max<float>(TargetArmor, 0.f); // 护甲不能为负

	float SourceArmorPenetration = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().ArmorPenetrationDef, EvaluationParameters, SourceArmorPenetration);
	SourceArmorPenetration = FMath::Max<float>(SourceArmorPenetration, 0.f); // 穿甲不能为负

	// 步骤 B：从曲线表查 ArmorPenetration 系数（随攻击方等级提升，穿甲效率递增）
	// CharacterClassInfo.DamageCalculationCoefficients 是一张 CurveTable，
	// 每行对应一个系数名称（ArmorPenetration/EffectiveArmor/CriticalHitResistance），
	// Eval(Level) 返回该等级对应的系数值（通常为 0.x 范围的小数）
	const UCharacterClassInfo* CharacterClassInfo = UAuraAbilitySystemLibrary::GetCharacterClassInfo(SourceAvatar);
	const FRealCurve* ArmorPenetrationCurve = CharacterClassInfo->DamageCalculationCoefficients->FindCurve(FName("ArmorPenetration"), FString());
	const float ArmorPenetrationCoefficient = ArmorPenetrationCurve->Eval(SourcePlayerLevel);

	// 公式：有效护甲 = 目标护甲 × (100 - 穿甲值 × 穿甲系数) / 100
	// 穿甲会无视目标一定比例的护甲：穿甲越高 × 系数越大 → 有效护甲越低
	// 穿甲系数随攻击方等级增长，高等级角色穿甲更高效
	const float EffectiveArmor = TargetArmor * ( 100 - SourceArmorPenetration * ArmorPenetrationCoefficient ) / 100.f;

	// 步骤 C：从曲线表查 EffectiveArmor 系数（随防御方等级提升，护甲减伤效率递增）
	const FRealCurve* EffectiveArmorCurve = CharacterClassInfo->DamageCalculationCoefficients->FindCurve(FName("EffectiveArmor"), FString());
	const float EffectiveArmorCoefficient = EffectiveArmorCurve->Eval(TargetPlayerLevel);

	// 公式：护甲后伤害 = 护甲前伤害 × (100 - 有效护甲 × 护甲系数) / 100
	// 有效护甲越高 × 系数越大 → 减伤越多
	// 护甲系数随防御方等级增长，高等级角色单位护甲提供更高减伤
	Damage *= ( 100 - EffectiveArmor * EffectiveArmorCoefficient ) / 100.f;

	// ============================================================
	// 阶段⑦ 暴击判定
	// ============================================================
	//
	// 【三步计算逻辑】
	//   步骤 A：读取攻击方暴击率、防御方暴击抗性、攻击方暴击伤害加成
	//   步骤 B：用曲线表查暴击抗性系数，计算有效暴击率（抗性降低暴击概率）
	//   步骤 C：随机判定，暴击时伤害翻倍并叠加固定加成

	// 步骤 A：读取暴击相关属性
	float SourceCriticalHitChance = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().CriticalHitChanceDef, EvaluationParameters, SourceCriticalHitChance);
	SourceCriticalHitChance = FMath::Max<float>(SourceCriticalHitChance, 0.f);

	float TargetCriticalHitResistance = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().CriticalHitResistanceDef, EvaluationParameters, TargetCriticalHitResistance);
	TargetCriticalHitResistance = FMath::Max<float>(TargetCriticalHitResistance, 0.f);

	float SourceCriticalHitDamage = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().CriticalHitDamageDef, EvaluationParameters, SourceCriticalHitDamage);
	SourceCriticalHitDamage = FMath::Max<float>(SourceCriticalHitDamage, 0.f);

	// 步骤 B：从曲线表查暴击抗性系数（随防御方等级提升，单位抗性降低的暴击率更多）
	const FRealCurve* CriticalHitResistanceCurve = CharacterClassInfo->DamageCalculationCoefficients->FindCurve(FName("CriticalHitResistance"), FString());
	const float CriticalHitResistanceCoefficient = CriticalHitResistanceCurve->Eval(TargetPlayerLevel);

	// 公式：有效暴击率 = 攻击方暴击率 - 目标暴击抗性 × 暴击抗性系数
	// 暴击抗性会直接扣减攻击方的暴击率，系数随等级增大使高等级防御方对暴击更耐受
	const float EffectiveCriticalHitChance = SourceCriticalHitChance - TargetCriticalHitResistance * CriticalHitResistanceCoefficient;

	// 步骤 C：随机判定是否触发暴击
	const bool bCriticalHit = FMath::RandRange(1, 100) < EffectiveCriticalHitChance;

	// 将暴击结果写入 EffectContext，供表现层（暴击数字颜色/特效）读取
	UAuraAbilitySystemLibrary::SetIsCriticalHit(EffectContextHandle, bCriticalHit);

	// 公式：暴击后伤害 = 暴击成功 ? 伤害 × 2 + 暴击伤害加成 : 伤害（不变）
	// 设计上：暴击 = 双倍基础伤害 + 固定加成（而非仅乘倍率），
	// 使暴击伤害加成属性有独立的提升空间，不受基础伤害高低影响
	Damage = bCriticalHit ? 2.f * Damage + SourceCriticalHitDamage : Damage;

	// ============================================================
	// 阶段⑧ 将最终伤害写入 IncomingDamage 元属性
	// ============================================================
	//
	// 元属性（Meta Attribute）IncomingDamage 不代表真实游戏状态，
	// 仅作为一次性数据通道：本帧写入 → PostGameplayEffectExecute 中读取
	// 并用于扣减 Health → 清零，避免伤害值"留存"到下一帧。
	//
	// EGameplayModOp::Additive：以加法方式修改属性（支持多个 ExecCalc 叠加，但通常只有一个）
	const FGameplayModifierEvaluatedData EvaluatedData(UAuraAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, Damage);
	OutExecutionOutput.AddOutputModifier(EvaluatedData);
}
