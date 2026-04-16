// Copyright Druid Mechanics

#pragma once

/**
 * ============================================================
 * OverlayWidgetController — HUD 覆盖层的数据控制器
 * ============================================================
 *
 * 负责驱动游戏主 HUD（血条、魔力条、经验条、等级、消息提示、技能栏）的所有实时数据。
 *
 * 【数据流总览】
 *
 *   ┌─────────────────────────────────────────────────────┐
 *   │  数据源                  传递路径              委托/Widget │
 *   ├─────────────────────────────────────────────────────┤
 *   │  AttributeSet.Health  → AttributeValueChange  → OnHealthChanged       │
 *   │  AttributeSet.Mana    → AttributeValueChange  → OnManaChanged         │
 *   │  PlayerState.XP       → OnXPChangedDelegate   → OnXPPercentChanged    │
 *   │  PlayerState.Level    → OnLevelChangedDelegate→ OnPlayerLevelChanged  │
 *   │  GameplayEffect Tag   → EffectAssetTags        → MessageWidgetRowDelegate│
 *   │  AbilityInfo DA       → ForEachAbility         → AbilityInfoDelegate  │
 *   └─────────────────────────────────────────────────────┘
 *
 * 【消息提示机制】
 *   GameplayEffect 可携带 AssetTag（如 Message.HealthPotion）。
 *   ASC 应用 GE 时触发 EffectAssetTags 事件，OverlayWidgetController 检测
 *   Tag 是否匹配 "Message" 父标签，若匹配则在 MessageWidgetDataTable 中
 *   查找对应行（FUIWidgetRow），广播给 Widget 显示弹出提示框。
 *   这样物品/技能效果只需配 Tag，无需硬编码 UI 逻辑。
 * ============================================================
 */

#include "CoreMinimal.h"
#include "UI/WidgetController/AuraWidgetController.h"
#include "OverlayWidgetController.generated.h"

struct FAuraAbilityInfo;

/**
 * FUIWidgetRow — 消息提示的 DataTable 行结构体
 *
 * 每个 "Message.XXX" GameplayTag 对应 DataTable 中的一行配置，
 * 定义该消息弹出时显示的文字、图标和 Widget 类型。
 * 当玩家拾取道具/触发特殊效果时，GE 的 AssetTag 驱动此系统显示提示。
 */
USTRUCT(BlueprintType)
struct FUIWidgetRow : public FTableRowBase
{
	GENERATED_BODY()

	// 触发此消息的 GameplayTag（对应 DataTable 的行名）
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag MessageTag = FGameplayTag();

	// 显示给玩家的提示文本（支持本地化）
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Message = FText();

	// 弹出提示使用的 Widget 类（不同消息可用不同样式的 Widget）
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<class UAuraUserWidget> MessageWidget;

	// 配合消息显示的图标纹理（如道具图标）
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	UTexture2D* Image = nullptr;
};

class UAuraUserWidget;
class UAbilityInfo;
class UAuraAbilitySystemComponent;

// 属性值浮点数变化委托（血量、魔力等连续值）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAttributeChangedSignature, float, NewValue);

// 等级变化委托（携带新等级值和是否是升级事件标志）
// bLevelUp = true  表示主动升级（播放升级特效）
// bLevelUp = false 表示读档恢复等级（不播放升级特效）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLevelChangedSignature, int32, NewLevel, bool, bLevelUp);

// 消息提示委托（将完整的 FUIWidgetRow 传给 Widget，Widget 据此创建提示框）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMessageWidgetRowSignature, FUIWidgetRow, Row);

/**
 * UOverlayWidgetController — 主 HUD 覆盖层控制器
 *
 * 所有公开的 UPROPERTY(BlueprintAssignable) 委托均可在蓝图 Widget 中绑定，
 * 实现零代码的 UI 数据驱动更新。
 */
UCLASS(BlueprintType, Blueprintable)
class AURA_API UOverlayWidgetController : public UAuraWidgetController
{
	GENERATED_BODY()
public:
	/**
	 * 初始化推送：Widget 首次显示时立刻填充血条/魔力条的当前值，
	 * 避免进入游戏时显示空进度条再跳变到正确值。
	 */
	virtual void BroadcastInitialValues() override;

	/**
	 * 绑定所有 HUD 数据的实时更新监听。
	 * 包括属性变化、XP/等级变化、GE 消息标签、技能信息广播。
	 */
	virtual void BindCallbacksToDependencies() override;

	// ---- 血条/魔力条相关委托 ----
	// Widget 的 ProgressBar 绑定这些委托，属性变化时自动更新填充比例

	/** 当前血量变化（0 ~ MaxHealth 的浮点数）*/
	UPROPERTY(BlueprintAssignable, Category="GAS|Attributes")
	FOnAttributeChangedSignature OnHealthChanged;

	/** 最大血量变化（血条上限改变，例如装备提升后）*/
	UPROPERTY(BlueprintAssignable, Category="GAS|Attributes")
	FOnAttributeChangedSignature OnMaxHealthChanged;

	/** 当前魔力变化 */
	UPROPERTY(BlueprintAssignable, Category="GAS|Attributes")
	FOnAttributeChangedSignature OnManaChanged;

	/** 最大魔力变化 */
	UPROPERTY(BlueprintAssignable, Category="GAS|Attributes")
	FOnAttributeChangedSignature OnMaxManaChanged;

	/**
	 * 消息提示委托。
	 * 当 GE 携带 "Message.XXX" 标签时触发，广播对应的 FUIWidgetRow 配置，
	 * Widget 据此弹出图文提示（如"拾取了生命药水"）。
	 */
	UPROPERTY(BlueprintAssignable, Category="GAS|Messages")
	FMessageWidgetRowSignature MessageWidgetRowDelegate;

	/**
	 * 经验条百分比委托（0.0 ~ 1.0）。
	 * 由 OnXPChanged 内部计算当前等级下的 XP 进度后广播。
	 * Widget 直接将此值绑定到经验条 ProgressBar 的 Percent 属性。
	 */
	UPROPERTY(BlueprintAssignable, Category="GAS|XP")
	FOnAttributeChangedSignature OnXPPercentChangedDelegate;

	/**
	 * 玩家等级变化委托。
	 * bLevelUp 参数控制 Widget 是否播放升级动画/音效/特效，
	 * 读档恢复等级时 bLevelUp = false，正常升级时 bLevelUp = true。
	 */
	UPROPERTY(BlueprintAssignable, Category="GAS|Level")
	FOnLevelChangedSignature OnPlayerLevelChangedDelegate;

protected:

	/**
	 * 消息提示配置表（在蓝图默认值中指定）。
	 * 行名 = GameplayTag 名（如 "Message.HealthPotion"）
	 * 行数据 = FUIWidgetRow（文本/图标/Widget类）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Widget Data")
	TObjectPtr<UDataTable> MessageWidgetDataTable;

	/**
	 * 按 GameplayTag 在 DataTable 中查找对应行数据。
	 * 模板函数以支持不同结构体类型的 DataTable（目前用 FUIWidgetRow）。
	 * Tag.GetTagName() 作为行名进行查找。
	 */
	template<typename T>
	T* GetDataTableRowByTag(UDataTable* DataTable, const FGameplayTag& Tag);

	/**
	 * PlayerState.XP 变化时的回调函数。
	 * 将原始 XP 整数转换为当前等级下的百分比（0.0~1.0），
	 * 公式：(XP - 上一等级门槛) / (本等级门槛 - 上一等级门槛)
	 * 然后广播 OnXPPercentChangedDelegate。
	 */
	void OnXPChanged(int32 NewXP);

	/**
	 * 技能装备完成时的回调（由 AuraASC.AbilityEquipped 委托触发）。
	 *
	 * 【处理逻辑】
	 *   1. 广播"清空旧槽位"信息（PreviousSlot + Abilities_None）
	 *      ——让旧槽位的技能球 Widget 恢复空白状态
	 *   2. 广播"填充新槽位"信息（新的 AbilityTag + Slot）
	 *      ——让新槽位的技能球 Widget 显示正确的技能图标
	 */
	void OnAbilityEquipped(const FGameplayTag& AbilityTag, const FGameplayTag& Status, const FGameplayTag& Slot, const FGameplayTag& PreviousSlot) const;
};

/**
 * 模板函数实现（必须在头文件中，否则链接时找不到具体实例化）。
 * 直接使用 DataTable 的 FindRow<T>，以 Tag 名字符串作为行键查找。
 */
template <typename T>
T* UOverlayWidgetController::GetDataTableRowByTag(UDataTable* DataTable, const FGameplayTag& Tag)
{
	return DataTable->FindRow<T>(Tag.GetTagName(), TEXT(""));
}
