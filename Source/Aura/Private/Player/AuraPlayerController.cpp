// Copyright Druid Mechanics


#include "Player/AuraPlayerController.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AuraGameplayTags.h"
#include "EnhancedInputSubsystems.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "AbilitySystem/AuraAbilitySystemComponent.h"
#include "Actor/MagicCircle.h"
#include "Aura/Aura.h"
#include "Components/DecalComponent.h"
#include "Components/SplineComponent.h"
#include "Input/AuraInputComponent.h"
#include "Interaction/EnemyInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "Character/AuraCharacter.h"
#include "Interaction/HighlightInterface.h"
#include "Player/AuraCheatManager.h"
#include "UI/Widget/DamageTextComponent.h"

/**
 * 构造函数：
 *   - bReplicates = true：PlayerController 在多人游戏中需要复制（服务器和客户端各有一个）
 *   - 创建 Spline 默认子对象：Spline 用于存储自动寻路路径，需在构造时创建
 */
AAuraPlayerController::AAuraPlayerController()
{
	bReplicates = true;
	Spline = CreateDefaultSubobject<USplineComponent>("Spline");
	CheatClass = UAuraCheatManager::StaticClass();
}

/**
 * 每帧执行三件事：
 *   1. CursorTrace()             —— 更新鼠标悬停目标高亮
 *   2. AutoRun()                 —— 自动寻路移动（bAutoRunning 为 false 时立刻返回）
 *   3. UpdateMagicCircleLocation —— 魔法圆阵跟随鼠标（未激活时立刻返回）
 */
void AAuraPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	CursorTrace();
	AutoRun();
	UpdateMagicCircleLocation();
}

/**
 * 显示魔法圆阵（AoE 技能选择施法位置时调用）。
 * 若圆阵已存在则不重复创建（IsValid 检查避免重复 Spawn）。
 * 可选传入自定义材质，不同技能可呈现不同的圆阵外观。
 */
void AAuraPlayerController::ShowMagicCircle(UMaterialInterface* DecalMaterial)
{
	if (!IsValid(MagicCircle))
	{
		MagicCircle = GetWorld()->SpawnActor<AMagicCircle>(MagicCircleClass);
		if (DecalMaterial)
		{
			MagicCircle->MagicCircleDecal->SetMaterial(0, DecalMaterial);
		}
	}
}

/**
 * 销毁魔法圆阵，技能确认施法位置或取消时调用。
 */
void AAuraPlayerController::HideMagicCircle()
{
	if (IsValid(MagicCircle))
	{
		MagicCircle->Destroy();
	}
}

/**
 * UI 打开时调用：暂停游戏并禁用玩家操控输入（支持多层 UI 嵌套）。
 *
 * 【计数器机制】
 *   GamePauseCounter 记录当前有多少层 UI 要求暂停状态。
 *   仅当计数器从 0 变为 1 时执行真正的暂停操作，
 *   避免嵌套 UI 场景（如背包内打开装备详情）导致重复暂停。
 *
 * 【暂停步骤】
 *   1. SetPause(true)     — 引擎层暂停，WorldSettings.TimeDilation 归零，敌人/物理静止
 *   2. GAS 阻断标签      — 向 ASC 添加四个 Block 标签，AuraPlayerController 的所有输入回调
 *                           均在顶部检查这些标签，激活则立即 return，彻底阻断玩家操控
 *   3. FInputModeUIOnly  — 鼠标事件仅路由给 UI，不穿透到游戏世界
 */
void AAuraPlayerController::ShowUI_PauseGame()
{
	GamePauseCounter++;
	if (GamePauseCounter == 1)
	{
		// 1. 暂停游戏时间（TimeDilation = 0）
		SetPause(true);

		// 2. 通过 GAS 标签阻断所有玩家输入路径
		if (GetASC())
		{
			const FAuraGameplayTags& AuraTags = FAuraGameplayTags::Get();
			GetASC()->AddLooseGameplayTag(AuraTags.Player_Block_InputPressed);
			GetASC()->AddLooseGameplayTag(AuraTags.Player_Block_InputHeld);
			GetASC()->AddLooseGameplayTag(AuraTags.Player_Block_InputReleased);
			GetASC()->AddLooseGameplayTag(AuraTags.Player_Block_CursorTrace);
		}

		// 3. 切换为纯 UI 输入模式，鼠标点击不再穿透到游戏世界
		const FInputModeUIOnly InputModeData;
		SetInputMode(InputModeData);
		bShowMouseCursor = true;
	}
}

/**
 * UI 关闭时调用：恢复游戏并启用玩家操控输入（支持多层 UI 嵌套）。
 *
 * 【计数器机制】
 *   仅当计数器从 1 变为 0 时执行真正的恢复操作。
 *   FMath::Max(0, ...) 防止因调用不匹配导致计数器变为负数。
 *
 * 【恢复步骤】
 *   1. SetPause(false)           — 恢复游戏时间
 *   2. 移除 GAS 阻断标签         — 输入路径重新响应玩家操作
 *   3. FInputModeGameAndUI       — 恢复游戏+UI 混合输入模式（与 BeginPlay 一致）
 */
void AAuraPlayerController::HideUI_ResumeGame()
{
	GamePauseCounter = FMath::Max(0, GamePauseCounter - 1);
	if (GamePauseCounter == 0)
	{
		// 1. 恢复游戏时间
		SetPause(false);

		// 2. 移除所有输入阻断标签，玩家恢复完整操控能力
		if (GetASC())
		{
			const FAuraGameplayTags& AuraTags = FAuraGameplayTags::Get();
			GetASC()->RemoveLooseGameplayTag(AuraTags.Player_Block_InputPressed);
			GetASC()->RemoveLooseGameplayTag(AuraTags.Player_Block_InputHeld);
			GetASC()->RemoveLooseGameplayTag(AuraTags.Player_Block_InputReleased);
			GetASC()->RemoveLooseGameplayTag(AuraTags.Player_Block_CursorTrace);
		}

		// 3. 恢复游戏+UI 混合输入模式（与 BeginPlay 中保持一致）
		FInputModeGameAndUI InputModeData;
		InputModeData.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputModeData.SetHideCursorDuringCapture(false);
		SetInputMode(InputModeData);
	}
}

/**
 * Client RPC 实现：在目标角色上创建漂浮伤害数字。
 *
 * 【为何使用动态创建的 Component 而非 Actor？】
 *   DamageTextComponent 是挂载在角色上的 UMG 组件，
 *   相比单独 Spawn Actor 更轻量，且天然跟随角色位置。
 *   它在显示完成后自动销毁，无需手动管理生命周期。
 *
 * 【Attach + Detach 的原因】
 *   先 Attach（KeepRelative）让组件初始位置对齐角色根组件，
 *   再立刻 Detach（KeepWorld）让组件在世界坐标中自由运动（播放浮起动画），
 *   不再跟随角色移动，视觉上更自然。
 */
void AAuraPlayerController::ShowDamageNumber_Implementation(float DamageAmount, ACharacter* TargetCharacter, bool bBlockedHit, bool bCriticalHit)
{
	if (IsValid(TargetCharacter) && DamageTextComponentClass && IsLocalController())
	{
		UDamageTextComponent* DamageText = NewObject<UDamageTextComponent>(TargetCharacter, DamageTextComponentClass);
		DamageText->RegisterComponent();
		// 先相对附加，获取正确的初始位置
		DamageText->AttachToComponent(TargetCharacter->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		// 立刻解除附加，让数字在世界空间中自由浮动
		DamageText->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		DamageText->SetDamageText(DamageAmount, bBlockedHit, bCriticalHit);
	}
}

/**
 * 每帧执行自动寻路：沿 Spline 路径驱动 Pawn 移动直至到达目标。
 *
 * 【工作原理】
 *   Spline 上存储了 NavigationSystem 计算出的路径点。
 *   FindLocationClosestToWorldLocation 找到 Spline 上最接近 Pawn 当前位置的点，
 *   FindDirectionClosestToWorldLocation 获取该点处 Spline 的切线方向（即前进方向），
 *   AddMovementInput 让 Pawn 向该方向移动（由 CharacterMovementComponent 处理物理）。
 *   当 Pawn 足够接近 CachedDestination 时，bAutoRunning = false，停止寻路。
 */
void AAuraPlayerController::AutoRun()
{
	if (!bAutoRunning) return;
	if (APawn* ControlledPawn = GetPawn())
	{
		// 在 Spline 上找到最近的位置点
		const FVector LocationOnSpline = Spline->FindLocationClosestToWorldLocation(ControlledPawn->GetActorLocation(), ESplineCoordinateSpace::World);
		// 获取该位置处的 Spline 切线方向（即沿路径的前进方向）
		const FVector Direction = Spline->FindDirectionClosestToWorldLocation(LocationOnSpline, ESplineCoordinateSpace::World);
		ControlledPawn->AddMovementInput(Direction);

		// 检查是否已到达目标附近，到达则停止自动寻路
		const float DistanceToDestination = (LocationOnSpline - CachedDestination).Length();
		if (DistanceToDestination <= AutoRunAcceptanceRadius)
		{
			bAutoRunning = false;
		}
	}
}

/**
 * 每帧将魔法圆阵的位置同步到鼠标命中点。
 * CursorHit.ImpactPoint 在 CursorTrace() 中每帧更新，
 * 两者在 PlayerTick 中连续调用，确保圆阵实时跟随鼠标。
 */
void AAuraPlayerController::UpdateMagicCircleLocation()
{
	if (IsValid(MagicCircle))
	{
		MagicCircle->SetActorLocation(CursorHit.ImpactPoint);
	}
}

/**
 * 对实现 IHighlightInterface 的 Actor 执行高亮（轮廓发光效果）。
 * 静态函数，无副作用，直接通过接口分发，不依赖具体 Actor 类型。
 */
void AAuraPlayerController::HighlightActor(AActor* InActor)
{
	if (IsValid(InActor) && InActor->Implements<UHighlightInterface>())
	{
		IHighlightInterface::Execute_HighlightActor(InActor);
	}
}

/**
 * 取消 Actor 的高亮效果。
 */
void AAuraPlayerController::UnHighlightActor(AActor* InActor)
{
	if (IsValid(InActor) && InActor->Implements<UHighlightInterface>())
	{
		IHighlightInterface::Execute_UnHighlightActor(InActor);
	}
}

/**
 * 每帧执行鼠标光标射线检测，实时更新鼠标悬停目标的高亮状态。
 *
 * 【阻断检查】
 *   若 ASC 上激活了 Player_Block_CursorTrace 标签（例如施法动作中），
 *   清除当前高亮并直接返回，避免施法期间误高亮目标。
 *
 * 【检测通道选择】
 *   - 正常情况：ECC_Visibility（检测所有可见物体）
 *   - 魔法圆阵显示中：ECC_ExcludePlayers（排除玩家碰撞体，使圆阵能贴地显示）
 *
 * 【LastActor / ThisActor 切换机制】
 *   每帧先将 ThisActor 记录为 LastActor，再用本帧检测结果更新 ThisActor。
 *   只有当两者不同时才调用高亮/取消高亮，避免无意义的重复调用。
 */
void AAuraPlayerController::CursorTrace()
{
	// 若施法阻断标签激活，清除高亮并停止检测
	if (GetASC() && GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_CursorTrace))
	{
		UnHighlightActor(LastActor);
		UnHighlightActor(ThisActor);
		if (IsValid(ThisActor) && ThisActor->Implements<UHighlightInterface>())

		LastActor = nullptr;
		ThisActor = nullptr;
		return;
	}
	// 魔法圆阵显示时切换到排除玩家的通道，避免玩家碰撞体阻挡地面检测
	const ECollisionChannel TraceChannel = IsValid(MagicCircle) ? ECC_ExcludePlayers : ECC_Visibility;
	GetHitResultUnderCursor(TraceChannel, false, CursorHit);
	if (!CursorHit.bBlockingHit) return;

	// 更新 LastActor 和 ThisActor
	LastActor = ThisActor;
	if (IsValid(CursorHit.GetActor()) && CursorHit.GetActor()->Implements<UHighlightInterface>())
	{
		ThisActor = CursorHit.GetActor();
	}
	else
	{
		ThisActor = nullptr;
	}

	// 仅在目标切换时更新高亮状态，避免每帧重复调用
	if (LastActor != ThisActor)
	{
		UnHighlightActor(LastActor);
		HighlightActor(ThisActor);
	}
}

/**
 * 技能键按下时的处理（所有输入键共用此入口，通过 InputTag 区分）。
 *
 * 【LMB 特殊处理】
 *   记录按下瞬间的瞄准状态（ThisActor 是否是敌人），
 *   此状态将在 Held/Released 中使用来决定走攻击路线还是寻路路线。
 *   同时停止当前自动寻路（新的点击意图覆盖旧的寻路目标）。
 *
 * 【输入阻断检查】
 *   Player_Block_InputPressed 标签激活时（如特定施法动作锁定输入），
 *   忽略所有按下事件，防止打断关键技能序列。
 */
void AAuraPlayerController::AbilityInputTagPressed(FGameplayTag InputTag)
{
	if (GetASC() && GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_InputPressed))
	{
		return;
	}
	if (InputTag.MatchesTagExact(FAuraGameplayTags::Get().InputTag_LMB))
	{
		// 记录按下瞬间的瞄准状态：是敌人/非敌人/空地
		if (IsValid(ThisActor))
		{
			TargetingStatus = ThisActor->Implements<UEnemyInterface>() ? ETargetingStatus::TargetingEnemy : ETargetingStatus::TargetingNonEnemy;
		}
		else
		{
			TargetingStatus = ETargetingStatus::NotTargeting;
		}
		// 新的点击操作开始，取消之前的自动寻路
		bAutoRunning = false;
	}
	// 通知 ASC 按键已按下（ASC 根据 Tag 找到绑定的技能并触发 WaitForInputPress 等任务）
	if (GetASC()) GetASC()->AbilityInputTagPressed(InputTag);
}

/**
 * 技能键抬起时的处理。
 *
 * 【非 LMB】直接转发给 ASC（结束持续技能 / 触发 WaitForInputRelease 任务）。
 *
 * 【LMB 逻辑分支】
 *   先无条件通知 ASC 按键已释放（无论走哪个分支，ASC 都需要知道按键状态）。
 *   然后判断是否触发自动寻路：
 *     条件：未瞄准敌人 AND 未按 Shift AND 持续时间 < 短按阈值
 *     如果满足：
 *       - 若目标点有可交互对象（IHighlightInterface），通知其设置移动目标（如点击 NPC）
 *       - 否则在目标点播放点击特效（Niagara），视觉反馈玩家点击位置
 *       - 调用 NavigationSystem 同步计算路径，写入 Spline，启动自动寻路
 *     执行完后重置 FollowTime 和 TargetingStatus。
 */
void AAuraPlayerController::AbilityInputTagReleased(FGameplayTag InputTag)
{
	if (GetASC() && GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_InputReleased))
	{
		return;
	}
	// 非 LMB：直接转发给 ASC，不处理寻路逻辑
	if (!InputTag.MatchesTagExact(FAuraGameplayTags::Get().InputTag_LMB))
	{
		if (GetASC()) GetASC()->AbilityInputTagReleased(InputTag);
		return;
	}

	// LMB 释放：先通知 ASC（技能层的按键释放处理）
	if (GetASC()) GetASC()->AbilityInputTagReleased(InputTag);

	// 未瞄准敌人且未按 Shift：判断是否触发自动寻路
	if (TargetingStatus != ETargetingStatus::TargetingEnemy && !bShiftKeyDown)
	{
		const APawn* ControlledPawn = GetPawn();
		// 短按且键盘未按住才触发寻路；长按是持续移动，键盘移动中也不触发
		if (FollowTime <= ShortPressThreshold && ControlledPawn && !bKeyboardMoving)
		{
			// 目标点有可交互的高亮对象（如传送点），通知它设置移动目标
			if (IsValid(ThisActor) && ThisActor->Implements<UHighlightInterface>())
			{
				IHighlightInterface::Execute_SetMoveToLocation(ThisActor, CachedDestination);
			}
			else if (GetASC() && !GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_InputPressed))
			{
				// 普通地面点击：播放点击 Niagara 特效作为视觉反馈
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ClickNiagaraSystem, CachedDestination);
			}
			// 使用导航系统同步计算从 Pawn 到 CachedDestination 的路径
			if (UNavigationPath* NavPath = UNavigationSystemV1::FindPathToLocationSynchronously(this, ControlledPawn->GetActorLocation(), CachedDestination))
			{
				// 清空旧路径，写入新路径点到 Spline
				Spline->ClearSplinePoints();
				for (const FVector& PointLoc : NavPath->PathPoints)
				{
					Spline->AddSplinePoint(PointLoc, ESplineCoordinateSpace::World);
				}
				// 取路径最后一个点作为实际目标（防止导航调整导致目标偏移）
				if (NavPath->PathPoints.Num() > 0)
				{
					CachedDestination = NavPath->PathPoints[NavPath->PathPoints.Num() - 1];
					bAutoRunning = true;
				}
			}
		}
		// 重置按键持续时间和瞄准状态，为下次点击做准备
		FollowTime = 0.f;
		TargetingStatus = ETargetingStatus::NotTargeting;
	}
}

/**
 * 技能键持续按下（每帧调用）。
 *
 * 【非 LMB】直接转发给 ASC（持续施法技能每帧激活）。
 *
 * 【LMB 逻辑分支】
 *   - 瞄准敌人 or 按住 Shift → 转发给 ASC（持续攻击/强制技能模式）
 *   - 否则（移动模式）：
 *       累积 FollowTime（超过阈值后 Released 时不触发寻路）
 *       更新 CachedDestination 为当前鼠标命中点
 *       直接给 Pawn 添加朝目标方向的移动输入（实时跟随鼠标移动）
 *
 * 【为何移动模式下也要 AddMovementInput 而不只等待 Released 的寻路？】
 *   长按模式应立即移动（玩家期望鼠标长按时角色实时跟随），
 *   寻路只在短按时触发（单次点击导航到远处目标的快捷方式）。
 */
void AAuraPlayerController::AbilityInputTagHeld(FGameplayTag InputTag)
{
	if (GetASC() && GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_InputHeld))
	{
		return;
	}
	// 非 LMB：直接交给 ASC 处理持续技能
	if (!InputTag.MatchesTagExact(FAuraGameplayTags::Get().InputTag_LMB))
	{
		if (GetASC()) GetASC()->AbilityInputTagHeld(InputTag);
		return;
	}

	// 键盘移动时强制走技能路径（不判断瞄准状态）；瞄准敌人或按 Shift 同理
	if (TargetingStatus == ETargetingStatus::TargetingEnemy || bShiftKeyDown || bKeyboardMoving)
	{
		// 技能/攻击模式：持续激活绑定到 LMB 的技能
		if (GetASC()) GetASC()->AbilityInputTagHeld(InputTag);
	}
	else
	{
		// 移动模式：长按跟随鼠标移动
		FollowTime += GetWorld()->GetDeltaSeconds();
		// 更新目标位置为当前鼠标命中点
		if (CursorHit.bBlockingHit) CachedDestination = CursorHit.ImpactPoint;

		// 向目标点方向直接添加移动输入（不经过寻路，实时响应）
		if (APawn* ControlledPawn = GetPawn())
		{
			const FVector WorldDirection = (CachedDestination - ControlledPawn->GetActorLocation()).GetSafeNormal();
			ControlledPawn->AddMovementInput(WorldDirection);
		}
	}
}

/**
 * 懒加载获取 ASC。
 * 通过 AbilitySystemBlueprintLibrary 从当前控制的 Pawn 上获取 ASC。
 * 不在 BeginPlay 中缓存是因为网络环境下 Pawn Possess 时序不确定，
 * 可能 BeginPlay 时 Pawn 尚未持有 ASC（如 PlayerState 上的 ASC 需要等复制完成）。
 */
UAuraAbilitySystemComponent* AAuraPlayerController::GetASC()
{
	if (AuraAbilitySystemComponent == nullptr)
	{
		AuraAbilitySystemComponent = Cast<UAuraAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetPawn<APawn>()));
	}
	return AuraAbilitySystemComponent;
}

/**
 * 初始化 Enhanced Input 系统并配置鼠标显示模式。
 *
 * 【Enhanced Input 注册】
 *   AddMappingContext 将预配置的输入映射上下文注册到 EnhancedInputLocalPlayerSubsystem，
 *   优先级 0 表示最低优先级（数字越大优先级越高），可被其他上下文覆盖。
 *
 * 【鼠标模式配置】
 *   GameAndUI 模式允许鼠标同时控制游戏和 UI（点击 UI 不会消耗游戏输入），
 *   DoNotLock 允许鼠标移出视口（多显示器友好），
 *   不隐藏鼠标（HideCursorDuringCapture = false）保持光标可见。
 */
void AAuraPlayerController::BeginPlay()
{
	Super::BeginPlay();
	check(AuraContext);

	// 注册输入映射上下文（仅在本地玩家端有效，Subsystem 是本地子系统）
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(AuraContext, 0);
	}

	// 显示鼠标光标（TopDown RPG 需要可见的光标用于点击移动和技能瞄准）
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;

	// 游戏+UI 混合输入模式：允许同时操作游戏世界和 UI 面板
	FInputModeGameAndUI InputModeData;
	InputModeData.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputModeData.SetHideCursorDuringCapture(false);
	SetInputMode(InputModeData);
}

/**
 * 绑定所有输入动作到对应的处理函数。
 *
 * 【UAuraInputComponent 的作用】
 *   继承自 UEnhancedInputComponent，扩展了 BindAbilityActions 方法，
 *   可以批量将 InputConfig DataAsset 中配置的 InputTag → InputAction 映射
 *   绑定到三个统一的回调（Pressed/Released/Held），回调中通过 InputTag 区分具体键位。
 *   这样新增/修改技能键位只需修改 DataAsset，不需要改代码。
 */
void AAuraPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UAuraInputComponent* AuraInputComponent = CastChecked<UAuraInputComponent>(InputComponent);
	// 键盘移动：WASD / 方向键，Completed 用于追踪键盘是否仍在按住
	AuraInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AAuraPlayerController::Move);
	AuraInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &AAuraPlayerController::MoveCompleted);
	// 鼠标滚轮缩放
	if (CameraFovAction)
	{
		AuraInputComponent->BindAction(CameraFovAction, ETriggerEvent::Triggered, this, &AAuraPlayerController::CameraZoom);
	}
	// Shift 键：强制技能/攻击模式
	AuraInputComponent->BindAction(ShiftAction, ETriggerEvent::Started, this, &AAuraPlayerController::ShiftPressed);
	AuraInputComponent->BindAction(ShiftAction, ETriggerEvent::Completed, this, &AAuraPlayerController::ShiftReleased);
	// 所有技能键（LMB/RMB/1/2/3/4 等）：统一绑定到三个阶段回调
	AuraInputComponent->BindAbilityActions(InputConfig, this, &ThisClass::AbilityInputTagPressed, &ThisClass::AbilityInputTagReleased, &ThisClass::AbilityInputTagHeld);
}

/**
 * 键盘移动处理（WASD / 方向键）。
 *
 * 【方向计算】
 *   基于控制器 Yaw 旋转（相机水平朝向）而非角色朝向计算前/右方向，
 *   确保按 W 始终朝摄像机视角的"前方"移动（TopDown 视角下为画面上方）。
 *   忽略 Pitch/Roll 分量避免角色沿斜面飞起。
 *
 * 【输入阻断】
 *   Player_Block_InputPressed 标签激活时忽略键盘移动输入（与技能键共用阻断标签）。
 */
void AAuraPlayerController::Move(const FInputActionValue& InputActionValue)
{
	if (GetASC() && GetASC()->HasMatchingGameplayTag(FAuraGameplayTags::Get().Player_Block_InputPressed))
	{
		return;
	}
	const FVector2D InputAxisVector = InputActionValue.Get<FVector2D>();
	const FRotator Rotation = GetControlRotation();
	// 仅取 Yaw 分量，消除 Pitch 倾斜的影响
	const FRotator YawRotation(0.f, Rotation.Yaw, 0.f);

	// 从旋转矩阵提取前方向量（X 轴）和右方向量（Y 轴）
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	if (APawn* ControlledPawn = GetPawn<APawn>())
	{
		// 键盘移动优先：停止自动寻路，标记键盘活跃
		bAutoRunning = false;
		bKeyboardMoving = true;
		// InputAxisVector.Y = 前后轴（W/S），InputAxisVector.X = 左右轴（A/D）
		ControlledPawn->AddMovementInput(ForwardDirection, InputAxisVector.Y);
		ControlledPawn->AddMovementInput(RightDirection, InputAxisVector.X);
	}
}

/** 键盘移动键松开时清除标志，允许后续短按点击正常触发 AutoRun。*/
void AAuraPlayerController::MoveCompleted()
{
	bKeyboardMoving = false;
}

/**
 * 鼠标滚轮缩放处理：调整弹簧臂长度实现相机拉近/拉远。
 *
 * 【原理】
 *   滚轮向上 → 输入值为正 → 减小 TargetArmLength → 镜头拉近
 *   滚轮向下 → 输入值为负 → 增大 TargetArmLength → 镜头拉远
 *   FMath::Clamp 将臂长限制在 [CameraZoomMin, CameraZoomMax] 范围内，
 *   防止镜头过近（穿模）或过远（看不清角色）。
 */
void AAuraPlayerController::CameraZoom(const FInputActionValue& InputActionValue)
{
	const float ZoomAxis = InputActionValue.Get<float>();
	if (AAuraCharacter* AuraCharacter = GetPawn<AAuraCharacter>())
	{
		USpringArmComponent* SpringArm = AuraCharacter->GetCameraBoom();
		SpringArm->TargetArmLength = FMath::Clamp(
			SpringArm->TargetArmLength - ZoomAxis * CameraZoomStep,
			CameraZoomMin,
			CameraZoomMax
		);
	}
}
