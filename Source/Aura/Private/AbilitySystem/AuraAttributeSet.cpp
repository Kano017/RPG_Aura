// Copyright Druid Mechanics


#include "AbilitySystem/AuraAttributeSet.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AuraAbilityTypes.h"
#include "GameFramework/Character.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"
#include "AuraGameplayTags.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Interaction/CombatInterface.h"
#include "Interaction/PlayerInterface.h"
#include "Player/AuraPlayerController.h"

UAuraAttributeSet::UAuraAttributeSet()
{
	// ─────────────────────────────────────────────────────────────────────────
	// TagsToAttributes 初始化 — 将每个 GameplayTag 映射到对应属性的静态访问函数指针
	//
	// 这里直接传入 GetXxxAttribute 函数指针（不带括号，取地址写法省略 & 是 C++ 惯例），
	// 运行时通过 Map.Find(Tag) 获得函数指针后调用 (*FuncPtr)() 即得 FGameplayAttribute 对象。
	//
	// 维护规范：新增属性时，同时在此处添加对应条目，
	// 否则 AttributeMenu UI 和 ExecCalc_Damage 的抗性查找将无法感知新属性。
	// ─────────────────────────────────────────────────────────────────────────
	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();

	/* 主属性 */
	TagsToAttributes.Add(GameplayTags.Attributes_Primary_Strength, GetStrengthAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Primary_Intelligence, GetIntelligenceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Primary_Resilience, GetResilienceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Primary_Vigor, GetVigorAttribute);

	/* 次要属性 */
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_Armor, GetArmorAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_ArmorPenetration, GetArmorPenetrationAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_BlockChance, GetBlockChanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_CriticalHitChance, GetCriticalHitChanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_CriticalHitResistance, GetCriticalHitResistanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_CriticalHitDamage, GetCriticalHitDamageAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_HealthRegeneration, GetHealthRegenerationAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_ManaRegeneration, GetManaRegenerationAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_MaxHealth, GetMaxHealthAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Secondary_MaxMana, GetMaxManaAttribute);

	/* 抗性属性 */
	TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Arcane, GetArcaneResistanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Fire, GetFireResistanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Lightning, GetLightningResistanceAttribute);
	TagsToAttributes.Add(GameplayTags.Attributes_Resistance_Physical, GetPhysicalResistanceAttribute);

}

void UAuraAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	// ─────────────────────────────────────────────────────────────────────────
	// 网络复制注册 — 使用 DOREPLIFETIME_CONDITION_NOTIFY 宏注册所有需要复制的属性
	//
	// 参数说明：
	//   COND_None        — 无条件复制给所有客户端（含自身）
	//   REPNOTIFY_Always — 无论值是否真的改变，都触发 OnRep 回调
	//
	// 为何使用 REPNOTIFY_Always 而不是 REPNOTIFY_OnChanged？
	//   GAS 在客户端做属性预测时，预测值可能已经等于服务器值，
	//   如果用 OnChanged 则 OnRep 不触发，导致 GAS 内部的预测状态机无法正确回滚/确认。
	//   Always 保证服务器每次更新都能触发客户端的 GAS 感知逻辑。
	//
	// 元属性（IncomingDamage / IncomingXP）不在此处注册，因为它们只在服务器使用。
	// ─────────────────────────────────────────────────────────────────────────
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 主属性

	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Strength, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Intelligence, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Resilience, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Vigor, COND_None, REPNOTIFY_Always);

	// 次要属性

	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Armor, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, ArmorPenetration, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, BlockChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, CriticalHitChance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, CriticalHitDamage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, CriticalHitResistance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, HealthRegeneration, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, ManaRegeneration, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, MaxMana, COND_None, REPNOTIFY_Always);

	// 抗性属性

	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, FireResistance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, LightningResistance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, ArcaneResistance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, PhysicalResistance, COND_None, REPNOTIFY_Always);

	// 生命属性

	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAuraAttributeSet, Mana, COND_None, REPNOTIFY_Always);

}

void UAuraAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	// ─────────────────────────────────────────────────────────────────────────
	// PreAttributeChange — 属性写入前的 Clamp 拦截
	//
	// GAS 调用时机：任何 GE 修改器即将写入属性之前（包括来自 MMC、ExecCalc、直接 SetXxx）。
	// NewValue 是本次修改后的预期值，修改 NewValue 可以影响最终写入的值。
	//
	// 重要限制：
	//   此函数只影响 CurrentValue 的写入，不影响 BaseValue，
	//   因此如果 MaxHealth 被提升，Health 的 Clamp 上限不会自动提升，
	//   需要在 PostAttributeChange 中补处理（参见 bTopOffHealth 机制）。
	//
	// 只对 Health / Mana 做 Clamp，原因：
	//   - 其他属性（如 Armor、Strength）允许任意值，由设计层控制范围
	//   - Health/Mana 是玩家可见的资源条，必须保证不越界
	// ─────────────────────────────────────────────────────────────────────────
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		// 将 Health 限制在 [0, MaxHealth]，防止负血或超出上限
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	if (Attribute == GetManaAttribute())
	{
		// 将 Mana 限制在 [0, MaxMana]
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxMana());
	}
}

void UAuraAttributeSet::SetEffectProperties(const FGameplayEffectModCallbackData& Data, FEffectProperties& Props) const
{
	// Source = 效果的施加者（技能的使用者/攻击者）
	// Target = 效果的目标（此属性集的拥有者，即被攻击者）

	// 从 GE Spec 获取 EffectContext，它携带了施法者 ASC 和所有自定义扩展数据
	Props.EffectContextHandle = Data.EffectSpec.GetContext();
	// GetOriginalInstigatorAbilitySystemComponent 返回最原始的触发者 ASC，
	// 区别于 GetInstigatorAbilitySystemComponent（可能是中间代理），确保追溯到真正的施法者
	Props.SourceASC = Props.EffectContextHandle.GetOriginalInstigatorAbilitySystemComponent();

	// ── 填充 Source（施法者）信息 ──
	if (IsValid(Props.SourceASC) && Props.SourceASC->AbilityActorInfo.IsValid() && Props.SourceASC->AbilityActorInfo->AvatarActor.IsValid())
	{
		// AvatarActor 是"外观角色"，即在世界中实际存在的角色对象
		Props.SourceAvatarActor = Props.SourceASC->AbilityActorInfo->AvatarActor.Get();
		// AbilityActorInfo 的 PlayerController 仅在玩家控制时有效，AI 控制时为 nullptr
		Props.SourceController = Props.SourceASC->AbilityActorInfo->PlayerController.Get();
		if (Props.SourceController == nullptr && Props.SourceAvatarActor != nullptr)
		{
			// 回退方案：若 AvatarActor 是 Pawn（包括 AI 控制的敌人），
			// 从 Pawn 自身获取 Controller（可能是 AAIController）
			if (const APawn* Pawn = Cast<APawn>(Props.SourceAvatarActor))
			{
				Props.SourceController = Pawn->GetController();
			}
		}
		if (Props.SourceController)
		{
			// 从 Controller 获取 Character 引用（GetPawn() 再转型），
			// 后续调用 LaunchCharacter / Die 等需要 ACharacter 类型
			Props.SourceCharacter = Cast<ACharacter>(Props.SourceController->GetPawn());
		}
	}

	// ── 填充 Target（受击者）信息 ──
	// Data.Target 就是本属性集所属的 ASC，直接从它的 AbilityActorInfo 获取信息
	if (Data.Target.AbilityActorInfo.IsValid() && Data.Target.AbilityActorInfo->AvatarActor.IsValid())
	{
		Props.TargetAvatarActor = Data.Target.AbilityActorInfo->AvatarActor.Get();
		Props.TargetController = Data.Target.AbilityActorInfo->PlayerController.Get();
		Props.TargetCharacter = Cast<ACharacter>(Props.TargetAvatarActor);
		// 通过工具函数获取 Target 的 ASC，用于后续触发 HitReact 技能
		Props.TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Props.TargetAvatarActor);
	}
}

void UAuraAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	// ─────────────────────────────────────────────────────────────────────────
	// PostGameplayEffectExecute — GE 执行完毕后的游戏逻辑分发中枢
	//
	// 调用时机：一次 GE 应用的所有 Modifier 都计算并写入完毕后，由 GAS 内部调用。
	// 此时属性值已是最终值，可以安全地执行有副作用的游戏逻辑。
	//
	// 与 PreAttributeChange 的区别：
	//   Pre  → 在写入前拦截，只做范围限制，无副作用
	//   Post → 在写入后处理，执行扣血/死亡/升级等真实游戏逻辑
	// ─────────────────────────────────────────────────────────────────────────
	Super::PostGameplayEffectExecute(Data);

	// 将 GAS 原始回调数据解析成结构化的 FEffectProperties，方便后续函数使用
	FEffectProperties Props;
	SetEffectProperties(Data, Props);

	// 安全检查：若目标已死亡，忽略所有后续处理（避免对尸体重复触发死亡逻辑）
	if(Props.TargetCharacter->Implements<UCombatInterface>() && ICombatInterface::Execute_IsDead(Props.TargetCharacter)) return;

	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		// Health 的二次 Clamp（PostGameplayEffectExecute 中的 Clamp 会持久写入 CurrentValue，
		// 而 PreAttributeChange 的 Clamp 只影响修改器，不持久）
		SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
	}
	if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		SetMana(FMath::Clamp(GetMana(), 0.f, GetMaxMana()));
	}
	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		// IncomingDamage 有值 → 执行完整的伤害应用逻辑链
		HandleIncomingDamage(Props);
	}
	if (Data.EvaluatedData.Attribute == GetIncomingXPAttribute())
	{
		// IncomingXP 有值 → 执行经验值积累与升级判断逻辑链
		HandleIncomingXP(Props);
	}
}

void UAuraAttributeSet::HandleIncomingDamage(const FEffectProperties& Props)
{
	// ─────────────────────────────────────────────────────────────────────────
	// 读取并立即清零 IncomingDamage（"消费"元属性）
	// 先保存到局部变量，再清零，防止后续逻辑再次触发 PostGameplayEffectExecute 时重复处理
	// ─────────────────────────────────────────────────────────────────────────
	const float LocalIncomingDamage = GetIncomingDamage();
	SetIncomingDamage(0.f);
	if (LocalIncomingDamage > 0.f)
	{
		// ── 扣血 ──
		const float NewHealth = GetHealth() - LocalIncomingDamage;
		SetHealth(FMath::Clamp(NewHealth, 0.f, GetMaxHealth()));

		// ── 死亡判断 ──
		const bool bFatal = NewHealth <= 0.f;
		if (bFatal)
		{
			// 触发死亡：通过 CombatInterface 调用 Die()，传入死亡冲量向量
			// 死亡冲量存储在自定义的 FAuraGameplayEffectContext 中，由 ExecCalc_Damage 写入
			ICombatInterface* CombatInterface = Cast<ICombatInterface>(Props.TargetAvatarActor);
			if (CombatInterface)
			{
				FVector Impulse = UAuraAbilitySystemLibrary::GetDeathImpulse(Props.EffectContextHandle);
				CombatInterface->Die(UAuraAbilitySystemLibrary::GetDeathImpulse(Props.EffectContextHandle));
			}
			// 向攻击者发送经验值奖励事件（死亡才触发 XP，存活不触发）
			SendXPEvent(Props);

		}
		else
		{
			// ── 存活时的反馈 ──

			// 触发受击动画：通过 TryActivateAbilitiesByTag 激活 HitReact GA
			// 被眩晕（IsBeingShocked）时不触发 HitReact，因为眩晕本身已控制角色状态
			if (Props.TargetCharacter->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsBeingShocked(Props.TargetCharacter))
			{
				FGameplayTagContainer TagContainer;
				TagContainer.AddTag(FAuraGameplayTags::Get().Effects_HitReact);
				Props.TargetASC->TryActivateAbilitiesByTag(TagContainer);
			}

			// 击退：若 EffectContext 中携带了非零的击退力，对目标施加物理冲量
			// 击退向量由技能的 ExecCalc 或 AbilityTask 写入 FAuraGameplayEffectContext
			const FVector& KnockbackForce = UAuraAbilitySystemLibrary::GetKnockbackForce(Props.EffectContextHandle);
			if (!KnockbackForce.IsNearlyZero(1.f))
			{
				// LaunchCharacter 的后两个 true 表示：覆盖 XY 速度、覆盖 Z 速度
				Props.TargetCharacter->LaunchCharacter(KnockbackForce, true, true);
			}
		}

		// ── 飘字（无论存活还是死亡都显示）──
		const bool bBlock = UAuraAbilitySystemLibrary::IsBlockedHit(Props.EffectContextHandle);
		const bool bCriticalHit = UAuraAbilitySystemLibrary::IsCriticalHit(Props.EffectContextHandle);
		ShowFloatingText(Props, LocalIncomingDamage, bBlock, bCriticalHit);

		// ── Debuff 处理（无论存活还是死亡都可能触发，但实际上死亡角色已被忽略）──
		// IsSuccessfulDebuff 读取 FAuraGameplayEffectContext 中 ExecCalc 写入的 Debuff 成功标志
		if (UAuraAbilitySystemLibrary::IsSuccessfulDebuff(Props.EffectContextHandle))
		{
			Debuff(Props);
		}
	}
}

void UAuraAttributeSet::Debuff(const FEffectProperties& Props)
{
	// ─────────────────────────────────────────────────────────────────────────
	// 动态构建 Debuff GameplayEffect
	//
	// Debuff 参数全部来自 FAuraGameplayEffectContext（由 ExecCalc_Damage 写入）：
	//   DamageType      → 决定施加哪种 Debuff Tag（通过 DamageTypesToDebuffs 映射）
	//   DebuffDamage    → Debuff 每次触发时对 IncomingDamage 的加法修改量
	//   DebuffDuration  → Debuff 的总持续时间
	//   DebuffFrequency → Debuff 的触发周期（Period，即多久触发一次伤害）
	// ─────────────────────────────────────────────────────────────────────────
	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();
	// 以施法者的 ASC 为上下文构建新 GE，确保 Debuff 的"来源"归属正确
	FGameplayEffectContextHandle EffectContext = Props.SourceASC->MakeEffectContext();
	EffectContext.AddSourceObject(Props.SourceAvatarActor);

	// 从 EffectContext 中读取 ExecCalc_Damage 写入的 Debuff 参数
	const FGameplayTag DamageType = UAuraAbilitySystemLibrary::GetDamageType(Props.EffectContextHandle);
	const float DebuffDamage = UAuraAbilitySystemLibrary::GetDebuffDamage(Props.EffectContextHandle);
	const float DebuffDuration = UAuraAbilitySystemLibrary::GetDebuffDuration(Props.EffectContextHandle);
	const float DebuffFrequency = UAuraAbilitySystemLibrary::GetDebuffFrequency(Props.EffectContextHandle);

	// 动态创建 UGameplayEffect 对象（存放在 TransientPackage，生命周期由 GC 管理）
	// 命名格式 "DynamicDebuff_Damage.Type.Fire" 等，便于调试时识别
	FString DebuffName = FString::Printf(TEXT("DynamicDebuff_%s"), *DamageType.ToString());
	UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage(), FName(DebuffName));

	// 配置 GE 基本参数：有限持续时间 + 周期触发
	Effect->DurationPolicy = EGameplayEffectDurationType::HasDuration;
	Effect->Period = DebuffFrequency;          // 每隔 Frequency 秒触发一次 Modifier
	Effect->DurationMagnitude = FScalableFloat(DebuffDuration);

	// ── 为目标添加状态 Tag ──
	// 通过 TargetTagsGameplayEffectComponent 在 GE 激活期间向目标添加标签
	const FGameplayTag DebuffTag = GameplayTags.DamageTypesToDebuffs[DamageType];
	UTargetTagsGameplayEffectComponent& TargetTagsComponent = Effect->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
	FInheritedTagContainer InheritedTags;
	InheritedTags.Added.AddTag(DebuffTag);  // 如 Debuff.Burn / Debuff.Stun / Debuff.Shock
	if (DebuffTag.MatchesTagExact(GameplayTags.Debuff_Stun))
	{
		// 眩晕特殊处理：添加四个 Player_Block 标签完全锁定玩家输入
		// CursorTrace   → 禁止鼠标悬停检测（防止选中目标）
		// InputHeld     → 禁止长按输入
		// InputPressed  → 禁止按下输入
		// InputReleased → 禁止抬起输入
		InheritedTags.Added.AddTag(GameplayTags.Player_Block_CursorTrace);
		InheritedTags.Added.AddTag(GameplayTags.Player_Block_InputHeld);
		InheritedTags.Added.AddTag(GameplayTags.Player_Block_InputPressed);
		InheritedTags.Added.AddTag(GameplayTags.Player_Block_InputReleased);
	}
	TargetTagsComponent.SetAndApplyTargetTagChanges(InheritedTags);

	// ── 防止同源 Debuff 叠加超过 1 层 ──
	// AggregateBySource：相同 Source 施加的同类型 Debuff 只保留 1 个（刷新持续时间）
	Effect->StackingType = EGameplayEffectStackingType::AggregateBySource;
	Effect->StackLimitCount = 1;

	// ── 添加 Modifier：周期性地向 IncomingDamage 加值 ──
	// 每次 Period 到期，此 Modifier 执行一次，触发 PostGameplayEffectExecute 的 IncomingDamage 分支
	const int32 Index = Effect->Modifiers.Num();
	Effect->Modifiers.Add(FGameplayModifierInfo());
	FGameplayModifierInfo& ModifierInfo = Effect->Modifiers[Index];

	ModifierInfo.ModifierMagnitude = FScalableFloat(DebuffDamage);         // 每次触发的伤害量
	ModifierInfo.ModifierOp = EGameplayModOp::Additive;                    // 加法操作
	ModifierInfo.Attribute = UAuraAttributeSet::GetIncomingDamageAttribute(); // 目标属性：IncomingDamage

	// ── 构建 GE Spec 并将伤害类型写入自定义 Context ──
	FGameplayEffectSpec MutableSpec(Effect, EffectContext, 1.f);
	// 静态转型为自定义 Context 类，写入 DamageType 供受击方的 ExecCalc/Debuff 进一步使用
	FAuraGameplayEffectContext* AuraContext = static_cast<FAuraGameplayEffectContext*>(MutableSpec.GetContext().Get());
	TSharedPtr<FGameplayTag> DebuffDamageType = MakeShareable(new FGameplayTag(DamageType));
	AuraContext->SetDamageType(DebuffDamageType);

	// 以目标的 ASC 为接收者应用 Debuff GE
	Props.TargetASC->ApplyGameplayEffectSpecToSelf(MutableSpec);
}

void UAuraAttributeSet::HandleIncomingXP(const FEffectProperties& Props)
{
	// ─────────────────────────────────────────────────────────────────────────
	// 读取并立即清零 IncomingXP（"消费"元属性，防止重复处理）
	// ─────────────────────────────────────────────────────────────────────────
	const float LocalIncomingXP = GetIncomingXP();
	SetIncomingXP(0.f);

	// Source 角色是拥有者，因为 GA_ListenForEvents 应用了 GE_EventBasedEffect，向 IncomingXP 添加数值
	// 只有玩家角色才实现 PlayerInterface（敌人死亡时不触发自身升级）
	if (Props.SourceCharacter->Implements<UPlayerInterface>() && Props.SourceCharacter->Implements<UCombatInterface>())
	{
		// 查询当前等级和当前 XP 总量
		const int32 CurrentLevel = ICombatInterface::Execute_GetPlayerLevel(Props.SourceCharacter);
		const int32 CurrentXP = IPlayerInterface::Execute_GetXP(Props.SourceCharacter);

		// 根据"当前 XP + 本次获得 XP"的总量查询应达到的目标等级
		// FindLevelForXP 内部查询 LevelUpInfo 数据表（每级所需累计 XP 阈值）
		const int32 NewLevel = IPlayerInterface::Execute_FindLevelForXP(Props.SourceCharacter, CurrentXP + LocalIncomingXP);
		const int32 NumLevelUps = NewLevel - CurrentLevel;  // 本次可升多少级（可能跨级）
		if (NumLevelUps > 0)
		{
			// 更新玩家等级（PlayerState 或角色组件中存储的等级字段）
			IPlayerInterface::Execute_AddToPlayerLevel(Props.SourceCharacter, NumLevelUps);

			// 累计本次升级获得的属性点和技能点奖励
			// 注意：从 CurrentLevel 开始逐级累加，每级奖励可能不同（数据表驱动）
			int32 AttributePointsReward = 0;
			int32 SpellPointsReward = 0;

			for (int32 i = 0; i < NumLevelUps; ++i)
			{
				// GetSpellPointsReward / GetAttributePointsReward 查询 LevelUpInfo 数据表
				SpellPointsReward += IPlayerInterface::Execute_GetSpellPointsReward(Props.SourceCharacter, CurrentLevel + i);
				AttributePointsReward += IPlayerInterface::Execute_GetAttributePointsReward(Props.SourceCharacter, CurrentLevel + i);
			}

			// 发放属性点和技能点
			IPlayerInterface::Execute_AddToAttributePoints(Props.SourceCharacter, AttributePointsReward);
			IPlayerInterface::Execute_AddToSpellPoints(Props.SourceCharacter, SpellPointsReward);

			// 标记需要补满 HP/MP（在 PostAttributeChange 中等 MaxHealth/MaxMana 更新后执行）
			// 不在此处直接 SetHealth(GetMaxHealth())，因为此时 MaxHealth 的新值可能尚未写入
			bTopOffHealth = true;
			bTopOffMana = true;

			// 触发升级表现：播放升级特效、UI 动画等（通过 Blueprint 实现）
			IPlayerInterface::Execute_LevelUp(Props.SourceCharacter);
		}

		// 无论是否升级，都累加 XP 总量到角色数据中（用于存档和 UI 显示）
		IPlayerInterface::Execute_AddToXP(Props.SourceCharacter, LocalIncomingXP);
	}
}

void UAuraAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	// ─────────────────────────────────────────────────────────────────────────
	// 升级后补满 HP/MP
	//
	// 升级流程的时序问题：
	//   1. HandleIncomingXP 检测到升级 → bTopOffHealth = true
	//   2. LevelUp 触发 → Infinite GE 重算 → MaxHealth 提升
	//   3. MaxHealth 的 PostAttributeChange 在此触发 → 此时才能正确读到新的 MaxHealth
	//   4. SetHealth(GetMaxHealth()) 补满血量
	//
	// 若在步骤 1 直接 SetHealth(GetMaxHealth())，MaxHealth 还是旧值，补满不完整。
	// ─────────────────────────────────────────────────────────────────────────
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	if (Attribute == GetMaxHealthAttribute() && bTopOffHealth)
	{
		// MaxHealth 已更新完毕，现在可以安全地将 Health 补满
		SetHealth(GetMaxHealth());
		bTopOffHealth = false;
	}
	if (Attribute == GetMaxManaAttribute() && bTopOffMana)
	{
		SetMana(GetMaxMana());
		bTopOffMana = false;
	}
}

void UAuraAttributeSet::SendXPEvent(const FEffectProperties& Props)
{
	// ─────────────────────────────────────────────────────────────────────────
	// 死亡 XP 广播：将死亡敌人的 XP 奖励发送给击杀者（Source）
	//
	// 事件流程：
	//   此函数 → SendGameplayEventToActor (Tag: Attributes.Meta.IncomingXP, 数量: XPReward)
	//   → Source 上的 GA_ListenForEvents 监听到此 Tag
	//   → GA 应用 GE_EventBasedEffect（将事件 Magnitude 写入 IncomingXP 属性）
	//   → PostGameplayEffectExecute 检测到 IncomingXP → HandleIncomingXP
	//
	// XP 奖励量通过 GetXPRewardForClassAndLevel 查询 CharacterClassInfo 数据表，
	// 根据目标的 ECharacterClass 和等级返回配置的 XP 值（数据驱动）。
	// ─────────────────────────────────────────────────────────────────────────
	if (Props.TargetCharacter->Implements<UCombatInterface>())
	{
		// 从死亡的目标角色身上读取等级和职业分类
		const int32 TargetLevel = ICombatInterface::Execute_GetPlayerLevel(Props.TargetCharacter);
		const ECharacterClass TargetClass = ICombatInterface::Execute_GetCharacterClass(Props.TargetCharacter);
		// 查询数据表获得该等级该职业对应的 XP 奖励值
		const int32 XPReward = UAuraAbilitySystemLibrary::GetXPRewardForClassAndLevel(Props.TargetCharacter, TargetClass, TargetLevel);

		const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();
		// 构建 GameplayEvent Payload，EventMagnitude 携带 XP 数量
		FGameplayEventData Payload;
		Payload.EventTag = GameplayTags.Attributes_Meta_IncomingXP;
		Payload.EventMagnitude = XPReward;
		// 向 Source（击杀者）发送事件，触发其上的 GA_ListenForEvents
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Props.SourceCharacter, GameplayTags.Attributes_Meta_IncomingXP, Payload);
	}
}

void UAuraAttributeSet::ShowFloatingText(const FEffectProperties& Props, float Damage, bool bBlockedHit, bool bCriticalHit) const
{
	// ─────────────────────────────────────────────────────────────────────────
	// 飘字显示策略：
	//   1. 跳过自伤情况（Source == Target，如某些 AOE 影响自身时）
	//   2. 优先在 Source（攻击者）的 PlayerController 上调用 ShowDamageNumber
	//      → 玩家主动攻击时，数字显示在攻击者的本地屏幕上（最常见场景）
	//   3. 若 Source 不是玩家（AI 攻击），回退到 Target（受击者）的 PlayerController
	//      → 玩家被 AI 攻击时，在玩家自己的屏幕上显示受到的伤害数字
	//
	// ShowDamageNumber 是 AAuraPlayerController 的 Client RPC，确保飘字只在本地客户端显示。
	// ─────────────────────────────────────────────────────────────────────────
	if (!IsValid(Props.SourceCharacter) || !IsValid(Props.TargetCharacter)) return;
	if (Props.SourceCharacter != Props.TargetCharacter)
	{
		// 场景一：玩家攻击敌人 → Source 是玩家，转型成功，直接显示并返回
		if(AAuraPlayerController* PC = Cast<AAuraPlayerController>(Props.SourceCharacter->Controller))
		{
			PC->ShowDamageNumber(Damage, Props.TargetCharacter, bBlockedHit, bCriticalHit);
			return;
		}
		// 场景二：敌人攻击玩家 → Source 是 AI（转型失败），尝试在 Target（玩家）的 PC 上显示
		if(AAuraPlayerController* PC = Cast<AAuraPlayerController>(Props.TargetCharacter->Controller))
		{
			PC->ShowDamageNumber(Damage, Props.TargetCharacter, bBlockedHit, bCriticalHit);
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// OnRep 回调函数实现
//
// 每个函数体内只调用 GAMEPLAYATTRIBUTE_REPNOTIFY 宏，该宏展开后：
//   1. 通知 AbilitySystemComponent 属性已从网络更新
//   2. 触发 GAS 内部的预测状态更新（若客户端有本地预测值，与服务器值对比）
//   3. 广播 FOnGameplayAttributeChange 委托，驱动 UI 等监听者更新
//
// OldXxx 参数：复制前的旧值，GAS 内部用于预测回滚计算，业务层通常不直接使用。
// ═══════════════════════════════════════════════════════════════════════════

void UAuraAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Health, OldHealth);
}

void UAuraAttributeSet::OnRep_Mana(const FGameplayAttributeData& OldMana) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Mana, OldMana);
}

void UAuraAttributeSet::OnRep_Strength(const FGameplayAttributeData& OldStrength) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Strength, OldStrength);
}

void UAuraAttributeSet::OnRep_Intelligence(const FGameplayAttributeData& OldIntelligence) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Intelligence, OldIntelligence);
}

void UAuraAttributeSet::OnRep_Resilience(const FGameplayAttributeData& OldResilience) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Resilience, OldResilience);
}

void UAuraAttributeSet::OnRep_Vigor(const FGameplayAttributeData& OldVigor) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Vigor, OldVigor);
}

void UAuraAttributeSet::OnRep_Armor(const FGameplayAttributeData& OldArmor) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, Armor, OldArmor);
}

void UAuraAttributeSet::OnRep_ArmorPenetration(const FGameplayAttributeData& OldArmorPenetration) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, ArmorPenetration, OldArmorPenetration);
}

void UAuraAttributeSet::OnRep_BlockChance(const FGameplayAttributeData& OldBlockChance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, BlockChance, OldBlockChance);
}

void UAuraAttributeSet::OnRep_CriticalHitChance(const FGameplayAttributeData& OldCriticalHitChance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, CriticalHitChance, OldCriticalHitChance);
}

void UAuraAttributeSet::OnRep_CriticalHitDamage(const FGameplayAttributeData& OldCriticalHitDamage) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, CriticalHitDamage, OldCriticalHitDamage);
}

void UAuraAttributeSet::OnRep_CriticalHitResistance(const FGameplayAttributeData& OldCriticalHitResistance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, CriticalHitResistance, OldCriticalHitResistance);
}

void UAuraAttributeSet::OnRep_HealthRegeneration(const FGameplayAttributeData& OldHealthRegeneration) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, HealthRegeneration, OldHealthRegeneration);
}

void UAuraAttributeSet::OnRep_ManaRegeneration(const FGameplayAttributeData& OldManaRegeneration) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, ManaRegeneration, OldManaRegeneration);
}

void UAuraAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, MaxHealth, OldMaxHealth);
}

void UAuraAttributeSet::OnRep_MaxMana(const FGameplayAttributeData& OldMaxMana) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, MaxMana, OldMaxMana);
}

void UAuraAttributeSet::OnRep_FireResistance(const FGameplayAttributeData& OldFireResistance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, FireResistance, OldFireResistance);
}

void UAuraAttributeSet::OnRep_LightningResistance(const FGameplayAttributeData& OldLightningResistance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, LightningResistance, OldLightningResistance);
}

void UAuraAttributeSet::OnRep_ArcaneResistance(const FGameplayAttributeData& OldArcaneResistance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, ArcaneResistance, OldArcaneResistance);
}

void UAuraAttributeSet::OnRep_PhysicalResistance(const FGameplayAttributeData& OldPhysicalResistance) const
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAuraAttributeSet, PhysicalResistance, OldPhysicalResistance);
}
