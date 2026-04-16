// Copyright Druid Mechanics

#pragma once

/**
 * ============================================================
 * SpellMenuWidgetController — 法术菜单的数据控制器
 * ============================================================
 *
 * 负责驱动法术菜单的所有交互逻辑：技能球选择、消费法术点升级技能、
 * 将技能装备到快捷栏槽位。
 *
 * 【法术菜单选择状态机】
 *
 *   初始状态：SelectedAbility = {Abilities_None, Abilities_Status_Locked}
 *              bWaitingForEquipSelection = false
 *                          ↓
 *   点击技能球 SpellGlobeSelected(AbilityTag)
 *                          ↓
 *   更新 SelectedAbility.Ability / .Status
 *   调用 ShouldEnableButtons 计算按钮可用性
 *   广播 SpellGlobeSelectedDelegate（含按钮状态+技能描述）给 Widget
 *                          ↓
 *         ┌────────────────┴─────────────────┐
 *         ↓                                  ↓
 *  点击"消费点数"按钮                    点击"装备"按钮
 *  SpendPointButtonPressed()            EquipButtonPressed()
 *         ↓                                  ↓
 *  ServerSpendSpellPoint()          bWaitingForEquipSelection = true
 *  服务器升级技能                    广播 WaitForEquipDelegate（高亮槽位行）
 *  触发 AbilityStatusChanged              ↓
 *  → 更新 SelectedAbility.Status    点击快捷栏槽位球
 *  → 重新广播按钮状态               SpellRowGlobePressed(SlotTag, AbilityType)
 *                                         ↓
 *                                   类型匹配检查
 *                                         ↓
 *                                   ServerEquipAbility()
 *                                         ↓
 *                                   服务器装备完成
 *                                         ↓
 *                                   AbilityEquipped 委托触发
 *                                         ↓
 *                                   OnAbilityEquipped() 处理
 *                                   bWaitingForEquipSelection = false
 *                                   广播 StopWaitingForEquipDelegate
 *                                   广播 SpellGlobeReassignedDelegate
 *                                   GlobeDeselect()（重置选中状态）
 *
 * 【ShouldEnableButtons 按钮可用性规则】
 *
 *   状态\点数      点数 > 0    点数 = 0
 *   ──────────────────────────────────
 *   Locked         禁用/禁用   禁用/禁用
 *   Eligible       启用/禁用   禁用/禁用  （可解锁但无法装备未解锁技能）
 *   Unlocked       启用/启用   禁用/启用  （已解锁可升级可装备）
 *   Equipped       启用/启用   禁用/启用  （已装备可升级可换槽）
 *
 *   列顺序：消费点数按钮 / 装备按钮
 * ============================================================
 */

#include "CoreMinimal.h"
#include "AuraGameplayTags.h"
#include "UI/WidgetController/AuraWidgetController.h"
#include "GameplayTagContainer.h"
#include "SpellMenuWidgetController.generated.h"

/**
 * 技能球选中结果委托（一次广播包含全部 UI 所需数据，避免 Widget 多次查询）：
 *   bSpendPointsButtonEnabled  —— 消费点数按钮是否可用
 *   bEquipButtonEnabled        —— 装备按钮是否可用
 *   DescriptionString          —— 当前等级技能描述文本
 *   NextLevelDescriptionString —— 下一等级技能描述文本（点数不足时置灰提示）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FSpellGlobeSelectedSignature, bool, bSpendPointsButtonEnabled, bool, bEquipButtonEnabled, FString, DescriptionString, FString, NextLevelDescriptionString);

/**
 * 进入/退出"等待槽位选择"模式的委托。
 * 参数 AbilityType 用于 Widget 判断应高亮哪一行槽位（主动技能行 or 被动技能行），
 * 避免将攻击技能误装备到被动槽位。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWaitForEquipSelectionSignature, const FGameplayTag&, AbilityType);

/**
 * 技能被重新分配到新槽位时广播，通知 Widget 更新原槽位和新槽位的显示状态。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpellGlobeReassignedSignature, const FGameplayTag&, AbilityTag);

/**
 * FSelectedAbility — 当前在法术菜单中选中的技能快照
 * 用于在点击"消费点数"/"装备"按钮时知道操作的目标技能及其当前状态。
 */
struct FSelectedAbility
{
	FGameplayTag Ability = FGameplayTag();   // 选中的技能 Tag（默认 Abilities_None）
	FGameplayTag Status = FGameplayTag();    // 选中技能的当前状态 Tag（默认 Locked）
};

/**
 * USpellMenuWidgetController — 法术菜单控制器
 */
UCLASS(BlueprintType, Blueprintable)
class AURA_API USpellMenuWidgetController : public UAuraWidgetController
{
	GENERATED_BODY()
public:
	/**
	 * 初始化推送：广播所有技能信息（技能球初始状态）+ 当前法术点数。
	 */
	virtual void BroadcastInitialValues() override;

	/**
	 * 绑定技能状态变化、法术点变化、技能装备完成的监听。
	 */
	virtual void BindCallbacksToDependencies() override;

	// ---- 对外广播委托（Widget 绑定这些委托驱动 UI 状态变化）----

	/** 法术点数变化委托（升级获得点数 / 消费点数时触发）*/
	UPROPERTY(BlueprintAssignable)
	FOnPlayerStatChangedSignature SpellPointsChanged;

	/**
	 * 技能球被选中时广播。
	 * 传递按钮可用性 + 技能描述，Widget 据此更新详情面板和操作按钮状态。
	 * 也在点数变化时重新广播（点数变化可能影响按钮可用性）。
	 */
	UPROPERTY(BlueprintAssignable)
	FSpellGlobeSelectedSignature SpellGlobeSelectedDelegate;

	/**
	 * 进入"等待槽位选择"模式时广播。
	 * Widget 收到后高亮对应类型的槽位行，提示玩家选择目标槽位。
	 */
	UPROPERTY(BlueprintAssignable)
	FWaitForEquipSelectionSignature WaitForEquipDelegate;

	/**
	 * 退出"等待槽位选择"模式时广播。
	 * 场景：取消装备操作（点击其他技能球 / 取消选中）或装备成功完成后。
	 */
	UPROPERTY(BlueprintAssignable)
	FWaitForEquipSelectionSignature StopWaitingForEquipDelegate;

	/**
	 * 技能被重新分配到槽位后广播，通知 Widget 刷新对应技能球的高亮/动画状态。
	 */
	UPROPERTY(BlueprintAssignable)
	FSpellGlobeReassignedSignature SpellGlobeReassignedDelegate;

	// ---- 蓝图可调用的交互接口 ----

	/**
	 * 玩家点击法术球时调用。
	 * 若当前处于"等待槽位选择"模式，先退出该模式再处理新选择。
	 * 更新 SelectedAbility，计算按钮可用性，广播详情信息。
	 */
	UFUNCTION(BlueprintCallable)
	void SpellGlobeSelected(const FGameplayTag& AbilityTag);

	/**
	 * 玩家点击"消费点数"按钮时调用。
	 * 通过 ServerRPC 在服务器消费一个法术点，升级当前选中技能。
	 * 升级完成后服务器触发 AbilityStatusChanged 委托，客户端收到更新。
	 */
	UFUNCTION(BlueprintCallable)
	void SpendPointButtonPressed();

	/**
	 * 取消当前技能球选中状态。
	 * 若处于"等待槽位选择"模式，同时退出该模式并广播停止等待委托。
	 * 重置 SelectedAbility 为默认（None/Locked），广播空选中状态给 Widget。
	 */
	UFUNCTION(BlueprintCallable)
	void GlobeDeselect();

	/**
	 * 玩家点击"装备"按钮时调用。
	 * 进入"等待槽位选择"模式（bWaitingForEquipSelection = true），
	 * 广播 WaitForEquipDelegate 让 Widget 高亮可用槽位行。
	 * 若当前技能已装备，记录其当前槽位（SelectedSlot）供后续使用。
	 */
	UFUNCTION(BlueprintCallable)
	void EquipButtonPressed();

	/**
	 * 玩家点击快捷栏槽位球时调用（仅在 bWaitingForEquipSelection == true 时有效）。
	 * 检查技能类型与槽位类型是否匹配，匹配则调用 ServerEquipAbility。
	 * @param SlotTag      目标槽位的 InputTag（如 InputTag_1、InputTag_2 等）
	 * @param AbilityType  目标槽位支持的技能类型（主动/被动）
	 */
	UFUNCTION(BlueprintCallable)
	void SpellRowGlobePressed(const FGameplayTag& SlotTag, const FGameplayTag& AbilityType);

	/**
	 * 技能装备完成的回调（由 AuraASC.AbilityEquipped 委托触发）。
	 * 处理装备成功后的全部 UI 清理工作：
	 *   - 退出等待装备模式
	 *   - 清空旧槽位 / 填充新槽位信息
	 *   - 广播 SpellGlobeReassigned（更新技能球高亮）
	 *   - 取消选中状态
	 */
	void OnAbilityEquipped(const FGameplayTag& AbilityTag, const FGameplayTag& Status, const FGameplayTag& Slot, const FGameplayTag& PreviousSlot);

private:

	/**
	 * 根据技能状态和当前法术点数，判断两个操作按钮的可用性。
	 * 静态函数，无需访问成员状态，便于复用。
	 *
	 * 【判断规则】
	 *   Equipped  ：装备按钮始终可用；点数>0 时消费按钮可用
	 *   Eligible  ：只有点数>0 时消费按钮可用；装备按钮禁用（未解锁无法装备）
	 *   Unlocked  ：装备按钮始终可用；点数>0 时消费按钮可用
	 *   Locked    ：两个按钮均禁用
	 *
	 * @param AbilityStatus               当前技能状态 Tag
	 * @param SpellPoints                 当前可用法术点数
	 * @param bShouldEnableSpellPointsButton  输出：消费点数按钮是否启用
	 * @param bShouldEnableEquipButton        输出：装备按钮是否启用
	 */
	static void ShouldEnableButtons(const FGameplayTag& AbilityStatus, int32 SpellPoints, bool& bShouldEnableSpellPointsButton, bool& bShouldEnableEquipButton);

	/** 当前选中的技能及其状态（状态机核心数据）*/
	FSelectedAbility SelectedAbility = { FAuraGameplayTags::Get().Abilities_None,  FAuraGameplayTags::Get().Abilities_Status_Locked };

	/** 缓存的当前法术点数（点数变化时同步更新，供 ShouldEnableButtons 使用）*/
	int32 CurrentSpellPoints = 0;

	/**
	 * 是否处于"等待槽位选择"模式。
	 * true  —— 玩家点击了装备按钮，正在等待选择目标槽位
	 * false —— 正常浏览/选择技能状态
	 */
	bool bWaitingForEquipSelection = false;

	/**
	 * 当前选中技能已装备的槽位（仅当技能状态为 Equipped 时有效）。
	 * 在 EquipButtonPressed 中记录，用于装备到新槽位时的旧槽位清理。
	 */
	FGameplayTag SelectedSlot;
};
