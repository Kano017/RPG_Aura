// Copyright Druid Mechanics

#pragma once

/**
 * ============================================================
 * AAuraPlayerState — 玩家持久化状态容器
 * ============================================================
 *
 * 【核心设计决策：为何将 ASC 和 AttributeSet 放在 PlayerState 而非 Character？】
 *
 *   在多人游戏中，Character 可能因死亡被销毁（Destroy），
 *   随后通过 Respawn 重新创建一个新的 Character Actor。
 *   如果 ASC 和 AttributeSet 挂载在 Character 上，角色死亡时：
 *     ✗ 等级数据丢失
 *     ✗ 已学习的技能丢失（GiveAbility 的 GrantedAbilities 随 ASC 销毁）
 *     ✗ 属性永久重置
 *   这对 RPG 游戏是灾难性的，玩家辛苦提升的成长数据不应因死亡消失。
 *
 *   PlayerState 的生命周期与玩家连接绑定，不随 Character 销毁：
 *     ✓ Character 死亡 → Character Actor 销毁
 *     ✓ Respawn → 新 Character 创建，PlayerState 依然存在
 *     ✓ 新 Character Possess 时重新获取 PlayerState 上的 ASC
 *     ✓ 等级/技能/属性完整保留
 *
 *   相比之下，敌人（AAuraEnemy）没有 PlayerState，其 ASC 挂在自身，
 *   因为敌人死亡后不需要复活，数据不需要持久化。
 *
 * 【网络架构】
 *   - ASC 使用 Mixed 复制模式（GameplayEffect 完整复制给拥有者，仅 MinimalNet 复制给他人）
 *   - Level/XP/AttributePoints/SpellPoints 通过 ReplicatedUsing 复制，
 *     客户端收到新值后触发 OnRep 函数，OnRep 函数广播本地委托通知 WidgetController
 *   - NetUpdateFrequency = 100.f 提高更新频率（默认较低），
 *     确保玩家数据能快速同步到客户端，减少 XP/等级更新的延迟感
 *
 * 【委托设计：为何不用 DYNAMIC_MULTICAST 而用普通 MULTICAST？】
 *   这些委托只在 C++ 层（WidgetController）订阅，不需要蓝图绑定，
 *   普通多播委托（DECLARE_MULTICAST_DELEGATE）比动态多播更高效，
 *   无需 UObject 包装，没有反射开销。
 * ============================================================
 */

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/PlayerState.h"
#include "AuraPlayerState.generated.h"


class UAbilitySystemComponent;
class UAttributeSet;
class ULevelUpInfo;

// ---- 玩家数据变化多播委托 ----
// C++ 多播委托（非动态），仅供 C++ WidgetController 订阅，效率更高

/** 单整型参数的通用变化委托（用于 XP、属性点、法术点）*/
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlayerStatChanged, int32 /*StatValue*/)

/**
 * 等级变化委托（携带 bLevelUp 标志）：
 *   bLevelUp = true  —— 主动升级（播放升级特效/音效）
 *   bLevelUp = false —— 读档恢复（静默设置等级，不播放特效）
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnLevelChanged, int32 /*StatValue*/, bool /*bLevelUp*/)

/**
 * AAuraPlayerState — 玩家持久化状态类
 *
 * 同时实现 IAbilitySystemInterface，使引擎 GAS 系统能通过接口找到此对象上的 ASC。
 * AAuraCharacter 在 GetAbilitySystemComponent() 中委托给 PlayerState，
 * 保持 GAS 框架的标准访问路径。
 */
UCLASS()
class AURA_API AAuraPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	AAuraPlayerState();

	/** 注册所有需要网络复制的属性（Level/XP/AttributePoints/SpellPoints）*/
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** IAbilitySystemInterface 接口实现，返回挂载在 PlayerState 上的 ASC */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/** 获取属性集（玩家的 Health/Mana/Strength 等属性均存储在此）*/
	UAttributeSet* GetAttributeSet() const { return AttributeSet; }

	/**
	 * 升级所需 XP 配置表（在蓝图中指定）。
	 * 记录每个等级需要的累计 XP 门槛，供 OverlayWidgetController 计算经验条百分比。
	 */
	UPROPERTY(EditDefaultsOnly)
	TObjectPtr<ULevelUpInfo> LevelUpInfo;

	// ---- 对外暴露的数据变化委托 ----
	// OverlayWidgetController 和 SpellMenuWidgetController 订阅这些委托以更新 UI

	/** XP 变化时广播（OverlayWidgetController 订阅，用于更新经验条）*/
	FOnPlayerStatChanged OnXPChangedDelegate;

	/** 等级变化时广播（携带是否播放升级特效的标志）*/
	FOnLevelChanged OnLevelChangedDelegate;

	/** 属性点变化时广播（AttributeMenuWidgetController 订阅，控制升级按钮状态）*/
	FOnPlayerStatChanged OnAttributePointsChangedDelegate;

	/** 法术点变化时广播（SpellMenuWidgetController 订阅，控制消费按钮状态）*/
	FOnPlayerStatChanged OnSpellPointsChangedDelegate;

	// ---- 数据 Getter（FORCEINLINE 内联，频繁调用零开销）----

	FORCEINLINE int32 GetPlayerLevel() const { return Level; }
	FORCEINLINE int32 GetXP() const { return XP; }
	FORCEINLINE int32 GetAttributePoints() const { return AttributePoints; }
	FORCEINLINE int32 GetSpellPoints() const { return SpellPoints; }

	// ---- 增量修改接口（用于升级、击杀奖励等场景）----

	/** 累加 XP 并广播变化委托（服务器调用）*/
	void AddToXP(int32 InXP);
	/** 累加等级并广播（bLevelUp = true，触发升级特效）*/
	void AddToLevel(int32 InLevel);
	/** 累加属性点（升级时获得）*/
	void AddToAttributePoints(int32 InPoints);
	/** 累加法术点（升级时获得）*/
	void AddToSpellPoints(int32 InPoints);

	// ---- 直接赋值接口（用于读档恢复数据，不触发"升级"特效）----

	/** 直接设置 XP（读档用，广播委托让 UI 更新）*/
	void SetXP(int32 InXP);
	/** 直接设置等级（读档用，bLevelUp = false，不播放升级特效）*/
	void SetLevel(int32 InLevel);
	/** 直接设置属性点（读档用）*/
	void SetAttributePoints(int32 InPoints);
	/** 直接设置法术点（读档用）*/
	void SetSpellPoints(int32 InPoints);

protected:

	/**
	 * 挂载在 PlayerState 上的 AbilitySystemComponent。
	 * 使用 Mixed 复制模式：
	 *   - 拥有者客户端（自己）：完整复制所有 GE 数据
	 *   - 其他客户端：仅复制最小必要数据（如标签）
	 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	/**
	 * 挂载在 PlayerState 上的 AttributeSet。
	 * 玩家所有属性（生命/魔力/力量/智力/韧性等）均存储在此。
	 * 与 ASC 同在 PlayerState，随玩家连接持久存在。
	 */
	UPROPERTY()
	TObjectPtr<UAttributeSet> AttributeSet;

private:

	/**
	 * 玩家等级（从 1 开始）。
	 * ReplicatedUsing = OnRep_Level：值变化时自动调用 OnRep_Level 回调。
	 * 客户端通过 OnRep 广播 OnLevelChangedDelegate，驱动 UI 更新。
	 */
	UPROPERTY(VisibleAnywhere, ReplicatedUsing=OnRep_Level)
	int32 Level = 1;

	/**
	 * 玩家累计经验值（总经验，不是当前等级的局部经验）。
	 * OverlayWidgetController 通过 LevelUpInfo 将总 XP 转换为等级内百分比显示。
	 */
	UPROPERTY(VisibleAnywhere, ReplicatedUsing=OnRep_XP)
	int32 XP = 0;

	/**
	 * 可用属性点数（升级时获得，在属性菜单中消费）。
	 * 每个属性点可以提升一个基础属性（力量/智力/韧性/活力/敏捷/信仰）。
	 */
	UPROPERTY(VisibleAnywhere, ReplicatedUsing=OnRep_AttributePoints)
	int32 AttributePoints = 0;

	/**
	 * 可用法术点数（升级时获得，在法术菜单中消费解锁/升级技能）。
	 */
	UPROPERTY(VisibleAnywhere, ReplicatedUsing=OnRep_SpellPoints)
	int32 SpellPoints = 0;

	// ---- OnRep 回调（客户端收到服务器复制的新值后触发）----
	// 每个 OnRep 函数都广播对应的委托，确保客户端的 WidgetController 能收到数据变化通知。
	// 服务器端由 Add/Set 函数直接广播，客户端由 OnRep 广播，两条路径保证双端同步。

	/** 等级复制回调：广播等级变化委托（bLevelUp = true，客户端播放升级特效）*/
	UFUNCTION()
	void OnRep_Level(int32 OldLevel);

	/** XP 复制回调：广播 XP 变化委托，触发经验条更新 */
	UFUNCTION()
	void OnRep_XP(int32 OldXP);

	/** 属性点复制回调：广播属性点变化委托，属性菜单升级按钮状态更新 */
	UFUNCTION()
	void OnRep_AttributePoints(int32 OldAttributePoints);

	/** 法术点复制回调：广播法术点变化委托，法术菜单消费按钮状态更新 */
	UFUNCTION()
	void OnRep_SpellPoints(int32 OldSpellPoints);
};
