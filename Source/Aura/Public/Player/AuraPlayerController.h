// Copyright Druid Mechanics

#pragma once

/**
 * ============================================================
 * AAuraPlayerController — 玩家控制器
 * ============================================================
 *
 * 【核心职责】
 *   1. 输入处理：Enhanced Input 系统的绑定与分发
 *   2. 鼠标光标追踪：每帧 Trace 检测鼠标悬停目标，触发高亮效果
 *   3. 双模式移动：
 *      a. 自动寻路模式（短按左键）—— 通过 Spline + NavigationSystem 自动移动
 *      b. 即时移动模式（长按左键 / 键盘）—— 直接给 Pawn 添加移动输入
 *   4. 伤害数字显示：通过 Client RPC 在本地创建漂浮伤害文字组件
 *   5. 魔法圆阵控制：跟随鼠标位置显示施法预瞄圆阵
 *
 * 【输入模式区分（鼠标左键的双重语义）】
 *
 *   左键按下（Pressed）：
 *     - 记录 TargetingStatus（是否瞄准了敌人/可交互对象）
 *     - 停止自动寻路（bAutoRunning = false）
 *
 *   左键持续按下（Held）：
 *     ├── 若正在瞄准敌人 or Shift 键按住 → 转交 ASC 处理（触发 LMB 绑定的技能）
 *     └── 否则 → 累计 FollowTime，更新 CachedDestination（移动模式）
 *
 *   左键抬起（Released）：
 *     ├── 若瞄准了敌人 or Shift 键 → 转交 ASC（技能释放逻辑）
 *     └── 若 FollowTime < ShortPressThreshold（短按）→ 触发自动寻路
 *         ├── 调用 NavigationSystem 计算路径
 *         ├── 将路径点写入 Spline
 *         └── bAutoRunning = true，由 AutoRun() 每帧跟随 Spline 移动
 *
 * 【Spline 自动寻路协作机制】
 *   CachedDestination  —— 鼠标点击的世界坐标（寻路目标）
 *   Spline             —— 存储导航系统计算出的路径点曲线
 *   FollowTime         —— 左键持续按下的时长（区分短按/长按）
 *   bAutoRunning       —— 是否正在自动寻路中
 *   AutoRunAcceptanceRadius —— 到达目标的判断阈值（到达后停止自动寻路）
 *
 *   AutoRun() 每帧：
 *     1. 从 Spline 找到最近点
 *     2. 获取该点的切线方向
 *     3. 向该方向添加移动输入
 *     4. 若距 CachedDestination 足够近，停止自动寻路
 *
 * 【伤害数字显示（ShowDamageNumber）】
 *   服务器通过 GameplayCue 或 GameplayEffect 执行计算得到伤害值后，
 *   调用此 Client RPC（仅在拥有控制器的客户端执行）。
 *   在目标角色上创建 DamageTextComponent，它是一个自毁的 UMG 组件，
 *   显示数字后自动销毁，不占用网络带宽（纯客户端表现）。
 *
 * 【魔法圆阵（MagicCircle）】
 *   某些 AoE 技能（如 ArcaneShards）需要玩家手动选择施法位置。
 *   ShowMagicCircle() 创建圆阵 Actor，UpdateMagicCircleLocation() 每帧
 *   将其吸附到 CursorHit.ImpactPoint，实现跟随鼠标的地面选择效果。
 *   魔法圆阵显示期间，CursorTrace 切换到 ECC_ExcludePlayers 通道，
 *   避免光标检测被玩家自身阻挡。
 * ============================================================
 */

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "AuraPlayerController.generated.h"


class IHighlightInterface;
class UNiagaraSystem;
class UDamageTextComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UAuraInputConfig;
class UAuraAbilitySystemComponent;
class USplineComponent;
class AMagicCircle;

/**
 * ETargetingStatus — 鼠标左键按下瞬间的瞄准状态
 *
 * 用于区分"点击了敌人"/"点击了可交互物"/"点击了空地"三种情况，
 * 决定后续的 Held/Released 行为走哪条逻辑分支。
 */
enum class ETargetingStatus : uint8
{
	TargetingEnemy,      // 鼠标悬停在实现了 IEnemyInterface 的目标上（攻击模式）
	TargetingNonEnemy,   // 鼠标悬停在其他可交互对象上（非攻击交互模式）
	NotTargeting         // 鼠标悬停在空地或非交互对象上（寻路模式）
};

/**
 * AAuraPlayerController
 */
UCLASS(Config = Game)
class AURA_API AAuraPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AAuraPlayerController();

	/**
	 * 每帧更新：光标追踪 + 自动寻路 + 魔法圆阵位置更新。
	 * 使用 PlayerTick 而非 Tick，因为 PlayerController 使用前者。
	 */
	virtual void PlayerTick(float DeltaTime) override;

	/**
	 * 在目标角色位置显示漂浮伤害数字。
	 *
	 * 【Client RPC 的必要性】
	 *   伤害计算在服务器完成，但漂浮数字是纯客户端表现层，
	 *   使用 Client Reliable RPC 确保本地玩家一定能看到自己造成的伤害，
	 *   同时不在其他客户端创建（降低网络开销）。
	 *
	 * @param DamageAmount     最终伤害值（已计算暴击/格挡）
	 * @param TargetCharacter  受击角色（数字在其头顶显示）
	 * @param bBlockedHit      是否被格挡（影响数字颜色/样式）
	 * @param bCriticalHit     是否暴击（影响数字颜色/样式）
	 */
	UFUNCTION(Client, Reliable)
	void ShowDamageNumber(float DamageAmount, ACharacter* TargetCharacter, bool bBlockedHit, bool bCriticalHit);

	/**
	 * 显示施法预瞄魔法圆阵（AoE 技能选择目标位置时使用）。
	 * @param DecalMaterial  可选的自定义贴花材质（不同技能可用不同圆阵外观）
	 */
	UFUNCTION(BlueprintCallable)
	void ShowMagicCircle(UMaterialInterface* DecalMaterial = nullptr);

	/**
	 * 销毁魔法圆阵，结束目标位置选择模式。
	 */
	UFUNCTION(BlueprintCallable)
	void HideMagicCircle();

	/**
	 * UI 打开时调用：真正暂停游戏（TimeDilation = 0）并禁用玩家操控输入。
	 * 支持多层 UI 嵌套——每次调用计数器 +1，仅第一次（0→1）触发实际暂停。
	 * 在 Widget 的 Event Construct 或打开逻辑中调用。
	 */
	UFUNCTION(BlueprintCallable, Category = "UI|Pause")
	void ShowUI_PauseGame();

	/**
	 * UI 关闭时调用：恢复游戏并启用玩家操控输入。
	 * 支持多层 UI 嵌套——每次调用计数器 -1，仅最后一次（1→0）触发实际恢复。
	 * 在 Widget 的 Event Destruct 或关闭按钮逻辑中调用。
	 */
	UFUNCTION(BlueprintCallable, Category = "UI|Pause")
	void HideUI_ResumeGame();


protected:
	virtual void BeginPlay() override;

	/**
	 * 注册 Enhanced Input 绑定：
	 *   - MoveAction    → Move()（键盘移动）
	 *   - ShiftAction   → ShiftPressed/Released
	 *   - 所有技能输入  → AbilityInputTagPressed/Released/Held（按 InputTag 分发）
	 */
	virtual void SetupInputComponent() override;

private:
	// ---- Enhanced Input 配置 ----

	/** Enhanced Input 映射上下文（在蓝图中配置，BeginPlay 时注册到 Subsystem）*/
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> AuraContext;

	/** 键盘移动输入动作（WASD / 方向键）*/
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MoveAction;
	
	/** 摄像机缩放输入动作（鼠标滚轮）*/
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> CameraFovAction;

	/** 鼠标滚轮缩放处理：调整弹簧臂长度 */
	void CameraZoom(const FInputActionValue& InputActionValue);

	/** 弹簧臂最短长度（最近镜头，滚轮缩放下限）*/
	UPROPERTY(EditDefaultsOnly, Category="Camera")
	float CameraZoomMin = 300.f;

	/** 弹簧臂最长长度（最远镜头，滚轮缩放上限）*/
	UPROPERTY(EditDefaultsOnly, Category="Camera")
	float CameraZoomMax = 1000.f;

	/** 每次滚轮触发时弹簧臂长度的变化量 */
	UPROPERTY(EditDefaultsOnly, Category="Camera")
	float CameraZoomStep = 50.f;

	/** Shift 键输入动作（按住 Shift 强制进入技能释放模式，跳过寻路逻辑）*/
	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ShiftAction;

	// Shift 键状态回调与标志
	void ShiftPressed() { bShiftKeyDown = true; };
	void ShiftReleased() { bShiftKeyDown = false; };
	/** Shift 键是否按下（true 时左键行为等同于攻击模式，不触发寻路）*/
	bool bShiftKeyDown = false;

	/**
	 * 键盘移动是否正在按住（Move Triggered 时置 true，Move Completed 时置 false）。
	 * 持久状态，跨帧有效，不受同帧输入回调顺序影响。
	 * 键盘移动时：压制 AutoRun、LMB 强制走技能路径而非移动路径。
	 */
	bool bKeyboardMoving = false;
	void MoveCompleted();

	/**
	 * 键盘移动处理。
	 * 将输入轴值转换为基于控制器 Yaw 朝向的前/右向量，
	 * 添加到 Pawn 的移动输入中（方向无需与角色面朝方向一致）。
	 */
	void Move(const FInputActionValue& InputActionValue);

	// ---- 光标追踪（鼠标悬停高亮）----

	/**
	 * 每帧执行鼠标射线检测，维护 LastActor / ThisActor 的切换，
	 * 通过 IHighlightInterface 触发目标的高亮/取消高亮效果。
	 *
	 * 【状态切换逻辑】
	 *   LastActor = ThisActor（上一帧）
	 *   ThisActor = 本帧命中的可高亮 Actor
	 *   若 LastActor != ThisActor：取消旧目标高亮，高亮新目标
	 *
	 * 【特殊情况】
	 *   - 若 Player_Block_CursorTrace 标签激活（如施法中），停止追踪并清除高亮
	 *   - 若魔法圆阵正在显示，切换到 ECC_ExcludePlayers 检测通道（排除玩家自身）
	 */
	void CursorTrace();

	/** 上一帧鼠标命中的可高亮 Actor */
	TObjectPtr<AActor> LastActor;
	/** 本帧鼠标命中的可高亮 Actor */
	TObjectPtr<AActor> ThisActor;
	/** 每帧光标检测的命中结果（ImpactPoint 用于移动目标点和魔法圆阵位置）*/
	FHitResult CursorHit;

	/** 对实现 IHighlightInterface 的 Actor 执行高亮 */
	static void HighlightActor(AActor* InActor);
	/** 取消对实现 IHighlightInterface 的 Actor 的高亮 */
	static void UnHighlightActor(AActor* InActor);

	// ---- GAS 技能输入分发 ----
	// UAuraInputComponent 将 Enhanced Input 事件与 GameplayTag 绑定，
	// 下面三个函数按事件阶段（按下/抬起/持续）分别转发给 ASC

	/**
	 * 技能键按下时调用。
	 * 对于 LMB：记录 TargetingStatus，停止自动寻路。
	 * 对于其他键：直接转发给 ASC。
	 * 若 Player_Block_InputPressed 标签激活则忽略所有输入。
	 */
	void AbilityInputTagPressed(FGameplayTag InputTag);

	/**
	 * 技能键抬起时调用。
	 * 对于非 LMB：直接转发给 ASC。
	 * 对于 LMB：
	 *   - 若未瞄准敌人且未按 Shift → 短按判断，可能触发自动寻路
	 *   - 同时也通知 ASC（ASC 需要知道按键已释放以结束持续技能）
	 */
	void AbilityInputTagReleased(FGameplayTag InputTag);

	/**
	 * 技能键持续按下时每帧调用。
	 * 对于非 LMB：直接转发给 ASC（持续施法技能）。
	 * 对于 LMB：
	 *   - 若瞄准敌人 or Shift 按住 → 转发给 ASC（持续攻击/技能）
	 *   - 否则 → 累计 FollowTime，移动 Pawn 朝 CachedDestination 方向
	 */
	void AbilityInputTagHeld(FGameplayTag InputTag);

	/**
	 * 技能输入配置 DataAsset（配置 InputTag → InputAction 的映射关系）。
	 * UAuraInputComponent 使用此配置将 Enhanced Input 事件与 GAS Tag 关联。
	 */
	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<UAuraInputConfig> InputConfig;

	/** 缓存的 ASC 指针（懒加载，首次 GetASC() 时从 Pawn 上获取并缓存）*/
	UPROPERTY()
	TObjectPtr<UAuraAbilitySystemComponent> AuraAbilitySystemComponent;

	/**
	 * 懒加载获取 ASC。
	 * Pawn 在 BeginPlay 时可能尚未完全初始化（特别是网络环境），
	 * 因此不在构造函数或 BeginPlay 中缓存，而是首次使用时按需获取。
	 */
	UAuraAbilitySystemComponent* GetASC();

	// ---- 自动寻路状态数据 ----

	/** 当前寻路/移动的目标世界坐标（鼠标点击命中位置）*/
	FVector CachedDestination = FVector::ZeroVector;

	/** 鼠标左键持续按下的累计时间（用于区分短按/长按）*/
	float FollowTime = 0.f;

	/** 短按判断阈值（秒）：持续时间低于此值视为短按，触发自动寻路 */
	float ShortPressThreshold = 0.5f;

	/** 是否正在自动寻路中（true 时 AutoRun() 每帧驱动 Pawn 沿 Spline 移动）*/
	bool bAutoRunning = false;

	/** 当前的瞄准状态（Pressed 时记录，影响 Held/Released 行为）*/
	ETargetingStatus TargetingStatus = ETargetingStatus::NotTargeting;

	/** 到达目标点的接受半径（Pawn 与目标的距离小于此值时停止自动寻路）*/
	UPROPERTY(EditDefaultsOnly)
	float AutoRunAcceptanceRadius = 50.f;

	/**
	 * 路径样条曲线（在构造函数中创建为默认子对象）。
	 * NavigationSystem 计算路径后，路径点被写入此 Spline，
	 * AutoRun() 每帧从 Spline 获取当前位置的切线方向作为移动方向。
	 * 使用 Spline 而非直线移动是为了沿导航网格路径平滑转弯。
	 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USplineComponent> Spline;

	/** 点击地面时在目标位置播放的 Niagara 特效（视觉反馈，表示移动目标点）*/
	UPROPERTY(EditDefaultsOnly)
	TObjectPtr<UNiagaraSystem> ClickNiagaraSystem;

	/**
	 * 每帧执行自动寻路逻辑：
	 *   1. 在 Spline 上找到最接近 Pawn 当前位置的点
	 *   2. 获取该点的切线方向
	 *   3. 向该方向添加移动输入
	 *   4. 若已足够接近 CachedDestination，停止自动寻路
	 */
	void AutoRun();

	/** 漂浮伤害数字组件的类（在蓝图中配置，运行时 NewObject 动态创建）*/
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<UDamageTextComponent> DamageTextComponentClass;

	/** 魔法圆阵 Actor 的类（AoE 技能选择施法位置时使用）*/
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<AMagicCircle> MagicCircleClass;

	/** 当前场景中的魔法圆阵实例（null 表示未激活）*/
	UPROPERTY()
	TObjectPtr<AMagicCircle> MagicCircle;

	/**
	 * 将魔法圆阵的位置更新为鼠标命中点（CursorHit.ImpactPoint）。
	 * 每帧在 PlayerTick 中调用，实现圆阵跟随鼠标的视觉效果。
	 */
	void UpdateMagicCircleLocation();

	/** UI 暂停引用计数器：支持多层 UI 同时打开时的嵌套暂停，仅 0→1 真正暂停、1→0 真正恢复 */
	int32 GamePauseCounter = 0;
};
