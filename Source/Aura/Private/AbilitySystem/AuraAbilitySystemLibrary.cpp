// Copyright Druid Mechanics


#include "AbilitySystem/AuraAbilitySystemLibrary.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AuraAbilityTypes.h"
#include "AuraGameplayTags.h"
#include "Engine/OverlapResult.h"
#include "Game/AuraGameModeBase.h"
#include "Game/LoadScreenSaveGame.h"
#include "Interaction/CombatInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Player/AuraPlayerState.h"
#include "UI/HUD/AuraHUD.h"
#include "UI/WidgetController/AuraWidgetController.h"

// ─────────────────────────────────────────────────────────────
// MakeWidgetControllerParams — 构造 WidgetController 参数包
// ─────────────────────────────────────────────────────────────
bool UAuraAbilitySystemLibrary::MakeWidgetControllerParams(const UObject* WorldContextObject, FWidgetControllerParams& OutWCParams, AAuraHUD*& OutAuraHUD)
{
	// GetPlayerController(0)：始终获取本地玩家的 PC（多人游戏中每个客户端索引 0 都是自己）。
	// 这保证了 WidgetController 只在本地客户端创建，不会在其他客户端的屏幕上出现 UI。
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
	{
		OutAuraHUD = Cast<AAuraHUD>(PC->GetHUD());
		if (OutAuraHUD)
		{
			// PlayerState 持有 ASC 和 AttributeSet（而非 PlayerController），
			// 这是 GAS 官方推荐的多人网络架构：PlayerState 在客户端也有完整复制。
			AAuraPlayerState* PS = PC->GetPlayerState<AAuraPlayerState>();
			UAbilitySystemComponent* ASC = PS->GetAbilitySystemComponent();
			UAttributeSet* AS = PS->GetAttributeSet();

			// 将四个引用打包成 FWidgetControllerParams，
			// WidgetController 需要这四个依赖来监听属性变化和发起 ASC 操作。
			OutWCParams.AttributeSet = AS;
			OutWCParams.AbilitySystemComponent = ASC;
			OutWCParams.PlayerState = PS;
			OutWCParams.PlayerController = PC;
			return true;
		}
	}
	return false;
}

// ─────────────────────────────────────────────────────────────
// GetOverlayWidgetController
// ─────────────────────────────────────────────────────────────
UOverlayWidgetController* UAuraAbilitySystemLibrary::GetOverlayWidgetController(const UObject* WorldContextObject)
{
	FWidgetControllerParams WCParams;
	AAuraHUD* AuraHUD = nullptr;
	if (MakeWidgetControllerParams(WorldContextObject, WCParams, AuraHUD))
	{
		// AAuraHUD::GetOverlayWidgetController 内部做懒加载：
		// 若控制器尚未创建则 NewObject 并绑定委托，否则直接返回缓存的实例。
		return AuraHUD->GetOverlayWidgetController(WCParams);
	}
	return nullptr;
}

// ─────────────────────────────────────────────────────────────
// GetAttributeMenuWidgetController
// ─────────────────────────────────────────────────────────────
UAttributeMenuWidgetController* UAuraAbilitySystemLibrary::GetAttributeMenuWidgetController(const UObject* WorldContextObject)
{
	FWidgetControllerParams WCParams;
	AAuraHUD* AuraHUD = nullptr;
	if (MakeWidgetControllerParams(WorldContextObject, WCParams, AuraHUD))
	{
		return AuraHUD->GetAttributeMenuWidgetController(WCParams);
	}
	return nullptr;
}

// ─────────────────────────────────────────────────────────────
// GetSpellMenuWidgetController
// ─────────────────────────────────────────────────────────────
USpellMenuWidgetController* UAuraAbilitySystemLibrary::GetSpellMenuWidgetController(const UObject* WorldContextObject)
{
	FWidgetControllerParams WCParams;
	AAuraHUD* AuraHUD = nullptr;
	if (MakeWidgetControllerParams(WorldContextObject, WCParams, AuraHUD))
	{
		return AuraHUD->GetSpellMenuWidgetController(WCParams);
	}
	return nullptr;
}

// ─────────────────────────────────────────────────────────────
// InitializeDefaultAttributes — 通过 GE 初始化角色三层属性
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemLibrary::InitializeDefaultAttributes(const UObject* WorldContextObject, ECharacterClass CharacterClass, float Level, UAbilitySystemComponent* ASC)
{
	AActor* AvatarActor = ASC->GetAvatarActor();

	// 从 GameMode 获取职业配置数据资产。
	UCharacterClassInfo* CharacterClassInfo = GetCharacterClassInfo(WorldContextObject);
	FCharacterClassDefaultInfo ClassDefaultInfo = CharacterClassInfo->GetClassDefaultInfo(CharacterClass);

	// ── 第一层：主要属性（Primary Attributes）─────────────────
	// 每个职业有自己的 PrimaryAttributes GE（如战士有高力量，法师有高智力）。
	// Level 传入 MakeOutgoingSpec，GE 中的曲线（FScalableFloat）会根据等级缩放数值。
	FGameplayEffectContextHandle PrimaryAttributesContextHandle = ASC->MakeEffectContext();
	PrimaryAttributesContextHandle.AddSourceObject(AvatarActor);  // Source = 角色自身（自我增益）
	const FGameplayEffectSpecHandle PrimaryAttributesSpecHandle = ASC->MakeOutgoingSpec(ClassDefaultInfo.PrimaryAttributes, Level, PrimaryAttributesContextHandle);
	ASC->ApplyGameplayEffectSpecToSelf(*PrimaryAttributesSpecHandle.Data.Get());

	// ── 第二层：次要属性（Secondary Attributes）──────────────
	// 通过 MMC（ModMagnitudeCalculation）从主要属性计算：
	// 例如 MaxHealth = Vigor * 10 + 80，由 MMC_MaxHealth 实现。
	// 所有职业共用同一套次要属性 GE（CharacterClassInfo->SecondaryAttributes）。
	FGameplayEffectContextHandle SecondaryAttributesContextHandle = ASC->MakeEffectContext();
	SecondaryAttributesContextHandle.AddSourceObject(AvatarActor);
	const FGameplayEffectSpecHandle SecondaryAttributesSpecHandle = ASC->MakeOutgoingSpec(CharacterClassInfo->SecondaryAttributes, Level, SecondaryAttributesContextHandle);
	ASC->ApplyGameplayEffectSpecToSelf(*SecondaryAttributesSpecHandle.Data.Get());

	// ── 第三层：生命/法力属性（Vital Attributes）─────────────
	// 初始化当前生命值为最大值（MaxHealth 已由第二层 GE 设置好）。
	// 必须在第二层之后应用，否则 MaxHealth 尚未确定。
	FGameplayEffectContextHandle VitalAttributesContextHandle = ASC->MakeEffectContext();
	VitalAttributesContextHandle.AddSourceObject(AvatarActor);
	const FGameplayEffectSpecHandle VitalAttributesSpecHandle = ASC->MakeOutgoingSpec(CharacterClassInfo->VitalAttributes, Level, VitalAttributesContextHandle);
	ASC->ApplyGameplayEffectSpecToSelf(*VitalAttributesSpecHandle.Data.Get());
}

// ─────────────────────────────────────────────────────────────
// InitializeDefaultAttributesFromSaveData — 从存档恢复属性
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemLibrary::InitializeDefaultAttributesFromSaveData(const UObject* WorldContextObject, UAbilitySystemComponent* ASC, ULoadScreenSaveGame* SaveGame)
{
	UCharacterClassInfo* CharacterClassInfo = GetCharacterClassInfo(WorldContextObject);
	if (CharacterClassInfo == nullptr) return;

	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();

	const AActor* SourceAvatarActor = ASC->GetAvatarActor();

	FGameplayEffectContextHandle EffectContexthandle = ASC->MakeEffectContext();
	EffectContexthandle.AddSourceObject(SourceAvatarActor);

	// PrimaryAttributes_SetByCaller：专为存档设计的特殊 GE，
	// 每个主要属性由独立的 SetByCaller Tag 标识，数值直接从存档读取（不经曲线缩放）。
	// 这保证了存档的忠实还原：玩家之前升级加点的属性值被精确恢复。
	const FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(CharacterClassInfo->PrimaryAttributes_SetByCaller, 1.f, EffectContexthandle);

	// AssignTagSetByCallerMagnitude：将具体数值绑定到对应的 Tag 上，
	// GE 执行时会读取这些 Tag 对应的数值来设置属性。
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Attributes_Primary_Strength, SaveGame->Strength);
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Attributes_Primary_Intelligence, SaveGame->Intelligence);
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Attributes_Primary_Resilience, SaveGame->Resilience);
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Attributes_Primary_Vigor, SaveGame->Vigor);

	ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);

	// 次要属性使用 Infinite（无限持续）类型 GE（SecondaryAttributes_Infinite），
	// 防止加载后 GE 到期导致属性重置。玩家存档需要持久性，Instant GE 不适用。
	FGameplayEffectContextHandle SecondaryAttributesContextHandle = ASC->MakeEffectContext();
	SecondaryAttributesContextHandle.AddSourceObject(SourceAvatarActor);
	const FGameplayEffectSpecHandle SecondaryAttributesSpecHandle = ASC->MakeOutgoingSpec(CharacterClassInfo->SecondaryAttributes_Infinite, 1.f, SecondaryAttributesContextHandle);
	ASC->ApplyGameplayEffectSpecToSelf(*SecondaryAttributesSpecHandle.Data.Get());

	// 生命/法力属性的恢复：将当前值设置为最大值（重进游戏视为满状态）。
	FGameplayEffectContextHandle VitalAttributesContextHandle = ASC->MakeEffectContext();
	VitalAttributesContextHandle.AddSourceObject(SourceAvatarActor);
	const FGameplayEffectSpecHandle VitalAttributesSpecHandle = ASC->MakeOutgoingSpec(CharacterClassInfo->VitalAttributes, 1.f, VitalAttributesContextHandle);
	ASC->ApplyGameplayEffectSpecToSelf(*VitalAttributesSpecHandle.Data.Get());
}

// ─────────────────────────────────────────────────────────────
// GiveStartupAbilities — 批量赋予角色初始技能
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemLibrary::GiveStartupAbilities(const UObject* WorldContextObject, UAbilitySystemComponent* ASC, ECharacterClass CharacterClass)
{
	UCharacterClassInfo* CharacterClassInfo = GetCharacterClassInfo(WorldContextObject);
	if (CharacterClassInfo == nullptr) return;

	// ── 第一批：所有职业共用技能（CommonAbilities）───────────
	// 例如：受击反应（HitReact）、近战攻击基类等所有角色都有的通用技能。
	// 等级固定为 1（这类技能通常无等级差异）。
	for (TSubclassOf<UGameplayAbility> AbilityClass : CharacterClassInfo->CommonAbilities)
	{
		FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(AbilityClass, 1);
		ASC->GiveAbility(AbilitySpec);
	}

	// ── 第二批：职业专属技能（StartupAbilities）──────────────
	// 敌人的特定攻击技能（如精英巫师的火球）。
	// 技能等级与角色当前等级挂钩，使敌人的技能随等级提升而变强。
	const FCharacterClassDefaultInfo& DefaultInfo = CharacterClassInfo->GetClassDefaultInfo(CharacterClass);
	for (TSubclassOf<UGameplayAbility> AbilityClass : DefaultInfo.StartupAbilities)
	{
		// 通过 ICombatInterface 获取等级，解耦 ASC 与具体角色类实现。
		if (ASC->GetAvatarActor()->Implements<UCombatInterface>())
		{
			FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(AbilityClass, ICombatInterface::Execute_GetPlayerLevel(ASC->GetAvatarActor()));
			ASC->GiveAbility(AbilitySpec);
		}
	}
}

// ─────────────────────────────────────────────────────────────
// GetXPRewardForClassAndLevel — 查询职业等级对应的经验奖励
// ─────────────────────────────────────────────────────────────
int32 UAuraAbilitySystemLibrary::GetXPRewardForClassAndLevel(const UObject* WorldContextObject, ECharacterClass CharacterClass, int32 CharacterLevel)
{
	UCharacterClassInfo* CharacterClassInfo = GetCharacterClassInfo(WorldContextObject);
	if (CharacterClassInfo == nullptr) return 0;

	const FCharacterClassDefaultInfo& Info = CharacterClassInfo->GetClassDefaultInfo(CharacterClass);

	// XPReward 是 FScalableFloat（数据表曲线），随等级非线性增长，
	// 高等级敌人奖励更多经验，驱动玩家挑战更强的敌人。
	const float XPReward = Info.XPReward.GetValueAtLevel(CharacterLevel);

	return static_cast<int32>(XPReward);
}

// ─────────────────────────────────────────────────────────────
// SetIsRadialDamageEffectParam — 批量设置范围伤害参数
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemLibrary::SetIsRadialDamageEffectParam(FDamageEffectParams& DamageEffectParams, bool bIsRadial, float InnerRadius, float OuterRadius, FVector Origin)
{
	// 一次性设置四个相关字段，减少蓝图连线数量。
	DamageEffectParams.bIsRadialDamage = bIsRadial;
	DamageEffectParams.RadialDamageInnerRadius = InnerRadius;
	DamageEffectParams.RadialDamageOuterRadius = OuterRadius;
	DamageEffectParams.RadialDamageOrigin = Origin;
}

// ─────────────────────────────────────────────────────────────
// SetKnockbackDirection — 设置击退方向和力度
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemLibrary::SetKnockbackDirection(FDamageEffectParams& DamageEffectParams, FVector KnockbackDirection, float Magnitude)
{
	// Normalize 后只保留纯方向信息，力度由 KnockbackForceMagnitude 或 Magnitude 决定。
	KnockbackDirection.Normalize();
	if (Magnitude == 0.f)
	{
		// Magnitude 未传入（默认 0）：使用参数包中设计时预设的力度值。
		DamageEffectParams.KnockbackForce = KnockbackDirection * DamageEffectParams.KnockbackForceMagnitude;
	}
	else
	{
		// Magnitude 显式传入：覆盖预设值，适用于需要动态计算力度的场景（如爆炸近距离更强）。
		DamageEffectParams.KnockbackForce = KnockbackDirection * Magnitude;
	}
}

// ─────────────────────────────────────────────────────────────
// SetDeathImpulseDirection — 设置死亡冲量方向和力度
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemLibrary::SetDeathImpulseDirection(FDamageEffectParams& DamageEffectParams, FVector ImpulseDirection, float Magnitude)
{
	ImpulseDirection.Normalize();
	if (Magnitude == 0.f)
	{
		// 使用预设的 DeathImpulseMagnitude（通常比击退力更大，使尸体飞出）。
		DamageEffectParams.DeathImpulse = ImpulseDirection * DamageEffectParams.DeathImpulseMagnitude;
	}
	else
	{
		DamageEffectParams.DeathImpulse = ImpulseDirection * Magnitude;
	}
}

// ─────────────────────────────────────────────────────────────
// SetTargetEffectParamsASC — 替换目标 ASC（多目标复用）
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemLibrary::SetTargetEffectParamsASC(FDamageEffectParams& DamageEffectParams,
	UAbilitySystemComponent* InASC)
{
	// 仅替换目标 ASC，其余参数（伤害类型、数值、冲量等）保持不变。
	// 典型用法：链式闪电遍历每个目标时，只改这一个字段，其余参数复用。
	DamageEffectParams.TargetAbilitySystemComponent = InASC;
}

// ─────────────────────────────────────────────────────────────
// GetCharacterClassInfo — 从 GameMode 获取职业数据资产
// ─────────────────────────────────────────────────────────────
UCharacterClassInfo* UAuraAbilitySystemLibrary::GetCharacterClassInfo(const UObject* WorldContextObject)
{
	// GetGameMode 在 Client 端返回 nullptr，因此此函数只在 Server 端有效。
	// 调用此函数的上下文（属性初始化、技能赋予）本身也是 Server 操作，设计一致。
	const AAuraGameModeBase* AuraGameMode = Cast<AAuraGameModeBase>(UGameplayStatics::GetGameMode(WorldContextObject));
	if (AuraGameMode == nullptr) return nullptr;
	return AuraGameMode->CharacterClassInfo;
}

// ─────────────────────────────────────────────────────────────
// GetAbilityInfo — 从 GameMode 获取技能信息数据资产
// ─────────────────────────────────────────────────────────────
UAbilityInfo* UAuraAbilitySystemLibrary::GetAbilityInfo(const UObject* WorldContextObject)
{
	// 与 GetCharacterClassInfo 同理，Server Only。
	// AbilityInfo 存储所有技能的元数据，是技能状态管理和 UI 描述的数据来源。
	const AAuraGameModeBase* AuraGameMode = Cast<AAuraGameModeBase>(UGameplayStatics::GetGameMode(WorldContextObject));
	if (AuraGameMode == nullptr) return nullptr;
	return AuraGameMode->AbilityInfo;
}

// ─────────────────────────────────────────────────────────────
// GetLootTiers — 从 GameMode 获取战利品掉落配置
// ─────────────────────────────────────────────────────────────
ULootTiers* UAuraAbilitySystemLibrary::GetLootTiers(const UObject* WorldContextObject)
{
	const AAuraGameModeBase* AuraGameMode = Cast<AAuraGameModeBase>(UGameplayStatics::GetGameMode(WorldContextObject));
	if (AuraGameMode == nullptr) return nullptr;
	return AuraGameMode->LootTiers;
}

// ─────────────────────────────────────────────────────────────
// EffectContext 读取器实现
// ─────────────────────────────────────────────────────────────
// 所有读取函数使用 static_cast 而非 Cast<>（UE 的 Cast 用于 UObject，
// FAuraGameplayEffectContext 是结构体，使用 static_cast 是正确的 C++ 做法）。
// 安全性保证：整个项目中所有 EffectContext 都通过 ASC->MakeEffectContext() 创建，
// 而 UAbilitySystemGlobals 中已注册了自定义的 Context 类，确保类型一致。
// ─────────────────────────────────────────────────────────────

bool UAuraAbilitySystemLibrary::IsBlockedHit(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->IsBlockedHit();
	}
	return false;
}

bool UAuraAbilitySystemLibrary::IsSuccessfulDebuff(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->IsSuccessfulDebuff();
	}
	return false;
}

float UAuraAbilitySystemLibrary::GetDebuffDamage(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->GetDebuffDamage();
	}
	return 0.f;
}

float UAuraAbilitySystemLibrary::GetDebuffDuration(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->GetDebuffDuration();
	}
	return 0.f;
}

float UAuraAbilitySystemLibrary::GetDebuffFrequency(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->GetDebuffFrequency();
	}
	return 0.f;
}

FGameplayTag UAuraAbilitySystemLibrary::GetDamageType(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		// DamageType 存储为 TSharedPtr<FGameplayTag>，需要解引用后返回。
		// IsValid() 检查防止空指针（没有设置伤害类型时返回空 Tag）。
		if (AuraEffectContext->GetDamageType().IsValid())
		{
			return *AuraEffectContext->GetDamageType();
		}
	}
	return FGameplayTag();
}

FVector UAuraAbilitySystemLibrary::GetDeathImpulse(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->GetDeathImpulse();
	}
	return FVector::ZeroVector;
}

FVector UAuraAbilitySystemLibrary::GetKnockbackForce(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->GetKnockbackForce();
	}
	return FVector::ZeroVector;
}

bool UAuraAbilitySystemLibrary::IsCriticalHit(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->IsCriticalHit();
	}
	return false;
}

bool UAuraAbilitySystemLibrary::IsRadialDamage(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->IsRadialDamage();
	}
	return false;
}

float UAuraAbilitySystemLibrary::GetRadialDamageInnerRadius(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->GetRadialDamageInnerRadius();
	}
	return 0.f;
}

float UAuraAbilitySystemLibrary::GetRadialDamageOuterRadius(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->GetRadialDamageOuterRadius();
	}
	return 0.f;
}

FVector UAuraAbilitySystemLibrary::GetRadialDamageOrigin(const FGameplayEffectContextHandle& EffectContextHandle)
{
	if (const FAuraGameplayEffectContext* AuraEffectContext = static_cast<const FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		return AuraEffectContext->GetRadialDamageOrigin();
	}
	return FVector::ZeroVector;
}

// ─────────────────────────────────────────────────────────────
// EffectContext 写入器实现
// ─────────────────────────────────────────────────────────────
// 写入器使用非 const 的 static_cast，允许修改 Context 内部数据。
// EffectContextHandle 通过 UPARAM(ref) 传入引用，Handle 本身是浅拷贝安全的，
// 但内部 TSharedPtr<FGameplayEffectContext> 指向同一对象，修改会影响原始 Context。
// ─────────────────────────────────────────────────────────────

void UAuraAbilitySystemLibrary::SetIsBlockedHit(FGameplayEffectContextHandle& EffectContextHandle, bool bInIsBlockedHit)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetIsBlockedHit(bInIsBlockedHit);
	}
}

void UAuraAbilitySystemLibrary::SetIsCriticalHit(FGameplayEffectContextHandle& EffectContextHandle,
	bool bInIsCriticalHit)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetIsCriticalHit(bInIsCriticalHit);
	}
}

void UAuraAbilitySystemLibrary::SetIsSuccessfulDebuff(FGameplayEffectContextHandle& EffectContextHandle,
	bool bInSuccessfulDebuff)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetIsSuccessfulDebuff(bInSuccessfulDebuff);
	}
}

void UAuraAbilitySystemLibrary::SetDebuffDamage(FGameplayEffectContextHandle& EffectContextHandle, float InDamage)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetDebuffDamage(InDamage);
	}
}

void UAuraAbilitySystemLibrary::SetDebuffDuration(FGameplayEffectContextHandle& EffectContextHandle, float InDuration)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetDebuffDuration(InDuration);
	}
}

void UAuraAbilitySystemLibrary::SetDebuffFrequency(FGameplayEffectContextHandle& EffectContextHandle, float InFrequency)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetDebuffFrequency(InFrequency);
	}
}

void UAuraAbilitySystemLibrary::SetDamageType(FGameplayEffectContextHandle& EffectContextHandle,
	const FGameplayTag& InDamageType)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		// 使用 TSharedPtr 存储 FGameplayTag，避免在 Context 传递过程中发生栈对象生命周期问题。
		// 注意：此处每次设置都会创建新的 SharedPtr，但 Context 生命周期内只设置一次，开销可接受。
		const TSharedPtr<FGameplayTag> DamageType = MakeShared<FGameplayTag>(InDamageType);
		AuraEffectContext->SetDamageType(DamageType);
	}
}

void UAuraAbilitySystemLibrary::SetDeathImpulse(FGameplayEffectContextHandle& EffectContextHandle,
	const FVector& InImpulse)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetDeathImpulse(InImpulse);
	}
}

void UAuraAbilitySystemLibrary::SetKnockbackForce(FGameplayEffectContextHandle& EffectContextHandle,
	const FVector& InForce)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetKnockbackForce(InForce);
	}
}

void UAuraAbilitySystemLibrary::SetIsRadialDamage(FGameplayEffectContextHandle& EffectContextHandle,
	bool bInIsRadialDamage)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetIsRadialDamage(bInIsRadialDamage);
	}
}

void UAuraAbilitySystemLibrary::SetRadialDamageInnerRadius(FGameplayEffectContextHandle& EffectContextHandle,
	float InInnerRadius)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetRadialDamageInnerRadius(InInnerRadius);
	}
}

void UAuraAbilitySystemLibrary::SetRadialDamageOuterRadius(FGameplayEffectContextHandle& EffectContextHandle,
	float InOuterRadius)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetRadialDamageOuterRadius(InOuterRadius);
	}
}

void UAuraAbilitySystemLibrary::SetRadialDamageOrigin(FGameplayEffectContextHandle& EffectContextHandle,
	const FVector& InOrigin)
{
	if (FAuraGameplayEffectContext* AuraEffectContext = static_cast<FAuraGameplayEffectContext*>(EffectContextHandle.Get()))
	{
		AuraEffectContext->SetRadialDamageOrigin(InOrigin);
	}
}

// ─────────────────────────────────────────────────────────────
// GetLivePlayersWithinRadius — 球形范围存活目标检测
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemLibrary::GetLivePlayersWithinRadius(const UObject* WorldContextObject,
                                                           TArray<AActor*>& OutOverlappingActors, const TArray<AActor*>& ActorsToIgnore, float Radius,
                                                           const FVector& SphereOrigin)
{
	FCollisionQueryParams SphereParams;
	SphereParams.AddIgnoredActors(ActorsToIgnore);  // 排除施法者自身和已处理过的目标

	if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		TArray<FOverlapResult> Overlaps;
		// 使用 AllDynamicObjects（动态碰撞物体）：排除静态场景，只检测角色、道具等动态对象。
		// FCollisionShape::MakeSphere(Radius)：球形检测区域，效率高于胶囊体/盒体。
		World->OverlapMultiByObjectType(Overlaps, SphereOrigin, FQuat::Identity, FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllDynamicObjects), FCollisionShape::MakeSphere(Radius), SphereParams);
		for (FOverlapResult& Overlap : Overlaps)
		{
			// 双重过滤：
			//   1. 实现了 UCombatInterface（确保是可战斗角色）
			//   2. 未死亡（IsDead 检查，排除尸体）
			if (Overlap.GetActor()->Implements<UCombatInterface>() && !ICombatInterface::Execute_IsDead(Overlap.GetActor()))
			{
				// GetAvatar 返回实际的角色 Actor（有些 Actor 的碰撞组件不在根组件上，
				// 需要通过 CombatInterface 获取真正的 Avatar Actor）。
				// AddUnique 防止同一 Actor 被多个碰撞组件重复添加。
				OutOverlappingActors.AddUnique(ICombatInterface::Execute_GetAvatar(Overlap.GetActor()));
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────
// GetClosestTargets — 选取距原点最近的 N 个目标
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemLibrary::GetClosestTargets(int32 MaxTargets, const TArray<AActor*>& Actors, TArray<AActor*>& OutClosestTargets, const FVector& Origin)
{
	// 快速返回：候选数量不超过上限时全部返回，无需排序。
	if (Actors.Num() <= MaxTargets)
	{
		OutClosestTargets = Actors;
		return;
	}

	TArray<AActor*> ActorsToCheck = Actors;
	int32 NumTargetsFound = 0;

	// 贪心算法：每轮找最近的一个，从候选列表中移除，加入结果，重复 MaxTargets 次。
	// 时间复杂度 O(N * MaxTargets)，对于小规模目标（技能一般 5-10 个）性能可接受。
	while (NumTargetsFound < MaxTargets)
	{
		if (ActorsToCheck.Num() == 0) break;
		double ClosestDistance = TNumericLimits<double>::Max();
		AActor* ClosestActor;
		for (AActor* PotentialTarget : ActorsToCheck)
		{
			const double Distance = (PotentialTarget->GetActorLocation() - Origin).Length();
			if (Distance < ClosestDistance)
			{
				ClosestDistance = Distance;
				ClosestActor = PotentialTarget;
			}
		}
		ActorsToCheck.Remove(ClosestActor);
		OutClosestTargets.AddUnique(ClosestActor);
		++NumTargetsFound;
	}
}

// ─────────────────────────────────────────────────────────────
// IsNotFriend — 敌我阵营判断
// ─────────────────────────────────────────────────────────────
bool UAuraAbilitySystemLibrary::IsNotFriend(AActor* FirstActor, AActor* SecondActor)
{
	// 使用 Actor Tag（"Player" / "Enemy"）判断阵营，简单高效。
	// 两者都是玩家（协同多人）或都是敌人（敌人不互相攻击）= 友方。
	// 注意：此设计假设所有玩家为同一阵营，所有敌人为同一阵营，
	//       若需要更复杂的阵营系统（如 PvP），应扩展此逻辑。
	const bool bBothArePlayers = FirstActor->ActorHasTag(FName("Player")) && SecondActor->ActorHasTag(FName("Player"));
	const bool bBothAreEnemies = FirstActor->ActorHasTag(FName("Enemy")) && SecondActor->ActorHasTag(FName("Enemy"));
	const bool bFriends = bBothArePlayers || bBothAreEnemies;
	return !bFriends;
}

// ─────────────────────────────────────────────────────────────
// ApplyDamageEffect — 伤害效果完整投递流程
// ─────────────────────────────────────────────────────────────
FGameplayEffectContextHandle UAuraAbilitySystemLibrary::ApplyDamageEffect(const FDamageEffectParams& DamageEffectParams)
{
	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();
	const AActor* SourceAvatarActor = DamageEffectParams.SourceAbilitySystemComponent->GetAvatarActor();

	// ── 步骤 1：构建 EffectContext，填充扩展字段 ──────────────
	// EffectContext 会跟随整个 GE 生命周期传递（包括 ExecCalc_Damage 执行时），
	// 因此在创建 Spec 之前，先将所有物理反馈数据写入 Context。
	FGameplayEffectContextHandle EffectContexthandle = DamageEffectParams.SourceAbilitySystemComponent->MakeEffectContext();
	EffectContexthandle.AddSourceObject(SourceAvatarActor);  // 标识伤害来源 Actor
	SetDeathImpulse(EffectContexthandle, DamageEffectParams.DeathImpulse);
	SetKnockbackForce(EffectContexthandle, DamageEffectParams.KnockbackForce);

	// 范围伤害参数：ExecCalc_Damage 中如果 IsRadialDamage 为 true，
	// 会根据目标到 Origin 的距离和内外半径进行伤害衰减计算。
	SetIsRadialDamage(EffectContexthandle, DamageEffectParams.bIsRadialDamage);
	SetRadialDamageInnerRadius(EffectContexthandle, DamageEffectParams.RadialDamageInnerRadius);
	SetRadialDamageOuterRadius(EffectContexthandle, DamageEffectParams.RadialDamageOuterRadius);
	SetRadialDamageOrigin(EffectContexthandle, DamageEffectParams.RadialDamageOrigin);

	// ── 步骤 2：构建 GE Spec，绑定 SetByCaller 数值 ──────────
	// MakeOutgoingSpec 将 GE 类包装成 Spec，带有 Level 和 Context。
	// Level 影响 Spec 中曲线（FScalableFloat）的数值读取。
	const FGameplayEffectSpecHandle SpecHandle = DamageEffectParams.SourceAbilitySystemComponent->MakeOutgoingSpec(DamageEffectParams.DamageGameplayEffectClass, DamageEffectParams.AbilityLevel, EffectContexthandle);

	// AssignTagSetByCallerMagnitude：将运行时数值绑定到 GE 中预设的 SetByCaller Tag，
	// GE 的 Magnitude 计算方式设置为 SetByCaller 时，会在执行时读取这些数值。
	// DamageType Tag：ExecCalc_Damage 根据此 Tag 查找目标的对应抗性属性。
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, DamageEffectParams.DamageType, DamageEffectParams.BaseDamage);
	// 减益相关参数传入 GE，ExecCalc_Damage 读取后决定是否应用减益以及减益的具体数值。
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Debuff_Chance, DamageEffectParams.DebuffChance);
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Debuff_Damage, DamageEffectParams.DebuffDamage);
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Debuff_Duration, DamageEffectParams.DebuffDuration);
	UAbilitySystemBlueprintLibrary::AssignTagSetByCallerMagnitude(SpecHandle, GameplayTags.Debuff_Frequency, DamageEffectParams.DebuffFrequency);

	// ── 步骤 3：将 GE 应用到目标 ──────────────────────────────
	// ApplyGameplayEffectSpecToSelf：目标 ASC 将此 GE 应用到自身，
	// 触发 ExecCalc_Damage 进行最终伤害计算（含格挡、暴击、抗性减免等）。
	DamageEffectParams.TargetAbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data);

	// 返回 EffectContextHandle 供调用方检查（如是否格挡/暴击，用于连锁跳跃判断等）。
	return EffectContexthandle;
}

// ─────────────────────────────────────────────────────────────
// EvenlySpacedRotators — 均匀扩散方向（FRotator 版本）
// ─────────────────────────────────────────────────────────────
TArray<FRotator> UAuraAbilitySystemLibrary::EvenlySpacedRotators(const FVector& Forward, const FVector& Axis, float Spread, int32 NumRotators)
{
	TArray<FRotator> Rotators;

	// 从扩散角最左侧开始（以 Forward 为中心，向左旋转 Spread/2）。
	const FVector LeftOfSpread = Forward.RotateAngleAxis(-Spread / 2.f, Axis);
	if (NumRotators > 1)
	{
		// 多发弹幕：将扩散角均分为 NumRotators-1 个间隔，从左到右依次生成方向。
		// 例如 Spread=90°, NumRotators=3 → 0°、45°、90°（三个方向均匀分布）
		const float DeltaSpread = Spread / (NumRotators - 1);
		for (int32 i = 0; i < NumRotators; i++)
		{
			const FVector Direction = LeftOfSpread.RotateAngleAxis(DeltaSpread * i, FVector::UpVector);
			Rotators.Add(Direction.Rotation());
		}
	}
	else
	{
		// 单发：直接返回 Forward 方向，无需扩散计算。
		Rotators.Add(Forward.Rotation());
	}
	return Rotators;
}

// ─────────────────────────────────────────────────────────────
// EvenlyRotatedVectors — 均匀扩散方向（FVector 版本）
// ─────────────────────────────────────────────────────────────
TArray<FVector> UAuraAbilitySystemLibrary::EvenlyRotatedVectors(const FVector& Forward, const FVector& Axis, float Spread, int32 NumVectors)
{
	TArray<FVector> Vectors;

	// 算法与 EvenlySpacedRotators 完全相同，区别在于直接存储方向向量而非 FRotator。
	// 适用于直接设置弹幕初速度方向而不需要构造 FRotator 的场景。
	const FVector LeftOfSpread = Forward.RotateAngleAxis(-Spread / 2.f, Axis);
	if (NumVectors > 1)
	{
		const float DeltaSpread = Spread / (NumVectors - 1);
		for (int32 i = 0; i < NumVectors; i++)
		{
			const FVector Direction = LeftOfSpread.RotateAngleAxis(DeltaSpread * i, FVector::UpVector);
			Vectors.Add(Direction);
		}
	}
	else
	{
		Vectors.Add(Forward);
	}
	return Vectors;
}
