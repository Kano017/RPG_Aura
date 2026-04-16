// Copyright Druid Mechanics

#pragma once

/**
 * ============================================================
 * AttributeMenuWidgetController — 属性菜单的数据控制器
 * ============================================================
 *
 * 负责驱动属性详情菜单（力量、智力、韧性等各属性的数值显示与升级）。
 *
 * 【数据来源】
 *   - UAuraAttributeSet::TagsToAttributes —— Tag → FGameplayAttribute 的映射表
 *     遍历此表可获得所有可显示属性及其当前值
 *   - UAttributeInfo DataAsset —— Tag → 显示配置（名称/描述）的映射表
 *     合并两者才能得到"完整的可显示属性条目"：名称 + 当前值
 *   - AAuraPlayerState::AttributePoints —— 当前可用属性点数
 *
 * 【升级流程（服务器权威）】
 *   Widget 点击升级按钮
 *     → UpgradeAttribute(Tag)（在 WidgetController，客户端调用）
 *       → AuraASC->UpgradeAttribute(Tag)
 *         → ServerRPC（确保属性升级逻辑在服务器执行）
 *           → 服务器修改属性 + 扣除属性点
 *             → 属性变化通过 GAS 复制传回客户端
 *               → BindCallbacksToDependencies 中绑定的 Lambda 触发
 *                 → AttributeInfoDelegate 广播 → Widget 刷新数值
 *
 * 【与 OverlayWidgetController 的区别】
 *   Overlay 展示动态战斗数据（血量/魔力），数据源是连续变化的 GAS 属性。
 *   AttributeMenu 展示静态成长数据（力量/敏捷等），需要遍历全部属性类型，
 *   数据源结合了 DataAsset（显示信息）和 AttributeSet（当前数值）。
 * ============================================================
 */

#include "CoreMinimal.h"
#include "UI/WidgetController/AuraWidgetController.h"
#include "AttributeMenuWidgetController.generated.h"

class UAttributeInfo;
struct FAuraAttributeInfo;
struct FGameplayTag;

/**
 * 属性信息广播委托。
 * FAuraAttributeInfo 包含：属性 Tag、属性名称文本、属性描述文本、当前数值。
 * Widget 收到后找到对应的属性行 UI 并更新显示值。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAttributeInfoSignature, const FAuraAttributeInfo&, Info);

/**
 * UAttributeMenuWidgetController — 属性菜单控制器
 */
UCLASS(BlueprintType, Blueprintable)
class AURA_API UAttributeMenuWidgetController : public UAuraWidgetController
{
	GENERATED_BODY()
public:
	/**
	 * 绑定属性变化监听：
	 *   - 遍历 TagsToAttributes 中所有属性，为每个属性注册变化回调
	 *   - 订阅 PlayerState 的 AttributePoints 变化（升级时点数更新）
	 */
	virtual void BindCallbacksToDependencies() override;

	/**
	 * 初始化推送：
	 *   - 遍历全部属性，广播每个属性的当前值（让菜单打开时立刻显示正确数值）
	 *   - 广播当前可用属性点数
	 */
	virtual void BroadcastInitialValues() override;

	/**
	 * 属性信息委托。
	 * 每次广播传递一条 FAuraAttributeInfo（单个属性的完整信息），
	 * Widget 遍历接收所有属性后填充属性列表。
	 */
	UPROPERTY(BlueprintAssignable, Category="GAS|Attributes")
	FAttributeInfoSignature AttributeInfoDelegate;

	/**
	 * 属性点变化委托（整型）。
	 * 玩家升级或消费属性点时广播，Widget 据此更新"剩余属性点数"的显示
	 * 并控制升级按钮的可用状态（点数为 0 时禁用按钮）。
	 */
	UPROPERTY(BlueprintAssignable, Category="GAS|Attributes")
	FOnPlayerStatChangedSignature AttributePointsChangedDelegate;

	/**
	 * 玩家点击属性升级按钮时由蓝图调用。
	 * 通过 AuraASC->UpgradeAttribute 发起 ServerRPC，
	 * 确保升级逻辑在服务器执行（防止客户端作弊修改属性）。
	 * @param AttributeTag  要升级的属性对应的 GameplayTag
	 */
	UFUNCTION(BlueprintCallable)
	void UpgradeAttribute(const FGameplayTag& AttributeTag);

protected:

	/**
	 * 属性显示配置 DataAsset（在蓝图默认值中指定）。
	 * 存储 GameplayTag → FAuraAttributeInfo（属性名/描述）的映射，
	 * 与 AttributeSet 的数值合并后形成完整的属性 UI 数据。
	 */
	UPROPERTY(EditDefaultsOnly)
	TObjectPtr<UAttributeInfo> AttributeInfo;

private:

	/**
	 * 广播单条属性信息的内部辅助函数。
	 * 从 DataAsset 查找显示配置，从 AttributeSet 获取当前数值，合并后广播。
	 * @param AttributeTag  属性 GameplayTag（用于在 DataAsset 中查找）
	 * @param Attribute     对应的 FGameplayAttribute（用于读取当前数值）
	 */
	void BroadcastAttributeInfo(const FGameplayTag& AttributeTag, const FGameplayAttribute& Attribute) const;
};
