// Copyright Druid Mechanics

#pragma once

/*
 * AAuraCharacter — 玩家控制角色
 *
 * 【架构特点：ASC 和 AttributeSet 在 PlayerState 上】
 * 与普通 GAS 集成方式不同，玩家角色的 ASC 和 AttributeSet 不放在 Character 上，
 * 而是放在 AAuraPlayerState 上。这样设计的原因：
 *   1. 断线重连支持：PlayerState 的生命周期比 Character 长，玩家死亡/重生后
 *      Character 可能被销毁重建，但 PlayerState 保持不变，属性数据得以保留。
 *   2. 与 Unreal 网络架构一致：PlayerState 本身就是为持久化玩家数据设计的。
 *
 * 【GAS 初始化的双端处理】
 * GAS 要求在 Server 和 Client 两端都完成 InitAbilityActorInfo：
 *   - Server 端：Character 被 Controller Possess 后（PossessedBy），
 *     此时 PlayerState 已可访问，可以安全地从 PlayerState 获取 ASC 并初始化。
 *   - Client 端：Character 的 PlayerState 通过网络复制完成后（OnRep_PlayerState），
 *     才能从 PlayerState 获取 ASC 并初始化 UI 等客户端系统。
 * 若只在一端初始化，多人游戏中另一端的 GAS 功能（技能预测、UI 更新等）将失效。
 *
 * 【接口实现】
 * 继承自 AAuraCharacterBase 的同时，还实现了 IPlayerInterface，
 * 向 UI 系统、技能系统暴露等级、经验值、属性点、法术点等玩家专属数据的读写接口。
 */

#include "CoreMinimal.h"
#include "Character/AuraCharacterBase.h"
#include "Interaction/PlayerInterface.h"
#include "AuraCharacter.generated.h"

class UNiagaraComponent;
class UCameraComponent;
class USpringArmComponent;

UCLASS()
class AURA_API AAuraCharacter : public AAuraCharacterBase, public IPlayerInterface
{
	GENERATED_BODY()
public:
	AAuraCharacter();

	/*
	 * PossessedBy — Server 端 GAS 初始化入口
	 *
	 * 当 Controller（通常是 APlayerController）Possess 此 Character 时调用，
	 * 仅在服务器端执行。在此调用 InitAbilityActorInfo 初始化服务器的 ASC，
	 * 然后调用 LoadProgress 从存档恢复进度或初始化默认属性和技能。
	 */
	virtual void PossessedBy(AController* NewController) override;

	/*
	 * OnRep_PlayerState — Client 端 GAS 初始化入口
	 *
	 * 当 PlayerState 从服务器复制到本客户端完成时调用。
	 * 客户端无法使用 PossessedBy 来初始化 ASC（服务器才会调用 PossessedBy），
	 * 所以必须在此处完成客户端的 ASC 初始化，包括 HUD/UI 的绑定。
	 */
	virtual void OnRep_PlayerState() override;

	/** 玩家接口（IPlayerInterface）实现 */
	// 向 PlayerState 增加经验值，XP 满足条件后 PlayerState 会触发升级逻辑
	virtual void AddToXP_Implementation(int32 InXP) override;

	// 升级时通过 Multicast RPC 在所有客户端播放升级粒子特效
	virtual void LevelUp_Implementation() override;

	// 查询当前经验值，委托给 PlayerState 存储
	virtual int32 GetXP_Implementation() const override;

	// 根据经验值反查对应的等级，用于升级时判断是否连续升多级
	virtual int32 FindLevelForXP_Implementation(int32 InXP) const override;

	// 查询指定等级对应的属性点奖励数量（从 LevelUpInfo 数据表读取）
	virtual int32 GetAttributePointsReward_Implementation(int32 Level) const override;

	// 查询指定等级对应的法术点奖励数量
	virtual int32 GetSpellPointsReward_Implementation(int32 Level) const override;

	// 增加玩家等级，同时触发 ASC 更新技能解锁状态
	virtual void AddToPlayerLevel_Implementation(int32 InPlayerLevel) override;

	// 增加可分配的属性点（等级提升时调用）
	virtual void AddToAttributePoints_Implementation(int32 InAttributePoints) override;

	// 增加可分配的法术点（等级提升时调用）
	virtual void AddToSpellPoints_Implementation(int32 InSpellPoints) override;

	// 查询当前剩余属性点数量
	virtual int32 GetAttributePoints_Implementation() const override;

	// 查询当前剩余法术点数量
	virtual int32 GetSpellPoints_Implementation() const override;

	// 显示法术瞄准魔法圆（切换为法术目标选择模式，隐藏鼠标指针）
	virtual void ShowMagicCircle_Implementation(UMaterialInterface* DecalMaterial) override;

	// 隐藏法术瞄准魔法圆（恢复普通鼠标指针模式）
	virtual void HideMagicCircle_Implementation() override;

	// 存档进度：收集当前属性、技能状态写入存档文件，在到达检查点时调用
	virtual void SaveProgress_Implementation(const FName& CheckpointTag) override;
	/** 玩家接口 结束 */

	/** 战斗接口（ICombatInterface）重写 */
	// 从 PlayerState 获取玩家等级（覆盖基类默认实现）
	virtual int32 GetPlayerLevel_Implementation() override;

	// 玩家死亡：调用基类逻辑后，额外设置死亡计时器并分离摄像机
	// 死亡计时器到期后通知 GameMode 执行重生流程
	virtual void Die(const FVector& DeathImpulse) override;
	/** 战斗接口 结束 */

	// 玩家死亡后多少秒触发重生，可在蓝图中配置
	UPROPERTY(EditDefaultsOnly)
	float DeathTime = 5.f;

	// 死亡计时器句柄，Die() 中设置，到期后通知 GameMode::PlayerDied
	FTimerHandle DeathTimer;

	// 升级粒子特效组件，在 LevelUp 时激活并朝向摄像机方向播放
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UNiagaraComponent> LevelUpNiagaraComponent;

	// 重写基类的眩晕状态响应：玩家眩晕时还需额外阻断输入（添加 Block Tag）
	virtual void OnRep_Stunned() override;

	// 重写基类的燃烧状态响应：激活/停用燃烧粒子特效
	virtual void OnRep_Burned() override;

	// 从存档恢复游戏进度（属性、技能、等级、XP 等）
	// 首次进入游戏时走默认初始化路径，后续进入时从存档恢复
	void LoadProgress();

	FORCEINLINE USpringArmComponent* GetCameraBoom() const { return CameraBoom; }
private:
	// 俯视角摄像机组件，用于 ARPG 俯视角视角
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> TopDownCameraComponent;

	// 弹簧臂，控制摄像机与角色的距离和角度
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> CameraBoom;

	/*
	 * InitAbilityActorInfo — 玩家专属实现（覆盖基类空实现）
	 *
	 * 执行内容：
	 * 1. 从 PlayerState 获取 ASC，调用 ASC->InitAbilityActorInfo(PlayerState, Character)
	 *    告知 GAS：Owner 是 PlayerState，Avatar 是 Character 自身
	 * 2. 将 PlayerState 的 ASC 和 AttributeSet 引用缓存到基类成员变量
	 * 3. 广播 OnAscRegistered 委托，通知依赖 ASC 的组件（如 PassiveNiagaraComponent）
	 * 4. 注册 Stun Tag 监听，眩晕时驱动移动速度和输入屏蔽
	 * 5. 初始化 HUD（仅在有本地 Controller 的情况下才有效）
	 *
	 * 注意：只有 AAuraPlayerController 的本地拥有者才有 HUD，
	 * 服务器上其他玩家的 Character 没有本地 Controller，UI 初始化会被跳过。
	 */
	virtual void InitAbilityActorInfo() override;

	/*
	 * MulticastLevelUpParticles — NetMulticast Reliable RPC
	 *
	 * 升级粒子效果是纯视觉事件，需要在所有客户端上播放，
	 * 因此通过 Multicast 广播到全部端。
	 * 播放时旋转粒子组件朝向摄像机，确保升级特效面对玩家视角。
	 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastLevelUpParticles() const;
};
