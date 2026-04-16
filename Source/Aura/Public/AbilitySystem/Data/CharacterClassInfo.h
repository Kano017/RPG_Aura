// Copyright Druid Mechanics

#pragma once

/*
 * CharacterClassInfo.h
 *
 * 职业数据资产（Data Asset），是整个角色职业系统的核心配置中心。
 *
 * 【架构职责】
 * - 通过 ECharacterClass 枚举区分三种职业：Elementalist（元素师）、Warrior（战士）、Ranger（游侠）
 * - 每种职业拥有独立的 FCharacterClassDefaultInfo，配置该职业特有的初始主属性 GE 和专属技能
 * - 所有职业共享同一套次要属性 GE（SecondaryAttributes），由 Primary 属性通过 MMC 计算得出
 * - DamageCalculationCoefficients 是一张 CurveTable，存储随等级变化的伤害系数，供 ExecCalc_Damage 查询
 *
 * 【访问方式】
 * - 本资产挂载在 AuraGameModeBase 上，通过 UAuraAbilitySystemLibrary::GetCharacterClassInfo 全局静态访问
 * - 敌人和玩家初始化属性时均调用 UAuraAbilitySystemLibrary::InitializeDefaultAttributes 读取此资产
 */

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ScalableFloat.h"
#include "CharacterClassInfo.generated.h"


class UGameplayEffect;
class UGameplayAbility;

/** 职业枚举，用于在 CharacterClassInformation TMap 中索引对应职业的默认配置 */
UENUM(BlueprintType)
enum class ECharacterClass : uint8
{
	Elementalist,	// 元素师：高智力，施法流角色
	Warrior,		// 战士：高力量，近战角色
	Ranger			// 游侠：高敏捷，远程角色
};

/**
 * 每个职业独立拥有的默认配置信息
 * 存储在 UCharacterClassInfo::CharacterClassInformation TMap 中，以 ECharacterClass 为键
 */
USTRUCT(BlueprintType)
struct FCharacterClassDefaultInfo
{
	GENERATED_BODY()

	/**
	 * 职业专属的初始主属性 GameplayEffect（Instant 类型）
	 * 使用 SetByCaller 机制为 Strength / Intelligence / Resilience / Vigor 设置初始值，
	 * 不同职业配置不同的 DataTag 数值，实现差异化属性起点
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Class Defaults")
	TSubclassOf<UGameplayEffect> PrimaryAttributes;

	/**
	 * 职业专属的初始技能列表（如 Warrior 拥有的重击技能等）
	 * 在角色初始化时通过 AuraAbilitySystemComponent::AddCharacterAbilities 赋予
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Class Defaults")
	TArray<TSubclassOf<UGameplayAbility>> StartupAbilities;

	/**
	 * 击杀此职业敌人获得的经验值
	 * FScalableFloat 支持绑定 CurveTable，可根据敌人等级缩放奖励 XP
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Class Defaults")
	FScalableFloat XPReward = FScalableFloat();
};

/**
 * UCharacterClassInfo
 *
 * 职业信息数据资产，在蓝图编辑器中配置后挂载到 AuraGameModeBase。
 * 通过 UAuraAbilitySystemLibrary::GetCharacterClassInfo 全局访问。
 */
UCLASS()
class AURA_API UCharacterClassInfo : public UDataAsset
{
	GENERATED_BODY()
public:

	/**
	 * 以 ECharacterClass 为键的职业默认信息映射表
	 * 查询方式：CharacterClassInformation[ECharacterClass::Warrior]
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Character Class Defaults")
	TMap<ECharacterClass, FCharacterClassDefaultInfo> CharacterClassInformation;

	/**
	 * 通用的 SetByCaller 主属性 GE（用于存档读档后恢复属性）
	 * 与各职业专属的 PrimaryAttributes 不同，此 GE 通过 SetByCaller 在运行时动态传入具体数值，
	 * 适合从存档中读出属性点后重新应用
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Common Class Defaults")
	TSubclassOf<UGameplayEffect> PrimaryAttributes_SetByCaller;

	/**
	 * 所有职业共享的次要属性 GE（Instant 类型）
	 * 依赖主属性通过 MMC（Modifier Magnitude Calculation）计算最终值，
	 * 例如：MaxHealth = Vigor * 系数 + 基础值
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Common Class Defaults")
	TSubclassOf<UGameplayEffect> SecondaryAttributes;

	/**
	 * 无限持续的次要属性 GE（Infinite 类型）
	 * 持续监听主属性变化并实时更新次要属性，用于在主属性加点后自动重算次要属性
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Common Class Defaults")
	TSubclassOf<UGameplayEffect> SecondaryAttributes_Infinite;

	/**
	 * 生命值 / 法力值等生命属性 GE（Instant 类型）
	 * 在次要属性初始化后应用，将当前 HP/MP 设置为 MaxHP/MaxMP
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Common Class Defaults")
	TSubclassOf<UGameplayEffect> VitalAttributes;

	/**
	 * 所有职业共享的通用技能列表（如命中反应 HitReact 技能等）
	 * 在角色初始化时与职业专属技能一并赋予
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Common Class Defaults")
	TArray<TSubclassOf<UGameplayAbility>> CommonAbilities;

	/**
	 * 伤害计算系数 CurveTable
	 * 每行对应一种系数类型（如 Armor、ArmorPenetration、CriticalHitDamage 等），
	 * 列为等级，ExecCalc_Damage 通过 FindCurve 查询当前等级对应的系数值
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Common Class Defaults|Damage")
	TObjectPtr<UCurveTable> DamageCalculationCoefficients;

	/** 根据职业枚举获取对应的默认信息，找不到时会触发 check 崩溃（配置必须完整） */
	FCharacterClassDefaultInfo GetClassDefaultInfo(ECharacterClass CharacterClass);
};
