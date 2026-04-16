// Copyright Druid Mechanics


#include "AbilitySystem/AuraAbilitySystemComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AuraGameplayTags.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystem/Abilities/AuraGameplayAbility.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "Aura/AuraLogChannels.h"
#include "Game/LoadScreenSaveGame.h"
#include "Interaction/PlayerInterface.h"

// ─────────────────────────────────────────────────────────────
// AbilityActorInfoSet
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::AbilityActorInfoSet()
{
	// OnGameplayEffectAppliedDelegateToSelf 在每次 GE 被成功应用到此 ASC 时触发。
	// 我们将其绑定到 ClientEffectApplied，由后者提取 AssetTags 并广播给 UI 消息系统。
	// 注意：此委托在 Server 端触发（GE 应用是 Server 权威操作），
	// ClientEffectApplied 是 Client RPC，会自动送达对应客户端。
	OnGameplayEffectAppliedDelegateToSelf.AddUObject(this, &UAuraAbilitySystemComponent::ClientEffectApplied);

}

// ─────────────────────────────────────────────────────────────
// AddCharacterAbilitiesFromSaveData — 从存档恢复技能
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::AddCharacterAbilitiesFromSaveData(ULoadScreenSaveGame* SaveData)
{
	// 遍历存档中保存的每条技能记录，重建 AbilitySpec 并恢复所有运行时状态。
	for (const FSavedAbility& Data : SaveData->SavedAbilities)
	{
		const TSubclassOf<UGameplayAbility> LoadedAbilityClass = Data.GameplayAbility;

		// 用存档中的等级创建 Spec，保证角色技能等级与存档一致。
		FGameplayAbilitySpec LoadedAbilitySpec = FGameplayAbilitySpec(LoadedAbilityClass, Data.AbilityLevel);

		// 将槽位（InputTag）和状态（StatusTag）写回 DynamicAbilityTags，
		// 这两个 Tag 是本项目扩展的运行时信息，需要从存档手动恢复。
		LoadedAbilitySpec.DynamicAbilityTags.AddTag(Data.AbilitySlot);
		LoadedAbilitySpec.DynamicAbilityTags.AddTag(Data.AbilityStatus);

		// 主动技能：直接 GiveAbility，等待玩家输入触发。
		if (Data.AbilityType == FAuraGameplayTags::Get().Abilities_Type_Offensive)
		{
			GiveAbility(LoadedAbilitySpec);
		}
		// 被动技能：若存档中已是 Equipped 状态，需要立即激活（恢复持续效果），
		// 同时通过 NetMulticast 广播视觉特效激活事件，确保所有客户端看到光环等效果。
		else if (Data.AbilityType == FAuraGameplayTags::Get().Abilities_Type_Passive)
		{
			if (Data.AbilityStatus.MatchesTagExact(FAuraGameplayTags::Get().Abilities_Status_Equipped))
			{
				// GiveAbilityAndActivateOnce：赋予并立即在 Server 激活一次；
				// 被动技能内部有持续等待逻辑，激活后会保持运行直到手动取消。
				GiveAbilityAndActivateOnce(LoadedAbilitySpec);
				MulticastActivatePassiveEffect(Data.AbilityTag, true);
			}
			else
			{
				// 未装备的被动技能：仅赋予，不激活，等待玩家后续装备操作。
				GiveAbility(LoadedAbilitySpec);
			}
		}
	}

	// 标记技能已赋予完毕，广播委托通知 UI（法术菜单等）可以安全读取技能列表。
	bStartupAbilitiesGiven = true;
	AbilitiesGivenDelegate.Broadcast();
}

// ─────────────────────────────────────────────────────────────
// AddCharacterAbilities — 批量赋予主动技能（全新游戏路径）
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::AddCharacterAbilities(const TArray<TSubclassOf<UGameplayAbility>>& StartupAbilities)
{
	for (const TSubclassOf<UGameplayAbility> AbilityClass : StartupAbilities)
	{
		// 初始等级为 1；等级会随 ServerSpendSpellPoint 逐步提升。
		FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(AbilityClass, 1);

		if (const UAuraGameplayAbility* AuraAbility = Cast<UAuraGameplayAbility>(AbilitySpec.Ability))
		{
			// 将技能在设计时指定的默认输入 Tag 写入 DynamicAbilityTags，
			// 使得按下对应按键时可以通过 AbilityInputTagHeld 找到并激活此技能。
			AbilitySpec.DynamicAbilityTags.AddTag(AuraAbility->StartupInputTag);

			// 出生就配备的主动技能直接标记为 Equipped 状态，无需玩家手动装备。
			AbilitySpec.DynamicAbilityTags.AddTag(FAuraGameplayTags::Get().Abilities_Status_Equipped);
			GiveAbility(AbilitySpec);
		}
	}

	// 设置标志位并广播，防止 OnRep_ActivateAbilities 时重复广播。
	bStartupAbilitiesGiven = true;
	AbilitiesGivenDelegate.Broadcast();
}

// ─────────────────────────────────────────────────────────────
// AddCharacterPassiveAbilities — 批量赋予并激活被动技能
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::AddCharacterPassiveAbilities(const TArray<TSubclassOf<UGameplayAbility>>& StartupPassiveAbilities)
{
	for (const TSubclassOf<UGameplayAbility> AbilityClass : StartupPassiveAbilities)
	{
		FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(AbilityClass, 1);

		// 被动技能出生即装备，直接标记为 Equipped。
		AbilitySpec.DynamicAbilityTags.AddTag(FAuraGameplayTags::Get().Abilities_Status_Equipped);

		// GiveAbilityAndActivateOnce：Server 端赋予并立即激活。
		// 被动技能（如 AuraPassiveAbility）内部通过 WaitActivate Task 保持持续运行，
		// 不会因"激活一次"后就结束，而是一直保持激活状态直到被显式停用。
		GiveAbilityAndActivateOnce(AbilitySpec);
	}
}

// ─────────────────────────────────────────────────────────────
// AbilityInputTagPressed — 按键按下
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid()) return;

	// FScopedAbilityListLock：锁定 ActivatableAbilities 列表，防止遍历过程中
	// 因技能激活/取消/赋予导致的迭代器失效（GAS 内部机制）。
	FScopedAbilityListLock ActiveScopeLoc(*this);
	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		// 使用 HasTagExact 精确匹配：避免父 Tag "InputTag" 误匹配所有子 Tag。
		if (AbilitySpec.DynamicAbilityTags.HasTagExact(InputTag))
		{
			// 通知 GAS 此 Spec 收到了输入按下事件，更新 Spec 内部的输入计数器，
			// 使技能内的 WaitInputPress 异步任务能够响应。
			AbilitySpecInputPressed(AbilitySpec);

			// 若技能已经处于激活状态（如持续施法中），通过 InvokeReplicatedEvent
			// 将"输入按下"事件同步到 Server，确保 Server 侧的技能逻辑也能感知输入。
			if (AbilitySpec.IsActive())
			{
				InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, AbilitySpec.Handle, AbilitySpec.ActivationInfo.GetActivationPredictionKey());
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────
// AbilityInputTagHeld — 按键持续按住（每 Tick）
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::AbilityInputTagHeld(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid()) return;
	FScopedAbilityListLock ActiveScopeLoc(*this);
	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilitySpec.DynamicAbilityTags.HasTagExact(InputTag))
		{
			// 每 Tick 都通知 GAS"输入仍然按住"。
			AbilitySpecInputPressed(AbilitySpec);

			// 若技能尚未激活，则尝试激活：TryActivateAbility 内部会做 CoolDown/Cost 检查，
			// 以及网络预测（Client 先预测激活，Server 确认/回滚）。
			// 已激活的技能不重复调用 TryActivate，避免重复激活。
			if (!AbilitySpec.IsActive())
			{
				TryActivateAbility(AbilitySpec.Handle);
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────
// AbilityInputTagReleased — 按键松开
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid()) return;
	FScopedAbilityListLock ActiveScopeLoc(*this);
	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		// 仅对"绑定了此 InputTag 且当前处于激活状态"的技能发送松开事件。
		// 未激活的技能收到松开事件没有意义。
		if (AbilitySpec.DynamicAbilityTags.HasTagExact(InputTag) && AbilitySpec.IsActive())
		{
			// 通知 GAS 此 Spec 的输入已释放，更新内部输入状态计数器。
			AbilitySpecInputReleased(AbilitySpec);

			// 将"输入松开"事件同步到 Server，使 Server 侧 WaitInputRelease 任务能响应。
			// 例如：火球蓄力技能"按住蓄力，松开释放"就依赖此事件触发发射逻辑。
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, AbilitySpec.Handle, AbilitySpec.ActivationInfo.GetActivationPredictionKey());
		}
	}
}

// ─────────────────────────────────────────────────────────────
// ForEachAbility — 安全遍历技能列表
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::ForEachAbility(const FForEachAbility& Delegate)
{
	// 锁定列表，遍历期间新赋予/移除的技能会被延迟处理。
	FScopedAbilityListLock ActiveScopeLock(*this);
	for (const FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		// ExecuteIfBound 返回 false 说明 Delegate 未绑定（调用方传了空委托），记录错误。
		if (!Delegate.ExecuteIfBound(AbilitySpec))
		{
			UE_LOG(LogAura, Error, TEXT("Failed to execute delegate in %hs"), __FUNCTION__);
		}
	}
}

// ─────────────────────────────────────────────────────────────
// GetAbilityTagFromSpec — 提取技能身份 Tag
// ─────────────────────────────────────────────────────────────
FGameplayTag UAuraAbilitySystemComponent::GetAbilityTagFromSpec(const FGameplayAbilitySpec& AbilitySpec)
{
	if (AbilitySpec.Ability)
	{
		// AbilityTags 是技能 CDO（类默认对象）上配置的静态标签，
		// 遍历找到父 Tag 为 "Abilities" 的那个，即技能身份 Tag（如 Abilities.Fire.FireBolt）。
		for (FGameplayTag Tag : AbilitySpec.Ability.Get()->AbilityTags)
		{
			if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Abilities"))))
			{
				return Tag;
			}
		}
	}
	return FGameplayTag();
}

// ─────────────────────────────────────────────────────────────
// GetInputTagFromSpec — 提取输入（槽位）Tag
// ─────────────────────────────────────────────────────────────
FGameplayTag UAuraAbilitySystemComponent::GetInputTagFromSpec(const FGameplayAbilitySpec& AbilitySpec)
{
	// InputTag 存于 DynamicAbilityTags（运行时写入），父 Tag 为 "InputTag"。
	// 每个 Spec 最多只有一个 InputTag（装备到一个槽位）。
	for (FGameplayTag Tag : AbilitySpec.DynamicAbilityTags)
	{
		if (Tag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("InputTag"))))
		{
			return Tag;
		}
	}
	return FGameplayTag();
}

// ─────────────────────────────────────────────────────────────
// GetStatusFromSpec — 提取状态 Tag
// ─────────────────────────────────────────────────────────────
FGameplayTag UAuraAbilitySystemComponent::GetStatusFromSpec(const FGameplayAbilitySpec& AbilitySpec)
{
	// 状态 Tag 也存于 DynamicAbilityTags，父 Tag 为 "Abilities.Status"。
	// 四种可能：Locked / Eligible / Unlocked / Equipped。
	// 状态机流转：(赋予时) Eligible → (花费法术点) Unlocked → (装备到槽位) Equipped
	for (FGameplayTag StatusTag : AbilitySpec.DynamicAbilityTags)
	{
		if (StatusTag.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Abilities.Status"))))
		{
			return StatusTag;
		}
	}
	return FGameplayTag();
}

// ─────────────────────────────────────────────────────────────
// GetStatusFromAbilityTag / GetSlotFromAbilityTag — 组合查询
// ─────────────────────────────────────────────────────────────
FGameplayTag UAuraAbilitySystemComponent::GetStatusFromAbilityTag(const FGameplayTag& AbilityTag)
{
	// 先通过技能 Tag 找到 Spec，再从 Spec 提取状态 Tag。
	// 若技能尚未赋予（Spec 不存在），返回空 Tag。
	if (const FGameplayAbilitySpec* Spec = GetSpecFromAbilityTag(AbilityTag))
	{
		return GetStatusFromSpec(*Spec);
	}
	return FGameplayTag();
}

FGameplayTag UAuraAbilitySystemComponent::GetSlotFromAbilityTag(const FGameplayTag& AbilityTag)
{
	// 槽位本质上就是 InputTag，复用 GetInputTagFromSpec 实现。
	if (const FGameplayAbilitySpec* Spec = GetSpecFromAbilityTag(AbilityTag))
	{
		return GetInputTagFromSpec(*Spec);
	}
	return FGameplayTag();
}

// ─────────────────────────────────────────────────────────────
// SlotIsEmpty — 检查槽位是否空闲
// ─────────────────────────────────────────────────────────────
bool UAuraAbilitySystemComponent::SlotIsEmpty(const FGameplayTag& Slot)
{
	FScopedAbilityListLock ActiveScopeLoc(*this);
	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		// 只要有任意一个技能占用了该 Slot，就返回 false。
		if (AbilityHasSlot(AbilitySpec, Slot))
		{
			return false;
		}
	}
	return true;
}

// AbilityHasSlot（引用版）：精确匹配指定 Slot Tag。
bool UAuraAbilitySystemComponent::AbilityHasSlot(const FGameplayAbilitySpec& Spec, const FGameplayTag& Slot)
{
	return Spec.DynamicAbilityTags.HasTagExact(Slot);
}

// AbilityHasAnySlot：判断技能是否绑定了任意输入槽位（即是否已装备）。
// 使用 HasTag（层级匹配），任何以 "InputTag" 为父级的 Tag 都会命中。
bool UAuraAbilitySystemComponent::AbilityHasAnySlot(const FGameplayAbilitySpec& Spec)
{
	return Spec.DynamicAbilityTags.HasTag(FGameplayTag::RequestGameplayTag(FName("InputTag")));
}

// ─────────────────────────────────────────────────────────────
// GetSpecWithSlot — 查找占用指定槽位的 AbilitySpec
// ─────────────────────────────────────────────────────────────
FGameplayAbilitySpec* UAuraAbilitySystemComponent::GetSpecWithSlot(const FGameplayTag& Slot)
{
	FScopedAbilityListLock ActiveScopeLock(*this);
	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilitySpec.DynamicAbilityTags.HasTagExact(Slot))
		{
			return &AbilitySpec;
		}
	}
	return nullptr;
}

// ─────────────────────────────────────────────────────────────
// IsPassiveAbility — 判断是否为被动技能
// ─────────────────────────────────────────────────────────────
bool UAuraAbilitySystemComponent::IsPassiveAbility(const FGameplayAbilitySpec& Spec) const
{
	// 通过全局 AbilityInfo 数据资产（挂在 GameMode 上）查找技能类型。
	// 不在 AbilitySpec 或 AbilityClass 上直接存储类型，是为了数据驱动：
	// 技能类型配置在数据表中，修改类型无需重编译。
	const UAbilityInfo* AbilityInfo = UAuraAbilitySystemLibrary::GetAbilityInfo(GetAvatarActor());
	const FGameplayTag AbilityTag = GetAbilityTagFromSpec(Spec);
	const FAuraAbilityInfo& Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);
	const FGameplayTag AbilityType = Info.AbilityType;
	return AbilityType.MatchesTagExact(FAuraGameplayTags::Get().Abilities_Type_Passive);
}

// ─────────────────────────────────────────────────────────────
// AssignSlotToAbility — 清除旧槽位并写入新槽位
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::AssignSlotToAbility(FGameplayAbilitySpec& Spec, const FGameplayTag& Slot)
{
	// 先清除已有的 InputTag（若有），防止一个技能同时持有多个槽位 Tag。
	ClearSlot(&Spec);
	Spec.DynamicAbilityTags.AddTag(Slot);
}

// ─────────────────────────────────────────────────────────────
// MulticastActivatePassiveEffect — 被动特效同步（NetMulticast）
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::MulticastActivatePassiveEffect_Implementation(const FGameplayTag& AbilityTag, bool bActivate)
{
	// 广播给 Avatar Actor 上注册的监听者（通常是角色蓝图中的 Niagara/粒子组件），
	// 由它们负责根据 bActivate 显示或隐藏对应的被动技能视觉特效。
	ActivatePassiveEffect.Broadcast(AbilityTag, bActivate);
}

// ─────────────────────────────────────────────────────────────
// GetSpecFromAbilityTag — 通过技能 Tag 查找 AbilitySpec
// ─────────────────────────────────────────────────────────────
FGameplayAbilitySpec* UAuraAbilitySystemComponent::GetSpecFromAbilityTag(const FGameplayTag& AbilityTag)
{
	FScopedAbilityListLock ActiveScopeLoc(*this);
	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		// 遍历技能 CDO 的 AbilityTags，使用 MatchesTag（层级匹配）。
		for (FGameplayTag Tag : AbilitySpec.Ability.Get()->AbilityTags)
		{
			if (Tag.MatchesTag(AbilityTag))
			{
				return &AbilitySpec;
			}
		}
	}
	return nullptr;
}

// ─────────────────────────────────────────────────────────────
// UpgradeAttribute — 属性升级入口（Client 侧调用）
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::UpgradeAttribute(const FGameplayTag& AttributeTag)
{
	// 仅当玩家实现了 IPlayerInterface 且有剩余属性点时，才发起 Server RPC。
	// 客户端前置校验减少无效 RPC，但真正的权威校验在 Server 端（ServerUpgradeAttribute）。
	if (GetAvatarActor()->Implements<UPlayerInterface>())
	{
		if (IPlayerInterface::Execute_GetAttributePoints(GetAvatarActor()) > 0)
		{
			ServerUpgradeAttribute(AttributeTag);
		}
	}
}

// ─────────────────────────────────────────────────────────────
// ServerUpgradeAttribute_Implementation — 服务器端属性升级
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::ServerUpgradeAttribute_Implementation(const FGameplayTag& AttributeTag)
{
	// 构造 GameplayEventData，通过事件系统触发对应的属性升级 Ability。
	// 这种"事件触发 Ability"的模式优于直接修改属性，因为：
	//   1. Ability 可以附带动画/音效/粒子
	//   2. 升级逻辑（应用哪个 GE）完全由 Ability 的蓝图决定，设计者可自由配置
	//   3. 满足 GAS 的 Server 权威原则
	FGameplayEventData Payload;
	Payload.EventTag = AttributeTag;
	Payload.EventMagnitude = 1.f;  // 每次升级 1 点

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(GetAvatarActor(), AttributeTag, Payload);

	// 升级成功后扣除 1 个属性点（-1）。
	// AddToAttributePoints 通过 IPlayerInterface 接口调用，解耦了 ASC 与具体角色类。
	if (GetAvatarActor()->Implements<UPlayerInterface>())
	{
		IPlayerInterface::Execute_AddToAttributePoints(GetAvatarActor(), -1);
	}
}

// ─────────────────────────────────────────────────────────────
// UpdateAbilityStatuses — 根据等级解锁新技能（Eligible 状态）
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::UpdateAbilityStatuses(int32 Level)
{
	// 从 GameMode 持有的 AbilityInfo 数据资产获取所有技能的配置信息。
	UAbilityInfo* AbilityInfo = UAuraAbilitySystemLibrary::GetAbilityInfo(GetAvatarActor());

	for (const FAuraAbilityInfo& Info : AbilityInfo->AbilityInformation)
	{
		if (!Info.AbilityTag.IsValid()) continue;

		// 若角色等级不满足技能的等级需求，跳过。
		if (Level < Info.LevelRequirement) continue;

		// 若技能已经赋予（Spec 已存在），也跳过（避免重复赋予）。
		if (GetSpecFromAbilityTag(Info.AbilityTag) == nullptr)
		{
			// 首次满足等级条件：赋予技能，初始状态为 Eligible（可花费法术点解锁）。
			FGameplayAbilitySpec AbilitySpec = FGameplayAbilitySpec(Info.Ability, 1);
			AbilitySpec.DynamicAbilityTags.AddTag(FAuraGameplayTags::Get().Abilities_Status_Eligible);
			GiveAbility(AbilitySpec);

			// MarkAbilitySpecDirty：立即触发 Spec 复制（不等待下一帧批量复制）。
			MarkAbilitySpecDirty(AbilitySpec);

			// 通过 Client RPC 显式通知 UI 刷新（因为 DynamicAbilityTags 变化不保证触发 OnRep）。
			ClientUpdateAbilityStatus(Info.AbilityTag, FAuraGameplayTags::Get().Abilities_Status_Eligible, 1);
		}
	}
}

// ─────────────────────────────────────────────────────────────
// ServerSpendSpellPoint_Implementation — 消费法术点升级技能
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::ServerSpendSpellPoint_Implementation(const FGameplayTag& AbilityTag)
{
	if (FGameplayAbilitySpec* AbilitySpec = GetSpecFromAbilityTag(AbilityTag))
	{
		// 消耗法术点（-1），在状态修改前先扣除，防止因后续逻辑异常导致"免费升级"。
		if (GetAvatarActor()->Implements<UPlayerInterface>())
		{
			IPlayerInterface::Execute_AddToSpellPoints(GetAvatarActor(), -1);
		}

		const FAuraGameplayTags GameplayTags = FAuraGameplayTags::Get();
		FGameplayTag Status = GetStatusFromSpec(*AbilitySpec);

		// 状态机流转：
		// Eligible（可解锁）→ 花费法术点 → Unlocked（已解锁，可装备但未绑定输入）
		if (Status.MatchesTagExact(GameplayTags.Abilities_Status_Eligible))
		{
			AbilitySpec->DynamicAbilityTags.RemoveTag(GameplayTags.Abilities_Status_Eligible);
			AbilitySpec->DynamicAbilityTags.AddTag(GameplayTags.Abilities_Status_Unlocked);
			Status = GameplayTags.Abilities_Status_Unlocked;
		}
		// Unlocked 或 Equipped → 花费法术点 → 技能等级 +1（升级已解锁的技能）
		else if (Status.MatchesTagExact(GameplayTags.Abilities_Status_Equipped) || Status.MatchesTagExact(GameplayTags.Abilities_Status_Unlocked))
		{
			AbilitySpec->Level += 1;
		}

		// 通知 Client 刷新 UI，传递最新状态和等级。
		ClientUpdateAbilityStatus(AbilityTag, Status, AbilitySpec->Level);

		// 标记 Spec 为脏数据，触发复制。
		MarkAbilitySpecDirty(*AbilitySpec);
	}
}

// ─────────────────────────────────────────────────────────────
// ServerEquipAbility_Implementation — 服务器端技能装备
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::ServerEquipAbility_Implementation(const FGameplayTag& AbilityTag, const FGameplayTag& Slot)
{
	if (FGameplayAbilitySpec* AbilitySpec = GetSpecFromAbilityTag(AbilityTag))
	{
		const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();
		const FGameplayTag& PrevSlot = GetInputTagFromSpec(*AbilitySpec);   // 记录旧槽位，用于 UI 清除旧图标
		const FGameplayTag& Status = GetStatusFromSpec(*AbilitySpec);

		// 只有 Equipped 或 Unlocked 状态的技能才能被装备（Locked/Eligible 不能装备）。
		const bool bStatusValid = Status == GameplayTags.Abilities_Status_Equipped || Status == GameplayTags.Abilities_Status_Unlocked;
		if (bStatusValid)
		{
			// 处理被动技能的激活/停用

			// ── 步骤 1：处理目标槽位冲突 ──────────────────────────────
			if (!SlotIsEmpty(Slot)) // 该槽位已有技能，需要先处理冲突
			{
				FGameplayAbilitySpec* SpecWithSlot = GetSpecWithSlot(Slot);
				if (SpecWithSlot)
				{
					// 若目标槽位的技能就是自己，说明已经装备了，直接通知 Client 并返回。
					// （这是"重复装备到相同槽位"的幂等保护）
					if (AbilityTag.MatchesTagExact(GetAbilityTagFromSpec(*SpecWithSlot)))
					{
						ClientEquipAbility(AbilityTag, GameplayTags.Abilities_Status_Equipped, Slot, PrevSlot);
						return;
					}

					// 若槽位中的旧技能是被动技能，需要先停用它（取消持续效果和视觉特效）。
					if (IsPassiveAbility(*SpecWithSlot))
					{
						// NetMulticast 广播视觉特效停用（所有客户端同步）。
						MulticastActivatePassiveEffect(GetAbilityTagFromSpec(*SpecWithSlot), false);
						// 广播给被动技能的 WaitForEvent 任务，使其主动 EndAbility。
						DeactivatePassiveAbility.Broadcast(GetAbilityTagFromSpec(*SpecWithSlot));
					}

					// 清除旧技能的槽位绑定（移除其 InputTag），使其变为 Unlocked 但无槽位状态。
					ClearSlot(SpecWithSlot);
				}
			}

			// ── 步骤 2：激活被动技能（若首次装备）─────────────────────
			// AbilityHasAnySlot 为 false 说明技能之前没有槽位，即"首次装备"。
			if (!AbilityHasAnySlot(*AbilitySpec)) // 技能尚未分配槽位（未激活状态）
			{
				if (IsPassiveAbility(*AbilitySpec))
				{
					// 首次装备被动技能时激活它（开始持续效果）。
					TryActivateAbility(AbilitySpec->Handle);
					// 广播视觉特效激活事件。
					MulticastActivatePassiveEffect(AbilityTag, true);
				}
				// 状态从 Unlocked 升级为 Equipped（移除旧状态 Tag，写入新状态 Tag）。
				AbilitySpec->DynamicAbilityTags.RemoveTag(GetStatusFromSpec(*AbilitySpec));
				AbilitySpec->DynamicAbilityTags.AddTag(GameplayTags.Abilities_Status_Equipped);
			}

			// ── 步骤 3：分配新槽位并复制 ─────────────────────────────
			AssignSlotToAbility(*AbilitySpec, Slot);
			MarkAbilitySpecDirty(*AbilitySpec);
		}

		// 无论是否操作成功，都通知 Client 更新 UI（包含技能 Tag、状态、新旧槽位）。
		ClientEquipAbility(AbilityTag, GameplayTags.Abilities_Status_Equipped, Slot, PrevSlot);
	}
}

// ─────────────────────────────────────────────────────────────
// ClientEquipAbility_Implementation — 通知 Client UI 装备完成
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::ClientEquipAbility_Implementation(const FGameplayTag& AbilityTag, const FGameplayTag& Status, const FGameplayTag& Slot, const FGameplayTag& PreviousSlot)
{
	// 广播委托，由 SpellMenuWidgetController 和 OverlayWidgetController 订阅，
	// 分别更新法术菜单的选中高亮，以及 HUD 技能栏的图标位置。
	// PreviousSlot 使 UI 知道哪个旧槽位需要清空图标。
	AbilityEquipped.Broadcast(AbilityTag, Status, Slot, PreviousSlot);
}

// ─────────────────────────────────────────────────────────────
// GetDescriptionsByAbilityTag — 获取技能描述文本
// ─────────────────────────────────────────────────────────────
bool UAuraAbilitySystemComponent::GetDescriptionsByAbilityTag(const FGameplayTag& AbilityTag, FString& OutDescription,FString& OutNextLevelDescription)
{
	if (const FGameplayAbilitySpec* AbilitySpec = GetSpecFromAbilityTag(AbilityTag))
	{
		// 技能已赋予：通过多态虚函数获取当前等级和下一等级的本地化描述。
		// GetDescription / GetNextLevelDescription 在每个具体技能类中重写，
		// 可以动态生成包含伤害数值、冷却时间等的描述文本。
		if(UAuraGameplayAbility* AuraAbility = Cast<UAuraGameplayAbility>(AbilitySpec->Ability))
		{
			OutDescription = AuraAbility->GetDescription(AbilitySpec->Level);
			OutNextLevelDescription = AuraAbility->GetNextLevelDescription(AbilitySpec->Level + 1);
			return true;
		}
	}

	// 技能尚未赋予（Locked 状态）：显示解锁等级要求的提示文本。
	const UAbilityInfo* AbilityInfo = UAuraAbilitySystemLibrary::GetAbilityInfo(GetAvatarActor());
	if (!AbilityTag.IsValid() || AbilityTag.MatchesTagExact(FAuraGameplayTags::Get().Abilities_None))
	{
		// 无效 Tag 或空技能槽：显示空字符串。
		OutDescription = FString();
	}
	else
	{
		// 显示"需要 X 级才能解锁"文本，引导玩家了解技能需求。
		OutDescription = UAuraGameplayAbility::GetLockedDescription(AbilityInfo->FindAbilityInfoForTag(AbilityTag).LevelRequirement);
	}
	OutNextLevelDescription = FString();
	return false;
}

// ─────────────────────────────────────────────────────────────
// ClearSlot — 移除 Spec 上的输入槽位 Tag
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::ClearSlot(FGameplayAbilitySpec* Spec)
{
	// 获取当前绑定的 InputTag 并从 DynamicAbilityTags 中移除。
	// 移除后技能仍然存在于 ActivatableAbilities，只是不再响应任何输入。
	const FGameplayTag Slot = GetInputTagFromSpec(*Spec);
	Spec->DynamicAbilityTags.RemoveTag(Slot);
}

// ─────────────────────────────────────────────────────────────
// ClearAbilitiesOfSlot — 批量清除某槽位上绑定的所有技能
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::ClearAbilitiesOfSlot(const FGameplayTag& Slot)
{
	FScopedAbilityListLock ActiveScopeLock(*this);
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (AbilityHasSlot(&Spec, Slot))
		{
			ClearSlot(&Spec);
		}
	}
}

// AbilityHasSlot（指针版）：通过遍历 DynamicAbilityTags 精确匹配，
// 与引用版功能相同，仅接口不同（接受裸指针）。
bool UAuraAbilitySystemComponent::AbilityHasSlot(FGameplayAbilitySpec* Spec, const FGameplayTag& Slot)
{
	for (FGameplayTag Tag : Spec->DynamicAbilityTags)
	{
		if (Tag.MatchesTagExact(Slot))
		{
			return true;
		}
	}
	return false;
}

// ─────────────────────────────────────────────────────────────
// OnRep_ActivateAbilities — 技能列表 OnRep 回调（Client）
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::OnRep_ActivateAbilities()
{
	Super::OnRep_ActivateAbilities();

	// 当 Client 首次收到 ActivatableAbilities 的复制数据时（!bStartupAbilitiesGiven），
	// 触发 AbilitiesGivenDelegate，通知 UI 控制器技能数据已就绪可以读取。
	// bStartupAbilitiesGiven 防止因列表后续更新（如新赋予技能）再次触发委托。
	if (!bStartupAbilitiesGiven)
	{
		bStartupAbilitiesGiven = true;
		AbilitiesGivenDelegate.Broadcast();
	}
}

// ─────────────────────────────────────────────────────────────
// ClientUpdateAbilityStatus_Implementation — 技能状态变化通知
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::ClientUpdateAbilityStatus_Implementation(const FGameplayTag& AbilityTag, const FGameplayTag& StatusTag, int32 AbilityLevel)
{
	// 广播 AbilityStatusChanged 委托，法术菜单 WidgetController 订阅此委托，
	// 根据新状态 Tag 更新对应技能槽的视觉状态（解锁效果、可升级提示、等级数字等）。
	AbilityStatusChanged.Broadcast(AbilityTag, StatusTag, AbilityLevel);
}

// ─────────────────────────────────────────────────────────────
// ClientEffectApplied_Implementation — GE 应用通知（Client）
// ─────────────────────────────────────────────────────────────
void UAuraAbilitySystemComponent::ClientEffectApplied_Implementation(UAbilitySystemComponent* AbilitySystemComponent,
                                                                     const FGameplayEffectSpec& EffectSpec, FActiveGameplayEffectHandle ActiveEffectHandle)
{
	// 从 EffectSpec 提取 AssetTags（设计者手动配置的分类标签，如 "Message.HealthPotion"）。
	// AssetTags 与 GrantedTags 不同：GrantedTags 会应用到目标身上，AssetTags 仅用于标识/分类。
	// 广播给 OverlayWidgetController，后者根据 Tag 查找 DataTable 中的消息行（图标、文字、音效）。
	FGameplayTagContainer TagContainer;
	EffectSpec.GetAllAssetTags(TagContainer);

	EffectAssetTags.Broadcast(TagContainer);
}
