// Copyright Druid Mechanics

#pragma once

/*
 * AuraHUD.h
 *
 * UI 架构的顶层入口，负责持有并懒初始化三个核心 WidgetController。
 *
 * 【HUD 在 UI 架构中的角色】
 * AuraHUD 是整个 UI 系统的"单例工厂"：
 *   - 每个 WidgetController 只有一个实例（懒初始化：首次请求时创建，后续复用）
 *   - Widget 通过 GetOwningPlayer() → GetHUD() → GetXxxWidgetController() 访问控制器
 *   - HUD 持有覆盖层（OverlayWidget）的实例引用，负责创建并添加到视口
 *
 * 【三个 WidgetController 的职责】
 *   - OverlayWidgetController：驱动游戏内常驻 HUD（血条、法力条、经验值、技能槽冷却）
 *   - AttributeMenuWidgetController：驱动属性面板（主属性、次要属性展示和加点）
 *   - SpellMenuWidgetController：驱动法术菜单（技能树展示、技能解锁/升级/装备）
 *
 * 【初始化流程】
 * AAuraCharacter::BeginPlay → InitOverlay(PC, PS, ASC, AS)：
 *   1. 创建 OverlayWidget 实例并设置 WidgetController
 *   2. WidgetController 调用 BroadcastInitialValues 广播初始属性值
 *   3. 将 OverlayWidget 添加到视口
 * AttributeMenu 和 SpellMenu 按需在 Widget 首次打开时通过 GetXxxWidgetController 获取
 */

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AuraHUD.generated.h"

class UAttributeMenuWidgetController;
class UAttributeSet;
class UAbilitySystemComponent;
class UOverlayWidgetController;
class UAuraUserWidget;
struct FWidgetControllerParams;
class USpellMenuWidgetController;

/**
 * AAuraHUD
 *
 * 每个玩家控制器对应一个 HUD 实例（多人游戏下各客户端独立拥有）。
 * WidgetController 的类型通过 EditAnywhere UPROPERTY 在蓝图 BP_AuraHUD 中配置。
 */
UCLASS()
class AURA_API AAuraHUD : public AHUD
{
	GENERATED_BODY()
public:

	/**
	 * 获取覆盖层（游戏内常驻 HUD）的 WidgetController
	 * 懒初始化：首次调用时创建实例并绑定依赖回调，后续直接返回已有实例
	 * @param WCParams 包含 PlayerController、PlayerState、ASC、AttributeSet 的初始化参数包
	 */
	UOverlayWidgetController* GetOverlayWidgetController(const FWidgetControllerParams& WCParams);

	/**
	 * 获取属性菜单的 WidgetController
	 * 懒初始化模式同上，属性面板首次打开时才创建实例
	 */
	UAttributeMenuWidgetController* GetAttributeMenuWidgetController(const FWidgetControllerParams& WCParams);

	/**
	 * 获取法术菜单的 WidgetController
	 * 懒初始化模式同上，法术菜单首次打开时才创建实例
	 */
	USpellMenuWidgetController* GetSpellMenuWidgetController(const FWidgetControllerParams& WCParams);

	/**
	 * 初始化覆盖层 UI（在玩家角色 BeginPlay 时由 AAuraCharacter 调用）
	 * 创建 OverlayWidget → 设置 WidgetController → 广播初始值 → 添加到视口
	 * @param PC   玩家控制器（用于构建 WidgetControllerParams）
	 * @param PS   玩家状态（持有 ASC 和 AttributeSet）
	 * @param ASC  GAS 组件（监听属性变化和技能事件）
	 * @param AS   属性集（提供属性数值）
	 */
	void InitOverlay(APlayerController* PC, APlayerState* PS, UAbilitySystemComponent* ASC, UAttributeSet* AS);

protected:


private:

	/** 覆盖层 Widget 实例（游戏内常驻 HUD，血条/法力条/技能槽等） */
	UPROPERTY()
	TObjectPtr<UAuraUserWidget>  OverlayWidget;

	/** 覆盖层 Widget 的 UClass，在 BP_AuraHUD 中配置，运行时通过 CreateWidget 实例化 */
	UPROPERTY(EditAnywhere)
	TSubclassOf<UAuraUserWidget> OverlayWidgetClass;

	/** 覆盖层 WidgetController 单例实例（懒初始化，首次 GetOverlayWidgetController 时创建） */
	UPROPERTY()
	TObjectPtr<UOverlayWidgetController> OverlayWidgetController;

	/** 覆盖层 WidgetController 的 UClass，在 BP_AuraHUD 中配置 */
	UPROPERTY(EditAnywhere)
	TSubclassOf<UOverlayWidgetController> OverlayWidgetControllerClass;

	/** 属性菜单 WidgetController 单例实例（懒初始化） */
	UPROPERTY()
	TObjectPtr<UAttributeMenuWidgetController> AttributeMenuWidgetController;

	/** 属性菜单 WidgetController 的 UClass，在 BP_AuraHUD 中配置 */
	UPROPERTY(EditAnywhere)
	TSubclassOf<UAttributeMenuWidgetController> AttributeMenuWidgetControllerClass;

	/** 法术菜单 WidgetController 单例实例（懒初始化） */
	UPROPERTY()
	TObjectPtr<USpellMenuWidgetController> SpellMenuWidgetController;

	/** 法术菜单 WidgetController 的 UClass，在 BP_AuraHUD 中配置 */
	UPROPERTY(EditAnywhere)
	TSubclassOf<USpellMenuWidgetController> SpellMenuWidgetControllerClass;
};
