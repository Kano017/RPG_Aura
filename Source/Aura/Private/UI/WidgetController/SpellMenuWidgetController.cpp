// Copyright Druid Mechanics


#include "UI/WidgetController/SpellMenuWidgetController.h"

#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "Player/AuraPlayerState.h"

/**
 * 法术菜单打开时的初始化推送：
 *   1. BroadcastAbilityInfo —— 遍历所有技能，发送图标/状态/槽位信息给 Widget，
 *      让所有技能球呈现正确的初始状态（锁定/可解锁/已解锁/已装备）
 *   2. SpellPointsChanged   —— 推送当前法术点数，初始化顶部法术点显示
 */
void USpellMenuWidgetController::BroadcastInitialValues()
{
	BroadcastAbilityInfo();
	SpellPointsChanged.Broadcast(GetAuraPS()->GetSpellPoints());
}

/**
 * 绑定法术菜单所需的所有动态更新监听：
 *   - AbilityStatusChanged：技能升级后状态改变（Eligible → Unlocked），更新按钮和描述
 *   - AbilityEquipped：技能装备完成，更新槽位显示并退出等待模式
 *   - OnSpellPointsChangedDelegate：点数变化时重新计算按钮可用性
 */
void USpellMenuWidgetController::BindCallbacksToDependencies()
{
	// ---- 技能状态变化监听 ----
	// 当 ServerSpendSpellPoint 在服务器执行完毕，技能状态从 Eligible 升级到 Unlocked，
	// 或者从 Unlocked 升级时等级提升，ASC 会触发此委托（客户端通过 GAS 复制收到）
	GetAuraASC()->AbilityStatusChanged.AddLambda([this](const FGameplayTag& AbilityTag, const FGameplayTag& StatusTag, int32 NewLevel)
	{
		// 如果变化的技能正是当前选中的技能，需要同步更新 SelectedAbility.Status
		// 并重新广播详情面板（按钮状态可能发生变化，例如 Eligible→Unlocked 后装备按钮变可用）
		if (SelectedAbility.Ability.MatchesTagExact(AbilityTag))
		{
			SelectedAbility.Status = StatusTag;
			bool bEnableSpendPoints = false;
			bool bEnableEquip = false;
			ShouldEnableButtons(StatusTag, CurrentSpellPoints, bEnableSpendPoints, bEnableEquip);
			FString Description;
			FString NextLevelDescription;
			GetAuraASC()->GetDescriptionsByAbilityTag(AbilityTag, Description, NextLevelDescription);
			SpellGlobeSelectedDelegate.Broadcast(bEnableSpendPoints, bEnableEquip, Description, NextLevelDescription);
		}

		// 无论是否是当前选中技能，都广播新的技能信息给所有技能球 Widget，
		// 确保技能球图标/状态指示器（如锁定图标）能及时刷新
		if (AbilityInfo)
		{
			FAuraAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);
			Info.StatusTag = StatusTag;
			AbilityInfoDelegate.Broadcast(Info);
		}
	});

	// ---- 技能装备完成监听 ----
	GetAuraASC()->AbilityEquipped.AddUObject(this, &USpellMenuWidgetController::OnAbilityEquipped);

	// ---- 法术点数变化监听 ----
	// 点数变化来自：玩家升级获得点数（PlayerState.AddToSpellPoints），
	// 或消费点数后减少（ServerSpendSpellPoint）
	GetAuraPS()->OnSpellPointsChangedDelegate.AddLambda([this](int32 SpellPoints)
	{
		// 广播新点数给 Widget（更新顶部法术点数字显示）
		SpellPointsChanged.Broadcast(SpellPoints);
		CurrentSpellPoints = SpellPoints;

		// 点数变化可能影响按钮可用性（例如从 0 变为 1，消费按钮应变为可用），
		// 因此重新计算并广播当前选中技能的按钮状态
		bool bEnableSpendPoints = false;
		bool bEnableEquip = false;
		ShouldEnableButtons(SelectedAbility.Status, CurrentSpellPoints, bEnableSpendPoints, bEnableEquip);
		FString Description;
		FString NextLevelDescription;
		GetAuraASC()->GetDescriptionsByAbilityTag(SelectedAbility.Ability, Description, NextLevelDescription);
		SpellGlobeSelectedDelegate.Broadcast(bEnableSpendPoints, bEnableEquip, Description, NextLevelDescription);
	});
}

/**
 * 玩家点击技能球时的处理逻辑。
 *
 * 【等待装备模式的打断处理】
 *   若当前处于"等待槽位选择"模式（用户之前点击了"装备"按钮），
 *   点击其他技能球应视为取消装备操作：
 *   广播 StopWaitingForEquipDelegate（取消槽位行高亮），重置等待标志。
 *
 * 【技能状态获取策略】
 *   - AbilityTag 无效 or 为 None or 在 ASC 中找不到 Spec → 视为 Locked
 *   - 否则从 Spec 的 DynamicAbilityTags 中读取状态 Tag
 */
void USpellMenuWidgetController::SpellGlobeSelected(const FGameplayTag& AbilityTag)
{
	// 若处于等待槽位选择模式，先取消等待，再处理新的技能选择
	if (bWaitingForEquipSelection)
	{
		const FGameplayTag SelectedAbilityType = AbilityInfo->FindAbilityInfoForTag(AbilityTag).AbilityType;
		StopWaitingForEquipDelegate.Broadcast(SelectedAbilityType);
		bWaitingForEquipSelection = false;
	}

	const FAuraGameplayTags GameplayTags = FAuraGameplayTags::Get();
	const int32 SpellPoints = GetAuraPS()->GetSpellPoints();
	FGameplayTag AbilityStatus;

	// 判断 AbilityTag 是否指向一个有效的、已在 ASC 中注册的技能
	const bool bTagValid = AbilityTag.IsValid();
	const bool bTagNone = AbilityTag.MatchesTag(GameplayTags.Abilities_None);
	const FGameplayAbilitySpec* AbilitySpec = GetAuraASC()->GetSpecFromAbilityTag(AbilityTag);
	const bool bSpecValid = AbilitySpec != nullptr;

	if (!bTagValid || bTagNone || !bSpecValid)
	{
		// 无效技能（空槽位或未配置的 Tag），视为锁定状态
		AbilityStatus = GameplayTags.Abilities_Status_Locked;
	}
	else
	{
		// 从 AbilitySpec 的动态标签中读取当前状态
		AbilityStatus = GetAuraASC()->GetStatusFromSpec(*AbilitySpec);
	}

	// 更新选中状态
	SelectedAbility.Ability = AbilityTag;
	SelectedAbility.Status = AbilityStatus;

	// 计算按钮可用性并广播详情（技能描述 + 下一级描述 + 按钮状态）
	bool bEnableSpendPoints = false;
	bool bEnableEquip = false;
	ShouldEnableButtons(AbilityStatus, SpellPoints, bEnableSpendPoints, bEnableEquip);
	FString Description;
	FString NextLevelDescription;
	GetAuraASC()->GetDescriptionsByAbilityTag(AbilityTag, Description, NextLevelDescription);
	SpellGlobeSelectedDelegate.Broadcast(bEnableSpendPoints, bEnableEquip, Description, NextLevelDescription);
}

/**
 * 消费一个法术点升级当前选中技能。
 * ServerSpendSpellPoint 是 ServerRPC，仅在服务器执行，确保游戏逻辑的权威性。
 * 服务器执行后：
 *   - 技能等级 +1（如果是首次解锁则状态从 Eligible 变为 Unlocked）
 *   - PlayerState.SpellPoints -= 1
 * 这两个变化都会触发对应委托，BindCallbacksToDependencies 中的 Lambda 自动处理 UI 更新。
 */
void USpellMenuWidgetController::SpendPointButtonPressed()
{
	if (GetAuraASC())
	{
		GetAuraASC()->ServerSpendSpellPoint(SelectedAbility.Ability);
	}
}

/**
 * 取消当前技能球选中，重置法术菜单到空选中状态。
 *
 * 若当前处于等待槽位选择模式，同步退出该模式（广播 StopWaitingForEquipDelegate）。
 * 广播 SpellGlobeSelectedDelegate 传入全 false / 空字符串，
 * 让 Widget 隐藏详情面板并禁用所有操作按钮。
 */
void USpellMenuWidgetController::GlobeDeselect()
{
	// 若处于等待装备模式，广播停止等待（取消槽位行高亮）
	if (bWaitingForEquipSelection)
	{
		const FGameplayTag SelectedAbilityType = AbilityInfo->FindAbilityInfoForTag(SelectedAbility.Ability).AbilityType;
		StopWaitingForEquipDelegate.Broadcast(SelectedAbilityType);
		bWaitingForEquipSelection = false;
	}

	// 重置选中状态到初始值
	SelectedAbility.Ability = FAuraGameplayTags::Get().Abilities_None;
	SelectedAbility.Status = FAuraGameplayTags::Get().Abilities_Status_Locked;

	// 广播空选中状态，让 Widget 清空详情面板并禁用按钮
	SpellGlobeSelectedDelegate.Broadcast(false, false, FString(), FString());
}

/**
 * 玩家点击"装备"按钮时进入"等待槽位选择"模式。
 *
 * 1. 获取当前选中技能的类型（主动/被动），用于 Widget 高亮对应类型的槽位行
 * 2. 广播 WaitForEquipDelegate（携带技能类型），Widget 收到后对对应行的槽位球添加高亮效果
 * 3. 若技能已装备（Equipped 状态），记录 SelectedSlot，
 *    以便装备到新槽位时可以清空原来的槽位
 */
void USpellMenuWidgetController::EquipButtonPressed()
{
	// 获取技能类型（主动/被动），用于控制哪一行槽位可被选择
	const FGameplayTag AbilityType = AbilityInfo->FindAbilityInfoForTag(SelectedAbility.Ability).AbilityType;

	// 通知 Widget 进入等待装备模式并高亮对应类型的槽位
	WaitForEquipDelegate.Broadcast(AbilityType);
	bWaitingForEquipSelection = true;

	// 若技能已装备，记录当前所在槽位（OnAbilityEquipped 中需要清空该槽位）
	const FGameplayTag SelectedStatus = GetAuraASC()->GetStatusFromAbilityTag(SelectedAbility.Ability);
	if (SelectedStatus.MatchesTagExact(FAuraGameplayTags::Get().Abilities_Status_Equipped))
	{
		SelectedSlot = GetAuraASC()->GetSlotFromAbilityTag(SelectedAbility.Ability);
	}
}

/**
 * 玩家在"等待槽位选择"模式下点击目标槽位球时调用。
 *
 * 【防御性检查】
 *   - 若不在等待模式则直接返回（避免误触）
 *   - 检查技能类型与槽位类型是否匹配，防止将主动技能装到被动槽或反之
 *
 * 通过检查后调用 ServerEquipAbility，服务器完成装备后广播 AbilityEquipped 委托，
 * 触发 OnAbilityEquipped 进行后续 UI 清理。
 */
void USpellMenuWidgetController::SpellRowGlobePressed(const FGameplayTag& SlotTag, const FGameplayTag& AbilityType)
{
	// 未处于等待装备模式，忽略此次点击
	if (!bWaitingForEquipSelection) return;

	// 检查所选技能是否与槽位的技能类型匹配。
	// （不要将攻击性技能装备到被动槽位，反之亦然）
	const FGameplayTag& SelectedAbilityType = AbilityInfo->FindAbilityInfoForTag(SelectedAbility.Ability).AbilityType;
	if (!SelectedAbilityType.MatchesTagExact(AbilityType)) return;

	// 类型匹配，发起服务器装备请求
	GetAuraASC()->ServerEquipAbility(SelectedAbility.Ability, SlotTag);
}

/**
 * 技能装备完成后的全量 UI 清理与更新。
 * 此函数由 AbilityEquipped 委托触发（服务器完成装备 → GAS 复制 → 客户端回调）。
 *
 * 【执行顺序】
 *   1. 退出等待槽位选择模式（bWaitingForEquipSelection = false）
 *   2. 广播"清空旧槽位"信息（若技能原本已装备在某个槽位，清空那个槽位）
 *   3. 广播"填充新槽位"信息（新槽位显示技能图标 + 状态）
 *   4. 广播 StopWaitingForEquipDelegate（取消槽位行高亮）
 *   5. 广播 SpellGlobeReassignedDelegate（通知技能球更新外观/动画）
 *   6. GlobeDeselect()（重置选中状态，为下一次操作做准备）
 */
void USpellMenuWidgetController::OnAbilityEquipped(const FGameplayTag& AbilityTag, const FGameplayTag& Status, const FGameplayTag& Slot, const FGameplayTag& PreviousSlot)
{
	// 装备完成，退出等待装备模式
	bWaitingForEquipSelection = false;

	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();

	// 清空旧槽位：发送"空技能"信息给旧槽位，Widget 据此将其还原为空白状态
	FAuraAbilityInfo LastSlotInfo;
	LastSlotInfo.StatusTag = GameplayTags.Abilities_Status_Unlocked;
	LastSlotInfo.InputTag = PreviousSlot;
	LastSlotInfo.AbilityTag = GameplayTags.Abilities_None;
	// 如果 PreviousSlot 是有效槽位则广播空信息。仅在装备一个已装备的技能时触发
	AbilityInfoDelegate.Broadcast(LastSlotInfo);

	// 填充新槽位：发送完整技能信息到新槽位，Widget 显示技能图标和状态指示器
	FAuraAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);
	Info.StatusTag = Status;
	Info.InputTag = Slot;
	AbilityInfoDelegate.Broadcast(Info);

	// 通知 Widget 停止等待装备动画（取消槽位行高亮效果）
	StopWaitingForEquipDelegate.Broadcast(AbilityInfo->FindAbilityInfoForTag(AbilityTag).AbilityType);
	// 通知技能球 Widget 技能已被分配，可播放分配完成动画
	SpellGlobeReassignedDelegate.Broadcast(AbilityTag);
	// 重置选中状态（清空详情面板，禁用操作按钮）
	GlobeDeselect();
}

/**
 * 根据技能当前状态和可用法术点，输出两个操作按钮的可用性。
 *
 * 【设计为静态函数的原因】
 *   此逻辑只依赖传入参数，不访问任何成员变量，
 *   设为 static 明确表达其无副作用的纯计算性质，也方便单元测试。
 *
 * 【状态与按钮可用性对照表】
 *   Equipped  → 装备按钮：可用；消费按钮：点数>0 时可用
 *   Eligible  → 消费按钮：点数>0 时可用；装备按钮：不可用（未解锁无法装备）
 *   Unlocked  → 装备按钮：可用；消费按钮：点数>0 时可用
 *   Locked    → 两个按钮均不可用
 */
void USpellMenuWidgetController::ShouldEnableButtons(const FGameplayTag& AbilityStatus, int32 SpellPoints, bool& bShouldEnableSpellPointsButton, bool& bShouldEnableEquipButton)
{
	const FAuraGameplayTags GameplayTags = FAuraGameplayTags::Get();

	// 默认全部禁用，根据状态选择性开启
	bShouldEnableSpellPointsButton = false;
	bShouldEnableEquipButton = false;

	if (AbilityStatus.MatchesTagExact(GameplayTags.Abilities_Status_Equipped))
	{
		// 已装备：可以重新装备到其他槽位，有点数时也可以升级
		bShouldEnableEquipButton = true;
		if (SpellPoints > 0)
		{
			bShouldEnableSpellPointsButton = true;
		}
	}
	else if (AbilityStatus.MatchesTagExact(GameplayTags.Abilities_Status_Eligible))
	{
		// 可解锁但未解锁：只能消费点数解锁，无法装备（尚未解锁的技能不能放到快捷栏）
		if (SpellPoints > 0)
		{
			bShouldEnableSpellPointsButton = true;
		}
	}
	else if (AbilityStatus.MatchesTagExact(GameplayTags.Abilities_Status_Unlocked))
	{
		// 已解锁但未装备：可以装备到快捷栏，有点数时也可以升级
		bShouldEnableEquipButton = true;
		if (SpellPoints > 0)
		{
			bShouldEnableSpellPointsButton = true;
		}
	}
	// Locked 状态：两个按钮均不启用（默认值 false，无需额外处理）
}
