// Copyright Druid Mechanics

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AuraAbilitySystemComponent.generated.h"

class ULoadScreenSaveGame;

// ============================================================
// UAuraAbilitySystemComponent — GAS 组件的项目级扩展
// ============================================================
// 为何需要派生这个子类？
//   UAbilitySystemComponent（ASC）是 GAS 的核心，负责持有技能列表、
//   属性集、已激活的 GameplayEffect 等数据，并处理技能的激活/取消。
//   但原生 ASC 不提供：
//     1. 自定义输入 Tag → Ability 的映射（原生用 int32 InputID）
//     2. 技能状态管理（Locked/Eligible/Unlocked/Equipped 四态机）
//     3. 法术点升级、技能装备槽位的 Server RPC 链
//     4. 面向 UI 的委托广播（GE 应用通知、技能赋予完成通知等）
//   本类统一扩展上述能力，是整个技能系统的"枢纽"。
//
// 网络架构：
//   玩家角色的 ASC 挂在 AAuraPlayerState 上（随 PlayerState 跨关卡持久化）。
//   敌人的 ASC 挂在自身 AuraEnemy 上。
//   技能激活走 GAS 内置的预测机制（Client 侧预测激活，Server 确认）。
//   技能状态变更（花费法术点、装备技能）使用 Server Reliable RPC，
//   结果再通过 Client Reliable RPC 回调通知 UI。
// ============================================================

// ─── 委托声明 ───────────────────────────────────────────────

// GE 被应用时广播其 AssetTag 容器，供 UI 消息系统（飘字、状态图标）订阅。
// 触发时机：OnGameplayEffectAppliedDelegateToSelf 在 Client 端回调（见 ClientEffectApplied）。
// 订阅方：OverlayWidgetController 监听此委托，根据 Tag 显示对应消息。
DECLARE_MULTICAST_DELEGATE_OneParam(FEffectAssetTags, const FGameplayTagContainer& /*AssetTags*/);

// 初始技能批量赋予完成后广播，通知 UI（如法术菜单）可以安全读取技能列表。
// 之所以需要这个委托：在网络环境下，Client 的 ActivatableAbilities 通过
// OnRep_ActivateAbilities 复制到来，时序不确定；UI 控制器可能先于技能到达
// 而尝试读取，本委托确保 UI 等到技能真正就绪后再刷新。
DECLARE_MULTICAST_DELEGATE(FAbilitiesGiven);

// 遍历技能列表时使用的单次委托（非多播），用于安全地对每个 AbilitySpec 执行操作。
// 设计为 Delegate 而非直接 lambda，方便蓝图/C++ 统一调用并记录错误日志。
DECLARE_DELEGATE_OneParam(FForEachAbility, const FGameplayAbilitySpec&);

// 技能状态发生变化时广播（如从 Eligible → Unlocked），参数包含技能 Tag、
// 新状态 Tag 和当前等级。UI 的法术菜单订阅此委托来刷新按钮样式/文本。
// 触发路径：Server 修改 Spec → ClientUpdateAbilityStatus RPC → 广播本委托。
DECLARE_MULTICAST_DELEGATE_ThreeParams(FAbilityStatusChanged, const FGameplayTag& /*AbilityTag*/, const FGameplayTag& /*StatusTag*/, int32 /*AbilityLevel*/);

// 技能被成功装备到槽位时广播，携带技能 Tag、新状态、目标槽位、以及之前槽位。
// "之前槽位"用于让 UI 清除旧槽位的图标显示。
// 触发路径：ServerEquipAbility → ClientEquipAbility RPC → 广播本委托。
DECLARE_MULTICAST_DELEGATE_FourParams(FAbilityEquipped, const FGameplayTag& /*AbilityTag*/, const FGameplayTag& /*Status*/, const FGameplayTag& /*Slot*/, const FGameplayTag& /*PrevSlot*/);

// 被动技能停用时广播（例如从槽位移除时），通知被动技能自身结束持续效果。
// 被动技能通过 WaitForEvent 等异步任务监听此委托，收到后主动 EndAbility。
DECLARE_MULTICAST_DELEGATE_OneParam(FDeactivatePassiveAbility, const FGameplayTag& /*AbilityTag*/);

// 被动技能视觉特效激活/停用事件，广播给角色 Actor 上的粒子/Niagara 组件，
// 控制持续特效的显示与隐藏（如护盾光晕）。
// 使用 NetMulticast 确保所有客户端同步看到效果（见 MulticastActivatePassiveEffect）。
DECLARE_MULTICAST_DELEGATE_TwoParams(FActivatePassiveEffect, const FGameplayTag& /*AbilityTag*/, bool /*bActivate*/);

/**
 * UAuraAbilitySystemComponent
 *
 * 项目的 AbilitySystemComponent 扩展类，在原生 GAS 基础上增加：
 *   - 基于 GameplayTag 的输入处理（替代原生 int32 InputID）
 *   - 技能四状态管理（Locked/Eligible/Unlocked/Equipped）
 *   - DynamicAbilityTags 中同时存储 InputTag / StatusTag / SlotTag 三类信息
 *   - 法术点消费与属性点升级的 Server 权威 RPC 链
 *   - 面向 UI 的多播委托系统
 */
UCLASS()
class AURA_API UAuraAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
public:

	// ──────────────────────────────────────────────────────────
	// 初始化
	// ──────────────────────────────────────────────────────────

	/**
	 * 在 AbilityActorInfo 设置完成后调用（即 Avatar Actor 已绑定时）。
	 * 此时 ASC 已知晓自己服务的 Actor，可以安全绑定各种委托。
	 * 具体做法：将 OnGameplayEffectAppliedDelegateToSelf 绑定到
	 * ClientEffectApplied，使 GE 应用事件能传递给 UI 消息系统。
	 * 调用方：AAuraCharacter::OnRep_PlayerState() 和 AuraEnemy 的初始化流程。
	 */
	void AbilityActorInfoSet();

	// ──────────────────────────────────────────────────────────
	// 对外暴露的委托（UI 等外部系统订阅）
	// ──────────────────────────────────────────────────────────

	/** GE 应用时携带 AssetTags 广播，OverlayWidgetController 订阅以显示飘字/状态 */
	FEffectAssetTags EffectAssetTags;

	/** 初始技能赋予完成（含存档加载路径），UI 控制器订阅后安全读取技能列表 */
	FAbilitiesGiven AbilitiesGivenDelegate;

	/** 技能状态变化（花费法术点时），法术菜单 WidgetController 订阅刷新 UI */
	FAbilityStatusChanged AbilityStatusChanged;

	/** 技能成功装备到槽位，法术菜单和 HUD 技能栏订阅以更新图标 */
	FAbilityEquipped AbilityEquipped;

	/** 被动技能需要停用时广播，被动技能的异步任务（WaitForEvent）监听此委托 */
	FDeactivatePassiveAbility DeactivatePassiveAbility;

	/** 被动技能视觉特效开关，NetMulticast 后在 Avatar Actor 上触发粒子显隐 */
	FActivatePassiveEffect ActivatePassiveEffect;

	// ──────────────────────────────────────────────────────────
	// 技能赋予
	// ──────────────────────────────────────────────────────────

	/**
	 * 从存档数据恢复技能列表（存档加载路径）。
	 * 与 AddCharacterAbilities 的区别：此函数从 FSavedAbility 重建
	 * AbilitySpec，包括恢复 AbilityLevel、SlotTag、StatusTag。
	 * 被动技能若为 Equipped 状态，则直接 GiveAbilityAndActivateOnce 并
	 * 广播 MulticastActivatePassiveEffect 恢复视觉特效。
	 * 调用方：AAuraCharacter::OnRep_PlayerState（存档加载时）。
	 */
	void AddCharacterAbilitiesFromSaveData(ULoadScreenSaveGame* SaveData);

	/**
	 * 批量赋予主动（攻击/工具）技能，仅在首次初始化时调用。
	 * 流程：
	 *   1. 遍历 StartupAbilities 数组，每个 Class 创建 Level=1 的 AbilitySpec
	 *   2. 将 UAuraGameplayAbility::StartupInputTag 写入 DynamicAbilityTags（绑定按键）
	 *   3. 写入 Abilities_Status_Equipped Tag（直接可用）
	 *   4. GiveAbility 注册到 ASC
	 * 完成后设置 bStartupAbilitiesGiven=true 并广播 AbilitiesGivenDelegate。
	 * 仅在 Server 端调用（GAS 要求技能赋予必须在 Server 上执行）。
	 */
	void AddCharacterAbilities(const TArray<TSubclassOf<UGameplayAbility>>& StartupAbilities);

	/**
	 * 批量赋予并立即激活被动技能（如持续光环效果）。
	 * 使用 GiveAbilityAndActivateOnce：赋予后立即在 Server 激活一次，
	 * 被动技能内部通过 WaitActivate 等异步任务保持持续运行。
	 * 被动技能的 StatusTag 初始即为 Equipped（出生时就装备）。
	 */
	void AddCharacterPassiveAbilities(const TArray<TSubclassOf<UGameplayAbility>>& StartupPassiveAbilities);

	/**
	 * 标记位：初始技能是否已赋予完毕。
	 * 必要性：网络环境下 Client 侧通过 OnRep_ActivateAbilities 接收技能列表，
	 * 其时序早于或晚于 UI 初始化均有可能。通过此标志，
	 * OnRep_ActivateAbilities 中可判断是否需要广播 AbilitiesGivenDelegate，
	 * 避免重复广播（Server 已广播一次，Client 收到 OnRep 时再广播一次）。
	 */
	bool bStartupAbilitiesGiven = false;

	// ──────────────────────────────────────────────────────────
	// 输入处理（Tag 驱动的 GAS 输入映射）
	// ──────────────────────────────────────────────────────────
	// 整体架构：
	//   AuraPlayerController 捕获 EnhancedInput 事件
	//     → 调用 AbilityInputTagPressed/Held/Released(InputTag)
	//     → 本类遍历 ActivatableAbilities，找到 DynamicAbilityTags 含该 InputTag 的 Spec
	//     → 调用 GAS 原生的 AbilitySpecInputPressed/Released 更新 Spec 的输入状态
	//     → 按需调用 TryActivateAbility 或 InvokeReplicatedEvent
	// 优势：用 FGameplayTag 替代原生 int32 InputID，类型安全且易扩展。
	// ──────────────────────────────────────────────────────────

	/**
	 * 按键按下事件入口（单次触发，非持续）。
	 * 对已激活的技能：调用 AbilitySpecInputPressed + InvokeReplicatedEvent(InputPressed)，
	 * 使技能内的 WaitInputPress 异步任务能感知到按下（如蓄力释放）。
	 * 对未激活的技能：不在此处激活（等待 Held 或依技能设计在 Released 激活）。
	 * FScopedAbilityListLock 保护遍历过程中 ActivatableAbilities 列表不被修改。
	 */
	void AbilityInputTagPressed(const FGameplayTag& InputTag);

	/**
	 * 按键持续按住事件（每 Tick 触发）。
	 * 对未激活的技能：调用 TryActivateAbility 尝试激活（持续按住 = 希望保持激活）。
	 * 对已激活的技能：调用 AbilitySpecInputPressed 更新输入状态即可（不重复激活）。
	 * 适合火球等"按住持续追踪/蓄力"类技能。
	 */
	void AbilityInputTagHeld(const FGameplayTag& InputTag);

	/**
	 * 按键抬起事件。
	 * 仅对当前已激活的技能生效：调用 AbilitySpecInputReleased + InvokeReplicatedEvent(InputReleased)，
	 * 使技能内的 WaitInputRelease 异步任务能感知到松开（如松开时才发射）。
	 * 不激活新技能，只通知已激活技能"输入已结束"。
	 */
	void AbilityInputTagReleased(const FGameplayTag& InputTag);

	/**
	 * 对所有可激活技能安全地执行一个委托操作。
	 * 内部持有 FScopedAbilityListLock，遍历期间列表被锁定，
	 * 避免遍历过程中因技能激活/取消导致列表变化的崩溃。
	 * 调用方：SpellMenuWidgetController 刷新法术菜单时遍历所有技能。
	 */
	void ForEachAbility(const FForEachAbility& Delegate);

	// ──────────────────────────────────────────────────────────
	// AbilitySpec 信息提取（静态工具函数）
	// ──────────────────────────────────────────────────────────
	// DynamicAbilityTags 在本项目中承载三类数据：
	//   - AbilityTag    : 来自 AbilitySpec.Ability->AbilityTags，标识技能身份（如 Abilities.Fire.FireBolt）
	//   - InputTag      : 技能绑定的输入按键（如 InputTag.LMB），存在 DynamicAbilityTags 中
	//   - StatusTag     : Abilities.Status.Locked/Eligible/Unlocked/Equipped
	// 以下静态函数封装了从 Spec 中提取这三类 Tag 的逻辑。

	/**
	 * 从 AbilitySpec 的静态 AbilityTags 中提取技能身份 Tag（父 Tag 为 "Abilities"）。
	 * 静态：不依赖 ASC 实例状态，纯数据提取，可在任意上下文调用。
	 */
	static FGameplayTag GetAbilityTagFromSpec(const FGameplayAbilitySpec& AbilitySpec);

	/**
	 * 从 DynamicAbilityTags 中提取输入 Tag（父 Tag 为 "InputTag"）。
	 * 输入 Tag 是运行时动态写入的（装备到槽位时赋予），因此在 DynamicAbilityTags 中。
	 */
	static FGameplayTag GetInputTagFromSpec(const FGameplayAbilitySpec& AbilitySpec);

	/**
	 * 从 DynamicAbilityTags 中提取状态 Tag（父 Tag 为 "Abilities.Status"）。
	 * 状态 Tag 随技能解锁/装备流程动态更新，每次只有一个状态 Tag 存在于 DynamicAbilityTags。
	 */
	static FGameplayTag GetStatusFromSpec(const FGameplayAbilitySpec& AbilitySpec);

	/**
	 * 通过技能 Tag 查找 AbilitySpec，再提取其状态 Tag。
	 * 是上面两个函数的组合查询，供外部（如 UI）通过 Tag 快速获取技能当前状态。
	 */
	FGameplayTag GetStatusFromAbilityTag(const FGameplayTag& AbilityTag);

	/**
	 * 通过技能 Tag 查找 AbilitySpec，再提取其当前槽位（InputTag）。
	 * 槽位本质上就是 InputTag，"装备到哪个槽" = "绑定了哪个输入 Tag"。
	 */
	FGameplayTag GetSlotFromAbilityTag(const FGameplayTag& AbilityTag);

	// ──────────────────────────────────────────────────────────
	// 槽位管理辅助函数
	// ──────────────────────────────────────────────────────────

	/**
	 * 检查指定槽位（InputTag）是否空闲（没有任何技能绑定在此 Tag 上）。
	 * 用于 ServerEquipAbility 判断目标槽位是否需要先踢出旧技能。
	 */
	bool SlotIsEmpty(const FGameplayTag& Slot);

	/**
	 * 检查 Spec 的 DynamicAbilityTags 是否精确包含指定 Slot Tag。
	 * 用于遍历查找"占用了该槽位的技能"。
	 * 静态重载（引用版）：用于对已知 Spec 直接检查。
	 */
	static bool AbilityHasSlot(const FGameplayAbilitySpec& Spec, const FGameplayTag& Slot);

	/**
	 * 检查 Spec 是否已绑定任意 InputTag（即是否已装备到某个槽位）。
	 * 通过父 Tag "InputTag" 做包含匹配（HasTag 而非 HasTagExact）。
	 * 用于 ServerEquipAbility 判断技能是否是"首次装备"（之前从未有过槽位）。
	 */
	static bool AbilityHasAnySlot(const FGameplayAbilitySpec& Spec);

	/**
	 * 在所有可激活技能中，找到绑定了指定槽位 Tag 的 AbilitySpec，返回指针。
	 * 返回裸指针：调用方必须在 ScopedAbilityListLock 保护内使用，或立即使用后不缓存。
	 */
	FGameplayAbilitySpec* GetSpecWithSlot(const FGameplayTag& Slot);

	/**
	 * 通过 AbilityInfo 数据资产判断该 Spec 对应的是否是被动技能。
	 * 用于 ServerEquipAbility 决定是否需要 TryActivate/Deactivate 被动技能。
	 * 调用链：GetAbilityTagFromSpec → AbilityInfo->FindAbilityInfoForTag → 检查 AbilityType Tag。
	 */
	bool IsPassiveAbility(const FGameplayAbilitySpec& Spec) const;

	/**
	 * 为 AbilitySpec 设置新的槽位 Tag：先清除旧 InputTag，再添加新 Slot Tag。
	 * 静态：操作 Spec 结构体本身，不依赖 ASC 实例。
	 * 调用后需配合 MarkAbilitySpecDirty 触发复制。
	 */
	static void AssignSlotToAbility(FGameplayAbilitySpec& Spec, const FGameplayTag& Slot);

	/**
	 * 通过 NetMulticast 在所有客户端（含 Server）广播被动特效激活/停用事件。
	 * 使用 Unreliable：视觉特效允许偶发丢包（不影响游戏逻辑），用 Reliable 会浪费带宽。
	 * 实现中直接广播 ActivatePassiveEffect 委托，由 Avatar Actor 上的组件响应。
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastActivatePassiveEffect(const FGameplayTag& AbilityTag, bool bActivate);

	/**
	 * 通过技能身份 Tag 在 ActivatableAbilities 中查找对应的 AbilitySpec。
	 * 使用 MatchesTag（层级匹配）而非 MatchesTagExact，兼容带子 Tag 的情况。
	 * 返回裸指针，调用方需注意生命周期（列表锁内安全使用）。
	 */
	FGameplayAbilitySpec* GetSpecFromAbilityTag(const FGameplayTag& AbilityTag);

	// ──────────────────────────────────────────────────────────
	// 属性升级（属性点消费）
	// ──────────────────────────────────────────────────────────

	/**
	 * 客户端调用入口：验证玩家有剩余属性点后，发起 ServerUpgradeAttribute RPC。
	 * 本函数在 Client 执行，只做前置检查，不直接修改属性（服务器权威）。
	 * 调用方：AttributeMenuWidgetController 中的按钮点击回调。
	 */
	void UpgradeAttribute(const FGameplayTag& AttributeTag);

	/**
	 * 服务器端属性升级实现：
	 *   1. 构造 FGameplayEventData，EventTag = AttributeTag，Magnitude = 1
	 *   2. SendGameplayEventToActor → 触发对应属性升级 GameplayAbility（通过 EventTag 响应）
	 *   3. 消耗 1 个属性点（AddToAttributePoints(-1)）
	 * 设计模式：通过 GameplayEvent 触发 Ability 来升级属性，而非直接修改，
	 * 好处是升级逻辑（GE、动画、音效等）可以在 Ability 中统一管理。
	 */
	UFUNCTION(Server, Reliable)
	void ServerUpgradeAttribute(const FGameplayTag& AttributeTag);

	// ──────────────────────────────────────────────────────────
	// 技能状态更新（等级驱动的技能解锁）
	// ──────────────────────────────────────────────────────────

	/**
	 * 根据当前角色等级，将满足等级要求但尚未赋予的技能标记为 Eligible 状态。
	 * 流程：
	 *   1. 从 AbilityInfo 遍历所有技能配置
	 *   2. 若角色等级 >= 技能的 LevelRequirement，且该技能尚未赋予（GetSpecFromAbilityTag == null）
	 *   3. 赋予技能（GiveAbility），初始状态设为 Abilities_Status_Eligible
	 *   4. 调用 ClientUpdateAbilityStatus RPC 通知 Client 刷新 UI
	 * 调用时机：玩家升级时（AuraCharacter 监听 OnLevelUp 事件后调用）。
	 * 仅在 Server 端调用，修改后通过 MarkAbilitySpecDirty + ClientRPC 同步。
	 */
	void UpdateAbilityStatuses(int32 Level);

	/**
	 * 法术点消费 RPC（Server 端执行）：
	 *   - 若当前状态为 Eligible → 升级为 Unlocked（第一次解锁）
	 *   - 若当前状态为 Unlocked 或 Equipped → AbilitySpec.Level += 1（升级已解锁技能）
	 *   - 同时扣除 1 个法术点（AddToSpellPoints(-1)）
	 *   - 最后通过 ClientUpdateAbilityStatus 回调通知 UI 刷新
	 * Server Reliable：法术点是重要游戏资源，必须保证不丢包。
	 */
	UFUNCTION(Server, Reliable)
	void ServerSpendSpellPoint(const FGameplayTag& AbilityTag);

	/**
	 * 技能装备到槽位的 Server 端 RPC，完整流程：
	 *   1. 验证技能状态合法（Unlocked 或 Equipped 才可装备）
	 *   2. 若目标槽位已有其他技能 → 清除旧技能槽位（若为被动技能，先停用）
	 *   3. 若待装备技能之前无槽位（首次装备）→ 若为被动技能则激活
	 *   4. 更新状态 Tag 为 Equipped，通过 AssignSlotToAbility 写入新 InputTag
	 *   5. MarkAbilitySpecDirty 触发复制
	 *   6. ClientEquipAbility RPC 回调通知 UI
	 * 设计要点：主动技能装备到槽位后才能通过 InputTag 触发；
	 *           被动技能装备即激活，移除即停用。
	 */
	UFUNCTION(Server, Reliable)
	void ServerEquipAbility(const FGameplayTag& AbilityTag, const FGameplayTag& Slot);

	/**
	 * 技能装备完成后的 Client 端回调 RPC。
	 * 在 Server 完成所有状态修改后调用，广播 AbilityEquipped 委托，
	 * 驱动 UI（法术菜单、技能栏）更新图标和高亮状态。
	 * 使用 Client Reliable 而非 NetMulticast：装备技能的 UI 反馈仅需告知
	 * 发起请求的那个客户端，无需广播给所有人。
	 */
	UFUNCTION(Client, Reliable)
	void ClientEquipAbility(const FGameplayTag& AbilityTag, const FGameplayTag& Status, const FGameplayTag& Slot, const FGameplayTag& PreviousSlot);

	/**
	 * 获取技能的当前等级描述和下一等级描述（用于法术菜单详情面板）。
	 * 若技能已赋予（AbilitySpec 存在）：调用 UAuraGameplayAbility 的虚函数获取本地化描述。
	 * 若技能尚未解锁（Spec 不存在）：返回"需要 X 级才能解锁"的 Locked 描述。
	 * 返回 bool 表示技能是否已解锁（UI 可据此决定是否显示"升级"按钮）。
	 */
	bool GetDescriptionsByAbilityTag(const FGameplayTag& AbilityTag, FString& OutDescription, FString& OutNextLevelDescription);

	/**
	 * 清除 AbilitySpec 的槽位 Tag（移除其 DynamicAbilityTags 中的 InputTag）。
	 * 静态：操作 Spec 结构体，不依赖 ASC 实例。
	 * 注意：清除后技能仍然存在于 ActivatableAbilities，只是不再绑定任何输入。
	 */
	static void ClearSlot(FGameplayAbilitySpec* Spec);

	/**
	 * 清除所有绑定了指定槽位 Tag 的技能的槽位信息。
	 * 内部使用 FScopedAbilityListLock 确保遍历安全。
	 * 用于"清空某个输入槽位"的场景（如重置技能绑定）。
	 */
	void ClearAbilitiesOfSlot(const FGameplayTag& Slot);

	/**
	 * 指针版重载：通过遍历 DynamicAbilityTags 精确匹配 Slot Tag。
	 * 与引用版 AbilityHasSlot 的区别：接受裸指针，用于指针版本的遍历场景。
	 */
	static bool AbilityHasSlot(FGameplayAbilitySpec* Spec, const FGameplayTag& Slot);

protected:

	/**
	 * GAS 内置的 ActivatableAbilities 数组复制回调（Client 端）。
	 * 重写目的：在 Client 侧技能列表首次复制完成后，触发 AbilitiesGivenDelegate，
	 * 通知 UI 控制器可以安全读取技能数据（弥补 Server 端 AddCharacterAbilities
	 * 广播时 Client 尚未收到数据的时序问题）。
	 * bStartupAbilitiesGiven 防止重复广播。
	 */
	virtual void OnRep_ActivateAbilities() override;

	/**
	 * GE 被应用时的 Client 端回调（通过 OnGameplayEffectAppliedDelegateToSelf 触发）。
	 * 从 EffectSpec 提取 AssetTags，广播 EffectAssetTags 委托给 UI 消息系统。
	 * 使用 Client Reliable RPC：GE 应用在 Server，但 UI 通知需要送达 Client。
	 * EffectSpec 中的 AssetTags（非 GrantedTags）是技能设计者手动配置的分类标签，
	 * 例如 "Message.HealthPotion"，UI 据此查找对应的消息数据表行。
	 */
	UFUNCTION(Client, Reliable)
	void ClientEffectApplied(UAbilitySystemComponent* AbilitySystemComponent, const FGameplayEffectSpec& EffectSpec, FActiveGameplayEffectHandle ActiveEffectHandle);

	/**
	 * 技能状态更新的 Client 端回调 RPC。
	 * Server 在修改 AbilitySpec 状态后调用此函数，Client 收到后广播 AbilityStatusChanged 委托，
	 * 驱动法术菜单刷新对应技能项的视觉状态（解锁效果、等级数字等）。
	 * 为何不直接依赖 ActivatableAbilities 复制？
	 * 因为 Spec 的 DynamicAbilityTags 改变不一定触发 OnRep，手动 RPC 更可靠。
	 */
	UFUNCTION(Client, Reliable)
	void ClientUpdateAbilityStatus(const FGameplayTag& AbilityTag, const FGameplayTag& StatusTag, int32 AbilityLevel);
};
