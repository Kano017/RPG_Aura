// Copyright Druid Mechanics


#include "UI/HUD/AuraHUD.h"

#include "UI/Widget/AuraUserWidget.h"
#include "UI/WidgetController/AttributeMenuWidgetController.h"
#include "UI/WidgetController/OverlayWidgetController.h"
#include "UI/WidgetController/SpellMenuWidgetController.h"

/**
 * 懒初始化获取覆盖层 WidgetController
 *
 * 首次调用时：
 *   1. NewObject 创建 OverlayWidgetController 实例（以 HUD 为 Outer）
 *   2. SetWidgetControllerParams 注入 PC/PS/ASC/AS 四个 GAS 核心对象
 *   3. BindCallbacksToDependencies 绑定对 ASC 属性变化和技能状态变化的监听回调
 * 后续调用直接返回已有实例，避免重复创建和重复绑定
 */
UOverlayWidgetController* AAuraHUD::GetOverlayWidgetController(const FWidgetControllerParams& WCParams)
{
	if (OverlayWidgetController == nullptr)
	{
		OverlayWidgetController = NewObject<UOverlayWidgetController>(this, OverlayWidgetControllerClass);
		OverlayWidgetController->SetWidgetControllerParams(WCParams);
		OverlayWidgetController->BindCallbacksToDependencies();
	}
	return OverlayWidgetController;
}

/**
 * 懒初始化获取属性菜单 WidgetController
 *
 * 属性菜单不在游戏开始时创建，只有玩家第一次打开属性面板时才执行初始化。
 * 初始化后绑定属性变化回调，后续属性加点时自动广播更新给 UI。
 */
UAttributeMenuWidgetController* AAuraHUD::GetAttributeMenuWidgetController(const FWidgetControllerParams& WCParams)
{
	if (AttributeMenuWidgetController == nullptr)
	{
		AttributeMenuWidgetController = NewObject<UAttributeMenuWidgetController>(this, AttributeMenuWidgetControllerClass);
		AttributeMenuWidgetController->SetWidgetControllerParams(WCParams);
		AttributeMenuWidgetController->BindCallbacksToDependencies();
	}
	return AttributeMenuWidgetController;
}

/**
 * 懒初始化获取法术菜单 WidgetController
 *
 * 法术菜单不在游戏开始时创建，只有玩家第一次打开法术菜单时才执行初始化。
 * 初始化后绑定技能状态变化回调，技能解锁/升级/装备时自动广播给 UI。
 */
USpellMenuWidgetController* AAuraHUD::GetSpellMenuWidgetController(const FWidgetControllerParams& WCParams)
{
	if (SpellMenuWidgetController == nullptr)
	{
		SpellMenuWidgetController = NewObject<USpellMenuWidgetController>(this, SpellMenuWidgetControllerClass);
		SpellMenuWidgetController->SetWidgetControllerParams(WCParams);
		SpellMenuWidgetController->BindCallbacksToDependencies();
	}
	return SpellMenuWidgetController;
}

/**
 * 初始化并显示游戏内覆盖层 UI
 *
 * 调用时机：AAuraCharacter::BeginPlay（玩家角色完成 GAS 初始化后立即调用）
 *
 * 执行步骤：
 * 1. checkf 验证蓝图配置完整性（未配置类型则在开发阶段早期崩溃提示）
 * 2. CreateWidget 创建 OverlayWidget 实例
 * 3. 通过 GetOverlayWidgetController 获取（或创建）WidgetController
 * 4. SetWidgetController 将控制器注入 Widget（Widget 通过 OnWidgetControllerSet 事件初始化绑定）
 * 5. BroadcastInitialValues 立即广播当前属性值，确保 Widget 显示正确的初始数据
 * 6. AddToViewport 将覆盖层添加到屏幕
 */
void AAuraHUD::InitOverlay(APlayerController* PC, APlayerState* PS, UAbilitySystemComponent* ASC, UAttributeSet* AS)
{
	checkf(OverlayWidgetClass, TEXT("Overlay Widget Class uninitialized, please fill out BP_AuraHUD"));
	checkf(OverlayWidgetControllerClass, TEXT("Overlay Widget Controller Class uninitialized, please fill out BP_AuraHUD"));

	UUserWidget* Widget = CreateWidget<UUserWidget>(GetWorld(), OverlayWidgetClass);
	OverlayWidget = Cast<UAuraUserWidget>(Widget);

	const FWidgetControllerParams WidgetControllerParams(PC, PS, ASC, AS);
	UOverlayWidgetController* WidgetController = GetOverlayWidgetController(WidgetControllerParams);

	// 将控制器注入 Widget，Widget 内部响应 OnWidgetControllerSet 事件完成自身绑定
	OverlayWidget->SetWidgetController(WidgetController);
	// 广播初始值，使 Widget 一出现就显示正确的血量/法力值等数据
	WidgetController->BroadcastInitialValues();
	Widget->AddToViewport();
}
