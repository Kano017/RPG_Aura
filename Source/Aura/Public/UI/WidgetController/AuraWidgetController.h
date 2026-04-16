// Copyright Druid Mechanics

#pragma once

/**
 * ============================================================
 * AuraWidgetController — UI 与 GAS 之间的中介层（WidgetController 模式）
 * ============================================================
 *
 * 【设计动机：为何不让 Widget 直接访问 GAS？】
 *   - UMG Widget 是纯粹的视图层（View），它不应该知道属性是怎么算出来的、
 *     技能系统内部结构如何，更不应该持有 PlayerState / ASC 的强引用。
 *   - 如果 Widget 直接 Cast 到 ASC 并订阅事件，则 UI 蓝图与 C++ 游戏逻辑之间
 *     形成强耦合，后续更换 ASC 实现或重构属性时，所有 Widget 都需要改动。
 *
 * 【WidgetController 模式的数据流向】
 *
 *   GAS（ASC / AttributeSet / PlayerState）
 *        ↓  属性变化 / GE 触发 / 技能状态变化
 *   UAuraWidgetController（中介层，C++ 订阅 GAS 事件）
 *        ↓  DECLARE_DYNAMIC_MULTICAST_DELEGATE 广播
 *   UAuraUserWidget（蓝图 Widget）
 *        ↓  在蓝图中 BindEvent 绑定委托，驱动 UI 动画 / 文本 / 进度条更新
 *
 *   这样 Widget 只需绑定委托，完全不感知 GAS 内部，实现 MVVM 思想中
 *   View（Widget）与 ViewModel（WidgetController）的解耦。
 *
 * 【生命周期】
 *   AAuraHUD::InitOverlay() 中负责创建 WidgetController 并调用：
 *     1. SetWidgetControllerParams()  —— 注入依赖（PC/PS/ASC/AS）
 *     2. BroadcastInitialValues()     —— 推送初始值，让 Widget 首次显示正确数据
 *     3. BindCallbacksToDependencies()—— 绑定后续动态更新
 * ============================================================
 */

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "UObject/NoExportTypes.h"
#include "AuraWidgetController.generated.h"

// 通用整型玩家数据变化委托（用于等级、属性点、法术点等）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerStatChangedSignature, int32, NewValue);

// 技能信息广播委托（遍历所有技能时，每条技能信息单独广播）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAbilityInfoSignature, const FAuraAbilityInfo&, Info);

class UAttributeSet;
class UAbilitySystemComponent;
class AAuraPlayerController;
class AAuraPlayerState;
class UAuraAbilitySystemComponent;
class UAuraAttributeSet;
class UAbilityInfo;

/**
 * FWidgetControllerParams — WidgetController 初始化参数包
 *
 * 【为何打包成结构体？】
 *   WidgetController 需要同时依赖 4 个核心对象（PC/PS/ASC/AS），
 *   若逐参数传递会导致函数签名冗长，且每次新增依赖都要修改所有调用处。
 *   打包成结构体后，调用方只需构造一个对象传入，扩展时只需修改结构体定义，
 *   符合"开放封闭原则"——对扩展开放，对修改关闭。
 *
 *   AAuraHUD 在初始化时统一构造一个 FWidgetControllerParams，
 *   传给 Overlay / AttributeMenu / SpellMenu 三个 WidgetController，保持一致性。
 */
USTRUCT(BlueprintType)
struct FWidgetControllerParams
{
	GENERATED_BODY()

	FWidgetControllerParams() {}
	// 便捷构造函数：一次性设置全部 4 个依赖
	FWidgetControllerParams(APlayerController* PC, APlayerState* PS, UAbilitySystemComponent* ASC, UAttributeSet* AS)
	: PlayerController(PC), PlayerState(PS), AbilitySystemComponent(ASC), AttributeSet(AS) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<APlayerController> PlayerController = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<APlayerState> PlayerState = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UAttributeSet> AttributeSet = nullptr;
};

/**
 * UAuraWidgetController — 所有 WidgetController 的公共基类
 *
 * 职责：
 *   - 持有游戏核心引用（PC/PS/ASC/AS），以基类指针存储，子类用 Cast 访问派生类功能
 *   - 提供两个核心虚函数供子类重写：
 *       BroadcastInitialValues()      —— 初始化推送
 *       BindCallbacksToDependencies() —— 动态绑定
 *   - 持有 AbilityInfo DataAsset，提供通用的技能信息广播（BroadcastAbilityInfo）
 *   - 提供懒加载 Getter（GetAuraPC/PS/ASC/AS），避免每次 Cast 的开销
 */
UCLASS()
class AURA_API UAuraWidgetController : public UObject
{
	GENERATED_BODY()
public:
	/**
	 * 注入全部依赖引用。
	 * 由 AAuraHUD 在创建 WidgetController 后立即调用，
	 * 必须在 BroadcastInitialValues / BindCallbacksToDependencies 之前执行。
	 */
	UFUNCTION(BlueprintCallable)
	void SetWidgetControllerParams(const FWidgetControllerParams& WCParams);

	/**
	 * 推送初始值 —— Widget 刚创建/显示时调用。
	 *
	 * 【职责】：主动"推送"当前所有数据的快照，让 Widget 立刻显示正确值。
	 *           例如：血条显示当前血量而非默认的 0。
	 * 【时机】：Widget 构造完成后、绑定委托之后立刻调用一次，此后不再调用。
	 * 【注意】：此函数不负责后续的动态更新，那是 BindCallbacksToDependencies 的职责。
	 */
	UFUNCTION(BlueprintCallable)
	virtual void BroadcastInitialValues();

	/**
	 * 绑定动态更新回调 —— 订阅 GAS 事件，确保属性变化时自动推送新值给 Widget。
	 *
	 * 【职责】：向 ASC / AttributeSet / PlayerState 注册监听，
	 *           当数据变化时触发对应委托广播，驱动 Widget 实时刷新。
	 * 【时机】：SetWidgetControllerParams 之后调用一次，生命周期内持续有效。
	 * 【区别于 BroadcastInitialValues】：
	 *   BroadcastInitialValues 是"拍快照"（主动推），
	 *   BindCallbacksToDependencies 是"订阅事件"（被动推）。
	 *   两者缺一不可：前者保证初始显示正确，后者保证后续变化能及时反映。
	 */
	virtual void BindCallbacksToDependencies();

	/**
	 * 技能信息广播委托。
	 * 每条技能的图标/名称/描述/状态等信息打包为 FAuraAbilityInfo 广播给 Widget。
	 * Widget 在蓝图中绑定此委托，收到后更新对应的技能球 UI。
	 */
	UPROPERTY(BlueprintAssignable, Category="GAS|Messages")
	FAbilityInfoSignature AbilityInfoDelegate;

	/**
	 * 遍历 ASC 中所有已注册技能，逐一广播 FAuraAbilityInfo 给 Widget。
	 * 仅在 bStartupAbilitiesGiven == true（初始技能已全部赋予）时才执行，
	 * 避免在技能尚未初始化时广播不完整数据。
	 */
	void BroadcastAbilityInfo();

protected:

	/**
	 * 技能信息 DataAsset（在蓝图默认值中配置）。
	 * 存储所有技能的 Tag → 显示信息（图标/名称/描述/类型）的映射。
	 * 供 BroadcastAbilityInfo 查询，也供 SpellMenuWidgetController 查询技能类型。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Widget Data")
	TObjectPtr<UAbilityInfo> AbilityInfo;

	// ---- 基类类型引用（由 SetWidgetControllerParams 注入） ----
	// 使用基类指针存储，以便 WidgetController 基类代码不依赖具体派生类实现

	UPROPERTY(BlueprintReadOnly, Category="WidgetController")
	TObjectPtr<APlayerController> PlayerController;

	UPROPERTY(BlueprintReadOnly, Category="WidgetController")
	TObjectPtr<APlayerState> PlayerState;

	UPROPERTY(BlueprintReadOnly, Category="WidgetController")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(BlueprintReadOnly, Category="WidgetController")
	TObjectPtr<UAttributeSet> AttributeSet;

	// ---- 派生类类型引用（懒加载 Cast 缓存，首次访问时初始化） ----
	// 子类频繁需要访问 Aura 特有功能，提前 Cast 并缓存避免反复开销

	UPROPERTY(BlueprintReadOnly, Category="WidgetController")
	TObjectPtr<AAuraPlayerController> AuraPlayerController;

	UPROPERTY(BlueprintReadOnly, Category="WidgetController")
	TObjectPtr<AAuraPlayerState> AuraPlayerState;

	UPROPERTY(BlueprintReadOnly, Category="WidgetController")
	TObjectPtr<UAuraAbilitySystemComponent> AuraAbilitySystemComponent;

	UPROPERTY(BlueprintReadOnly, Category="WidgetController")
	TObjectPtr<UAuraAttributeSet> AuraAttributeSet;

	// 懒加载 Getter：首次调用时 Cast 并缓存结果，后续直接返回缓存
	AAuraPlayerController* GetAuraPC();
	AAuraPlayerState* GetAuraPS();
	UAuraAbilitySystemComponent* GetAuraASC();
	UAuraAttributeSet* GetAuraAS();
};
