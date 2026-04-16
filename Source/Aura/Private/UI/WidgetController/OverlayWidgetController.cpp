// Copyright Druid Mechanics


#include "UI/WidgetController/OverlayWidgetController.h"

#include "AuraGameplayTags.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Data/LevelUpInfo.h"
#include "Player/AuraPlayerState.h"

/**
 * 推送血量/魔力当前值快照，让 Widget 在首次显示时立刻呈现正确数值，
 * 而不是默认的 0/空状态。
 * 注意：此处不广播 XP 和等级，因为 XP 需要转换为百分比（由 OnXPChanged 处理），
 * 等级在 Widget 首帧由蓝图通过 GetAuraPS()->GetPlayerLevel() 主动查询即可。
 */
void UOverlayWidgetController::BroadcastInitialValues()
{
	// 直接取 AttributeSet 当前快照值，广播给绑定的血条/魔力条 Widget
	OnHealthChanged.Broadcast(GetAuraAS()->GetHealth());
	OnMaxHealthChanged.Broadcast(GetAuraAS()->GetMaxHealth());
	OnManaChanged.Broadcast(GetAuraAS()->GetMana());
	OnMaxManaChanged.Broadcast(GetAuraAS()->GetMaxMana());
}

/**
 * 绑定所有 HUD 数据的动态更新监听。
 *
 * 绑定顺序说明：
 *   1. PlayerState 的 XP / 等级变化（非 GAS 属性，PlayerState 自己管理）
 *   2. ASC 的属性值变化委托（Health/MaxHealth/Mana/MaxMana）
 *   3. ASC 的技能装备通知（更新技能栏 Widget）
 *   4. ASC 的技能初始化完成通知（BroadcastAbilityInfo）
 *   5. ASC 的 GE 消息标签（消息提示弹窗）
 */
void UOverlayWidgetController::BindCallbacksToDependencies()
{
	// ---- 1. 订阅 PlayerState 的 XP 变化 ----
	// PlayerState 管理 XP，不走 GAS 属性系统，需要单独订阅其自定义委托
	GetAuraPS()->OnXPChangedDelegate.AddUObject(this, &UOverlayWidgetController::OnXPChanged);

	// 订阅等级变化，直接转发给 Widget 委托（保留 bLevelUp 标志供 Widget 判断是否播特效）
	GetAuraPS()->OnLevelChangedDelegate.AddLambda(
		[this](int32 NewLevel, bool bLevelUp)
		{
			OnPlayerLevelChangedDelegate.Broadcast(NewLevel, bLevelUp);
		}
	);

	// ---- 2. 订阅 GAS 属性值变化 ----
	// GetGameplayAttributeValueChangeDelegate 返回属性专用的多播委托，
	// 每次 GameplayEffect 修改该属性后自动触发，传入 FOnAttributeChangeData（含新旧值）

	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(GetAuraAS()->GetHealthAttribute()).AddLambda(
			[this](const FOnAttributeChangeData& Data)
			{
				OnHealthChanged.Broadcast(Data.NewValue);
			}
		);

	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(GetAuraAS()->GetMaxHealthAttribute()).AddLambda(
			[this](const FOnAttributeChangeData& Data)
			{
				OnMaxHealthChanged.Broadcast(Data.NewValue);
			}
		);

	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(GetAuraAS()->GetManaAttribute()).AddLambda(
			[this](const FOnAttributeChangeData& Data)
			{
				OnManaChanged.Broadcast(Data.NewValue);
			}
		);

	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(GetAuraAS()->GetMaxManaAttribute()).AddLambda(
			[this](const FOnAttributeChangeData& Data)
			{
				OnMaxManaChanged.Broadcast(Data.NewValue);
			}
		);

	if (GetAuraASC())
	{
		// ---- 3. 订阅技能装备完成通知 ----
		// 当玩家在法术菜单中把技能拖到快捷栏槽位后，ASC 广播此事件，
		// OverlayWidgetController 响应后更新主 HUD 上的技能球显示
		GetAuraASC()->AbilityEquipped.AddUObject(this, &UOverlayWidgetController::OnAbilityEquipped);

		// ---- 4. 订阅技能初始化完成通知 ----
		// bStartupAbilitiesGiven 可能在绑定之前已经为 true（单人游戏本地加载较快），
		// 也可能在绑定之后才变为 true（网络同步延迟场景）。
		// 两种情况都需要正确处理，确保 BroadcastAbilityInfo 一定被调用一次。
		if (GetAuraASC()->bStartupAbilitiesGiven)
		{
			// 技能已经全部赋予，直接广播
			BroadcastAbilityInfo();
		}
		else
		{
			// 技能尚未赋予，注册回调等待赋予完成后再广播
			GetAuraASC()->AbilitiesGivenDelegate.AddUObject(this, &UOverlayWidgetController::BroadcastAbilityInfo);
		}

		// ---- 5. 订阅 GE 消息标签（道具拾取提示等）----
		// 每当 ASC 应用一个 GameplayEffect 时，EffectAssetTags 委托触发，
		// 传入该 GE 携带的全部 AssetTag 容器。
		// 我们只关心 "Message.*" 前缀的标签，用它在 DataTable 中查找提示配置。
		GetAuraASC()->EffectAssetTags.AddLambda(
			[this](const FGameplayTagContainer& AssetTags)
			{
				for (const FGameplayTag& Tag : AssetTags)
				{
					// 例如，假设 Tag = Message.HealthPotion
					// "Message.HealthPotion".MatchesTag("Message") 将返回 True，"Message".MatchesTag("Message.HealthPotion") 将返回 False
					FGameplayTag MessageTag = FGameplayTag::RequestGameplayTag(FName("Message"));
					if (Tag.MatchesTag(MessageTag))
					{
						// 在 DataTable 中按 Tag 名查找对应行配置，广播给 Widget 弹出提示框
						const FUIWidgetRow* Row = GetDataTableRowByTag<FUIWidgetRow>(MessageWidgetDataTable, Tag);
						MessageWidgetRowDelegate.Broadcast(*Row);
					}
				}
			}
		);
	}
}

/**
 * PlayerState.XP 变化时将原始 XP 值转换为当前等级内的进度百分比。
 *
 * 【计算逻辑】
 *   - 通过 LevelUpInfo 查找当前 XP 对应的等级（Level）
 *   - 取本等级升级门槛（LevelUpRequirement）和上一等级门槛（PreviousLevelUpRequirement）
 *   - 百分比 = (当前XP - 上级门槛) / (本级门槛 - 上级门槛)
 *   例：Level 3 门槛 1000，Level 2 门槛 500，当前 XP 750
 *       → 百分比 = (750 - 500) / (1000 - 500) = 0.5（经验条 50%）
 *
 * 广播 0.0 ~ 1.0 的浮点数给经验条 ProgressBar，Widget 无需了解升级算法细节。
 */
void UOverlayWidgetController::OnXPChanged(int32 NewXP)
{
	const ULevelUpInfo* LevelUpInfo = GetAuraPS()->LevelUpInfo;
	checkf(LevelUpInfo, TEXT("Unabled to find LevelUpInfo. Please fill out AuraPlayerState Blueprint"));

	// 根据总 XP 反查当前等级
	const int32 Level = LevelUpInfo->FindLevelForXP(NewXP);
	const int32 MaxLevel = LevelUpInfo->LevelUpInformation.Num();

	if (Level <= MaxLevel && Level > 0)
	{
		// 本等级升级所需总 XP 门槛
		const int32 LevelUpRequirement = LevelUpInfo->LevelUpInformation[Level].LevelUpRequirement;
		// 上一等级的 XP 门槛（作为本等级起点基准）
		const int32 PreviousLevelUpRequirement = LevelUpInfo->LevelUpInformation[Level - 1].LevelUpRequirement;

		// 本等级区间的 XP 跨度
		const int32 DeltaLevelRequirement = LevelUpRequirement - PreviousLevelUpRequirement;
		// 玩家在本等级区间内已累积的 XP
		const int32 XPForThisLevel = NewXP - PreviousLevelUpRequirement;

		// 归一化为 0~1 百分比，直接驱动经验条填充比例
		const float XPBarPercent = static_cast<float>(XPForThisLevel) / static_cast<float>(DeltaLevelRequirement);

		OnXPPercentChangedDelegate.Broadcast(XPBarPercent);
	}
}

/**
 * 技能装备到槽位完成后，同步更新主 HUD 技能栏的显示。
 *
 * 【为何需要广播两次？】
 *   装备技能可能涉及"技能迁移"：一个已装备到 Slot_A 的技能被移动到 Slot_B。
 *   此时需要：
 *     第一次广播：清空 Slot_A（PreviousSlot）—— 传入 Abilities_None 作为技能Tag，
 *                 StatusTag 设为 Unlocked（保持已解锁状态），告知 Widget 旧槽位变空。
 *     第二次广播：填充 Slot_B（Slot）—— 传入真实 AbilityTag 和新状态。
 *   Widget 收到后根据 InputTag（=槽位Tag）找到对应的技能球并更新图标/状态。
 */
void UOverlayWidgetController::OnAbilityEquipped(const FGameplayTag& AbilityTag, const FGameplayTag& Status, const FGameplayTag& Slot, const FGameplayTag& PreviousSlot) const
{
	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();

	// 构造"清空旧槽位"的信息：技能Tag为空(None)，状态为已解锁，InputTag为旧槽位
	FAuraAbilityInfo LastSlotInfo;
	LastSlotInfo.StatusTag = GameplayTags.Abilities_Status_Unlocked;
	LastSlotInfo.InputTag = PreviousSlot;
	LastSlotInfo.AbilityTag = GameplayTags.Abilities_None;
	// 如果 PreviousSlot 是有效槽位则广播空信息。仅在装备一个已装备的技能时触发
	AbilityInfoDelegate.Broadcast(LastSlotInfo);

	// 广播"填充新槽位"的信息：完整的技能信息 + 新槽位的 InputTag
	FAuraAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);
	Info.StatusTag = Status;
	Info.InputTag = Slot;
	AbilityInfoDelegate.Broadcast(Info);
}
