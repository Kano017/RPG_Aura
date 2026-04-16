// Copyright Druid Mechanics


#include "Player/AuraPlayerState.h"

#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "Net/UnrealNetwork.h"

/**
 * 构造函数：在 PlayerState 上创建 ASC 和 AttributeSet 默认子对象。
 *
 * 【复制模式选择：EGameplayEffectReplicationMode::Mixed】
 *   GAS 提供三种复制模式：
 *   - Full：所有 GE 数据完整复制给所有客户端（适合单人游戏）
 *   - Mixed：GE 完整复制给拥有者，仅 GameplayCue/Tag 复制给他人（适合多人 RPG）
 *   - Minimal：仅 GameplayCue/Tag 复制给所有人（适合大型多人游戏/大量 AI）
 *   Mixed 模式在多人 RPG 中最佳：
 *     拥有者客户端能看到自己完整的技能效果数据，
 *     其他客户端只需要标签用于判断逻辑，不需要完整 GE 数据，节省带宽。
 *
 * 【NetUpdateFrequency = 100.f】
 *   PlayerState 默认更新频率较低（约 1Hz），会导致 XP/等级数值更新延迟明显。
 *   设为 100Hz 确保玩家数据能在 10ms 内同步到客户端，提升游戏响应感。
 */
AAuraPlayerState::AAuraPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAuraAbilitySystemComponent>("AbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);
	// Mixed 模式：ASC 数据完整复制给拥有者，标签复制给其他玩家
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UAuraAttributeSet>("AttributeSet");

	// 提高网络更新频率，减少玩家状态数据的同步延迟
	NetUpdateFrequency = 100.f;
}

/**
 * 注册需要网络复制的属性。
 * DOREPLIFETIME 将成员变量注册到 UE 的复制系统，
 * 每次属性在服务器上改变时，引擎自动将新值复制给相关客户端，
 * 客户端收到后触发对应的 OnRep 函数。
 */
void AAuraPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 四个玩家成长数据全部复制：等级/经验/属性点/法术点
	DOREPLIFETIME(AAuraPlayerState, Level);
	DOREPLIFETIME(AAuraPlayerState, XP);
	DOREPLIFETIME(AAuraPlayerState, AttributePoints);
	DOREPLIFETIME(AAuraPlayerState, SpellPoints);
}

/**
 * IAbilitySystemInterface 接口实现。
 * GAS 框架通过此接口在 Actor 上查找 ASC，
 * AAuraCharacter::GetAbilitySystemComponent() 也委托给此函数，
 * 保证从 Character 和 PlayerState 两个入口都能找到同一个 ASC。
 */
UAbilitySystemComponent* AAuraPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

// ============================================================
// Add 系列：增量修改（用于击杀奖励 XP、升级获得点数等）
// 修改数据后立刻广播委托，服务器端的 WidgetController 能立即响应，
// 客户端则等 OnRep 触发后再广播（两条路径确保双端 UI 同步）
// ============================================================

/**
 * 累加 XP 并广播变化（用于击杀敌人获得经验）。
 * OverlayWidgetController 收到后将总 XP 转换为经验条百分比并更新显示。
 */
void AAuraPlayerState::AddToXP(int32 InXP)
{
	XP += InXP;
	OnXPChangedDelegate.Broadcast(XP);
}

/**
 * 累加等级并广播（bLevelUp = true，通知 Widget 播放升级特效）。
 * 通常配合 AddToAttributePoints/AddToSpellPoints 一起调用。
 */
void AAuraPlayerState::AddToLevel(int32 InLevel)
{
	Level += InLevel;
	OnLevelChangedDelegate.Broadcast(Level, true);
}

// ============================================================
// Set 系列：直接赋值（用于读档恢复保存的数据）
// bLevelUp = false 避免读档时触发升级特效
// ============================================================

/**
 * 直接设置 XP（读档恢复）。
 */
void AAuraPlayerState::SetXP(int32 InXP)
{
	XP = InXP;
	OnXPChangedDelegate.Broadcast(XP);
}

/**
 * 直接设置等级（读档恢复，bLevelUp = false 表示静默设置，不播放升级特效）。
 */
void AAuraPlayerState::SetLevel(int32 InLevel)
{
	Level = InLevel;
	OnLevelChangedDelegate.Broadcast(Level, false);
}

/**
 * 直接设置属性点（读档恢复）。
 */
void AAuraPlayerState::SetAttributePoints(int32 InPoints)
{
	AttributePoints = InPoints;
	OnAttributePointsChangedDelegate.Broadcast(AttributePoints);
}

/**
 * 直接设置法术点（读档恢复）。
 */
void AAuraPlayerState::SetSpellPoints(int32 InPoints)
{
	SpellPoints = InPoints;
	OnSpellPointsChangedDelegate.Broadcast(SpellPoints);
}

// ============================================================
// OnRep 系列：客户端收到服务器复制数据后的回调
// 服务器上的数据修改通过 Add/Set 函数直接广播委托，
// 客户端通过 OnRep 回调接收服务器复制的新值后再广播，
// 两条路径确保服务器和所有客户端的 UI 都能正确更新。
// ============================================================

/**
 * 客户端收到等级更新时的回调。
 * 广播 OnLevelChangedDelegate（bLevelUp = true），
 * 客户端 Widget 播放升级特效/动画。
 * OldLevel 参数可用于计算升了多少级（多级连升的情况）。
 */
void AAuraPlayerState::OnRep_Level(int32 OldLevel)
{
	OnLevelChangedDelegate.Broadcast(Level, true);
}

/**
 * 客户端收到 XP 更新时的回调。
 * 广播 OnXPChangedDelegate，触发经验条重新计算和刷新。
 */
void AAuraPlayerState::OnRep_XP(int32 OldXP)
{
	OnXPChangedDelegate.Broadcast(XP);
}

/**
 * 客户端收到属性点更新时的回调。
 * 广播 OnAttributePointsChangedDelegate，属性菜单控制器更新升级按钮状态。
 */
void AAuraPlayerState::OnRep_AttributePoints(int32 OldAttributePoints)
{
	OnAttributePointsChangedDelegate.Broadcast(AttributePoints);
}

/**
 * 客户端收到法术点更新时的回调。
 * 广播 OnSpellPointsChangedDelegate，法术菜单控制器更新消费点数按钮状态。
 */
void AAuraPlayerState::OnRep_SpellPoints(int32 OldSpellPoints)
{
	OnSpellPointsChangedDelegate.Broadcast(SpellPoints);
}

/**
 * 累加属性点（升级时由游戏模式调用）。
 */
void AAuraPlayerState::AddToAttributePoints(int32 InPoints)
{
	AttributePoints += InPoints;
	OnAttributePointsChangedDelegate.Broadcast(AttributePoints);
}

/**
 * 累加法术点（升级时由游戏模式调用）。
 */
void AAuraPlayerState::AddToSpellPoints(int32 InPoints)
{
	SpellPoints += InPoints;
	OnSpellPointsChangedDelegate.Broadcast(SpellPoints);
}
