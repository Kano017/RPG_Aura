// Copyright Druid Mechanics


#include "Character/AuraEnemy.h"

#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "Components/WidgetComponent.h"
#include "Aura/Aura.h"
#include "UI/Widget/AuraUserWidget.h"
#include "AuraGameplayTags.h"
#include "AI/AuraAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

AAuraEnemy::AAuraEnemy()
{
	// 骨骼网格体对 Visibility 通道产生遮挡，
	// 使玩家鼠标射线能命中敌人（用于高亮和选择目标）
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// 敌人的 ASC 直接创建在自身上（Owner = Self，Avatar = Self）
	// 使用 Minimal 复制模式：GE 不向所有客户端复制，降低网络开销
	// 适合敌人这种"只需要服务器权威计算，客户端只看结果"的场景
	AbilitySystemComponent = CreateDefaultSubobject<UAuraAbilitySystemComponent>("AbilitySystemComponent");
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	// 敌人朝向由 AI Controller 决定（使用 Controller 期望旋转），而非移动方向
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = true;

	// AttributeSet 直接创建在自身上，生命周期随 Actor 结束
	AttributeSet = CreateDefaultSubobject<UAuraAttributeSet>("AttributeSet");

	// 血条 UI 组件，附着在根组件上（显示在敌人头顶上方）
	HealthBar = CreateDefaultSubobject<UWidgetComponent>("HealthBar");
	HealthBar->SetupAttachment(GetRootComponent());

	// 设置 CustomDepth 模板值为红色（CUSTOM_DEPTH_RED）
	// 后期处理材质读取此模板值绘制红色轮廓高亮
	GetMesh()->SetCustomDepthStencilValue(CUSTOM_DEPTH_RED);
	GetMesh()->MarkRenderStateDirty();
	Weapon->SetCustomDepthStencilValue(CUSTOM_DEPTH_RED);
	Weapon->MarkRenderStateDirty();

	// 敌人移动速度比玩家慢（玩家默认 600）
	BaseWalkSpeed = 250.f;
}

/*
 * PossessedBy — AI Controller Possess 敌人时调用（Server Only）
 *
 * 敌人的 PossessedBy 主要负责启动 AI 行为树：
 * 1. 初始化黑板：将行为树配置的黑板资产绑定到 AI Controller
 * 2. 运行行为树：AI 开始按行为树逻辑决策
 * 3. 初始化关键黑板键：
 *    - HitReacting：是否正在受击（初始为 false）
 *    - RangedAttacker：是否是远程攻击者（非战士职业均为远程，影响保持距离逻辑）
 */
void AAuraEnemy::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (!HasAuthority()) return;
	AuraAIController = Cast<AAuraAIController>(NewController);
	AuraAIController->GetBlackboardComponent()->InitializeBlackboard(*BehaviorTree->BlackboardAsset);
	AuraAIController->RunBehaviorTree(BehaviorTree);
	AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("HitReacting"), false);
	// 非战士职业（法师/游荡者等）标记为远程攻击者，行为树会保持与目标的战斗距离
	AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("RangedAttacker"), CharacterClass != ECharacterClass::Warrior);
}

/*
 * HighlightActor_Implementation — IHighlightInterface 实现
 *
 * 开启骨骼网格体和武器的 CustomDepth 渲染。
 * 后期处理材质检测到 CustomDepth 中有对应模板值（红色）的像素时，
 * 在物体轮廓处绘制高亮边框，给玩家明确的"可交互/可攻击"视觉反馈。
 */
void AAuraEnemy::HighlightActor_Implementation()
{
	GetMesh()->SetRenderCustomDepth(true);
	Weapon->SetRenderCustomDepth(true);
}

// IHighlightInterface 实现：关闭高亮渲染，鼠标移开时调用
void AAuraEnemy::UnHighlightActor_Implementation()
{
	GetMesh()->SetRenderCustomDepth(false);
	Weapon->SetRenderCustomDepth(false);
}

// IHighlightInterface 实现：敌人不需要修改移动目标，空实现
// 此接口主要为可交互对象（如传送点）设计，敌人直接忽略
void AAuraEnemy::SetMoveToLocation_Implementation(FVector& OutDestination)
{
	// 不要修改 OutDestination
}

// ICombatInterface 实现：返回敌人等级，ExecCalc_Damage 用于计算伤害公式
int32 AAuraEnemy::GetPlayerLevel_Implementation()
{
	return Level;
}

/*
 * Die — 敌人死亡处理（覆盖基类）
 *
 * 敌人死亡时在基类布娃娃效果之前额外执行：
 * 1. SetLifeSpan：设定 Actor 存活倒计时，LifeSpan 秒后自动销毁（配合溶解动画时长）
 * 2. 更新 AI 黑板 Dead = true：停止行为树的所有 AI 决策（避免死亡后继续攻击）
 * 3. SpawnLoot：触发战利品掉落（蓝图实现，便于设计师配置掉落表）
 * 4. 调用 Super::Die：执行基类的武器分离 + Multicast 布娃娃 + 溶解效果
 */
void AAuraEnemy::Die(const FVector& DeathImpulse)
{
	SetLifeSpan(LifeSpan);
	if (AuraAIController) AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("Dead"), true);
	SpawnLoot();
	Super::Die(DeathImpulse);
}

// IEnemyInterface 实现：设置当前战斗目标，由 AI 行为树的感知系统调用
void AAuraEnemy::SetCombatTarget_Implementation(AActor* InCombatTarget)
{
	CombatTarget = InCombatTarget;
}

// IEnemyInterface 实现：查询当前战斗目标，供 AI 技能瞄准使用
AActor* AAuraEnemy::GetCombatTarget_Implementation() const
{
	return CombatTarget;
}

/*
 * BeginPlay — 敌人初始化主流程
 *
 * 【执行顺序】
 * 1. 设置初始移动速度
 * 2. InitAbilityActorInfo：因为 ASC 在自身上，此处即可安全初始化，无需等待
 * 3. HasAuthority 时授予初始技能（服务器权威）
 * 4. 绑定血条 UI 的 WidgetController（将敌人自身作为 WidgetController）
 * 5. 订阅属性变化委托：血量/最大血量变化时通知血条 UI 更新
 * 6. 订阅 HitReact Tag 事件：受击时暂停移动和 AI
 * 7. 广播初始血量值：确保血条 UI 初始化时显示正确数值
 */
void AAuraEnemy::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	// 敌人 ASC 在自身上，BeginPlay 时直接初始化，无需等待 PlayerState
	InitAbilityActorInfo();
	if (HasAuthority())
	{
		// 根据职业类型从 CharacterClassInfo 数据表查找并授予对应的初始技能（服务器专属）
		UAuraAbilitySystemLibrary::GiveStartupAbilities(this, AbilitySystemComponent, CharacterClass);
	}


	// 将敌人自身设置为血条 Widget 的 WidgetController，
	// 血条 Widget 通过 WidgetController 订阅血量变化委托
	if (UAuraUserWidget* AuraUserWidget = Cast<UAuraUserWidget>(HealthBar->GetUserWidgetObject()))
	{
		AuraUserWidget->SetWidgetController(this);
	}

	if (const UAuraAttributeSet* AuraAS = Cast<UAuraAttributeSet>(AttributeSet))
	{
		// 订阅血量属性变化：血量变化时广播 OnHealthChanged，血条 UI 接收并更新显示
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AuraAS->GetHealthAttribute()).AddLambda(
			[this](const FOnAttributeChangeData& Data)
			{
				OnHealthChanged.Broadcast(Data.NewValue);
			}
		);
		// 订阅最大血量属性变化：MaxHealth 变化时广播，血条 UI 重新计算血量百分比
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AuraAS->GetMaxHealthAttribute()).AddLambda(
			[this](const FOnAttributeChangeData& Data)
			{
				OnMaxHealthChanged.Broadcast(Data.NewValue);
			}
		);

		// 注册 HitReact Tag 监听：Tag 添加时停止移动，Tag 移除时恢复移动
		AbilitySystemComponent->RegisterGameplayTagEvent(FAuraGameplayTags::Get().Effects_HitReact, EGameplayTagEventType::NewOrRemoved).AddUObject(
			this,
			&AAuraEnemy::HitReactTagChanged
		);

		// 广播初始血量值，确保血条 UI 在 BeginPlay 时就显示正确的初始数值
		OnHealthChanged.Broadcast(AuraAS->GetHealth());
		OnMaxHealthChanged.Broadcast(AuraAS->GetMaxHealth());
	}

}

/*
 * HitReactTagChanged — 受击状态回调（服务器端执行）
 *
 * 通过 RegisterGameplayTagEvent 绑定到 Effects.HitReact Tag 的变化事件。
 * 受击 GE 添加此 Tag 时（NewCount > 0）：
 *   - 停止移动（MaxWalkSpeed = 0），呈现被击退效果
 *   - AI 黑板 HitReacting = true，行为树暂停追击和攻击决策
 * 受击 GE 到期移除 Tag 时（NewCount == 0）：
 *   - 恢复移动速度
 *   - AI 黑板 HitReacting = false，恢复正常 AI 行为
 */
void AAuraEnemy::HitReactTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	bHitReacting = NewCount > 0;
	GetCharacterMovement()->MaxWalkSpeed = bHitReacting ? 0.f : BaseWalkSpeed;
	if (AuraAIController && AuraAIController->GetBlackboardComponent())
	{
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("HitReacting"), bHitReacting);
	}
}

/*
 * InitAbilityActorInfo — 敌人 ASC 初始化（覆盖基类空实现）
 *
 * 敌人的 Owner 和 Avatar 均为自身，因此调用：
 *   InitAbilityActorInfo(this, this)
 *
 * 与玩家不同，此处 Owner = Avatar = this，没有 PlayerState 的间接层。
 * 初始化后：
 * 1. AbilityActorInfoSet()：内部完成 Tag 监听等初始化
 * 2. 注册 Stun Tag 回调（眩晕时控制移动速度和 AI 黑板）
 * 3. HasAuthority 时初始化默认属性（服务器计算属性，复制到客户端）
 * 4. 广播 OnAscRegistered（被动特效组件等待此事件）
 */
void AAuraEnemy::InitAbilityActorInfo()
{
	// 敌人：Owner 和 Avatar 都是自身
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	Cast<UAuraAbilitySystemComponent>(AbilitySystemComponent)->AbilityActorInfoSet();
	// 注册眩晕 Tag 监听，眩晕时调用 StunTagChanged 控制移动速度和 AI 状态
	AbilitySystemComponent->RegisterGameplayTagEvent(FAuraGameplayTags::Get().Debuff_Stun, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &AAuraEnemy::StunTagChanged);


	if (HasAuthority())
	{
		// 只在服务器初始化属性，通过 GAS 复制机制同步到客户端
		InitializeDefaultAttributes();
	}
	// 广播 ASC 就绪事件，被动特效组件（PassiveNiagaraComponent）等待此信号后才安全初始化
	OnAscRegistered.Broadcast(AbilitySystemComponent);
}

/*
 * InitializeDefaultAttributes — 敌人属性初始化（覆盖基类实现）
 *
 * 不使用编辑器配置的固定 GE，而是通过 AuraAbilitySystemLibrary 动态查找：
 * 根据 CharacterClass 和 Level 从 CharacterClassInfo 数据表（Data Asset）获取对应的
 * Primary/Secondary/Vital 属性 GE 并应用，实现数据驱动的属性缩放。
 * 这样设计师只需配置数据表，不需要为每种敌人手动指定 GE 类。
 */
void AAuraEnemy::InitializeDefaultAttributes() const
{
	UAuraAbilitySystemLibrary::InitializeDefaultAttributes(this, CharacterClass, Level, AbilitySystemComponent);
}

/*
 * StunTagChanged — 覆盖基类，敌人专属眩晕处理
 *
 * 基类负责：修改 bIsStunned 并控制移动速度（MaxWalkSpeed = 0 或恢复）
 * 敌人额外需要：同步 AI 黑板的 "Stunned" 键
 *   - Stunned = true：行为树检测到眩晕，暂停所有 AI 决策（停止攻击、追击）
 *   - Stunned = false：眩晕结束，恢复正常 AI 行为
 */
void AAuraEnemy::StunTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	Super::StunTagChanged(CallbackTag, NewCount);

	if (AuraAIController && AuraAIController->GetBlackboardComponent())
	{
		AuraAIController->GetBlackboardComponent()->SetValueAsBool(FName("Stunned"), bIsStunned);
	}
}
