// Copyright Druid Mechanics


#include "Character/AuraCharacterBase.h"
#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/Debuff/DebuffNiagaraComponent.h"
#include "AbilitySystem/Passive/PassiveNiagaraComponent.h"
#include "Aura/Aura.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

AAuraCharacterBase::AAuraCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;
	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();

	// 创建 Debuff 粒子组件并绑定对应的 Gameplay Tag
	// DebuffNiagaraComponent 内部会监听 ASC 上的 Tag 事件，自动控制粒子播放
	BurnDebuffComponent = CreateDefaultSubobject<UDebuffNiagaraComponent>("BurnDebuffComponent");
	BurnDebuffComponent->SetupAttachment(GetRootComponent());
	BurnDebuffComponent->DebuffTag = GameplayTags.Debuff_Burn;

	StunDebuffComponent = CreateDefaultSubobject<UDebuffNiagaraComponent>("StunDebuffComponent");
	StunDebuffComponent->SetupAttachment(GetRootComponent());
	StunDebuffComponent->DebuffTag = GameplayTags.Debuff_Stun;

	// 胶囊体不阻挡摄像机射线，也不产生 Overlap 事件（减少不必要的碰撞计算）
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetCapsuleComponent()->SetGenerateOverlapEvents(false);

	// 骨骼网格体：忽略摄像机，但与投射物产生 Overlap 事件（用于命中检测）
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Projectile, ECR_Overlap);
	GetMesh()->SetGenerateOverlapEvents(true);

	// 武器附着在手部插槽上，自身不参与碰撞（由角色网格体负责命中判定）
	Weapon = CreateDefaultSubobject<USkeletalMeshComponent>("Weapon");
	Weapon->SetupAttachment(GetMesh(), FName("WeaponHandSocket"));
	Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 被动特效统一附着点，在 Tick 中每帧重置旋转，确保特效始终朝向世界正方向
	EffectAttachComponent = CreateDefaultSubobject<USceneComponent>("EffectAttachPoint");
	EffectAttachComponent->SetupAttachment(GetRootComponent());
	HaloOfProtectionNiagaraComponent = CreateDefaultSubobject<UPassiveNiagaraComponent>("HaloOfProtectionComponent");
	HaloOfProtectionNiagaraComponent->SetupAttachment(EffectAttachComponent);
	LifeSiphonNiagaraComponent = CreateDefaultSubobject<UPassiveNiagaraComponent>("LifeSiphonNiagaraComponent");
	LifeSiphonNiagaraComponent->SetupAttachment(EffectAttachComponent);
	ManaSiphonNiagaraComponent = CreateDefaultSubobject<UPassiveNiagaraComponent>("ManaSiphonNiagaraComponent");
	ManaSiphonNiagaraComponent->SetupAttachment(EffectAttachComponent);

	// 始终更新动画姿势，防止角色离开摄像机视野后动画停止导致视觉错误
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

void AAuraCharacterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// 每帧重置特效附着点的世界旋转
	// 作用：被动光环特效（如护盾光环）不随角色旋转而偏转，始终保持直立
	EffectAttachComponent->SetWorldRotation(FRotator::ZeroRotator);
}

void AAuraCharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 注册需要网络复制的属性
	// bIsStunned 和 bIsBurned 使用 ReplicatedUsing，客户端收到后触发 OnRep_ 回调更新特效
	// bIsBeingShocked 只需要普通复制，客户端直接读取状态值即可
	DOREPLIFETIME(AAuraCharacterBase, bIsStunned);
	DOREPLIFETIME(AAuraCharacterBase, bIsBurned);
	DOREPLIFETIME(AAuraCharacterBase, bIsBeingShocked);
}

float AAuraCharacterBase::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float DamageTaken = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	// 广播受伤委托，供订阅方（如 UAuraAbilitySystemLibrary 中的伤害处理链）响应
	OnDamageDelegate.Broadcast(DamageTaken);
	return DamageTaken;
}

// IAbilitySystemInterface 接口实现
// GAS 在查找 ASC 时会调用此函数，必须返回有效指针
UAbilitySystemComponent* AAuraCharacterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

// ICombatInterface 实现：返回受击蒙太奇，供 HitReact 技能播放
UAnimMontage* AAuraCharacterBase::GetHitReactMontage_Implementation()
{
	return HitReactMontage;
}

void AAuraCharacterBase::Die(const FVector& DeathImpulse)
{
	// 死亡时先将武器从角色骨骼上分离（KeepWorld 保留当前世界位置），
	// 后续 MulticastHandleDeath 会为武器开启物理模拟，使其自然掉落
	Weapon->DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepWorld, true));
	// 通过 Multicast RPC 确保所有客户端都执行死亡视觉逻辑
	MulticastHandleDeath(DeathImpulse);
}

FOnDeathSignature& AAuraCharacterBase::GetOnDeathDelegate()
{
	return OnDeathDelegate;
}

/*
 * MulticastHandleDeath_Implementation
 *
 * 【执行位置】服务器 + 所有客户端（NetMulticast Reliable）
 *
 * 死亡的视觉处理必须在所有端执行：
 * 1. 播放死亡音效
 * 2. 武器和角色网格体切换为物理模拟（布娃娃/碎落效果），并施加死亡冲量
 * 3. 胶囊体碰撞关闭，防止尸体继续阻挡其他角色移动
 * 4. 启动溶解材质动画
 * 5. 关闭 Debuff 特效（死亡后不再显示燃烧/眩晕）
 * 6. 广播死亡委托（各端的订阅者各自响应，如 AI 管理器更新敌人计数）
 */
void AAuraCharacterBase::MulticastHandleDeath_Implementation(const FVector& DeathImpulse)
{
	UGameplayStatics::PlaySoundAtLocation(this, DeathSound, GetActorLocation(), GetActorRotation());

	// 武器物理模拟：开启重力和物理碰撞，施加冲量的 10%（武器比身体轻，冲量缩放）
	Weapon->SetSimulatePhysics(true);
	Weapon->SetEnableGravity(true);
	Weapon->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	Weapon->AddImpulse(DeathImpulse * 0.1f, NAME_None, true);

	// 角色网格体物理模拟：完整受到死亡冲量，产生被击飞的布娃娃效果
	// ECR_Block WorldStatic 确保尸体不会穿过地面
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetEnableGravity(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	GetMesh()->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	GetMesh()->AddImpulse(DeathImpulse, NAME_None, true);

	// 关闭胶囊体碰撞，尸体不再阻挡玩家走动
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 创建动态材质实例并启动溶解 Timeline
	Dissolve();
	bDead = true;
	// 死亡后停用 Debuff 特效，避免燃烧/眩晕粒子继续显示在尸体上
	BurnDebuffComponent->Deactivate();
	StunDebuffComponent->Deactivate();
	// 广播死亡事件（各端订阅者各自响应）
	OnDeathDelegate.Broadcast(this);
}

/*
 * StunTagChanged — Gameplay Tag 事件回调（服务器端执行）
 *
 * 当 ASC 上的 Debuff.Stun Tag 数量发生变化时触发：
 * - NewCount > 0：Tag 刚被添加，进入眩晕状态，移动速度清零
 * - NewCount == 0：Tag 全部移除，解除眩晕，恢复移动速度
 * 服务器修改 bIsStunned 后，会自动复制到客户端并触发 OnRep_Stunned
 */
void AAuraCharacterBase::StunTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	bIsStunned = NewCount > 0;
	GetCharacterMovement()->MaxWalkSpeed = bIsStunned ? 0.f : BaseWalkSpeed;
}

// OnRep_Stunned — 客户端收到 bIsStunned 复制时调用
// 基类为空实现，由子类（AAuraCharacter / AAuraEnemy）重写以更新对应的视觉和输入阻断
void AAuraCharacterBase::OnRep_Stunned()
{

}

// OnRep_Burned — 客户端收到 bIsBurned 复制时调用
// 基类为空实现，由子类重写以激活/停用燃烧特效
void AAuraCharacterBase::OnRep_Burned()
{
}

void AAuraCharacterBase::BeginPlay()
{
	Super::BeginPlay();

}

/*
 * GetCombatSocketLocation_Implementation — ICombatInterface 实现
 *
 * 根据传入的 MontageTag 返回对应骨骼插槽的世界坐标。
 * 技能（如 AuraFireBolt、ArcaneShards）在生成投射物时调用此函数，
 * 确保投射物从正确的身体部位飞出，而不是从角色原点发射。
 *
 * Tag 与插槽的对应关系由 FAuraGameplayTags 中的 CombatSocket_* 标签决定：
 *   CombatSocket.Weapon    -> 武器尖端插槽（大多数玩家职业使用）
 *   CombatSocket.LeftHand  -> 左手插槽（双手法术或怪物爪击）
 *   CombatSocket.RightHand -> 右手插槽
 *   CombatSocket.Tail      -> 尾巴末端插槽（蛇形/龙形敌人）
 */
FVector AAuraCharacterBase::GetCombatSocketLocation_Implementation(const FGameplayTag& MontageTag)
{
	const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();
	if (MontageTag.MatchesTagExact(GameplayTags.CombatSocket_Weapon) && IsValid(Weapon))
	{
		return Weapon->GetSocketLocation(WeaponTipSocketName);
	}
	if (MontageTag.MatchesTagExact(GameplayTags.CombatSocket_LeftHand))
	{
		return GetMesh()->GetSocketLocation(LeftHandSocketName);
	}
	if (MontageTag.MatchesTagExact(GameplayTags.CombatSocket_RightHand))
	{
		return GetMesh()->GetSocketLocation(RightHandSocketName);
	}
	if (MontageTag.MatchesTagExact(GameplayTags.CombatSocket_Tail))
	{
		return GetMesh()->GetSocketLocation(TailSocketName);
	}
	return FVector();
}

// ICombatInterface 实现：返回死亡状态，AI 和技能目标选择时使用
bool AAuraCharacterBase::IsDead_Implementation() const
{
	return bDead;
}

// ICombatInterface 实现：返回自身 Actor，供 GAS 技能获取实际的 Avatar
AActor* AAuraCharacterBase::GetAvatar_Implementation()
{
	return this;
}

// ICombatInterface 实现：返回全部攻击蒙太奇列表，
// 技能系统可从中随机选取一个播放，实现多样化攻击动作
TArray<FTaggedMontage> AAuraCharacterBase::GetAttackMontages_Implementation()
{
	return AttackMontages;
}

// ICombatInterface 实现：返回死亡血液特效资产
UNiagaraSystem* AAuraCharacterBase::GetBloodEffect_Implementation()
{
	return BloodEffect;
}

/*
 * GetTaggedMontageByTag_Implementation — ICombatInterface 实现
 *
 * 遍历 AttackMontages 数组，通过 MontageTag 找到对应的 FTaggedMontage。
 * ExecCalc_Damage 在计算近战伤害时调用此函数，通过蒙太奇 Tag 确认是哪种攻击，
 * 再通过 GetCombatSocketLocation 获取正确的伤害判定位置。
 */
FTaggedMontage AAuraCharacterBase::GetTaggedMontageByTag_Implementation(const FGameplayTag& MontageTag)
{
	for (FTaggedMontage TaggedMontage : AttackMontages)
	{
		if (TaggedMontage.MontageTag == MontageTag)
		{
			return TaggedMontage;
		}
	}
	return FTaggedMontage();
}

// ICombatInterface 实现：返回当前随从数量，召唤技能用于检查是否超过上限
int32 AAuraCharacterBase::GetMinionCount_Implementation()
{
	return MinionCount;
}

// ICombatInterface 实现：增加（或减少，Amount 为负数）随从计数
void AAuraCharacterBase::IncremenetMinionCount_Implementation(int32 Amount)
{
	MinionCount += Amount;
}

// ICombatInterface 实现：返回职业枚举，
// UAuraAbilitySystemLibrary::InitializeDefaultAttributes 根据此值读取正确的数据行
ECharacterClass AAuraCharacterBase::GetCharacterClass_Implementation()
{
	return CharacterClass;
}

FOnASCRegistered& AAuraCharacterBase::GetOnASCRegisteredDelegate()
{
	return OnAscRegistered;
}

// ICombatInterface 实现：返回武器骨骼网格体引用
USkeletalMeshComponent* AAuraCharacterBase::GetWeapon_Implementation()
{
	return Weapon;
}

// ICombatInterface 实现：设置被电击状态（Electrocute 技能持续伤害阶段使用）
void AAuraCharacterBase::SetIsBeingShocked_Implementation(bool bInShock)
{
	bIsBeingShocked = bInShock;
}

// ICombatInterface 实现：查询是否正在被电击，防止多个 Electrocute 叠加触电
bool AAuraCharacterBase::IsBeingShocked_Implementation() const
{
	return bIsBeingShocked;
}

FOnDamageSignature& AAuraCharacterBase::GetOnDamageSignature()
{
	return OnDamageDelegate;
}

// 基类空实现，子类各自重写初始化逻辑（详见头文件注释）
void AAuraCharacterBase::InitAbilityActorInfo()
{
}

/*
 * ApplyEffectToSelf — 将 GameplayEffect 应用到自身
 *
 * 标准 GAS 三步流程：
 * 1. MakeEffectContext：创建带有 Source 信息的上下文
 * 2. MakeOutgoingSpec：根据 GE 类和等级创建效果规格
 * 3. ApplyGameplayEffectSpecToTarget：目标指向自身，完成属性修改
 * 用于初始化属性时，Level 通常为 1.0（可根据角色等级传入不同值）
 */
void AAuraCharacterBase::ApplyEffectToSelf(TSubclassOf<UGameplayEffect> GameplayEffectClass, float Level) const
{
	check(IsValid(GetAbilitySystemComponent()));
	check(GameplayEffectClass);
	FGameplayEffectContextHandle ContextHandle = GetAbilitySystemComponent()->MakeEffectContext();
	ContextHandle.AddSourceObject(this);
	const FGameplayEffectSpecHandle SpecHandle = GetAbilitySystemComponent()->MakeOutgoingSpec(GameplayEffectClass, Level, ContextHandle);
	GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), GetAbilitySystemComponent());
}

/*
 * InitializeDefaultAttributes — 按顺序初始化三套属性
 *
 * 调用顺序严格遵循依赖关系：
 *   Primary → Secondary（依赖 Primary 值）→ Vital（依赖 Secondary 中的 MaxHealth/MaxMana）
 * 玩家角色从存档加载时会跳过此函数，改用 InitializeDefaultAttributesFromSaveData 恢复存档值
 */
void AAuraCharacterBase::InitializeDefaultAttributes() const
{
	ApplyEffectToSelf(DefaultPrimaryAttributes, 1.f);
	ApplyEffectToSelf(DefaultSecondaryAttributes, 1.f);
	ApplyEffectToSelf(DefaultVitalAttributes, 1.f);
}

/*
 * AddCharacterAbilities — 授予初始技能
 *
 * 只在 HasAuthority()（服务器）时执行，因为技能授予是服务器权威操作，
 * GAS 会自动将授予结果复制到客户端。
 * 主动技能和被动技能分开处理，被动技能在添加后立即激活。
 */
void AAuraCharacterBase::AddCharacterAbilities()
{
	UAuraAbilitySystemComponent* AuraASC = CastChecked<UAuraAbilitySystemComponent>(AbilitySystemComponent);
	if (!HasAuthority()) return;

	AuraASC->AddCharacterAbilities(StartupAbilities);
	AuraASC->AddCharacterPassiveAbilities(StartupPassiveAbilities);
}

/*
 * Dissolve — 创建动态材质实例并启动溶解时间轴
 *
 * UMaterialInstanceDynamic 允许在运行时修改材质参数（如溶解进度），
 * 通过蓝图实现的 StartDissolveTimeline 驱动参数随时间变化，
 * 最终角色网格体从实体逐渐溶解消失，比直接 Destroy 更具视觉效果。
 */
void AAuraCharacterBase::Dissolve()
{
	if (IsValid(DissolveMaterialInstance))
	{
		UMaterialInstanceDynamic* DynamicMatInst = UMaterialInstanceDynamic::Create(DissolveMaterialInstance, this);
		GetMesh()->SetMaterial(0, DynamicMatInst);
		StartDissolveTimeline(DynamicMatInst);
	}
	if (IsValid(WeaponDissolveMaterialInstance))
	{
		UMaterialInstanceDynamic* DynamicMatInst = UMaterialInstanceDynamic::Create(WeaponDissolveMaterialInstance, this);
		Weapon->SetMaterial(0, DynamicMatInst);
		StartWeaponDissolveTimeline(DynamicMatInst);
	}
}
