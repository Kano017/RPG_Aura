// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AuraAttributeSet.generated.h"

/**
 * ATTRIBUTE_ACCESSORS 宏 — 为每个属性自动生成四个访问函数
 *
 * 展开后等价于：
 *   static FGameplayAttribute GetXxxAttribute();   // 返回 FGameplayAttribute 对象
 *                                                  // 用于 GE 捕获（CaptureAttribute）、
 *                                                  // AttributeMenu 遍历、ExecCalc 查询
 *   float GetXxx() const;                          // 读取 CurrentValue（受修改器叠加后的实时值）
 *   void  SetXxx(float NewValue);                  // 直接写入 CurrentValue（绕过 GE 流程，
 *                                                  // 适合 Clamp 等后处理操作）
 *   void  InitXxx(float NewValue);                 // 写入 BaseValue（仅初始化时使用，
 *                                                  // 绕过所有修改器，通常由 GE Instant 调用）
 *
 * 设计原因：GAS 的属性访问模式固定，用宏消除重复样板代码，
 * 同时保证所有属性暴露一致的接口供 GE/ExecCalc/UI 系统使用。
 */
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * FEffectProperties — GE 执行时的完整上下文快照
 *
 * 职责：将 FGameplayEffectModCallbackData（GAS 原始回调数据）拆解为易用的字段。
 * 在 PostGameplayEffectExecute 中由 SetEffectProperties() 填充，
 * 随后传递给 HandleIncomingDamage / HandleIncomingXP / Debuff 等处理函数。
 *
 * Source = 技能的施法者（攻击方）
 * Target = 被 GE 命中的角色（防御方，即本 AttributeSet 的拥有者）
 *
 * 为什么需要同时存 ASC / AvatarActor / Controller / Character？
 *   - ASC：用于应用新 GE（如 Debuff）
 *   - AvatarActor：用于获取世界位置（飘字、击退方向）
 *   - Controller：区分玩家控制 vs AI，ShowDamageNumber 需要 PlayerController
 *   - Character：调用 LaunchCharacter 击退、Die 死亡等角色专属接口
 */
USTRUCT()
struct FEffectProperties
{
	GENERATED_BODY()

	FEffectProperties(){}

	// GE 的完整上下文句柄，携带：施法者信息、自定义扩展数据（FAuraGameplayEffectContext）
	// 通过它可读取：暴击标志、格挡标志、击退向量、Debuff 参数、伤害类型等
	FGameplayEffectContextHandle EffectContextHandle;

	// ——————————————— 施法者（Source）信息 ———————————————

	// 施法者的 AbilitySystemComponent，用于在 Debuff() 中以施法者身份构造并应用新 GE
	UPROPERTY()
	UAbilitySystemComponent* SourceASC = nullptr;

	// 施法者的 Avatar Actor（通常是 ACharacter），用于获取位置、判断阵营
	UPROPERTY()
	AActor* SourceAvatarActor = nullptr;

	// 施法者的 Controller，玩家攻击 → AAuraPlayerController，敌人攻击 → AAIController
	// 用于 ShowFloatingText 时区分谁是本地玩家
	UPROPERTY()
	AController* SourceController = nullptr;

	// 施法者的 Character 引用（从 Controller->GetPawn() 转换而来）
	// HandleIncomingXP 和 SendXPEvent 通过它调用 PlayerInterface / CombatInterface
	UPROPERTY()
	ACharacter* SourceCharacter = nullptr;

	// ——————————————— 受击者（Target）信息 ———————————————

	// 受击者的 ASC，用于触发 HitReact 技能（TryActivateAbilitiesByTag）
	UPROPERTY()
	UAbilitySystemComponent* TargetASC = nullptr;

	// 受击者的 Avatar Actor，ShowFloatingText 需要它的世界坐标来定位飘字
	UPROPERTY()
	AActor* TargetAvatarActor = nullptr;

	// 受击者的 Controller，多人游戏中本地玩家被攻击时需要在自己的 PC 上显示飘字
	UPROPERTY()
	AController* TargetController = nullptr;

	// 受击者的 Character，用于 LaunchCharacter 击退、Die 死亡、HitReact 等
	UPROPERTY()
	ACharacter* TargetCharacter = nullptr;
};

// TStaticFuncPtr — 指向"返回 FGameplayAttribute，无参数"静态函数的函数指针类型别名
// 用于 TagsToAttributes 映射表的值类型：存储各 GetXxxAttribute() 函数的地址
//
// 为何不用 typedef？
//   typedef 只能表示固定签名 FGameplayAttribute()，
//   而 TStaticFuncPtr<T> 是模板别名，可在其他场合复用于不同签名的函数指针，扩展性更好。
//   注释掉的旧写法保留仅供对比参考。
//
// typedef TBaseStaticDelegateInstance<FGameplayAttribute(), FDefaultDelegateUserPolicy>::FFuncPtr FAttributeFuncPtr;
template<class T>
using TStaticFuncPtr = typename TBaseStaticDelegateInstance<T, FDefaultDelegateUserPolicy>::FFuncPtr;

/**
 * UAuraAttributeSet — Aura RPG 游戏的全局属性集
 *
 * ══════════════════════════════════════════════════════════════════
 * 职责
 * ══════════════════════════════════════════════════════════════════
 * 定义并管理游戏中所有角色的数值属性，并在属性变化时执行游戏逻辑
 * （扣血、死亡、升级、Debuff 施加、飘字显示等）。
 *
 * ══════════════════════════════════════════════════════════════════
 * 属性分类（四层架构）
 * ══════════════════════════════════════════════════════════════════
 * 1. 主属性（Primary）   — Strength / Intelligence / Resilience / Vigor
 *    玩家手动分配点数，是其他一切属性的数据源。
 *    由 Instant GE（GE_SetPrimaryAttributes）直接写入 BaseValue。
 *
 * 2. 次要属性（Secondary）— Armor / ArmorPenetration / BlockChance 等
 *    由主属性通过 MMC（Modifier Magnitude Calculation）动态派生。
 *    使用 Infinite GE + Periodic 或直接 Instant 重算，随主属性变化自动更新。
 *
 * 3. 生命属性（Vital）   — Health / Mana / MaxHealth / MaxMana
 *    当前值，可在战斗中频繁变动。MaxHealth/MaxMana 也属于此层（由 Vigor 等派生）。
 *
 * 4. 元属性（Meta）      — IncomingDamage / IncomingXP
 *    "消息管道"，不是真实的角色属性。
 *    ExecCalc_Damage 将计算好的最终伤害写入 IncomingDamage，
 *    PostGameplayEffectExecute 读取后立即扣血并重置为 0。
 *    设计为元属性而非直接扣血的原因：
 *    让 ExecCalc 只负责"计算"，属性集负责"应用"，职责分离；
 *    同时便于在应用前做 Clamp、死亡判断、飘字等后处理。
 *
 * ══════════════════════════════════════════════════════════════════
 * 网络复制
 * ══════════════════════════════════════════════════════════════════
 * 所有真实属性（Primary / Secondary / Vital / Resistance）均开启复制，
 * 使用 REPNOTIFY_Always 确保即使值不变也触发 OnRep（GAS 内部需要此行为）。
 * 元属性（IncomingDamage / IncomingXP）不复制：它们只在服务器计算并立即消费。
 *
 * ══════════════════════════════════════════════════════════════════
 * 与其他系统的关系
 * ══════════════════════════════════════════════════════════════════
 * - ExecCalc_Damage：读取 Source/Target 的各属性，计算最终伤害，写入 IncomingDamage
 * - MMC_MaxHealth / MMC_MaxMana：读取 Vigor/Intelligence 等主属性，输出 MaxHealth/MaxMana
 * - AttributeMenuWidgetController：通过 TagsToAttributes 遍历所有属性并在 UI 展示
 * - SpellMenuWidgetController：读取 SpellPoints，触发技能升级
 * - AuraAbilitySystemLibrary：提供 GetXPRewardForClassAndLevel 等工具函数
 * - CombatInterface / PlayerInterface：Die()、LevelUp()、AddToXP() 等角色行为接口
 */
UCLASS()
class AURA_API UAuraAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
public:
	UAuraAttributeSet();

	// GAS 网络复制注册，在此函数中为每个需要复制的属性调用 DOREPLIFETIME_CONDITION_NOTIFY
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * PreAttributeChange — 属性值被修改"之前"的拦截点
	 *
	 * 调用时机：GE 修改器即将写入属性值前，NewValue 是计算后、写入前的值。
	 * 此处仅做 Clamp（范围限制），防止 Health 超过 MaxHealth 或低于 0。
	 *
	 * 注意：此时属性值尚未实际改变，不能在此执行扣血/死亡等副作用逻辑，
	 * 那些逻辑应放在 PostGameplayEffectExecute 中。
	 *
	 * 注意：修改 NewValue 只影响本次修改器，不会改变 BaseValue，
	 * MaxHealth 变化后需要在 PostAttributeChange 中二次 Clamp。
	 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/**
	 * PostGameplayEffectExecute — GE 执行完成后的核心处理入口
	 *
	 * 调用时机：一个 GameplayEffect 的所有修改器全部应用完毕之后。
	 * 这是执行游戏逻辑副作用的正确位置（区别于 PreAttributeChange 只做值限制）。
	 *
	 * 处理分支：
	 *   Health 变化    → Clamp 到 [0, MaxHealth]（PostGameplayEffectExecute 中的 Clamp 是持久的）
	 *   Mana 变化      → Clamp 到 [0, MaxMana]
	 *   IncomingDamage → 调用 HandleIncomingDamage()：扣血、死亡、HitReact、击退、飘字、Debuff
	 *   IncomingXP     → 调用 HandleIncomingXP()：累加经验、判断升级、发放奖励点数
	 *
	 * @param Data 包含本次 GE 执行的完整信息：触发的属性、修改量、Source/Target 的 ASC 等
	 */
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	/**
	 * PostAttributeChange — 属性值实际改变"之后"的回调
	 *
	 * 调用时机：属性的 CurrentValue 已写入完成后。
	 * 用途：当 MaxHealth / MaxMana 因升级而增大时，
	 *       需要将 Health / Mana 同步补满（"升级回血"效果）。
	 *
	 * 设计细节：
	 *   HandleIncomingXP 检测到升级时会将 bTopOffHealth / bTopOffMana 置为 true，
	 *   等到 MaxHealth / MaxMana 的变化在此回调触发后，再执行 SetHealth(GetMaxHealth())。
	 *   这样做是因为升级导致 MaxHealth 改变的时机比 HandleIncomingXP 晚，
	 *   必须等 Max 值更新完成才能正确补满。
	 *
	 * @param Attribute 发生变化的属性
	 * @param OldValue  变化前的值
	 * @param NewValue  变化后的值
	 */
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;

	/**
	 * TagsToAttributes — GameplayTag 到属性访问函数的映射表
	 *
	 * 键：FGameplayTag（如 Attributes.Primary.Strength）
	 * 值：指向对应 GetXxxAttribute() 静态函数的函数指针
	 *
	 * 使用者：
	 *   - AttributeMenuWidgetController：遍历此 Map 获取所有属性的 FGameplayAttribute，
	 *     用于向 UI 广播属性名称和数值，实现数据驱动的属性面板
	 *   - ExecCalc_Damage：通过伤害类型 Tag 查找对应的抗性属性（如 Fire → FireResistance），
	 *     动态捕获目标的抗性值进行伤害减免计算
	 *
	 * 设计意图：避免硬编码属性名，将"Tag → 属性"的对应关系集中在构造函数中维护，
	 * 新增属性类型时只需在此处添加一行映射即可，无需修改调用方。
	 */
	TMap<FGameplayTag, TStaticFuncPtr<FGameplayAttribute()>> TagsToAttributes;

	/*
	 * ════════════════════════════════════════════════════════════
	 * 主属性（Primary Attributes）
	 * 玩家手动分配属性点，是所有次要属性的计算源头。
	 * 由 GE_SetPrimaryAttributes（Instant GE）初始化写入 BaseValue。
	 * ════════════════════════════════════════════════════════════
	 */

	// 力量 — 影响 PhysicalDamage（物理攻击强度）和 Armor（护甲，通过 MMC_Armor 计算）
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Strength, Category = "Primary Attributes")
	FGameplayAttributeData Strength;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, Strength);

	// 智力 — 影响法术伤害倍率和 MaxMana（通过 MMC_MaxMana 计算）
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Intelligence, Category = "Primary Attributes")
	FGameplayAttributeData Intelligence;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, Intelligence);

	// 韧性 — 影响 Armor（物理防御）和 CriticalHitResistance（暴击抗性），
	//         提升生存能力的核心主属性
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Resilience, Category = "Primary Attributes")
	FGameplayAttributeData Resilience;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, Resilience);

	// 活力 — 影响 MaxHealth（通过 MMC_MaxHealth 计算）和 HealthRegeneration
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Vigor, Category = "Primary Attributes")
	FGameplayAttributeData Vigor;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, Vigor);

	/*
	 * ════════════════════════════════════════════════════════════
	 * 次要属性（Secondary Attributes）
	 * 由主属性通过 MMC（ModMagCalc）计算派生，使用 Infinite GE 持续施加。
	 * 主属性变化时，关联的 Infinite GE 会自动重新计算并更新这些属性。
	 * ════════════════════════════════════════════════════════════
	 */

	// 护甲 — 减少受到的物理伤害，在 ExecCalc_Damage 中与 ArmorPenetration 抗衡
	// 计算来源：Resilience（韧性），由 MMC_Armor 派生
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Armor, Category = "Secondary Attributes")
	FGameplayAttributeData Armor;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, Armor);

	// 护甲穿透 — 无视目标一定比例的护甲，攻击者的穿透越高，防御者的护甲越无效
	// 计算来源：Strength（力量），由 MMC_ArmorPenetration 派生
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ArmorPenetration, Category = "Secondary Attributes")
	FGameplayAttributeData ArmorPenetration;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, ArmorPenetration);

	// 格挡率 — 成功格挡时伤害减半，在 ExecCalc_Damage 中随机判定
	// 计算来源：Armor（护甲），由 MMC_BlockChance 派生
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_BlockChance, Category = "Secondary Attributes")
	FGameplayAttributeData BlockChance;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, BlockChance);

	// 暴击率 — 触发暴击的概率（%），暴击伤害 = 基础伤害 * 2 + CriticalHitDamage 加成
	// 计算来源：Strength（力量），由 MMC_CriticalHitChance 派生
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CriticalHitChance, Category = "Secondary Attributes")
	FGameplayAttributeData CriticalHitChance;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, CriticalHitChance);

	// 暴击伤害加成 — 暴击时额外叠加的伤害值（非倍率，是加法加成）
	// 计算来源：Strength（力量），由 MMC_CriticalHitDamage 派生
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CriticalHitDamage, Category = "Secondary Attributes")
	FGameplayAttributeData CriticalHitDamage;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, CriticalHitDamage);

	// 暴击抗性 — 降低对方的有效暴击率，在 ExecCalc_Damage 中从 CriticalHitChance 中减去
	// 计算来源：Resilience（韧性），由 MMC_CriticalHitResistance 派生
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CriticalHitResistance, Category = "Secondary Attributes")
	FGameplayAttributeData CriticalHitResistance;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, CriticalHitResistance);

	// 生命回复 — 每秒（或每周期）自动回复的生命值，通常由 Periodic GE 消耗此属性
	// 计算来源：Vigor（活力），由 MMC_HealthRegeneration 派生
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HealthRegeneration, Category = "Secondary Attributes")
	FGameplayAttributeData HealthRegeneration;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, HealthRegeneration);

	// 法力回复 — 每秒（或每周期）自动回复的法力值
	// 计算来源：Intelligence（智力），由 MMC_ManaRegeneration 派生
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ManaRegeneration, Category = "Secondary Attributes")
	FGameplayAttributeData ManaRegeneration;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, ManaRegeneration);

	// 最大生命值 — Health 的 Clamp 上限，由 Vigor 驱动（MMC_MaxHealth）
	// 分类归在 Vital 下（与 Health 同属生命资源组），但本质上是次要属性
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "Vital Attributes")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, MaxHealth);

	// 最大法力值 — Mana 的 Clamp 上限，由 Intelligence 驱动（MMC_MaxMana）
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxMana, Category = "Vital Attributes")
	FGameplayAttributeData MaxMana;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, MaxMana);

	/*
	 * ════════════════════════════════════════════════════════════
	 * 抗性属性（Resistance Attributes）
	 * 减少对应伤害类型的受击伤害，范围 [0, 100]（百分比减免）。
	 * 在 ExecCalc_Damage 中通过 TagsToAttributes 按伤害类型动态查找对应抗性。
	 * 由次要属性 GE（Infinite GE）计算，最终由 Resilience 等主属性影响基础值。
	 * ════════════════════════════════════════════════════════════
	 */

	// 火焰抗性 — 减少 FireDamage 的受击伤害百分比
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FireResistance, Category = "Resistance Attributes")
	FGameplayAttributeData FireResistance;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, FireResistance);

	// 闪电抗性 — 减少 LightningDamage 的受击伤害百分比
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_LightningResistance, Category = "Resistance Attributes")
	FGameplayAttributeData LightningResistance;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, LightningResistance);

	// 奥术抗性 — 减少 ArcaneDamage 的受击伤害百分比
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ArcaneResistance, Category = "Resistance Attributes")
	FGameplayAttributeData ArcaneResistance;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, ArcaneResistance);

	// 物理抗性 — 减少 PhysicalDamage 的受击伤害百分比
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_PhysicalResistance, Category = "Resistance Attributes")
	FGameplayAttributeData PhysicalResistance;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, PhysicalResistance);

	/*
	 * ════════════════════════════════════════════════════════════
	 * 生命属性（Vital Attributes）
	 * 当前生命/法力值，在战斗中频繁变化，始终 Clamp 在 [0, Max] 区间。
	 * ════════════════════════════════════════════════════════════
	 */

	// 当前生命值 — 降至 0 触发死亡流程（由 HandleIncomingDamage 检测并调用 Die()）
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "Vital Attributes")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, Health);

	// 当前法力值 — 使用技能时消耗，由技能的 Cost GE（Instant）直接扣除
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Mana, Category = "Vital Attributes")
	FGameplayAttributeData Mana;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, Mana);


	/*
	 * ════════════════════════════════════════════════════════════
	 * 元属性（Meta Attributes）
	 * 不代表真实的角色状态，而是作为"数据总线"在 GAS 系统间传递计算结果。
	 * 这两个属性不参与网络复制（服务器本地计算后立即消费）。
	 * ════════════════════════════════════════════════════════════
	 */

	// 传入伤害 — ExecCalc_Damage 将最终伤害值写入此属性（通过 Additive GE Modifier），
	// PostGameplayEffectExecute 检测到非零后从 Health 中扣除，随后立即重置为 0。
	// 设计为元属性而非直接修改 Health 的原因：
	//   ExecCalc 只负责"计算伤害"，AttributeSet 负责"应用后果"，单一职责；
	//   同时允许在扣血前插入死亡判断、击退、飘字、Debuff 等完整后处理链。
	UPROPERTY(BlueprintReadOnly, Category = "Meta Attributes")
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, IncomingDamage);

	// 传入经验值 — 敌人死亡时，SendXPEvent 发送 GameplayEvent，
	// GA_ListenForEvents 接收后应用 GE_EventBasedEffect 将 XP 写入此属性，
	// PostGameplayEffectExecute 检测到后调用 HandleIncomingXP 处理升级逻辑，随后重置为 0。
	UPROPERTY(BlueprintReadOnly, Category = "Meta Attributes")
	FGameplayAttributeData IncomingXP;
	ATTRIBUTE_ACCESSORS(UAuraAttributeSet, IncomingXP);

	// ═══════════════════════════════════════════════════════════════
	// OnRep 回调 — 网络复制通知函数
	// 每个复制属性必须声明对应的 OnRep 函数，函数体内调用
	// GAMEPLAYATTRIBUTE_REPNOTIFY 宏，通知 GAS 系统客户端属性已更新，
	// 使客户端的预测/回滚机制能正常工作。
	// ═══════════════════════════════════════════════════════════════

	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldHealth) const;

	UFUNCTION()
	void OnRep_Mana(const FGameplayAttributeData& OldMana) const;

	UFUNCTION()
	void OnRep_Strength(const FGameplayAttributeData& OldStrength) const;

	UFUNCTION()
	void OnRep_Intelligence(const FGameplayAttributeData& OldIntelligence) const;

	UFUNCTION()
	void OnRep_Resilience(const FGameplayAttributeData& OldResilience) const;

	UFUNCTION()
	void OnRep_Vigor(const FGameplayAttributeData& OldVigor) const;

	UFUNCTION()
	void OnRep_Armor(const FGameplayAttributeData& OldArmor) const;

	UFUNCTION()
	void OnRep_ArmorPenetration(const FGameplayAttributeData& OldArmorPenetration) const;

	UFUNCTION()
	void OnRep_BlockChance(const FGameplayAttributeData& OldBlockChance) const;

	UFUNCTION()
	void OnRep_CriticalHitChance(const FGameplayAttributeData& OldCriticalHitChance) const;

	UFUNCTION()
	void OnRep_CriticalHitDamage(const FGameplayAttributeData& OldCriticalHitDamage) const;

	UFUNCTION()
	void OnRep_CriticalHitResistance(const FGameplayAttributeData& OldCriticalHitResistance) const;

	UFUNCTION()
	void OnRep_HealthRegeneration(const FGameplayAttributeData& OldHealthRegeneration) const;

	UFUNCTION()
	void OnRep_ManaRegeneration(const FGameplayAttributeData& OldManaRegeneration) const;

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth) const;

	UFUNCTION()
	void OnRep_MaxMana(const FGameplayAttributeData& OldMaxMana) const;

	UFUNCTION()
	void OnRep_FireResistance(const FGameplayAttributeData& OldFireResistance) const;

	UFUNCTION()
	void OnRep_LightningResistance(const FGameplayAttributeData& OldLightningResistance) const;

	UFUNCTION()
	void OnRep_ArcaneResistance(const FGameplayAttributeData& OldArcaneResistance) const;

	UFUNCTION()
	void OnRep_PhysicalResistance(const FGameplayAttributeData& OldPhysicalResistance) const;

private:
	/**
	 * HandleIncomingDamage — 处理传入伤害的完整逻辑链
	 * 在 PostGameplayEffectExecute 检测到 IncomingDamage 非零时调用。
	 * 执行顺序：读取并清零 IncomingDamage → 扣血 → 判断死亡/存活 → HitReact/击退 → 飘字 → Debuff
	 */
	void HandleIncomingDamage(const FEffectProperties& Props);

	/**
	 * HandleIncomingXP — 处理传入经验值并判断升级
	 * 在 PostGameplayEffectExecute 检测到 IncomingXP 非零时调用。
	 * 执行顺序：读取并清零 IncomingXP → 查询当前等级/XP → 计算新等级 →
	 *           发放属性点/技能点 → 标记补满 HP/MP → 触发 LevelUp → 累加 XP
	 */
	void HandleIncomingXP(const FEffectProperties& Props);

	/**
	 * Debuff — 在 HandleIncomingDamage 确认 Debuff 成功触发后，动态构建并应用 Debuff GE
	 *
	 * 为何在运行时 NewObject 创建 GE 而不用预设资产？
	 *   Debuff 的参数（伤害类型、持续时间、频率、伤害量）来自 ExecCalc 写入 EffectContext，
	 *   是运行时确定的，无法预先配置固定的 GE 资产。
	 *   动态创建 GE 允许将这些参数直接"烘焙"进 GE 实例，灵活性更高。
	 *
	 * Stun 特殊处理：Stun Debuff 会额外添加四个 Player_Block_* 标签，
	 * 禁用玩家的输入响应和鼠标拣选，实现完整的眩晕效果。
	 */
	void Debuff(const FEffectProperties& Props);

	/**
	 * SetEffectProperties — 将 GAS 原始回调数据解析填充到 FEffectProperties 结构体
	 *
	 * 提取 Source（施法者）的 ASC / AvatarActor / Controller / Character，
	 * 以及 Target（受击者）的对应信息。
	 * Controller 的获取有回退逻辑：先查 AbilityActorInfo，再从 Pawn->GetController() 获取，
	 * 兼容 AIController 控制的敌人（其 PlayerController 为 nullptr）。
	 */
	void SetEffectProperties(const FGameplayEffectModCallbackData& Data, FEffectProperties& Props) const;

	/**
	 * ShowFloatingText — 在攻击者的 PlayerController 上请求显示伤害飘字
	 *
	 * 优先从 Source（攻击方）的 Controller 显示，这样玩家攻击敌人时可以看到数字。
	 * 若 Source 不是玩家（AI 攻击玩家），则回退到 Target（受击方）的 Controller 显示。
	 * 避免对自伤显示飘字（SourceCharacter == TargetCharacter 时跳过）。
	 *
	 * @param Damage       实际造成的伤害量（已经过 Clamp 处理）
	 * @param bBlockedHit  是否触发了格挡（飘字颜色/样式不同）
	 * @param bCriticalHit 是否触发了暴击（飘字颜色/样式不同）
	 */
	void ShowFloatingText(const FEffectProperties& Props, float Damage, bool bBlockedHit, bool bCriticalHit) const;

	/**
	 * SendXPEvent — 敌人死亡时向攻击者发送经验值事件
	 *
	 * 通过 UAbilitySystemBlueprintLibrary::SendGameplayEventToActor 向 Source 角色
	 * 发送带有 XP 数量的 GameplayEvent（Tag：Attributes.Meta.IncomingXP），
	 * Source 角色上的 GA_ListenForEvents 技能监听此事件后应用 GE 将 XP 写入 IncomingXP。
	 * XP 奖励值通过 AuraAbilitySystemLibrary::GetXPRewardForClassAndLevel 查询数据表获得。
	 */
	void SendXPEvent(const FEffectProperties& Props);

	// 升级标志 — HandleIncomingXP 检测到升级时置为 true，
	// 等待 PostAttributeChange 中 MaxHealth/MaxMana 更新完成后触发补满操作。
	// 使用成员变量而非局部变量是因为补满操作发生在不同的函数调用帧中。
	bool bTopOffHealth = false;
	bool bTopOffMana = false;
};

