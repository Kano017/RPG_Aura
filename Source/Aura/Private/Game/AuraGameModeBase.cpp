// Copyright Druid Mechanics


#include "Game/AuraGameModeBase.h"

#include "EngineUtils.h"
#include "Aura/AuraLogChannels.h"
#include "Game/AuraGameInstance.h"
#include "Game/LoadScreenSaveGame.h"
#include "GameFramework/PlayerStart.h"
#include "Interaction/SaveInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UI/ViewModel/MVVM_LoadSlot.h"
#include "GameFramework/Character.h"

/**
 * 在加载界面为指定槽位创建或覆盖存档
 *
 * 流程：先删除已有存档（避免脏数据叠加），再创建全新的 SaveGame 对象并填入基础信息：
 * - PlayerName、MapName、MapAssetName：加载界面读取自 ViewModel
 * - SaveSlotStatus = Taken：标记此槽位已被占用，加载界面据此显示"继续游戏"而非"新建"
 * - PlayerStartTag：记录玩家在目标地图的出生点，供 ChoosePlayerStart 使用
 */
void AAuraGameModeBase::SaveSlotData(UMVVM_LoadSlot* LoadSlot, int32 SlotIndex)
{
	if (UGameplayStatics::DoesSaveGameExist(LoadSlot->GetLoadSlotName(), SlotIndex))
	{
		UGameplayStatics::DeleteGameInSlot(LoadSlot->GetLoadSlotName(), SlotIndex);
	}
	USaveGame* SaveGameObject = UGameplayStatics::CreateSaveGameObject(LoadScreenSaveGameClass);
	ULoadScreenSaveGame* LoadScreenSaveGame = Cast<ULoadScreenSaveGame>(SaveGameObject);
	LoadScreenSaveGame->PlayerName = LoadSlot->GetPlayerName();
	LoadScreenSaveGame->SaveSlotStatus = Taken;
	LoadScreenSaveGame->MapName = LoadSlot->GetMapName();
	LoadScreenSaveGame->MapAssetName = LoadSlot->MapAssetName;
	LoadScreenSaveGame->PlayerStartTag = LoadSlot->PlayerStartTag;

	UGameplayStatics::SaveGameToSlot(LoadScreenSaveGame, LoadSlot->GetLoadSlotName(), SlotIndex);
}

/**
 * 读取或创建指定槽位的 SaveGame 对象
 *
 * - 若存档文件已存在：从磁盘加载并返回
 * - 若不存在：创建一个带默认值的新对象（SaveSlotStatus=Vacant）
 * 调用方：GetSaveSlotData 是许多存档相关函数的基础读取方法
 */
ULoadScreenSaveGame* AAuraGameModeBase::GetSaveSlotData(const FString& SlotName, int32 SlotIndex) const
{
	USaveGame* SaveGameObject = nullptr;
	if (UGameplayStatics::DoesSaveGameExist(SlotName, SlotIndex))
	{
		SaveGameObject = UGameplayStatics::LoadGameFromSlot(SlotName, SlotIndex);
	}
	else
	{
		SaveGameObject = UGameplayStatics::CreateSaveGameObject(LoadScreenSaveGameClass);
	}
	ULoadScreenSaveGame* LoadScreenSaveGame = Cast<ULoadScreenSaveGame>(SaveGameObject);
	return LoadScreenSaveGame;
}

/** 删除存档文件，存在时删除，不存在时静默跳过（避免抛出错误） */
void AAuraGameModeBase::DeleteSlot(const FString& SlotName, int32 SlotIndex)
{
	if (UGameplayStatics::DoesSaveGameExist(SlotName, SlotIndex))
	{
		UGameplayStatics::DeleteGameInSlot(SlotName, SlotIndex);
	}
}

/**
 * 在游戏运行时获取当前激活的存档数据
 *
 * GameInstance 在关卡加载时持久保存玩家选择的槽位名和索引，
 * 此函数从 GameInstance 读取这些信息后委托给 GetSaveSlotData 完成实际加载
 */
ULoadScreenSaveGame* AAuraGameModeBase::RetrieveInGameSaveData()
{
	UAuraGameInstance* AuraGameInstance = Cast<UAuraGameInstance>(GetGameInstance());

	const FString InGameLoadSlotName = AuraGameInstance->LoadSlotName;
	const int32 InGameLoadSlotIndex = AuraGameInstance->LoadSlotIndex;

	return GetSaveSlotData(InGameLoadSlotName, InGameLoadSlotIndex);
}

/**
 * 将当前游戏进度写回存档文件
 *
 * 同时同步更新 GameInstance::PlayerStartTag，确保关卡跳转后出生点信息不丢失
 * 调用方：Checkpoint Actor 触发检查点保存、玩家加点后自动存档等
 */
void AAuraGameModeBase::SaveInGameProgressData(ULoadScreenSaveGame* SaveObject)
{
	UAuraGameInstance* AuraGameInstance = Cast<UAuraGameInstance>(GetGameInstance());

	const FString InGameLoadSlotName = AuraGameInstance->LoadSlotName;
	const int32 InGameLoadSlotIndex = AuraGameInstance->LoadSlotIndex;
	AuraGameInstance->PlayerStartTag = SaveObject->PlayerStartTag;

	UGameplayStatics::SaveGameToSlot(SaveObject, InGameLoadSlotName, InGameLoadSlotIndex);
}

/**
 * 将当前世界中所有可保存 Actor 的状态序列化到存档
 *
 * 【序列化流程】
 * 1. 通过 FActorIterator 遍历场景中所有 Actor
 * 2. 过滤出实现 USaveInterface 的 Actor（机关、检查点、拾取物等）
 * 3. 记录 ActorName（用于读档时按名匹配）和 Transform（位置/旋转/缩放）
 * 4. 使用 FObjectAndNameAsStringProxyArchive（ArIsSaveGame=true）序列化 Actor：
 *    只有标记了 UPROPERTY(SaveGame) 的属性才会被写入二进制字节流
 * 5. 将所有 FSavedActor 存入对应地图的 FSavedMap 并写回磁盘
 *
 * @param DestinationMapAssetName 非空时更新存档中的目标地图信息（关卡跳转前调用）
 */
void AAuraGameModeBase::SaveWorldState(UWorld* World, const FString& DestinationMapAssetName) const
{
	FString WorldName = World->GetMapName();
	WorldName.RemoveFromStart(World->StreamingLevelsPrefix);

	UAuraGameInstance* AuraGI = Cast<UAuraGameInstance>(GetGameInstance());
	check(AuraGI);

	if (ULoadScreenSaveGame* SaveGame = GetSaveSlotData(AuraGI->LoadSlotName, AuraGI->LoadSlotIndex))
	{
		if (DestinationMapAssetName != FString(""))
		{
			// 更新存档中记录的目标地图，使下次加载时跳转到正确关卡
			SaveGame->MapAssetName = DestinationMapAssetName;
			SaveGame->MapName = GetMapNameFromMapAssetName(DestinationMapAssetName);
		}

		// 若此地图尚未在存档中有记录，先添加空条目
		if (!SaveGame->HasMap(WorldName))
		{
			FSavedMap NewSavedMap;
			NewSavedMap.MapAssetName = WorldName;
			SaveGame->SavedMaps.Add(NewSavedMap);
		}

		FSavedMap SavedMap = SaveGame->GetSavedMapWithMapName(WorldName);
		SavedMap.SavedActors.Empty(); // 清空旧数据，稍后用当前场景中的 Actor 重新填充

		for (FActorIterator It(World); It; ++It)
		{
			AActor* Actor = *It;

			// 跳过无效 Actor 和未实现存档接口的 Actor
			if (!IsValid(Actor) || !Actor->Implements<USaveInterface>()) continue;

			FSavedActor SavedActor;
			SavedActor.ActorName = Actor->GetFName();
			SavedActor.Transform = Actor->GetTransform();

			FMemoryWriter MemoryWriter(SavedActor.Bytes);

			// ArIsSaveGame=true：只序列化标记了 UPROPERTY(SaveGame) 的属性
			FObjectAndNameAsStringProxyArchive Archive(MemoryWriter, true);
			Archive.ArIsSaveGame = true;

			// 将 Actor 的带 SaveGame 标记的属性写入 Bytes 字节数组
			Actor->Serialize(Archive);

			SavedMap.SavedActors.AddUnique(SavedActor);
		}

		// 用新数据替换存档中的旧地图记录
		for (FSavedMap& MapToReplace : SaveGame->SavedMaps)
		{
			if (MapToReplace.MapAssetName == WorldName)
			{
				MapToReplace = SavedMap;
			}
		}
		UGameplayStatics::SaveGameToSlot(SaveGame, AuraGI->LoadSlotName, AuraGI->LoadSlotIndex);
	}
}

/**
 * 从存档还原当前世界中所有可保存 Actor 的状态
 *
 * 【还原流程】
 * 1. 从磁盘加载存档，找到当前地图对应的 FSavedMap
 * 2. 遍历场景中实现 USaveInterface 的 Actor
 * 3. 按 ActorName 匹配存档中的 FSavedActor
 * 4. 若 ShouldLoadTransform 为 true，还原 Actor 的 Transform（位置等）
 * 5. 使用 FObjectAndNameAsStringProxyArchive 从字节流反序列化属性
 * 6. 调用 ISaveInterface::LoadActor，通知 Actor 执行自定义还原逻辑（如重新应用 GE）
 */
void AAuraGameModeBase::LoadWorldState(UWorld* World) const
{
	FString WorldName = World->GetMapName();
	WorldName.RemoveFromStart(World->StreamingLevelsPrefix);

	UAuraGameInstance* AuraGI = Cast<UAuraGameInstance>(GetGameInstance());
	check(AuraGI);

	if (UGameplayStatics::DoesSaveGameExist(AuraGI->LoadSlotName, AuraGI->LoadSlotIndex))
	{

		ULoadScreenSaveGame* SaveGame = Cast<ULoadScreenSaveGame>(UGameplayStatics::LoadGameFromSlot(AuraGI->LoadSlotName, AuraGI->LoadSlotIndex));
		if (SaveGame == nullptr)
		{
			UE_LOG(LogAura, Error, TEXT("Failed to load slot"));
			return;
		}

		for (FActorIterator It(World); It; ++It)
		{
			AActor* Actor = *It;

			if (!Actor->Implements<USaveInterface>()) continue;

			// 遍历存档中此地图保存的所有 Actor，按名字精确匹配
			for (FSavedActor SavedActor : SaveGame->GetSavedMapWithMapName(WorldName).SavedActors)
			{
				if (SavedActor.ActorName == Actor->GetFName())
				{
					// 部分 Actor（如可移动机关）需要还原位置，固定 Actor 则跳过
					if (ISaveInterface::Execute_ShouldLoadTransform(Actor))
					{
						Actor->SetActorTransform(SavedActor.Transform);
					}

					FMemoryReader MemoryReader(SavedActor.Bytes);

					FObjectAndNameAsStringProxyArchive Archive(MemoryReader, true);
					Archive.ArIsSaveGame = true;
					Actor->Serialize(Archive); // 将二进制字节还原回变量

					// 通知 Actor 执行自定义还原逻辑（例如检查点恢复已激活状态）
					ISaveInterface::Execute_LoadActor(Actor);
				}
			}
		}
	}
}

/**
 * 执行关卡跳转
 * 通过 Maps TMap 查找关卡的软对象引用，使用 OpenLevelBySoftObjectPtr 异步加载
 * 跳转前应先调用 SaveWorldState 保存当前关卡状态
 */
void AAuraGameModeBase::TravelToMap(UMVVM_LoadSlot* Slot)
{
	const FString SlotName = Slot->GetLoadSlotName();
	const int32 SlotIndex = Slot->SlotIndex;

	UGameplayStatics::OpenLevelBySoftObjectPtr(Slot, Maps.FindChecked(Slot->GetMapName()));
}

/**
 * 根据地图资产名（如"Dungeon_01"）反查 Maps 中记录的可读关卡名（如"地下城第一层"）
 * 用于 SaveWorldState 中将 Asset Name 转换为存档里记录的 MapName 字段
 */
FString AAuraGameModeBase::GetMapNameFromMapAssetName(const FString& MapAssetName) const
{
	for (auto& Map : Maps)
	{
		if (Map.Value.ToSoftObjectPath().GetAssetName() == MapAssetName)
		{
			return Map.Key;
		}
	}
	return FString();
}

/**
 * 重写引擎默认的出生点选择逻辑
 *
 * 默认实现随机选择 PlayerStart，此处改为：
 * 读取 GameInstance::PlayerStartTag，遍历所有 APlayerStart，
 * 找到 PlayerStartTag 匹配的出生点并返回。
 * 支持多入口地图（地牢正向进入 vs 从下一关返回）选择不同出生点。
 */
AActor* AAuraGameModeBase::ChoosePlayerStart_Implementation(AController* Player)
{
	UAuraGameInstance* AuraGameInstance = Cast<UAuraGameInstance>(GetGameInstance());

	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), Actors);
	if (Actors.Num() > 0)
	{
		AActor* SelectedActor = Actors[0]; // 默认使用第一个出生点作为兜底
		for (AActor* Actor : Actors)
		{
			if (APlayerStart* PlayerStart = Cast<APlayerStart>(Actor))
			{
				if (PlayerStart->PlayerStartTag == AuraGameInstance->PlayerStartTag)
				{
					SelectedActor = PlayerStart;
					break;
				}
			}
		}
		return SelectedActor;
	}
	return nullptr;
}

/**
 * 玩家死亡处理：重新打开存档中记录的当前地图（相当于重开本关）
 * 使用 MapAssetName（资产名）而非 MapName（可读名），确保 OpenLevel 能找到正确关卡
 */
void AAuraGameModeBase::PlayerDied(ACharacter* DeadCharacter)
{
	ULoadScreenSaveGame* SaveGame = RetrieveInGameSaveData();
	if (!IsValid(SaveGame)) return;

	UGameplayStatics::OpenLevel(DeadCharacter, FName(SaveGame->MapAssetName));
}

/**
 * BeginPlay：将默认地图软引用注册到 Maps TMap
 * 确保 TravelToMap 和 OpenLevel 等逻辑在游戏开始时能找到默认关卡
 */
void AAuraGameModeBase::BeginPlay()
{
	Super::BeginPlay();
	Maps.Add(DefaultMapName, DefaultMap);
}
