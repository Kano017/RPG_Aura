// Copyright Druid Mechanics


#include "Character/AuraCharacter.h"

#include "AbilitySystemComponent.h"
#include "AuraGameplayTags.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "AbilitySystem/Data/LevelUpInfo.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/AuraPlayerController.h"
#include "Player/AuraPlayerState.h"
#include "NiagaraComponent.h"
#include "AbilitySystem/AuraAbilitySystemLibrary.h"
#include "AbilitySystem/AuraAttributeSet.h"
#include "AbilitySystem/Data/AbilityInfo.h"
#include "AbilitySystem/Debuff/DebuffNiagaraComponent.h"
#include "Camera/CameraComponent.h"
#include "Game/AuraGameModeBase.h"
#include "Game/LoadScreenSaveGame.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UI/HUD/AuraHUD.h"

AAuraCharacter::AAuraCharacter()
{
	// 弹簧臂：使用绝对旋转（不跟随 Controller 旋转），关闭碰撞检测（防止摄像机颤抖）
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>("CameraBoom");
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->bDoCollisionTest = false;

	// 摄像机附着在弹簧臂末端，不跟随 Controller 旋转（俯视角固定视角）
	TopDownCameraComponent = CreateDefaultSubobject<UCameraComponent>("TopDownCameraComponent");
	TopDownCameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TopDownCameraComponent->bUsePawnControlRotation = false;

	// 升级粒子特效：默认不自动激活，等升级时手动触发
	LevelUpNiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>("LevelUpNiagaraComponent");
	LevelUpNiagaraComponent->SetupAttachment(GetRootComponent());
	LevelUpNiagaraComponent->bAutoActivate = false;

	// 移动设置：角色朝移动方向旋转（非 Controller 方向），限制在平面上（俯视角 ARPG 标准设置）
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 400.f, 0.f);
	GetCharacterMovement()->bConstrainToPlane = true;
	GetCharacterMovement()->bSnapToPlaneAtStart = true;

	// 不使用 Controller 旋转控制角色朝向（由移动方向决定）
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	bUseControllerRotationYaw = false;

	// 玩家职业默认为元素法师
	CharacterClass = ECharacterClass::Elementalist;
}

/*
 * PossessedBy — 服务器端 GAS 初始化（Server Only）
 *
 * 当 APlayerController Possess 玩家角色时，仅在服务器调用此函数。
 * 执行顺序：
 * 1. InitAbilityActorInfo：从 PlayerState 获取并初始化 ASC
 * 2. LoadProgress：从存档加载属性/技能，或首次进入时走默认初始化
 * 3. LoadWorldState：恢复世界状态（已拾取物品、已激活传送点等）
 */
void AAuraCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 在服务器端初始化技能 Actor 信息
	InitAbilityActorInfo();
	LoadProgress();

	if (AAuraGameModeBase* AuraGameMode = Cast<AAuraGameModeBase>(UGameplayStatics::GetGameMode(this)))
	{
		AuraGameMode->LoadWorldState(GetWorld());
	}
}

/*
 * LoadProgress — 从存档恢复进度或执行首次初始化
 *
 * 【首次进入 bFirstTimeLoadIn == true】
 *   走正常的属性和技能初始化流程（InitializeDefaultAttributes + AddCharacterAbilities）
 *
 * 【读档进入 bFirstTimeLoadIn == false】
 *   从存档数据恢复：
 *   - 技能通过 AddCharacterAbilitiesFromSaveData 恢复技能状态（已解锁/已装备/等级）
 *   - PlayerState 的等级、XP、属性点、法术点直接 Set
 *   - 属性通过 InitializeDefaultAttributesFromSaveData 恢复存档中的具体数值
 *
 * 注意：此函数在 PossessedBy 中调用，只在服务器执行
 */
void AAuraCharacter::LoadProgress()
{
	AAuraGameModeBase* AuraGameMode = Cast<AAuraGameModeBase>(UGameplayStatics::GetGameMode(this));
	if (AuraGameMode)
	{
		ULoadScreenSaveGame* SaveData = AuraGameMode->RetrieveInGameSaveData();
		if (SaveData == nullptr) return;

		if (SaveData->bFirstTimeLoadIn)
		{
			// 首次进入：应用默认属性 GE 并授予初始技能
			InitializeDefaultAttributes();
			AddCharacterAbilities();
		}
		else
		{
			// 读档进入：从存档数据恢复技能状态（包含解锁状态、装备槽位、技能等级）
			if (UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(AbilitySystemComponent))
			{
				AuraASC->AddCharacterAbilitiesFromSaveData(SaveData);
			}

			// 恢复 PlayerState 中的等级、经验值和可分配点数
			if (AAuraPlayerState* AuraPlayerState = Cast<AAuraPlayerState>(GetPlayerState()))
			{
				AuraPlayerState->SetLevel(SaveData->PlayerLevel);
				AuraPlayerState->SetXP(SaveData->XP);
				AuraPlayerState->SetAttributePoints(SaveData->AttributePoints);
				AuraPlayerState->SetSpellPoints(SaveData->SpellPoints);
			}

			// 用存档中的具体属性数值覆盖 ASC 上的属性（绕过 MMC 直接设定基础值）
			UAuraAbilitySystemLibrary::InitializeDefaultAttributesFromSaveData(this, AbilitySystemComponent, SaveData);
		}
	}
}

/*
 * OnRep_PlayerState — 客户端 GAS 初始化（Client Only）
 *
 * 当 PlayerState 从服务器成功复制到客户端后调用。
 * 此时客户端才能安全地访问 PlayerState（之前 GetPlayerState 可能返回 nullptr）。
 * 在此初始化客户端的 ASC 引用和 HUD。
 * 服务器不会触发此函数（服务器已在 PossessedBy 中初始化）。
 */
void AAuraCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// 在客户端初始化技能 Actor 信息
	InitAbilityActorInfo();
}

// IPlayerInterface 实现：向 PlayerState 增加经验值
// PlayerState 内部会检查是否满足升级条件，满足则触发升级流程
void AAuraCharacter::AddToXP_Implementation(int32 InXP)
{
	AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	AuraPlayerState->AddToXP(InXP);
}

// IPlayerInterface 实现：通过 Multicast RPC 在所有客户端播放升级粒子效果
void AAuraCharacter::LevelUp_Implementation()
{
	MulticastLevelUpParticles();
}

/*
 * MulticastLevelUpParticles_Implementation — NetMulticast Reliable
 *
 * 升级粒子特效是所有玩家都应该能看到的视觉事件（不只是本地玩家），
 * 因此通过 Multicast 在所有连接的端上执行。
 * 旋转粒子组件朝向摄像机，确保升级光效面朝玩家视角，在俯视角游戏中视觉效果最佳。
 */
void AAuraCharacter::MulticastLevelUpParticles_Implementation() const
{
	if (IsValid(LevelUpNiagaraComponent))
	{
		const FVector CameraLocation = TopDownCameraComponent->GetComponentLocation();
		const FVector NiagaraSystemLocation = LevelUpNiagaraComponent->GetComponentLocation();
		const FRotator ToCameraRotation = (CameraLocation - NiagaraSystemLocation).Rotation();
		LevelUpNiagaraComponent->SetWorldRotation(ToCameraRotation);
		LevelUpNiagaraComponent->Activate(true);
	}
}

// IPlayerInterface 实现：从 PlayerState 读取当前经验值
int32 AAuraCharacter::GetXP_Implementation() const
{
	const AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	return AuraPlayerState->GetXP();
}

// IPlayerInterface 实现：从 LevelUpInfo 数据表反查 XP 对应的等级
// 用于连续升级场景：一次性获得大量 XP 时，需要逐级计算
int32 AAuraCharacter::FindLevelForXP_Implementation(int32 InXP) const
{
	const AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	return AuraPlayerState->LevelUpInfo->FindLevelForXP(InXP);
}

// IPlayerInterface 实现：查询指定等级的属性点奖励数量
// 升级时由技能（GA_ListenForEvents）调用，决定给玩家分配多少属性点
int32 AAuraCharacter::GetAttributePointsReward_Implementation(int32 Level) const
{
	const AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	return AuraPlayerState->LevelUpInfo->LevelUpInformation[Level].AttributePointAward;
}

// IPlayerInterface 实现：查询指定等级的法术点奖励数量
int32 AAuraCharacter::GetSpellPointsReward_Implementation(int32 Level) const
{
	const AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	return AuraPlayerState->LevelUpInfo->LevelUpInformation[Level].SpellPointAward;
}

// IPlayerInterface 实现：增加玩家等级
// 同时触发 ASC 更新技能解锁状态，新等级可能会解锁之前灰色的技能
void AAuraCharacter::AddToPlayerLevel_Implementation(int32 InPlayerLevel)
{
	AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	AuraPlayerState->AddToLevel(InPlayerLevel);

	if (UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(GetAbilitySystemComponent()))
	{
		// 等级提升后重新检查所有技能的解锁条件，更新技能状态（Locked -> Eligible -> Unlocked）
		AuraASC->UpdateAbilityStatuses(AuraPlayerState->GetPlayerLevel());
	}
}

// IPlayerInterface 实现：增加可分配属性点数（用于属性面板中手动分配）
void AAuraCharacter::AddToAttributePoints_Implementation(int32 InAttributePoints)
{
	AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	AuraPlayerState->AddToAttributePoints(InAttributePoints);
}

// IPlayerInterface 实现：增加可分配法术点数（用于技能菜单中解锁/升级技能）
void AAuraCharacter::AddToSpellPoints_Implementation(int32 InSpellPoints)
{
	AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	AuraPlayerState->AddToSpellPoints(InSpellPoints);
}

// IPlayerInterface 实现：查询当前剩余可分配属性点
int32 AAuraCharacter::GetAttributePoints_Implementation() const
{
	AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	return AuraPlayerState->GetAttributePoints();
}

// IPlayerInterface 实现：查询当前剩余可分配法术点
int32 AAuraCharacter::GetSpellPoints_Implementation() const
{
	AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	return AuraPlayerState->GetSpellPoints();
}

// IPlayerInterface 实现：显示法术瞄准魔法圆（委托给 PlayerController 处理显示逻辑）
// 同时隐藏普通鼠标指针，避免双指针干扰
void AAuraCharacter::ShowMagicCircle_Implementation(UMaterialInterface* DecalMaterial)
{
	if (AAuraPlayerController* AuraPlayerController = Cast<AAuraPlayerController>(GetController()))
	{
		AuraPlayerController->ShowMagicCircle(DecalMaterial);
		AuraPlayerController->bShowMouseCursor = false;
	}
}

// IPlayerInterface 实现：隐藏法术瞄准魔法圆，恢复普通鼠标指针
void AAuraCharacter::HideMagicCircle_Implementation()
{
	if (AAuraPlayerController* AuraPlayerController = Cast<AAuraPlayerController>(GetController()))
	{
		AuraPlayerController->HideMagicCircle();
		AuraPlayerController->bShowMouseCursor = true;
	}
}

/*
 * SaveProgress_Implementation — IPlayerInterface 实现
 *
 * 触发时机：玩家踩到检查点（Checkpoint Actor）时调用。
 * 保存内容：
 *   - 检查点 Tag（下次进入游戏时从此检查点位置出生）
 *   - PlayerState 中的等级、XP、属性点、法术点
 *   - 四项 Primary 属性的当前数值（Strength / Intelligence / Resilience / Vigor）
 *   - 所有技能的状态（类型、等级、槽位、解锁状态）
 *   - bFirstTimeLoadIn 标记为 false（后续进入走读档路径）
 *
 * 注意：技能保存需要 HasAuthority（服务器），因为 ForEachAbility 操作 ASC 内部数据
 */
void AAuraCharacter::SaveProgress_Implementation(const FName& CheckpointTag)
{
	AAuraGameModeBase* AuraGameMode = Cast<AAuraGameModeBase>(UGameplayStatics::GetGameMode(this));
	if (AuraGameMode)
	{
		ULoadScreenSaveGame* SaveData = AuraGameMode->RetrieveInGameSaveData();
		if (SaveData == nullptr) return;

		// 记录当前检查点 Tag，下次加载时角色从此位置出生
		SaveData->PlayerStartTag = CheckpointTag;

		// 保存等级和点数状态
		if (AAuraPlayerState* AuraPlayerState = Cast<AAuraPlayerState>(GetPlayerState()))
		{
			SaveData->PlayerLevel = AuraPlayerState->GetPlayerLevel();
			SaveData->XP = AuraPlayerState->GetXP();
			SaveData->AttributePoints = AuraPlayerState->GetAttributePoints();
			SaveData->SpellPoints = AuraPlayerState->GetSpellPoints();
		}
		// 通过 FGameplayAttribute 的静态 getter 从 AttributeSet 中直接读取数值
		// 这种方式比 GetAttributeSet()->GetStrength() 更通用，无需强转
		SaveData->Strength = UAuraAttributeSet::GetStrengthAttribute().GetNumericValue(GetAttributeSet());
		SaveData->Intelligence = UAuraAttributeSet::GetIntelligenceAttribute().GetNumericValue(GetAttributeSet());
		SaveData->Resilience = UAuraAttributeSet::GetResilienceAttribute().GetNumericValue(GetAttributeSet());
		SaveData->Vigor = UAuraAttributeSet::GetVigorAttribute().GetNumericValue(GetAttributeSet());

		// 标记为非首次加载，下次进入游戏走读档路径
		SaveData->bFirstTimeLoadIn = false;

		// 技能数据操作只在服务器执行
		if (!HasAuthority()) return;

		UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(AbilitySystemComponent);
		FForEachAbility SaveAbilityDelegate;
		SaveData->SavedAbilities.Empty();
		// 遍历所有已授予的技能，将每个技能的状态序列化到存档
		SaveAbilityDelegate.BindLambda([this, AuraASC, SaveData](const FGameplayAbilitySpec& AbilitySpec)
		{
			const FGameplayTag AbilityTag = AuraASC->GetAbilityTagFromSpec(AbilitySpec);
			UAbilityInfo* AbilityInfo = UAuraAbilitySystemLibrary::GetAbilityInfo(this);
			FAuraAbilityInfo Info = AbilityInfo->FindAbilityInfoForTag(AbilityTag);

			FSavedAbility SavedAbility;
			SavedAbility.GameplayAbility = Info.Ability;
			SavedAbility.AbilityLevel = AbilitySpec.Level;
			SavedAbility.AbilitySlot = AuraASC->GetSlotFromAbilityTag(AbilityTag);
			SavedAbility.AbilityStatus = AuraASC->GetStatusFromAbilityTag(AbilityTag);
			SavedAbility.AbilityTag = AbilityTag;
			SavedAbility.AbilityType = Info.AbilityType;

			SaveData->SavedAbilities.AddUnique(SavedAbility);

		});
		AuraASC->ForEachAbility(SaveAbilityDelegate);

		// 将组装好的存档数据写入磁盘
		AuraGameMode->SaveInGameProgressData(SaveData);
	}
}

// ICombatInterface 实现（覆盖基类）：从 PlayerState 获取玩家等级
// 玩家等级影响技能伤害计算（ExecCalc_Damage 中用到）
int32 AAuraCharacter::GetPlayerLevel_Implementation()
{
	const AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	return AuraPlayerState->GetPlayerLevel();
}

/*
 * Die — 玩家死亡处理（覆盖基类）
 *
 * 在基类 Die（分离武器 + Multicast 布娃娃效果）的基础上，玩家额外需要：
 * 1. 启动 DeathTimer：等待 DeathTime 秒后通知 GameMode 执行重生逻辑
 * 2. 分离摄像机：死亡后摄像机保持在原位不随布娃娃乱飞，
 *    给玩家一个固定视角观看死亡动画
 */
void AAuraCharacter::Die(const FVector& DeathImpulse)
{
	Super::Die(DeathImpulse);

	FTimerDelegate DeathTimerDelegate;
	DeathTimerDelegate.BindLambda([this]()
	{
		AAuraGameModeBase* AuraGM = Cast<AAuraGameModeBase>(UGameplayStatics::GetGameMode(this));
		if (AuraGM)
		{
			// 通知 GameMode 执行重生流程（重新加载关卡或传送到检查点）
			AuraGM->PlayerDied(this);
		}
	});
	GetWorldTimerManager().SetTimer(DeathTimer, DeathTimerDelegate, DeathTime, false);
	// 保持摄像机在死亡位置，避免随布娃娃物理移动而抖动
	TopDownCameraComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
}

/*
 * OnRep_Stunned — 客户端眩晕状态响应（覆盖基类空实现）
 *
 * 玩家眩晕时不仅需要停止移动（基类 StunTagChanged 在服务器处理），
 * 还需要在客户端阻断所有输入。通过向本地 ASC 添加 LooseGameplayTag 实现：
 *   - Player.Block.CursorTrace / InputHeld / InputPressed / InputReleased
 * 这些 Block Tag 被 AuraPlayerController 读取，从而屏蔽玩家操作。
 * 眩晕解除时移除这些 Tag，恢复正常输入。
 */
void AAuraCharacter::OnRep_Stunned()
{
	if (UAuraAbilitySystemComponent* AuraASC = Cast<UAuraAbilitySystemComponent>(AbilitySystemComponent))
	{
		const FAuraGameplayTags& GameplayTags = FAuraGameplayTags::Get();
		FGameplayTagContainer BlockedTags;
		BlockedTags.AddTag(GameplayTags.Player_Block_CursorTrace);
		BlockedTags.AddTag(GameplayTags.Player_Block_InputHeld);
		BlockedTags.AddTag(GameplayTags.Player_Block_InputPressed);
		BlockedTags.AddTag(GameplayTags.Player_Block_InputReleased);
		if (bIsStunned)
		{
			// 眩晕生效：添加输入阻断 Tag 并激活眩晕粒子特效
			AuraASC->AddLooseGameplayTags(BlockedTags);
			StunDebuffComponent->Activate();
		}
		else
		{
			// 眩晕解除：移除输入阻断 Tag 并停用眩晕特效
			AuraASC->RemoveLooseGameplayTags(BlockedTags);
			StunDebuffComponent->Deactivate();
		}
	}
}

// OnRep_Burned — 客户端燃烧状态响应（覆盖基类空实现）
// 根据 bIsBurned 激活或停用燃烧粒子特效
void AAuraCharacter::OnRep_Burned()
{
	if (bIsBurned)
	{
		BurnDebuffComponent->Activate();
	}
	else
	{
		BurnDebuffComponent->Deactivate();
	}
}

/*
 * InitAbilityActorInfo — 玩家 ASC 初始化（Server 和 Client 各自调用一次）
 *
 * 【核心步骤详解】
 * 1. 获取 PlayerState（ASC 和 AttributeSet 的真正拥有者）
 * 2. 调用 ASC->InitAbilityActorInfo(PlayerState, Character)：
 *    告知 GAS 框架：
 *      Owner = PlayerState（数据持久化，断线重连不丢失）
 *      Avatar = Character（实际的游戏世界实体，用于动画、位置等）
 * 3. AbilityActorInfoSet()：通知 AuraASC 初始化完成，内部绑定各种 Tag 监听
 * 4. 缓存 ASC 和 AttributeSet 引用到基类成员变量，方便快速访问
 * 5. 广播 OnAscRegistered：PassiveNiagaraComponent 等待此事件后才能安全访问 ASC
 * 6. 注册 Stun Tag 回调：眩晕时调用 StunTagChanged
 * 7. 初始化 HUD：只有本地拥有者（HasAuthority 的 Controller 或本地 Client）才有 HUD
 */
void AAuraCharacter::InitAbilityActorInfo()
{
	AAuraPlayerState* AuraPlayerState = GetPlayerState<AAuraPlayerState>();
	check(AuraPlayerState);
	// Owner = PlayerState（技能、属性数据归属于玩家状态）
	// Avatar = this（技能效果在 Character 身上表现，如动画、位置）
	AuraPlayerState->GetAbilitySystemComponent()->InitAbilityActorInfo(AuraPlayerState, this);
	Cast<UAuraAbilitySystemComponent>(AuraPlayerState->GetAbilitySystemComponent())->AbilityActorInfoSet();
	// 将 PlayerState 上的 ASC 引用缓存到基类，后续代码通过 AbilitySystemComponent 访问
	AbilitySystemComponent = AuraPlayerState->GetAbilitySystemComponent();
	AttributeSet = AuraPlayerState->GetAttributeSet();
	// 通知所有监听者（被动技能特效组件等）ASC 已就绪
	OnAscRegistered.Broadcast(AbilitySystemComponent);
	// 监听眩晕 Tag 变化，以便服务器端控制移动速度
	AbilitySystemComponent->RegisterGameplayTagEvent(FAuraGameplayTags::Get().Debuff_Stun, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &AAuraCharacter::StunTagChanged);

	// 初始化 HUD：只有本地玩家才有 Controller 和 HUD
	// 服务器上其他玩家的 Character 没有本地 Controller，此处 Cast 会失败，自然跳过
	if (AAuraPlayerController* AuraPlayerController = Cast<AAuraPlayerController>(GetController()))
	{
		if (AAuraHUD* AuraHUD = Cast<AAuraHUD>(AuraPlayerController->GetHUD()))
		{
			AuraHUD->InitOverlay(AuraPlayerController, AuraPlayerState, AbilitySystemComponent, AttributeSet);
		}
	}
}
