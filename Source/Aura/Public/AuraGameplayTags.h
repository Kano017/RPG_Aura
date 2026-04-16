// Copyright Druid Mechanics

/**
 * AuraGameplayTags.h
 *
 * 全局 Gameplay Tag 单例——集中管理项目中所有在 C++ 代码里使用的 Tag。
 *
 * 设计意图：
 *   GAS 允许在任意地方通过字符串请求 Tag：
 *     FGameplayTag::RequestGameplayTag(FName("Damage.Fire"))
 *   但这种方式有两个问题：
 *     1. 字符串写错时编译期不报错，只在运行时出现空 Tag 的 Bug；
 *     2. 同一个 Tag 字符串散落在代码各处，重命名时容易遗漏。
 *
 *   FAuraGameplayTags 将所有常用 Tag 存为具名成员变量，
 *   在游戏启动时一次性注册并缓存；业务代码通过：
 *     FAuraGameplayTags::Get().Damage_Fire
 *   访问，享受编译期检查和 IDE 跳转导航。
 *
 * 生命周期：
 *   InitializeNativeGameplayTags() 由 UAuraAssetManager::StartInitialLoading()
 *   在游戏启动的资产加载阶段调用，确保 Tag 在任何 GAS 对象初始化前就已注册。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/**
 * FAuraGameplayTags
 *
 * 原生 Gameplay Tag 的全局单例容器。
 * 所有在 C++ 代码中直接使用的 Tag 都在此声明并在启动时注册为 Native Tag。
 *
 * Native Tag 与编辑器 Tag 的区别：
 *   - Native Tag：通过 UGameplayTagsManager::AddNativeGameplayTag() 在代码中注册，
 *     保证运行时一定存在，不会因编辑器配置文件缺失或被意外删除而丢失。
 *   - 编辑器 Tag：在 DefaultGameplayTags.ini 或 Tag 编辑器中配置，
 *     灵活但依赖外部资产，存在被意外删除或拼写错误的风险。
 *   项目中频繁在逻辑代码里用到的 Tag 应统一注册为 Native Tag。
 */
struct FAuraGameplayTags
{
public:
    /** 获取全局单例实例，在 InitializeNativeGameplayTags() 调用之后才能安全使用 */
    static const FAuraGameplayTags& Get() { return GameplayTags;}

    /**
     * InitializeNativeGameplayTags
     *
     * 向 UGameplayTagsManager 注册所有原生 Tag，并构建映射表。
     * 由 UAuraAssetManager::StartInitialLoading() 在启动早期调用一次。
     * 实现见 AuraGameplayTags.cpp。
     */
    static void InitializeNativeGameplayTags();

    // =========================================================
    // 主要属性标签（Primary Attributes）
    // =========================================================
    // 对应 UAuraAttributeSet 中的四个基础属性。
    // 用途：
    //   1. AttributeMenuWidgetController 通过 TagsToAttributes 映射表
    //      将 Tag 关联到对应的 FGameplayAttribute，驱动 UI 显示。
    //   2. GE（GameplayEffect）的 SetByCaller 使用这些 Tag 传递数值。
    // =========================================================
	FGameplayTag Attributes_Primary_Strength;       // 力量：提升物理伤害
	FGameplayTag Attributes_Primary_Intelligence;   // 智慧：提升魔法伤害
	FGameplayTag Attributes_Primary_Resilience;     // 韧性：提升护甲和护甲穿透
	FGameplayTag Attributes_Primary_Vigor;          // 活力：提升最大生命值

    // =========================================================
    // 次要属性标签（Secondary Attributes）
    // =========================================================
    // 由主要属性通过 GE（MMC 或 Modifier）计算得出的派生属性。
    // 用途：
    //   1. AttributeMenuWidgetController 的 TagsToAttributes 映射，驱动属性面板 UI；
    //   2. ExecCalc_Damage 捕获护甲、暴击率等属性进行伤害计算。
    // =========================================================
	FGameplayTag Attributes_Secondary_Armor;                  // 护甲：减少受到的伤害，提升格挡概率
	FGameplayTag Attributes_Secondary_ArmorPenetration;       // 护甲穿透：忽略目标一定比例护甲，提升暴击率
	FGameplayTag Attributes_Secondary_BlockChance;            // 格挡概率：有概率将受到的伤害减半
	FGameplayTag Attributes_Secondary_CriticalHitChance;      // 暴击概率：有概率造成双倍+暴击加成伤害
	FGameplayTag Attributes_Secondary_CriticalHitDamage;      // 暴击加成：暴击时额外附加的伤害值
	FGameplayTag Attributes_Secondary_CriticalHitResistance;  // 暴击抗性：降低攻击者对自己的暴击概率
	FGameplayTag Attributes_Secondary_HealthRegeneration;     // 生命回复：每秒自动回复的生命值
	FGameplayTag Attributes_Secondary_ManaRegeneration;       // 法力回复：每秒自动回复的法力值
	FGameplayTag Attributes_Secondary_MaxHealth;              // 最大生命上限
	FGameplayTag Attributes_Secondary_MaxMana;                // 最大法力上限

    // =========================================================
    // 元属性标签（Meta Attributes）
    // =========================================================
    // 元属性是"中间值"，不代表持久的角色属性，
    // 而是作为临时数据管道在 GE 应用流程中使用。
    // =========================================================
	FGameplayTag Attributes_Meta_IncomingXP; // 传入经验值：通过 GE SetByCaller 传递 XP 数量，PostGameplayEffectExecute 读取后加到经验条

    // =========================================================
    // 输入标签（Input Tags）
    // =========================================================
    // 绑定到玩家输入动作的 Tag，存储在 AbilitySpec.DynamicAbilityTags 中。
    // 用途：
    //   AuraAbilitySystemComponent::AbilityInputTagPressed/Held/Released
    //   遍历所有已激活的 AbilitySpec，通过匹配 DynamicAbilityTags 来
    //   确定哪个技能响应当前按下的按键，实现输入与技能的解耦绑定。
    // =========================================================
	FGameplayTag InputTag_LMB;       // 鼠标左键：默认绑定移动/基础攻击
	FGameplayTag InputTag_RMB;       // 鼠标右键：默认绑定二技能
	FGameplayTag InputTag_1;         // 数字键 1：技能槽 1
	FGameplayTag InputTag_2;         // 数字键 2：技能槽 2
	FGameplayTag InputTag_3;         // 数字键 3：技能槽 3
	FGameplayTag InputTag_4;         // 数字键 4：技能槽 4
	FGameplayTag InputTag_Passive_1; // 被动技能槽 1（无需按键触发，装备时自动生效）
	FGameplayTag InputTag_Passive_2; // 被动技能槽 2

    // =========================================================
    // 伤害类型标签（Damage Types）
    // =========================================================
    // 伤害通道的标识符，与抗性属性一一对应。
    // 用途：
    //   1. FDamageEffectParams.DamageType 携带此 Tag 进入 ExecCalc_Damage；
    //   2. ExecCalc_Damage 在 DamageTypesToResistances 映射表中查找
    //      对应的抗性 Tag，再通过 AttributeSet.TagsToAttributes 捕获抗性属性值；
    //   3. DamageTypesToDebuffs 映射表通过此 Tag 找到对应的减益类型。
    // =========================================================
	FGameplayTag Damage;           // 伤害父 Tag，可用于 HasTag 检测是否为任意类型伤害
	FGameplayTag Damage_Fire;      // 火焰伤害 → 对应 Attributes.Resistance.Fire
	FGameplayTag Damage_Lightning; // 闪电伤害 → 对应 Attributes.Resistance.Lightning
	FGameplayTag Damage_Arcane;    // 奥术伤害 → 对应 Attributes.Resistance.Arcane
	FGameplayTag Damage_Physical;  // 物理伤害 → 对应 Attributes.Resistance.Physical

    // =========================================================
    // 抗性属性标签（Resistance Attributes）
    // =========================================================
    // 与伤害类型一一对应，存储在 UAuraAttributeSet 中。
    // ExecCalc_Damage 通过 FAuraGameplayTags::DamageTypesToResistances
    // 由伤害类型 Tag 直接查到对应抗性 Tag，
    // 再通过 AttributeSet.TagsToAttributes 映射查到 FGameplayAttribute，
    // 最后用 ExecutionParams.AttemptCalculateCapturedAttributeMagnitude() 捕获属性值。
    // =========================================================
	FGameplayTag Attributes_Resistance_Fire;      // 火焰抗性（百分比）：降低受到的火焰伤害
	FGameplayTag Attributes_Resistance_Lightning; // 闪电抗性（百分比）：降低受到的闪电伤害
	FGameplayTag Attributes_Resistance_Arcane;    // 奥术抗性（百分比）：降低受到的奥术伤害
	FGameplayTag Attributes_Resistance_Physical;  // 物理抗性（百分比）：降低受到的物理伤害

    // =========================================================
    // 减益效果类型标签（Debuff Types）
    // =========================================================
    // 每种伤害类型对应一种减益效果，在 DamageTypesToDebuffs 映射中配对。
    // 命中触发减益时，PostGameplayEffectExecute 通过伤害类型 Tag
    // 查找此组中对应的减益 Tag，进而动态创建并应用相应的减益 GE。
    //   Damage.Fire      → Debuff.Burn      （燃烧：持续火焰伤害）
    //   Damage.Lightning → Debuff.Stun      （眩晕：短暂无法行动）
    //   Damage.Arcane    → Debuff.Arcane    （奥术腐蚀：持续奥术伤害）
    //   Damage.Physical  → Debuff.Physical  （撕裂：持续物理伤害）
    // =========================================================
	FGameplayTag Debuff_Burn;     // 燃烧减益：火焰伤害触发，持续造成火焰跳伤
	FGameplayTag Debuff_Stun;     // 眩晕减益：闪电伤害触发，短暂使目标无法行动
	FGameplayTag Debuff_Arcane;   // 奥术腐蚀减益：奥术伤害触发，持续造成奥术跳伤
	FGameplayTag Debuff_Physical; // 撕裂减益：物理伤害触发，持续造成物理跳伤

    // =========================================================
    // 减益参数标签（Debuff Parameters）
    // =========================================================
    // 这四个 Tag 作为 GE SetByCaller 的键，在动态创建减益 GE 时
    // 将 FDamageEffectParams 中的减益参数值注入 GE Spec。
    // 减益 GE 蓝图内部通过对应 Tag 读取这些数值。
    // =========================================================
	FGameplayTag Debuff_Chance;    // SetByCaller 键：减益触发概率（由 ExecCalc 读取）
	FGameplayTag Debuff_Damage;    // SetByCaller 键：减益每次跳伤值
	FGameplayTag Debuff_Duration;  // SetByCaller 键：减益总持续时间（秒）
	FGameplayTag Debuff_Frequency; // SetByCaller 键：减益跳伤间隔（秒）

    // =========================================================
    // 技能标签（Abilities）
    // =========================================================

    /** Abilities.None：技能"空"标识，相当于技能 Tag 的 nullptr，用于表示槽位未装备技能 */
	FGameplayTag Abilities_None;

    /** Abilities.Attack：普通攻击标签，用于在 AI 和技能系统中识别并触发攻击技能 */
	FGameplayTag Abilities_Attack;

    /** Abilities.Summon：召唤技能标签，用于识别并触发召唤类技能 */
	FGameplayTag Abilities_Summon;

    /** Abilities.HitReact：受击反应技能标签，被命中时自动激活播放受击动画 */
	FGameplayTag Abilities_HitReact;

    // =========================================================
    // 技能状态机标签（Ability Status）
    // =========================================================
    // 技能解锁流程的四个状态，存储在 AbilitySpec.DynamicAbilityTags 中。
    // SpellMenuWidgetController 读取这些状态来决定技能格子的 UI 显示效果：
    //   Locked    → 灰色锁图标，玩家等级不足，无法解锁
    //   Eligible  → 高亮可解锁状态，玩家等级足够但尚未消费点数解锁
    //   Unlocked  → 已解锁但未装备到任何槽位
    //   Equipped  → 已装备到技能槽，可以使用
    // =========================================================
	FGameplayTag Abilities_Status_Locked;    // 锁定：等级不足，不可解锁
	FGameplayTag Abilities_Status_Eligible;  // 可解锁：等级达标，可消费技能点解锁
	FGameplayTag Abilities_Status_Unlocked;  // 已解锁：已学会但未分配到快捷槽
	FGameplayTag Abilities_Status_Equipped;  // 已装备：已分配到快捷键槽，可激活使用

    // =========================================================
    // 技能类型标签（Ability Types）
    // =========================================================
    // 用于区分技能分类，SpellMenu 用此类型过滤显示主动/被动技能格子
    // =========================================================
	FGameplayTag Abilities_Type_Offensive; // 攻击型主动技能（需要手动激活）
	FGameplayTag Abilities_Type_Passive;   // 被动技能（装备后自动生效，不占用输入槽）
	FGameplayTag Abilities_Type_None;      // 无类型（占位，避免空 Tag 比较问题）

    // =========================================================
    // 具体技能标签（Specific Abilities）
    // =========================================================
    // 每个技能的唯一标识符，作为 AbilitySpec.Ability.AbilityTags 存储。
    // SpellMenu 通过这些 Tag 在数据表中查找技能信息（名称、描述、图标）。
    // WaitCooldownChange 通过 Cooldown 标签监听冷却 GE 的应用/移除事件。
    // =========================================================
	FGameplayTag Abilities_Fire_FireBolt;             // 火球术
	FGameplayTag Abilities_Fire_FireBlast;            // 火爆（范围爆炸）
	FGameplayTag Abilities_Lightning_Electrocute;     // 电击链（闪电链）
	FGameplayTag Abilities_Arcane_ArcaneShards;       // 奥术碎片（地面尖刺）

	FGameplayTag Abilities_Passive_HaloOfProtection;  // 被动：护盾光环
	FGameplayTag Abilities_Passive_LifeSiphon;        // 被动：生命汲取
	FGameplayTag Abilities_Passive_ManaSiphon;        // 被动：法力汲取

    // =========================================================
    // 冷却标签（Cooldown Tags）
    // =========================================================
    // 冷却 GE 上会授予此标签；WaitCooldownChange 异步任务
    // 监听此 Tag 的添加/移除事件，驱动 UI 冷却转盘的显示。
    // =========================================================
	FGameplayTag Cooldown_Fire_FireBolt; // 火球术冷却标签

    // =========================================================
    // 战斗插槽标签（Combat Sockets）
    // =========================================================
    // 标识攻击特效/命中检测的骨骼插槽位置。
    // ICombatInterface::GetCombatSocketLocation() 根据此 Tag
    // 返回对应骨骼插槽的世界坐标，用于生成投射物或计算近战命中点。
    // =========================================================
	FGameplayTag CombatSocket_Weapon;    // 武器插槽（持握武器的骨骼）
	FGameplayTag CombatSocket_RightHand; // 右手插槽（无武器时的右手施法点）
	FGameplayTag CombatSocket_LeftHand;  // 左手插槽（双手施法或副手特效）
	FGameplayTag CombatSocket_Tail;      // 尾部插槽（尾部攻击的怪物使用）

    // =========================================================
    // 攻击蒙太奇标签（Attack Montage Tags）
    // =========================================================
    // 标识 FTaggedMontage 中蒙太奇动画的攻击类型编号。
    // ICombatInterface::GetAttackMontages() 返回带 Tag 的蒙太奇列表，
    // AI 或技能随机选择一条蒙太奇播放，并通过 Tag 确定对应的命中插槽。
    // =========================================================
	FGameplayTag Montage_Attack_1; // 攻击动画 1
	FGameplayTag Montage_Attack_2; // 攻击动画 2
	FGameplayTag Montage_Attack_3; // 攻击动画 3
	FGameplayTag Montage_Attack_4; // 攻击动画 4

    // =========================================================
    // 伤害类型 → 抗性属性 映射表
    // =========================================================
    // 键：伤害类型 Tag（如 Damage.Fire）
    // 值：对应的抗性属性 Tag（如 Attributes.Resistance.Fire）
    //
    // ExecCalc_Damage 遍历此映射表：
    //   for (auto& [DmgTag, ResTag] : DamageTypesToResistances)
    //     → 通过 ResTag 在 AttributeSet.TagsToAttributes 中找到 FGameplayAttribute
    //     → 调用 AttemptCalculateCapturedAttributeMagnitude 捕获目标的抗性值
    //     → 从基础伤害中减去对应抗性百分比
    // 无需 switch/if 分支，新增伤害类型只需添加一行映射即可扩展。
    // =========================================================
	TMap<FGameplayTag, FGameplayTag> DamageTypesToResistances;

    // =========================================================
    // 伤害类型 → 减益效果 映射表
    // =========================================================
    // 键：伤害类型 Tag（如 Damage.Lightning）
    // 值：对应的减益 Tag（如 Debuff.Stun）
    //
    // 命中后减益判定流程：
    //   ExecCalc_Damage 通过 DamageType 查此映射 → 得到减益 Tag
    //   → 将减益 Tag 写入 GE 上下文
    //   → PostGameplayEffectExecute 读取减益 Tag
    //   → 动态创建并应用对应的减益 GameplayEffect
    // 同样使用映射表而非分支，便于后续扩展新伤害/减益类型。
    // =========================================================
	TMap<FGameplayTag, FGameplayTag> DamageTypesToDebuffs;

    // =========================================================
    // 效果标签（Effects）
    // =========================================================

    /**
     * Effects.HitReact
     * 受击时授予目标的标签。
     * AuraAbilitySystemComponent 在激活 HitReact 技能时授予此 Tag；
     * AI 行为树读取此 Tag 来判断目标是否正在播放受击动画，
     * 避免在受击硬直期间继续攻击造成动画穿插。
     */
	FGameplayTag Effects_HitReact;

    // =========================================================
    // 玩家阻塞标签（Player Block Tags）
    // =========================================================
    // 这组 Tag 用于在特定游戏状态下临时屏蔽玩家的输入或光标追踪。
    // 例如：打开技能菜单时添加 Block_CursorTrace，防止 3D 世界中的
    // 拾取高亮逻辑与 UI 交互冲突；播放特殊动画时屏蔽所有输入。
    // AuraPlayerController 在每帧输入处理前检查这些 Tag 是否存在。
    // =========================================================
	FGameplayTag Player_Block_InputPressed;  // 屏蔽"按键按下"回调
	FGameplayTag Player_Block_InputHeld;     // 屏蔽"按键长按"回调
	FGameplayTag Player_Block_InputReleased; // 屏蔽"按键松开"回调
	FGameplayTag Player_Block_CursorTrace;   // 屏蔽光标下的场景射线检测（防止 3D 物体高亮）

    // =========================================================
    // GameplayCue 标签（Visual/Audio Effects）
    // =========================================================
    // GameplayCue 标签用于触发纯表现层效果（粒子、音效、贴花）。
    // 与 Gameplay 逻辑完全解耦：不修改任何 Attribute，不应用 GE，
    // 只在客户端触发对应的 UGameplayCueNotify 类（Burst/Looping）。
    // 多人游戏中 GAS 会自动将 Cue 复制到所有客户端，
    // 无需手动 Multicast RPC。
    // =========================================================
	FGameplayTag GameplayCue_FireBlast; // FireBlast 爆炸视觉效果（粒子+音效）

private:
    /** 全局单例实例，由 InitializeNativeGameplayTags() 填充 */
    static FAuraGameplayTags GameplayTags;
};
