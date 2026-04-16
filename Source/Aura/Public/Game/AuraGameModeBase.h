// Copyright Druid Mechanics

#pragma once

/*
 * AuraGameModeBase.h
 *
 * 游戏模式基类，充当全局数据仓库和存档系统的中心控制点。
 *
 * 【作为全局数据仓库】
 * GameMode 在服务器上唯一存在，持有以下核心数据资产引用：
 *   - CharacterClassInfo：职业配置（属性 GE、技能列表、伤害系数）
 *   - AbilityInfo：技能元数据字典（图标、冷却 Tag、等级需求等）
 *   - LootTiers：战利品分层配置
 * 客户端代码通过 UAuraAbilitySystemLibrary 的静态函数访问这些数据，
 * 内部逻辑为：GetWorld() → GetAuthGameMode() → Cast<AAuraGameModeBase>
 *
 * 【存档/读档中心】
 * - SaveSlotData：在加载界面创建/覆盖存档槽（新游戏或覆盖存档）
 * - GetSaveSlotData：读取或创建指定槽位的 SaveGame 对象
 * - RetrieveInGameSaveData：游戏进行中从 GameInstance 获取当前激活存档
 * - SaveInGameProgressData：将玩家进度（等级、XP、属性、技能）写回存档
 * - SaveWorldState / LoadWorldState：遍历场景中实现 ISaveInterface 的 Actor，
 *   序列化/反序列化其带 SaveGame 说明符的属性（存储机关状态、检查点等）
 *
 * 【地图管理】
 * - Maps TMap 存储关卡名 → 软引用，BeginPlay 时自动注册默认地图
 * - TravelToMap：通过 LoadSlot 携带的地图名跳转关卡
 * - ChoosePlayerStart_Implementation：根据 GameInstance 中保存的 PlayerStartTag
 *   选择正确的出生点（支持多入口地图）
 */

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AuraGameModeBase.generated.h"

class ULootTiers;
class ULoadScreenSaveGame;
class USaveGame;
class UMVVM_LoadSlot;
class UAbilityInfo;
class UCharacterClassInfo;

/**
 * AAuraGameModeBase
 *
 * 仅在服务器上运行（单机也等同于服务器）。
 * 客户端通过 AuraAbilitySystemLibrary 的静态方法间接访问此类持有的数据资产。
 */
UCLASS()
class AURA_API AAuraGameModeBase : public AGameModeBase
{
	GENERATED_BODY()
public:

	/**
	 * 职业信息数据资产，包含每个职业的主属性 GE、专属技能和伤害系数 CurveTable
	 * 通过 UAuraAbilitySystemLibrary::GetCharacterClassInfo 全局访问
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Character Class Defaults")
	TObjectPtr<UCharacterClassInfo> CharacterClassInfo;

	/**
	 * 技能信息数据资产，存储所有可学习技能的静态元数据（图标、冷却 Tag、等级需求等）
	 * 通过 UAuraAbilitySystemLibrary::GetAbilityInfo 全局访问
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Ability Info")
	TObjectPtr<UAbilityInfo> AbilityInfo;

	/**
	 * 战利品分层配置数据资产，定义不同质量等级掉落物的概率和内容
	 * 敌人死亡时的掉落逻辑从此资产查询
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Loot Tiers")
	TObjectPtr<ULootTiers> LootTiers;

	/**
	 * 在加载界面创建或覆盖指定槽位的存档
	 * 若槽位已存在则先删除旧存档再创建新的，避免脏数据叠加
	 * @param LoadSlot   来自加载界面 MVVM ViewModel 的槽位数据
	 * @param SlotIndex  槽位索引（0/1/2 等）
	 */
	void SaveSlotData(UMVVM_LoadSlot* LoadSlot, int32 SlotIndex);

	/**
	 * 读取指定槽位的 SaveGame 对象；若槽位不存在则创建一个空的新对象
	 * @param SlotName   存档槽名称字符串
	 * @param SlotIndex  槽位索引
	 * @return 已加载或新创建的 ULoadScreenSaveGame 实例
	 */
	ULoadScreenSaveGame* GetSaveSlotData(const FString& SlotName, int32 SlotIndex) const;

	/** 删除指定槽位的存档文件（存在则删除，不存在则忽略） */
	static void DeleteSlot(const FString& SlotName, int32 SlotIndex);

	/**
	 * 游戏进行中获取当前激活的存档数据
	 * 从 AuraGameInstance 读取槽位名和索引，再调用 GetSaveSlotData
	 */
	ULoadScreenSaveGame* RetrieveInGameSaveData();

	/**
	 * 将玩家当前进度（等级、XP、属性点、技能点、主属性值、技能状态）写回存档文件
	 * 同时更新 GameInstance 中的 PlayerStartTag
	 */
	void SaveInGameProgressData(ULoadScreenSaveGame* SaveObject);

	/**
	 * 将当前世界中所有实现 ISaveInterface 的 Actor 状态序列化到存档
	 * 使用 FObjectAndNameAsStringProxyArchive（ArIsSaveGame=true）只序列化
	 * 带 UPROPERTY(SaveGame) 说明符的属性，避免存储冗余数据
	 * @param DestinationMapAssetName 非空时更新存档中记录的目标地图（用于关卡跳转前保存）
	 */
	void SaveWorldState(UWorld* World, const FString& DestinationMapAssetName = FString("")) const;

	/**
	 * 从存档中还原当前世界的 Actor 状态
	 * 遍历场景中实现 ISaveInterface 的 Actor，按 ActorName 匹配并反序列化字节数据，
	 * 然后调用 ISaveInterface::LoadActor 通知 Actor 执行自定义还原逻辑（如重建 GE）
	 */
	void LoadWorldState(UWorld* World) const;

	/**
	 * 根据 LoadSlot 中记录的地图名执行关卡跳转
	 * 使用软引用异步加载目标地图，避免强引用导致内存常驻
	 */
	void TravelToMap(UMVVM_LoadSlot* Slot);

	/** SaveGame 对象的类型，用于 CreateSaveGameObject 实例化正确的子类 */
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<USaveGame> LoadScreenSaveGameClass;

	/** 默认地图的显示名称（用于存档中记录和加载界面显示） */
	UPROPERTY(EditDefaultsOnly)
	FString DefaultMapName;

	/** 默认地图的软引用，BeginPlay 时自动注册到 Maps TMap */
	UPROPERTY(EditDefaultsOnly)
	TSoftObjectPtr<UWorld> DefaultMap;

	/** 玩家在默认地图中的出生点 Tag（新游戏时使用） */
	UPROPERTY(EditDefaultsOnly)
	FName DefaultPlayerStartTag;

	/**
	 * 关卡名 → 地图软引用的映射表
	 * Key 为关卡显示名（与存档中 MapName 一致），Value 为软对象引用
	 * BeginPlay 时默认地图自动加入此表
	 */
	UPROPERTY(EditDefaultsOnly)
	TMap<FString, TSoftObjectPtr<UWorld>> Maps;

	/**
	 * 根据地图资产名（Asset Name）反查关卡显示名
	 * 用于 SaveWorldState 中将资产名转换为可读地图名存入存档
	 */
	FString GetMapNameFromMapAssetName(const FString& MapAssetName) const;

	/**
	 * 重写出生点选择逻辑：根据 GameInstance 中保存的 PlayerStartTag 寻找对应 APlayerStart
	 * 支持多个出生点的地图（如地牢入口和出口各有独立出生点）
	 */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/**
	 * 处理玩家死亡：重新加载当前存档记录的地图（即重开当前关卡）
	 * @param DeadCharacter 死亡的玩家角色，用于获取 World 上下文
	 */
	void PlayerDied(ACharacter* DeadCharacter);

protected:
	/** BeginPlay 时将默认地图注册到 Maps 映射表，确保跳转逻辑能找到默认关卡 */
	virtual void BeginPlay() override;

};
