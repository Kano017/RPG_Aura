// Copyright Druid Mechanics

#pragma once

/*
 * AAuraCharacterBase — 玩家角色与敌人角色的公共基类
 *
 * 【职责概述】
 * 本类是整个角色体系的根基，抽象出所有角色（无论玩家还是敌人）共享的：
 *   - GAS 组件引用（AbilitySystemComponent / AttributeSet）
 *   - 战斗逻辑（受击、死亡、攻击插槽、Debuff 特效）
 *   - 属性初始化流程
 *
 * 【接口实现】
 * 1. IAbilitySystemInterface：让 GAS 框架能通过 GetAbilitySystemComponent()
 *    找到本角色关联的 ASC，是 GAS 与 Actor 集成的必要接口。
 * 2. ICombatInterface：向外部系统（技能、AI、伤害计算器）暴露战斗能力，
 *    如获取攻击插槽位置、播放受击蒙太奇、查询死亡状态等。
 *
 * 【UCLASS(Abstract)】
 * 标记为抽象类，不能直接在编辑器中放置实例，必须通过子类使用。
 */

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "AbilitySystem/Data/CharacterClassInfo.h"
#include "Interaction/CombatInterface.h"
#include "AuraCharacterBase.generated.h"

class UPassiveNiagaraComponent;
class UDebuffNiagaraComponent;
class UNiagaraSystem;
class UAbilitySystemComponent;
class UAttributeSet;
class UGameplayEffect;
class UGameplayAbility;
class UAnimMontage;

UCLASS(Abstract)
class AURA_API AAuraCharacterBase : public ACharacter, public IAbilitySystemInterface, public ICombatInterface
{
	GENERATED_BODY()

public:
	AAuraCharacterBase();
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const;
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// IAbilitySystemInterface 接口实现
	// GAS 内部通过此函数找到角色的 ASC，是整个 GAS 运转的入口
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UAttributeSet* GetAttributeSet() const { return AttributeSet; }

	/** 战斗接口（ICombatInterface）实现 */
	// 返回受击蒙太奇，供受击技能（GA_HitReact）播放动画
	virtual UAnimMontage* GetHitReactMontage_Implementation() override;

	// 死亡入口函数：先分离武器，再通过 Multicast RPC 广播死亡事件
	// 子类可重写以添加死亡后的特殊逻辑（如玩家触发重生计时器）
	virtual void Die(const FVector& DeathImpulse) override;

	// 返回死亡委托引用，外部系统（如 XP 奖励逻辑）可订阅此委托
	virtual FOnDeathSignature& GetOnDeathDelegate() override;

	// 根据蒙太奇 Tag 返回对应插槽的世界坐标
	// 技能用此坐标生成投射物或特效（如从武器尖端发射火球）
	virtual FVector GetCombatSocketLocation_Implementation(const FGameplayTag& MontageTag) override;

	// 查询角色是否已死亡，技能和 AI 在选择目标时会调用此函数
	virtual bool IsDead_Implementation() const override;

	// 返回自身作为战斗中的 Avatar Actor
	virtual AActor* GetAvatar_Implementation() override;

	// 返回所有带 Tag 的攻击蒙太奇列表，供技能系统随机选取攻击动作
	virtual TArray<FTaggedMontage> GetAttackMontages_Implementation() override;

	// 返回死亡时播放的血液 Niagara 特效
	virtual UNiagaraSystem* GetBloodEffect_Implementation() override;

	// 通过 MontageTag 精确查找对应的 TaggedMontage，
	// ExecCalc_Damage 用此函数确定伤害来自哪个攻击插槽
	virtual FTaggedMontage GetTaggedMontageByTag_Implementation(const FGameplayTag& MontageTag) override;

	// 返回当前召唤的随从数量，用于控制召唤上限
	virtual int32 GetMinionCount_Implementation() override;

	// 增加/减少随从数量计数
	virtual void IncremenetMinionCount_Implementation(int32 Amount) override;

	// 返回角色职业枚举（战士/法师/游荡者等），
	// AuraAbilitySystemLibrary 用此决定初始化哪套属性和技能
	virtual ECharacterClass GetCharacterClass_Implementation() override;

	// 返回 ASC 注册委托引用，UI 和其他系统可订阅以在 ASC 就绪后执行初始化
	virtual FOnASCRegistered& GetOnASCRegisteredDelegate() override;

	// 返回武器骨骼网格体，被动技能特效附着等场景会用到
	virtual USkeletalMeshComponent* GetWeapon_Implementation() override;

	// 设置/查询是否正在受到电击（Electrocute 技能的持续效果）
	virtual void SetIsBeingShocked_Implementation(bool bInShock) override;
	virtual bool IsBeingShocked_Implementation() const override;

	// 返回受伤委托，外部可订阅以在每次受伤时执行回调（如护盾吸收计算）
	virtual FOnDamageSignature& GetOnDamageSignature() override;
	/** 战斗接口 结束 */

	// ASC 就绪委托：当 InitAbilityActorInfo 完成后广播，
	// PassiveNiagaraComponent 等依赖 ASC 的组件监听此委托做延迟初始化
	FOnASCRegistered OnAscRegistered;

	// 死亡委托：角色死亡后广播，XP 奖励系统、AI 管理器等订阅此事件
	FOnDeathSignature OnDeathDelegate;

	// 受伤委托：每次 TakeDamage 被调用后广播实际伤害值
	FOnDamageSignature OnDamageDelegate;

	/*
	 * MulticastHandleDeath — NetMulticast Reliable RPC
	 *
	 * 【为何需要 Multicast】
	 * 死亡是需要在所有客户端都呈现的视觉事件：
	 *   - 播放死亡音效
	 *   - 武器和网格体开启物理模拟并施加冲量（布娃娃效果）
	 *   - 胶囊体碰撞关闭（防止阻挡玩家移动）
	 *   - 触发溶解材质时间轴
	 * 如果只在服务器执行，客户端看到的角色会僵在原地，
	 * 因此必须通过 Multicast 确保所有端都执行这些视觉操作。
	 */
	UFUNCTION(NetMulticast, Reliable)
	virtual void MulticastHandleDeath(const FVector& DeathImpulse);

	/*
	 * AttackMontages — 带 Tag 的攻击蒙太奇数组
	 *
	 * 【FTaggedMontage 的设计意图】
	 * 不同攻击动作绑定不同的 GameplayTag（如 CombatSocket.Weapon / CombatSocket.LeftHand）。
	 * 技能在发射投射物时，通过 Tag 调用 GetCombatSocketLocation() 获取正确的发射位置。
	 * 这样一套系统可以支持：单手武器用武器尖端、双手攻击用左右手、尾部攻击用尾巴末端，
	 * 而无需为每种角色类型硬编码逻辑。
	 */
	UPROPERTY(EditAnywhere, Category = "Combat")
	TArray<FTaggedMontage> AttackMontages;

	/*
	 * bIsStunned / bIsBurned — ReplicatedUsing 状态标志
	 *
	 * 【ReplicatedUsing 的设计模式】
	 * 服务器修改这些布尔值，Unreal 自动将新值复制到所有客户端，
	 * 并在客户端触发对应的 OnRep_ 回调函数。
	 * 子类在 OnRep_ 中根据新状态激活/停用对应的 Debuff Niagara 特效和动画状态。
	 * 这样做的好处：视觉效果由客户端自行响应，不需要额外的 RPC。
	 */
	UPROPERTY(ReplicatedUsing=OnRep_Stunned, BlueprintReadOnly)
	bool bIsStunned = false;

	UPROPERTY(ReplicatedUsing=OnRep_Burned, BlueprintReadOnly)
	bool bIsBurned = false;

	// bIsBeingShocked 不需要 OnRep 回调（直接读取状态即可），用普通 Replicated
	UPROPERTY(Replicated, BlueprintReadOnly)
	bool bIsBeingShocked = false;

	// OnRep_Stunned：客户端接收到眩晕状态变化时调用，子类重写以更新视觉和输入
	UFUNCTION()
	virtual void OnRep_Stunned();

	// OnRep_Burned：客户端接收到燃烧状态变化时调用，子类重写以激活/停用燃烧特效
	UFUNCTION()
	virtual void OnRep_Burned();

	// 运行时设置角色职业，由 Spawn 系统在创建敌人时调用
	void SetCharacterClass(ECharacterClass InClass) { CharacterClass = InClass; }
protected:
	virtual void BeginPlay() override;

	// 武器骨骼网格体，附着在角色手部插槽上
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<USkeletalMeshComponent> Weapon;

	// 各攻击插槽名称，在蓝图/编辑器中配置，对应骨骼上的 Socket
	UPROPERTY(EditAnywhere, Category = "Combat")
	FName WeaponTipSocketName;

	UPROPERTY(EditAnywhere, Category = "Combat")
	FName LeftHandSocketName;

	UPROPERTY(EditAnywhere, Category = "Combat")
	FName RightHandSocketName;

	UPROPERTY(EditAnywhere, Category = "Combat")
	FName TailSocketName;

	// 死亡标志，不需要网络复制（死亡通过 Multicast RPC 同步，bDead 只在本地使用）
	UPROPERTY(BlueprintReadOnly)
	bool bDead = false;

	/*
	 * StunTagChanged — Gameplay Tag 事件回调
	 *
	 * 在 InitAbilityActorInfo 中通过 RegisterGameplayTagEvent 注册，
	 * 当 Debuff.Stun Tag 的数量从 0 变为非 0（或反向）时被调用。
	 * 服务器端：直接修改 bIsStunned 并调整移动速度。
	 * 客户端：通过 OnRep_Stunned 响应 bIsStunned 的复制。
	 */
	virtual void StunTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

	// 基础移动速度，眩晕时设为 0，恢复时还原为此值
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float BaseWalkSpeed = 600.f;

	/*
	 * AbilitySystemComponent / AttributeSet — 裸 TObjectPtr 引用
	 *
	 * 【为何不用强持有】
	 * 对于玩家角色（AAuraCharacter），ASC 和 AttributeSet 实际上属于 PlayerState，
	 * 这里保存的只是从 PlayerState 获取的引用指针，生命周期由 PlayerState 管理。
	 * 对于敌人（AAuraEnemy），这两个组件是在自身的构造函数中创建的，
	 * 由 UObject 组件系统管理生命周期，此处指针也只是访问入口。
	 * TObjectPtr 提供了空指针安全检查，比裸指针更安全，但语义上仍是"引用不是拥有"。
	 */
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UAttributeSet> AttributeSet;

	/*
	 * InitAbilityActorInfo — 虚函数，由子类各自实现
	 *
	 * 【为何必须是虚函数】
	 * 玩家和敌人的 ASC Owner 不同，初始化时机也不同：
	 *
	 * 玩家（AAuraCharacter）：
	 *   - ASC 在 PlayerState 上，Owner = PlayerState，Avatar = Character
	 *   - Server 端：在 PossessedBy() 中调用（Controller 已绑定）
	 *   - Client 端：在 OnRep_PlayerState() 中调用（PlayerState 复制完成后）
	 *
	 * 敌人（AAuraEnemy）：
	 *   - ASC 在自身上，Owner = Self，Avatar = Self
	 *   - 在 BeginPlay() 中即可调用，无需等待 PlayerState
	 *
	 * 基类提供空实现，子类必须重写并完成实际的 InitAbilityActorInfo() 调用。
	 */
	virtual void InitAbilityActorInfo();

	/*
	 * 默认属性 GameplayEffect 配置
	 *
	 * 【三步初始化的顺序依赖关系】
	 * 1. DefaultPrimaryAttributes（力量、智力、韧性、活力）
	 *    — 手动分配的基础属性，由编辑器直接配置数值
	 * 2. DefaultSecondaryAttributes（护甲、魔抗、暴击率等）
	 *    — 通过 MMC（Modifier Magnitude Calculation）从 Primary 属性计算而来，
	 *      必须在 Primary 已经设置好之后才能正确计算
	 * 3. DefaultVitalAttributes（当前生命值、当前魔法值）
	 *    — 依赖 MaxHealth / MaxMana（属于 Secondary）已经计算完毕，
	 *      才能将当前值初始化为最大值
	 * 顺序若颠倒，Secondary 会读到 Primary 的默认 0 值，导致属性计算错误。
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Attributes")
	TSubclassOf<UGameplayEffect> DefaultPrimaryAttributes;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Attributes")
	TSubclassOf<UGameplayEffect> DefaultSecondaryAttributes;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Attributes")
	TSubclassOf<UGameplayEffect> DefaultVitalAttributes;

	// 将 GameplayEffect 应用到自身，用于属性初始化
	// 创建 EffectContext -> EffectSpec -> ApplyToSelf
	void ApplyEffectToSelf(TSubclassOf<UGameplayEffect> GameplayEffectClass, float Level) const;

	// 按 Primary -> Secondary -> Vital 顺序依次应用三个属性 GE
	virtual void InitializeDefaultAttributes() const;

	// 将 StartupAbilities 和 StartupPassiveAbilities 授予给角色
	// 只在 HasAuthority() 的情况下执行（服务器权威）
	void AddCharacterAbilities();

	/* 溶解效果 */

	// 死亡后触发溶解流程：为角色和武器分别创建动态材质实例，
	// 再通过蓝图实现的 Timeline 驱动材质参数（溶解程度）变化
	void Dissolve();

	// 蓝图实现事件：接收动态材质实例，在蓝图中驱动溶解 Timeline
	UFUNCTION(BlueprintImplementableEvent)
	void StartDissolveTimeline(UMaterialInstanceDynamic* DynamicMaterialInstance);

	UFUNCTION(BlueprintImplementableEvent)
	void StartWeaponDissolveTimeline(UMaterialInstanceDynamic* DynamicMaterialInstance);

	// 角色身体溶解材质和武器溶解材质，在编辑器中指定
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMaterialInstance> DissolveMaterialInstance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UMaterialInstance> WeaponDissolveMaterialInstance;

	// 死亡时播放的血液粒子特效（Niagara System）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	UNiagaraSystem* BloodEffect;

	// 死亡时播放的音效
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	USoundBase* DeathSound;

	/* 随从 */

	// 当前召唤的随从数量，不需要网络复制（只在服务器逻辑中使用）
	int32 MinionCount = 0;

	// 角色职业，决定从 CharacterClassInfo 数据表中读取哪套技能和属性配置
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Class Defaults")
	ECharacterClass CharacterClass = ECharacterClass::Warrior;

	// 燃烧 Debuff 粒子组件，在 BeginPlay 时绑定 DebuffTag = Debuff.Burn
	// 当 Burn Tag 激活时自动播放燃烧特效
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDebuffNiagaraComponent> BurnDebuffComponent;

	// 眩晕 Debuff 粒子组件，绑定 DebuffTag = Debuff.Stun
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDebuffNiagaraComponent> StunDebuffComponent;

private:

	// 初始主动技能列表，角色初始化时通过 AddCharacterAbilities() 授予
	UPROPERTY(EditAnywhere, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> StartupAbilities;

	// 初始被动技能列表，随角色一起激活并持续生效
	UPROPERTY(EditAnywhere, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> StartupPassiveAbilities;

	// 受击蒙太奇：当角色受到伤害时，由 HitReact 技能负责播放
	UPROPERTY(EditAnywhere, Category = "Combat")
	TObjectPtr<UAnimMontage> HitReactMontage;

	// 被动技能对应的视觉特效组件，当对应被动技能激活/停用时自动控制显示
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPassiveNiagaraComponent> HaloOfProtectionNiagaraComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPassiveNiagaraComponent> LifeSiphonNiagaraComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPassiveNiagaraComponent> ManaSiphonNiagaraComponent;

	// 被动特效的统一附着点（SceneComponent），每帧在 Tick 中重置世界旋转为零，
	// 防止特效随角色旋转而偏转
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> EffectAttachComponent;
};
