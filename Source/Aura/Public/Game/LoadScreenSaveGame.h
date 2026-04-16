// Copyright Druid Mechanics

#pragma once

/*
 * LoadScreenSaveGame.h
 *
 * 存档数据容器，继承自 USaveGame，通过 UGameplayStatics::SaveGameToSlot 序列化到磁盘。
 *
 * 【分层存储设计】
 * 存档数据分为三个层级，对应游戏的不同状态维度：
 *
 * 1. 槽位元数据层（加载界面使用）
 *    - SlotName / SlotIndex：槽位标识
 *    - PlayerName / MapName / MapAssetName：在加载界面显示
 *    - SaveSlotStatus：Vacant（空槽）/ EnterName（输入名字中）/ Taken（已使用）
 *    - PlayerStartTag：记录玩家在目标地图的出生点
 *
 * 2. 玩家进度层（游戏内角色数据）
 *    - 等级、XP、属性点、技能点（整数值，直接存储）
 *    - 主属性数值：Strength / Intelligence / Resilience / Vigor（浮点值）
 *    - SavedAbilities：FSavedAbility 数组，保存每个技能的状态、槽位绑定和等级
 *
 * 3. 世界状态层（场景中 Actor 状态）
 *    - SavedMaps：FSavedMap 数组，每张地图独立一条记录
 *    - 每条 FSavedMap 包含若干 FSavedActor，每个 FSavedActor 存储：
 *        · ActorName：用于读档时按名匹配场景中的 Actor
 *        · Transform：Actor 的位置/旋转/缩放
 *        · Bytes：带 UPROPERTY(SaveGame) 说明符的属性的二进制序列化数据
 *
 * 【UPROPERTY(SaveGame) 说明符】
 * Actor 上标记了 SaveGame 的属性才会被 FObjectAndNameAsStringProxyArchive 写入 Bytes，
 * 未标记的属性（如运行时临时状态）不会被序列化，节省存储空间
 */

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/SaveGame.h"
#include "LoadScreenSaveGame.generated.h"

class UGameplayAbility;

/**
 * 存档槽位状态枚举，用于加载界面判断如何渲染槽位按钮
 */
UENUM(BlueprintType)
enum ESaveSlotStatus
{
	Vacant,		// 空槽位：显示"新建游戏"按钮
	EnterName,	// 正在输入玩家名字：显示输入框
	Taken		// 已有存档：显示存档信息和"继续游戏"/"删除"按钮
};

/**
 * 单个 Actor 的存档数据
 * 由 AAuraGameModeBase::SaveWorldState 在关卡切换或检查点触发时填充
 */
USTRUCT()
struct FSavedActor
{
	GENERATED_BODY()

	/** Actor 的 FName 标识，读档时用于在场景中精确匹配对应 Actor */
	UPROPERTY()
	FName ActorName = FName();

	/** Actor 的世界变换（位置、旋转、缩放），供 ShouldLoadTransform 为 true 的 Actor 还原位置 */
	UPROPERTY()
	FTransform Transform = FTransform();

	// 从 Actor 序列化而来的变量——仅包含标记了 SaveGame 说明符的属性
	/** 带 UPROPERTY(SaveGame) 属性的二进制序列化数据，读档时通过 Deserialize 还原 */
	UPROPERTY()
	TArray<uint8> Bytes;
};

/** FSavedActor 相等比较运算符：以 ActorName 为唯一键，防止 AddUnique 重复添加 */
inline bool operator==(const FSavedActor& Left, const FSavedActor& Right)
{
	return Left.ActorName == Right.ActorName;
}

/**
 * 单张地图的存档数据
 * 每张地图独立存储，互不影响，支持多地图游戏的分别保存和加载
 */
USTRUCT()
struct FSavedMap
{
	GENERATED_BODY()

	/** 地图资产名（用于与 World->GetMapName() 比对匹配） */
	UPROPERTY()
	FString MapAssetName = FString();

	/** 此地图中所有已保存的 Actor 列表 */
	UPROPERTY()
	TArray<FSavedActor> SavedActors;
};

/**
 * 单个技能的存档数据
 * 在每次存档时从 ASC 中读取技能规格并填充此结构体
 */
USTRUCT(BlueprintType)
struct FSavedAbility
{
	GENERATED_BODY()

	/** 技能类引用，读档时用于重新 GiveAbility（赋予技能实例） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "ClassDefaults")
	TSubclassOf<UGameplayAbility> GameplayAbility;

	/** 技能的唯一标识 Tag，与 AbilityInfo 中的 AbilityTag 对应 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FGameplayTag AbilityTag = FGameplayTag();

	/**
	 * 技能当前解锁状态 Tag（Locked / Eligible / Unlocked / Equipped）
	 * 读档后恢复此状态，避免玩家重新进入关卡时技能状态重置
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FGameplayTag AbilityStatus = FGameplayTag();

	/**
	 * 技能绑定的输入槽 Tag（InputTag.Spell1 等）
	 * Equipped 状态的技能才有有效的 AbilitySlot
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FGameplayTag AbilitySlot = FGameplayTag();

	/** 技能类型（Offensive / Passive / None），用于读档后区分装备到哪类槽 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FGameplayTag AbilityType = FGameplayTag();

	/** 技能当前等级，读档后通过 SetAbilityLevel 恢复 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	int32 AbilityLevel = 1;
};

/** FSavedAbility 相等比较运算符：以 AbilityTag 精确匹配，防止同一技能重复存储 */
inline bool operator==(const FSavedAbility& Left, const FSavedAbility& Right)
{
	return Left.AbilityTag.MatchesTagExact(Right.AbilityTag);
}

/**
 * ULoadScreenSaveGame
 *
 * 完整的存档数据容器，通过 UGameplayStatics::SaveGameToSlot 序列化到平台存储。
 * 由 AAuraGameModeBase 统一管理读写操作。
 */
UCLASS()
class AURA_API ULoadScreenSaveGame : public USaveGame
{
	GENERATED_BODY()
public:

	// ─── 槽位元数据 ───────────────────────────────────────────────

	/** 存档槽名称，与 UGameplayStatics::SaveGameToSlot 的 SlotName 参数一致 */
	UPROPERTY()
	FString SlotName = FString();

	/** 存档槽索引（同一名称下可有多个槽位） */
	UPROPERTY()
	int32 SlotIndex = 0;

	/** 玩家在创建存档时输入的角色名，显示在加载界面 */
	UPROPERTY()
	FString PlayerName = FString("Default Name");

	/** 当前存档所在地图的可读名称（与 Maps TMap 的 Key 一致），显示在加载界面 */
	UPROPERTY()
	FString MapName = FString("Default Map Name");

	/** 当前地图的资产名（用于 OpenLevel），与 World->GetMapName() 一致 */
	UPROPERTY()
	FString MapAssetName = FString("Default Map Asset Name");

	/** 玩家在当前地图应使用的出生点 Tag，供 ChoosePlayerStart 选择正确 APlayerStart */
	UPROPERTY()
	FName PlayerStartTag;

	/** 槽位占用状态（Vacant / EnterName / Taken），加载界面据此渲染对应 UI */
	UPROPERTY()
	TEnumAsByte<ESaveSlotStatus> SaveSlotStatus = Vacant;

	/** 是否为首次加载此存档（首次加载时应用默认属性而非从存档恢复） */
	UPROPERTY()
	bool bFirstTimeLoadIn = true;

	// ─── 玩家进度数据 ─────────────────────────────────────────────

	/* 玩家 */

	/** 玩家当前等级，读档时写入 AuraPlayerState::Level */
	UPROPERTY()
	int32 PlayerLevel = 1;

	/** 玩家当前累计总经验值，读档时写入 AuraPlayerState::XP */
	UPROPERTY()
	int32 XP = 0;

	/** 剩余可用技能点，读档时写入 AuraPlayerState::SpellPoints */
	UPROPERTY()
	int32 SpellPoints = 0;

	/** 剩余可用属性点，读档时写入 AuraPlayerState::AttributePoints */
	UPROPERTY()
	int32 AttributePoints = 0;

	/** 力量主属性当前数值，读档时通过 PrimaryAttributes_SetByCaller GE 重新应用 */
	UPROPERTY()
	float Strength = 0;

	/** 智力主属性当前数值 */
	UPROPERTY()
	float Intelligence = 0;

	/** 韧性主属性当前数值 */
	UPROPERTY()
	float Resilience = 0;

	/** 体力主属性当前数值 */
	UPROPERTY()
	float Vigor = 0;

	// ─── 技能与世界状态 ────────────────────────────────────────────

	/* 技能 */

	/** 所有已解锁/已装备技能的存档数组，读档时逐一恢复到 ASC */
	UPROPERTY()
	TArray<FSavedAbility> SavedAbilities;

	/** 所有已访问地图的世界状态数组，每张地图独立一条记录 */
	UPROPERTY()
	TArray<FSavedMap> SavedMaps;

	/** 根据地图资产名获取对应的 FSavedMap（不存在时返回空结构体） */
	FSavedMap GetSavedMapWithMapName(const FString& InMapName);

	/** 检查存档中是否已有指定地图的记录 */
	bool HasMap(const FString& InMapName);
};
