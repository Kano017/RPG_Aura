// Copyright Druid Mechanics

#pragma once

/*
 * AAuraEnemy — 敌人角色类
 *
 * 【ASC 和 AttributeSet 在自身上的设计】
 * 与玩家角色不同，敌人的 ASC 和 AttributeSet 直接创建在自身（构造函数中）。
 * 原因：
 *   1. 敌人不需要断线重连保存：敌人死亡后数据无需持久化，ASC 随 Actor 销毁即可。
 *   2. 更简单：不依赖 PlayerState，BeginPlay 就能完成 InitAbilityActorInfo，
 *      不需要等待 PossessedBy 或 OnRep_PlayerState 的时机。
 *   3. 敌人使用 Minimal 复制模式（EGameplayEffectReplicationMode::Minimal），
 *      减少网络开销（技能效果不需要复制到其他客户端，只在服务器计算）。
 *
 * 【初始化时机】
 * 敌人在 BeginPlay 中调用 InitAbilityActorInfo，因为 ASC Owner = Self，
 * 不需要等待任何外部对象就绪。
 *
 * 【接口实现】
 * 1. IHighlightInterface：玩家鼠标悬停时高亮显示（开启 CustomDepth 渲染）
 * 2. IEnemyInterface：暴露 CombatTarget 的读写接口，供 AI 行为树使用
 * 3. ICombatInterface（继承自基类）：死亡、等级、攻击插槽等战斗相关接口
 */

#include "CoreMinimal.h"
#include "Character/AuraCharacterBase.h"
#include "Interaction/EnemyInterface.h"
#include "Interaction/HighlightInterface.h"
#include "UI/WidgetController/OverlayWidgetController.h"
#include "AuraEnemy.generated.h"

class UWidgetComponent;
class UBehaviorTree;
class AAuraAIController;

UCLASS()
class AURA_API AAuraEnemy : public AAuraCharacterBase, public IEnemyInterface, public IHighlightInterface
{
	GENERATED_BODY()
public:
	AAuraEnemy();

	/*
	 * PossessedBy — 敌人 AI 初始化入口（Server Only）
	 *
	 * 敌人被 AAuraAIController Possess 后执行：
	 * 1. 初始化黑板（Blackboard）：将行为树的黑板资产绑定到 AI Controller
	 * 2. 运行行为树：启动 AI 决策循环
	 * 3. 设置初始黑板键值：HitReacting=false，RangedAttacker 根据职业类型判断
	 * 注意：此处加了 HasAuthority 检查，确保 AI 逻辑只在服务器启动
	 */
	virtual void PossessedBy(AController* NewController) override;

	/** 高亮接口（IHighlightInterface）实现 */
	// 开启 CustomDepth 渲染（轮廓高亮），鼠标悬停时由 PlayerController 调用
	virtual void HighlightActor_Implementation() override;

	// 关闭 CustomDepth 渲染，鼠标移开时调用
	virtual void UnHighlightActor_Implementation() override;

	// 敌人不需要修改移动目标（空实现），接口由 Cursor 点击移动系统使用
	virtual void SetMoveToLocation_Implementation(FVector& OutDestination) override;
	/** 高亮接口 结束 */

	/** 战斗接口（ICombatInterface）实现 */
	// 返回敌人等级（使用本地 Level 变量，非 PlayerState）
	virtual int32 GetPlayerLevel_Implementation() override;

	// 敌人死亡：设置生命倒计时、通知 AI 黑板、触发战利品掉落、调用基类死亡逻辑
	virtual void Die(const FVector& DeathImpulse) override;

	// 设置当前战斗目标（AI 行为树的追击和攻击目标）
	virtual void SetCombatTarget_Implementation(AActor* InCombatTarget) override;

	// 查询当前战斗目标，供 AI 技能技能瞄准使用
	virtual AActor* GetCombatTarget_Implementation() const override;
	/** 战斗接口 结束 */

	// 当前战斗目标 Actor，由 AI 行为树的追击任务设置
	UPROPERTY(BlueprintReadWrite, Category = "Combat")
	TObjectPtr<AActor> CombatTarget;

	// 血量变化委托，UI 血条（HealthBar Widget）订阅此委托更新显示
	UPROPERTY(BlueprintAssignable)
	FOnAttributeChangedSignature OnHealthChanged;

	// 最大血量变化委托，血条 Widget 用于计算血量百分比
	UPROPERTY(BlueprintAssignable)
	FOnAttributeChangedSignature OnMaxHealthChanged;

	/*
	 * HitReactTagChanged — GAS Tag 事件回调
	 *
	 * 当 Effects.HitReact Tag 数量变化时触发（Tag 由受击 GE 临时添加）。
	 * 受击期间：停止移动（MaxWalkSpeed = 0），AI 黑板 HitReacting = true（暂停追击）
	 * 受击结束：恢复移动速度，AI 恢复正常追击行为
	 */
	void HitReactTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

	// 是否正在播放受击动画（本地状态，不需要网络复制，服务器权威计算）
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bHitReacting = false;

	// 死亡后多少秒 Actor 被销毁（溶解动画结束后清理）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float LifeSpan = 5.f;

	// 由 SpawnSystem 调用，动态设置敌人等级（用于缩放属性数值）
	void SetLevel(const int32 InLevel) { Level = InLevel; }
protected:
	virtual void BeginPlay() override;

	/*
	 * InitAbilityActorInfo — 敌人 ASC 初始化（覆盖基类空实现）
	 *
	 * 敌人的 Owner 和 Avatar 都是自身，直接调用：
	 *   AbilitySystemComponent->InitAbilityActorInfo(this, this)
	 * 在 BeginPlay 中调用（不需要等待 Controller 或 PlayerState）。
	 * 初始化完成后广播 OnAscRegistered，并在有权限时初始化默认属性。
	 */
	virtual void InitAbilityActorInfo() override;

	/*
	 * InitializeDefaultAttributes — 敌人属性初始化（覆盖基类实现）
	 *
	 * 敌人不使用编辑器配置的固定 GE，而是通过 AuraAbilitySystemLibrary 根据：
	 *   - CharacterClass（职业）
	 *   - Level（等级）
	 * 从 CharacterClassInfo 数据表动态查找并应用对应的 GE，
	 * 实现"不同职业、不同等级的敌人自动获得对应属性"。
	 */
	virtual void InitializeDefaultAttributes() const override;

	/*
	 * StunTagChanged — 覆盖基类，额外同步 AI 黑板
	 *
	 * 基类处理移动速度控制，敌人额外需要更新 AI 黑板的 "Stunned" 键，
	 * 行为树读取此键决定是否暂停 AI 决策（如停止追击、停止攻击）。
	 */
	virtual void StunTagChanged(const FGameplayTag CallbackTag, int32 NewCount) override;

	// 敌人等级：影响属性数值缩放，由 CharacterClassInfo 数据表查找对应行
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Class Defaults")
	int32 Level = 1;

	// 头顶血条 UI 组件，通过 SetWidgetController(this) 将敌人自身设为 WidgetController
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UWidgetComponent> HealthBar;

	// AI 行为树资产，在编辑器中配置
	UPROPERTY(EditAnywhere, Category = "AI")
	TObjectPtr<UBehaviorTree> BehaviorTree;

	// 持有 AI Controller 的引用，用于访问黑板组件
	UPROPERTY()
	TObjectPtr<AAuraAIController> AuraAIController;

	// 战利品掉落逻辑在蓝图中实现（方便设计师配置掉落表）
	UFUNCTION(BlueprintImplementableEvent)
	void SpawnLoot();
};
