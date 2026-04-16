/**
 * AuraAbilityTypes.h
 *
 * 本文件定义了两个核心数据结构：
 *
 * 1. FDamageEffectParams —— 技能伤害参数包
 *    用于在技能逻辑与伤害执行器之间传递所有伤害相关参数。
 *    技能蓝图或 C++ 技能类填好这个结构后，调用库函数
 *    UAuraAbilitySystemLibrary::ApplyDamageEffect() 统一完成 GE 应用，
 *    无需在每个技能里重复写 GE 创建和参数设置代码。
 *
 * 2. FAuraGameplayEffectContext —— 扩展的 GE 上下文
 *    GAS 默认的 FGameplayEffectContext 只携带施法者、命中结果等基本信息，
 *    不包含任何游戏特定数据（暴击、减益、冲击力等）。
 *    通过继承并扩展，这些信息可以在整个伤害流程（ExecCalc → PostGameplayEffectExecute
 *    → GameplayCue）中随 GE 一起传播，避免额外的参数传递或全局状态。
 *
 *    使用方式：
 *      FAuraGameplayEffectContext* AuraContext = static_cast<FAuraGameplayEffectContext*>(
 *          EffectContextHandle.Get());
 */

#pragma once

#include "GameplayEffectTypes.h"
#include "AuraAbilityTypes.generated.h"

class UGameplayEffect;

/**
 * FDamageEffectParams
 *
 * 技能伤害参数包——将一次伤害所需的全部数据聚合到一个结构中。
 *
 * 设计意图：
 *   GAS 的 GameplayEffect 需要通过 SetByCaller 或 Modifier 等方式
 *   传入运行时参数，但参数种类繁多（伤害值、减益概率、物理冲击等），
 *   分散设置容易出错。此结构将所有参数集中管理，由技能类填充后
 *   交给 UAuraAbilitySystemLibrary::ApplyDamageEffect() 统一处理。
 *
 * 数据流：
 *   UAuraDamageGameplayAbility（技能）
 *     → 填充 FDamageEffectParams
 *     → UAuraAbilitySystemLibrary::ApplyDamageEffect()
 *     → UExecCalc_Damage（读取各字段执行伤害计算）
 */
USTRUCT(BlueprintType)
struct FDamageEffectParams
{
	GENERATED_BODY()

	FDamageEffectParams(){}

	/**
	 * WorldContextObject
	 *
	 * 用于从任意蓝图/C++ 对象获取 UWorld 指针（如 GetWorld()）。
	 * ApplyDamageEffect 内部需要 World 来创建 GE 上下文和应用效果，
	 * 因此这里传入调用者自身（通常是 this 或技能的 OwnerActor）。
	 */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UObject> WorldContextObject = nullptr;

	/**
	 * DamageGameplayEffectClass
	 *
	 * 实际执行伤害的 GameplayEffect 蓝图类（如 GE_FireDamage）。
	 * 该 GE 内部配置了 ExecCalc_Damage 作为执行计算器；
	 * 此处只传类引用，由库函数动态创建 GE Spec 并应用。
	 * 各技能在蓝图默认值中配置各自对应的 GE 类。
	 */
	UPROPERTY(BlueprintReadWrite)
	TSubclassOf<UGameplayEffect> DamageGameplayEffectClass = nullptr;

	/**
	 * SourceAbilitySystemComponent
	 *
	 * 伤害来源方的 ASC（通常为施法者角色的 ASC）。
	 * ExecCalc_Damage 用它来捕获施法者的属性（如攻击力、暴击率）。
	 * 多人游戏中，ASC 可能位于 PlayerState 上，需通过 ICombatInterface 获取。
	 */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UAbilitySystemComponent> SourceAbilitySystemComponent;

	/**
	 * TargetAbilitySystemComponent
	 *
	 * 伤害受击方的 ASC（通常为被攻击角色的 ASC）。
	 * ExecCalc_Damage 用它来捕获目标的属性（如护甲、抗性），
	 * 并最终对目标应用伤害 GE。
	 */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<UAbilitySystemComponent> TargetAbilitySystemComponent;

	/**
	 * BaseDamage
	 *
	 * 技能的基础伤害值，通常由技能蓝图中的 ScalableFloat 随技能等级缩放后填入。
	 * 例如：FireBolt 1 级基础伤害 10，5 级缩放到 50。
	 * ExecCalc_Damage 以此为起点，再乘以暴击/抗性等系数得到最终伤害。
	 */
	UPROPERTY(BlueprintReadWrite)
	float BaseDamage = 0.f;

	/**
	 * AbilityLevel
	 *
	 * 技能当前等级，用于从 SetByCaller 或 ScalableFloat 曲线中查找对应数值。
	 * 例如不同等级的 DebuffChance 可以通过技能等级查表获得。
	 */
	UPROPERTY(BlueprintReadWrite)
	float AbilityLevel = 1.f;

	/**
	 * DamageType
	 *
	 * 伤害类型标签（如 Damage.Fire / Damage.Lightning / Damage.Arcane / Damage.Physical）。
	 * ExecCalc_Damage 通过此 Tag 在 DamageTypesToResistances 映射表中查找
	 * 对应的抗性属性 Tag，再通过 TagsToAttributes 捕获目标的抗性值，
	 * 最终从原始伤害中减去抗性百分比。
	 */
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag DamageType = FGameplayTag();

	/**
	 * DebuffChance
	 *
	 * 本次攻击触发减益的概率（0~100，百分比）。
	 * ExecCalc_Damage 对比随机数决定是否触发减益，
	 * 若触发则将 bIsSuccessfulDebuff 写入 GE 上下文，
	 * 后续 PostGameplayEffectExecute 读取并动态创建减益 GE。
	 */
	UPROPERTY(BlueprintReadWrite)
	float DebuffChance = 0.f;

	/**
	 * DebuffDamage
	 *
	 * 减益效果每次跳伤造成的伤害值（如燃烧每秒 5 点火焰伤害）。
	 * 与 DebuffDuration / DebuffFrequency 一起打包写入 GE 上下文，
	 * 供动态创建的减益 GE 通过 SetByCaller 读取。
	 */
	UPROPERTY(BlueprintReadWrite)
	float DebuffDamage = 0.f;

	/**
	 * DebuffDuration
	 *
	 * 减益效果的总持续时间（秒）。
	 * 动态创建减益 GE 时作为 Duration Policy 的 Duration Magnitude 填入。
	 */
	UPROPERTY(BlueprintReadWrite)
	float DebuffDuration = 0.f;

	/**
	 * DebuffFrequency
	 *
	 * 减益效果的跳伤间隔（秒），即每隔多少秒触发一次 DebuffDamage。
	 * 例如频率 1.0 表示每秒跳一次伤害；0.5 表示每 0.5 秒一次。
	 */
	UPROPERTY(BlueprintReadWrite)
	float DebuffFrequency = 0.f;

	/**
	 * DeathImpulseMagnitude
	 *
	 * 死亡冲击力的强度标量（牛·秒）。
	 * 目标死亡时，技能类用命中方向乘以此值计算出 DeathImpulse 向量，
	 * 再通过 GE 上下文传给 PostGameplayEffectExecute，
	 * 最终调用 AddImpulse() 让目标 RagDoll 弹飞。
	 */
	UPROPERTY(BlueprintReadWrite)
	float DeathImpulseMagnitude = 0.f;

	/**
	 * DeathImpulse
	 *
	 * 已计算好的死亡冲击力向量（方向 × 强度）。
	 * 由技能类在调用前根据命中方向和 DeathImpulseMagnitude 计算并填入，
	 * ExecCalc_Damage 将其写入 GE 上下文，死亡回调中读取并应用到物理体。
	 */
	UPROPERTY(BlueprintReadWrite)
	FVector DeathImpulse = FVector::ZeroVector;

	/**
	 * KnockbackForceMagnitude
	 *
	 * 击退力的强度标量（牛·秒）。
	 * 与 DeathImpulseMagnitude 类似，但用于活着的目标被击飞（非死亡）。
	 * KnockbackChance 未通过时此值不产生效果。
	 */
	UPROPERTY(BlueprintReadWrite)
	float KnockbackForceMagnitude = 0.f;

	/**
	 * KnockbackChance
	 *
	 * 触发击退的概率（0~100，百分比）。
	 * ExecCalc_Damage 对比随机数决定是否施加击退力；
	 * 未触发时 KnockbackForce 不写入上下文，节省不必要的物理计算。
	 */
	UPROPERTY(BlueprintReadWrite)
	float KnockbackChance = 0.f;

	/**
	 * KnockbackForce
	 *
	 * 已计算好的击退力向量（方向 × 强度）。
	 * 由技能类根据攻击方向和 KnockbackForceMagnitude 计算并填入，
	 * 写入 GE 上下文后在 PostGameplayEffectExecute 中对活体目标应用冲击。
	 */
	UPROPERTY(BlueprintReadWrite)
	FVector KnockbackForce = FVector::ZeroVector;

	/**
	 * bIsRadialDamage
	 *
	 * 是否为范围（爆炸/溅射）伤害。
	 * 为 true 时，ExecCalc_Damage 会根据目标与爆炸中心的距离
	 * 在内外半径之间插值衰减伤害（内圈全伤，外圈边缘趋近于 0）。
	 */
	UPROPERTY(BlueprintReadWrite)
	bool bIsRadialDamage = false;

	/**
	 * RadialDamageInnerRadius
	 *
	 * 范围伤害的全伤半径（cm）。
	 * 目标距爆炸中心小于此距离时受到完整的 BaseDamage，不做衰减。
	 */
	UPROPERTY(BlueprintReadWrite)
	float RadialDamageInnerRadius = 0.f;

	/**
	 * RadialDamageOuterRadius
	 *
	 * 范围伤害的最大影响半径（cm）。
	 * 目标超出此距离时不受伤害；
	 * 在内外半径之间线性插值衰减伤害。
	 */
	UPROPERTY(BlueprintReadWrite)
	float RadialDamageOuterRadius = 0.f;

	/**
	 * RadialDamageOrigin
	 *
	 * 爆炸中心的世界空间坐标（cm）。
	 * ExecCalc_Damage 用此坐标计算每个目标到中心的距离，
	 * 进而决定该目标受到的伤害衰减比例。
	 */
	UPROPERTY(BlueprintReadWrite)
	FVector RadialDamageOrigin = FVector::ZeroVector;

};

/**
 * FAuraGameplayEffectContext
 *
 * 扩展的 GAS GameplayEffect 上下文，携带伤害流程中的游戏特定数据。
 *
 * 设计背景：
 *   GAS 原生的 FGameplayEffectContext 只包含施法者、目标、命中结果等通用信息。
 *   本项目需要在 ExecCalc → GE 应用 → GameplayCue 整个链路上传递
 *   暴击/格挡标志、减益参数、物理冲击力、范围伤害参数等游戏特定数据。
 *   通过继承扩展上下文，这些数据可以随 FGameplayEffectContextHandle 一起
 *   在服务器和客户端之间传输，无需额外 RPC 或全局变量。
 *
 * 使用方式：
 *   - ExecCalc_Damage 通过 static_cast<FAuraGameplayEffectContext*> 转型后调用 Set* 系列函数写入数据
 *   - UAuraAttributeSet::PostGameplayEffectExecute 调用 Get* 系列函数读取数据
 *   - UAuraAbilitySystemLibrary 提供了便捷的静态包装函数供蓝图使用
 *
 * 注意：
 *   DamageType 使用 TSharedPtr 而非 UPROPERTY，因为 FGameplayTag 在此处
 *   需要可空语义（nullptr 表示无类型），TSharedPtr 比裸指针更安全。
 */
USTRUCT(BlueprintType)
struct FAuraGameplayEffectContext : public FGameplayEffectContext
{
	GENERATED_BODY()

public:

	// ——— Getter 函数 ———
	// 供 PostGameplayEffectExecute 和 GameplayCue 读取伤害计算结果

	/** 是否暴击命中：由 ExecCalc_Damage 在暴击概率检测后写入，PostGameplayEffectExecute 读取以决定浮动文字样式 */
	bool IsCriticalHit() const { return bIsCriticalHit; }

	/** 是否被格挡：由 ExecCalc_Damage 在格挡概率检测后写入，格挡时伤害减半，PostGameplayEffectExecute 读取以调整伤害文字颜色 */
	bool IsBlockedHit () const { return bIsBlockedHit; }

	/** 是否成功触发减益：由 ExecCalc_Damage 在 DebuffChance 概率检测后写入，PostGameplayEffectExecute 读取后动态创建减益 GE */
	bool IsSuccessfulDebuff() const { return bIsSuccessfulDebuff; }

	/** 减益每次跳伤值：成功触发减益时有效，PostGameplayEffectExecute 通过 SetByCaller 传给动态创建的减益 GE */
	float GetDebuffDamage() const { return DebuffDamage; }

	/** 减益持续时间（秒）：动态创建减益 GE 时设置其 Duration Magnitude */
	float GetDebuffDuration() const { return DebuffDuration; }

	/** 减益跳伤间隔（秒）：动态创建减益 GE 时设置其 Period（跳伤频率）*/
	float GetDebuffFrequency() const { return DebuffFrequency; }

	/** 伤害类型标签（如 Damage.Fire）：PostGameplayEffectExecute 用于在 DamageTypesToDebuffs 映射中查找对应减益 Tag */
	TSharedPtr<FGameplayTag> GetDamageType() const { return DamageType; }

	/** 死亡冲击力向量：目标死亡时由 PostGameplayEffectExecute 调用 AddImpulse() 推动 RagDoll */
	FVector GetDeathImpulse() const { return DeathImpulse; }

	/** 击退力向量：目标存活时由 PostGameplayEffectExecute 施加物理冲击使目标短暂飞出 */
	FVector GetKnockbackForce() const { return KnockbackForce; }

	/** 是否为范围伤害：为 true 时 ExecCalc 按距离衰减伤害 */
	bool IsRadialDamage() const { return bIsRadialDamage; }

	/** 全伤半径（cm）：目标在此半径内受完整伤害 */
	float GetRadialDamageInnerRadius() const { return RadialDamageInnerRadius; }

	/** 最大影响半径（cm）：超出此范围的目标不受伤害 */
	float GetRadialDamageOuterRadius() const { return RadialDamageOuterRadius; }

	/** 爆炸中心世界坐标：计算目标到中心距离以确定衰减系数 */
	FVector GetRadialDamageOrigin() const { return RadialDamageOrigin; }

	// ——— Setter 函数 ———
	// 由 ExecCalc_Damage 在计算过程中写入各字段

	void SetIsCriticalHit(bool bInIsCriticalHit) { bIsCriticalHit = bInIsCriticalHit; }
	void SetIsBlockedHit(bool bInIsBlockedHit) { bIsBlockedHit = bInIsBlockedHit; }
	void SetIsSuccessfulDebuff(bool bInIsDebuff) { bIsSuccessfulDebuff = bInIsDebuff; }
	void SetDebuffDamage(float InDamage) { DebuffDamage = InDamage; }
	void SetDebuffDuration(float InDuration) { DebuffDuration = InDuration; }
	void SetDebuffFrequency(float InFrequency) { DebuffFrequency = InFrequency; }
	void SetDamageType(TSharedPtr<FGameplayTag> InDamageType) { DamageType = InDamageType; }
	void SetDeathImpulse(const FVector& InImpulse) { DeathImpulse = InImpulse; }
	void SetKnockbackForce(const FVector& InForce) { KnockbackForce = InForce; }
	void SetIsRadialDamage(bool bInIsRadialDamage) { bIsRadialDamage = bInIsRadialDamage; }
	void SetRadialDamageInnerRadius(float InRadialDamageInnerRadius) { RadialDamageInnerRadius = InRadialDamageInnerRadius; }
	void SetRadialDamageOuterRadius(float InRadialDamageOuterRadius) { RadialDamageOuterRadius = InRadialDamageOuterRadius; }
	void SetRadialDamageOrigin(const FVector& InRadialDamageOrigin) { RadialDamageOrigin = InRadialDamageOrigin; }

	/**
	 * GetScriptStruct
	 *
	 * GAS 反射机制要求：每个 FGameplayEffectContext 子类必须重写此函数，
	 * 返回自身的 UScriptStruct，否则 GAS 内部的序列化/复制逻辑
	 * 仍会按基类结构操作，导致自定义字段丢失。
	 *
	 * 注意：当前实现仍返回基类 StaticStruct()，这是一个 Bug——
	 * 应该返回 FAuraGameplayEffectContext::StaticStruct()，
	 * 否则网络序列化时自定义字段不会被正确处理。
	 * 正确的实现应为：return FAuraGameplayEffectContext::StaticStruct();
	 */
	virtual UScriptStruct* GetScriptStruct() const
	{
		return FGameplayEffectContext::StaticStruct();
	}

	/**
	 * Duplicate
	 *
	 * 创建此上下文的深拷贝副本。
	 * GAS 在某些情况下（如 GameplayCue 触发时）需要复制上下文，
	 * 子类必须重写此函数以确保自定义字段也被复制到新对象中，
	 * 而不是只拷贝基类部分。
	 */
	virtual FGameplayEffectContext* Duplicate() const
	{
		FGameplayEffectContext* NewContext = new FGameplayEffectContext();
		*NewContext = *this;
		if (GetHitResult())
		{
			// 对命中结果进行深拷贝，避免多个上下文共享同一个 HitResult 指针
			NewContext->AddHitResult(*GetHitResult(), true);
		}
		return NewContext;
	}

	/**
	 * NetSerialize
	 *
	 * 自定义网络序列化函数，负责将所有自定义字段打包/解包用于网络传输。
	 *
	 * 为何必须重写：
	 *   FGameplayEffectContext 的 NetSerialize 只序列化基类已知的字段。
	 *   FAuraGameplayEffectContext 新增的字段（bIsCriticalHit、减益参数、
	 *   物理冲击力等）必须在此函数中手动序列化，否则客户端收到的上下文
	 *   中这些字段全为默认值，导致客户端表现与服务器不一致
	 *   （如客户端看不到暴击特效、减益效果等）。
	 *
	 *   具体实现在 AuraAbilityTypes.cpp 中，使用 Ar << 操作符逐字段序列化。
	 */
	virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

protected:

	/**
	 * bIsBlockedHit
	 * 是否被格挡命中。
	 * ExecCalc_Damage 检测格挡概率后设置；
	 * PostGameplayEffectExecute 读取后将伤害减半，
	 * 并通知 UI 显示"BLOCKED"样式的浮动文字。
	 */
	UPROPERTY()
	bool bIsBlockedHit = false;

	/**
	 * bIsCriticalHit
	 * 是否暴击命中。
	 * ExecCalc_Damage 检测暴击概率后设置；
	 * PostGameplayEffectExecute 读取后在基础伤害上叠加暴击加成，
	 * 并通知 UI 显示高亮的暴击浮动文字。
	 */
	UPROPERTY()
	bool bIsCriticalHit = false;

	/**
	 * bIsSuccessfulDebuff
	 * 本次攻击是否成功触发减益。
	 * ExecCalc_Damage 根据 DebuffChance 随机决定后设置；
	 * PostGameplayEffectExecute 检查此标志，
	 * 为 true 时动态创建对应类型的减益 GE 并应用到目标。
	 */
	UPROPERTY()
	bool bIsSuccessfulDebuff = false;

	/**
	 * DebuffDamage / DebuffDuration / DebuffFrequency
	 * 减益效果的三个关键参数，从 FDamageEffectParams 原样传入上下文。
	 * PostGameplayEffectExecute 在动态创建减益 GE 时
	 * 通过 SetByCaller 将这三个值注入 GE Spec，
	 * 使减益 GE 知道每次跳多少伤、持续多久、多久跳一次。
	 */
	UPROPERTY()
	float DebuffDamage = 0.f;

	UPROPERTY()
	float DebuffDuration = 0.f;

	UPROPERTY()
	float DebuffFrequency = 0.f;

	/**
	 * DamageType
	 * 伤害类型标签的共享指针（可为空）。
	 * 使用 TSharedPtr 而非 UPROPERTY + FGameplayTag 的原因：
	 * 需要可空语义来区分"未设置类型"和"物理伤害类型"；
	 * 同时 FGameplayTag 不需要 GC 管理，TSharedPtr 足够安全。
	 * PostGameplayEffectExecute 通过此 Tag 在映射表中查找减益类型。
	 */
	TSharedPtr<FGameplayTag> DamageType;

	/**
	 * DeathImpulse
	 * 目标死亡时施加的物理冲击力向量（方向已归一化 × 强度）。
	 * PostGameplayEffectExecute 在目标生命值归零时读取，
	 * 调用目标物理体的 AddImpulse() 产生 RagDoll 飞出效果。
	 */
	UPROPERTY()
	FVector DeathImpulse = FVector::ZeroVector;

	/**
	 * KnockbackForce
	 * 目标存活时施加的击退力向量。
	 * 与 DeathImpulse 的区别：目标不死亡时才生效，
	 * 通过短暂的物理冲击让目标向后弹出，产生被击打的反馈感。
	 */
	UPROPERTY()
	FVector KnockbackForce = FVector::ZeroVector;

	/**
	 * bIsRadialDamage
	 * 是否为范围（爆炸）伤害。
	 * true 时 ExecCalc_Damage 启用距离衰减逻辑；
	 * false 时所有目标受到相同的 BaseDamage（如直线飞弹）。
	 */
	UPROPERTY()
	bool bIsRadialDamage = false;

	/**
	 * RadialDamageInnerRadius
	 * 范围伤害全伤半径（cm）。目标在此圆内受完整基础伤害。
	 */
	UPROPERTY()
	float RadialDamageInnerRadius = 0.f;

	/**
	 * RadialDamageOuterRadius
	 * 范围伤害最大半径（cm）。超出此圆的目标不受伤害。
	 * 在 Inner～Outer 之间按距离线性插值衰减伤害。
	 */
	UPROPERTY()
	float RadialDamageOuterRadius = 0.f;

	/**
	 * RadialDamageOrigin
	 * 爆炸中心世界坐标（cm）。
	 * ExecCalc_Damage 计算每个目标与此点的距离来确定衰减系数。
	 */
	UPROPERTY()
	FVector RadialDamageOrigin = FVector::ZeroVector;
};

/**
 * TStructOpsTypeTraits<FAuraGameplayEffectContext>
 *
 * 向 UE 反射系统声明此结构体支持的特殊操作：
 *
 * WithNetSerializer = true：
 *   告知 GAS 网络层使用本结构体自定义的 NetSerialize() 函数
 *   来序列化/反序列化上下文数据，而非自动生成的默认序列化。
 *   没有此声明，自定义字段无法通过网络同步。
 *
 * WithCopy = true：
 *   告知反射系统此结构体支持普通的值拷贝语义（operator=）。
 *   GAS 在内部传递上下文时会进行值拷贝，此标志保证拷贝行为正确。
 */
template<>
struct TStructOpsTypeTraits<FAuraGameplayEffectContext> : public TStructOpsTypeTraitsBase2<FAuraGameplayEffectContext>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};
