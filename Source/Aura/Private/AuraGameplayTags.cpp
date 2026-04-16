// Copyright Druid Mechanics

/**
 * AuraGameplayTags.cpp
 *
 * FAuraGameplayTags 单例的静态成员定义与所有 Native Tag 的注册实现。
 *
 * Native Tag 注册机制：
 *   UGameplayTagsManager::AddNativeGameplayTag() 会在 TagsManager 内部
 *   的 Native Tag 表中注册该字符串，并返回对应的 FGameplayTag 句柄。
 *   Native Tag 的特点：
 *     - 不依赖 DefaultGameplayTags.ini，代码即配置；
 *     - 项目打包时自动包含，不会被资产管理器遗漏；
 *     - 可以在编辑器 Tag 列表中看到，但不能从编辑器删除（只能从代码移除）。
 *
 * 调用时机：
 *   UAuraAssetManager::StartInitialLoading() 在引擎启动阶段、
 *   任何 UObject 加载之前调用 InitializeNativeGameplayTags()，
 *   保证所有 GAS 对象初始化时 Tag 表已完整。
 */

#include "AuraGameplayTags.h"
#include "GameplayTagsManager.h"

/** 全局单例实例的定义（声明在头文件 private: 区域） */
FAuraGameplayTags FAuraGameplayTags::GameplayTags;

/**
 * InitializeNativeGameplayTags
 *
 * 向引擎的 GameplayTagsManager 注册本项目所有 Native Tag，
 * 并构建两个运行时查询映射表（DamageTypesToResistances / DamageTypesToDebuffs）。
 *
 * 函数结构：
 *   1. 按功能分组注册各类 Tag（属性、输入、伤害、减益、技能、战斗等）；
 *   2. 注册完成后构建两个 TMap，供 ExecCalc_Damage 在运行时 O(1) 查询。
 *
 * 扩展指南：
 *   新增伤害类型时，在对应分组各添加一行注册，
 *   并在两个映射表中各添加一条配对，无需修改 ExecCalc_Damage 的逻辑代码。
 */
void FAuraGameplayTags::InitializeNativeGameplayTags()
{
	/*
	 * 主要属性
	 * 对应 UAuraAttributeSet 的四个基础属性（Strength/Intelligence/Resilience/Vigor）。
	 * AttributeMenuWidgetController.TagsToAttributes 用这些 Tag 关联到 FGameplayAttribute，
	 * 驱动属性面板 UI 的数值显示和升级按钮逻辑。
	 */
	GameplayTags.Attributes_Primary_Strength = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Primary.Strength"),
		FString("Increases physical damage")
		);

	GameplayTags.Attributes_Primary_Intelligence = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Primary.Intelligence"),
		FString("Increases magical damage")
		);

	GameplayTags.Attributes_Primary_Resilience = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Primary.Resilience"),
		FString("Increases Armor and Armor Penetration")
		);

	GameplayTags.Attributes_Primary_Vigor = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Primary.Vigor"),
		FString("Increases Health")
		);

	/*
	 * 次要属性
	 * 由主属性派生的战斗数值，通过 GE（带 MMC 或 Modifier）计算。
	 * ExecCalc_Damage 捕获 Armor、ArmorPenetration、CriticalHitChance 等属性
	 * 完成伤害最终计算；HealthRegeneration/ManaRegeneration 由独立的持续性 GE 驱动。
	 */

	GameplayTags.Attributes_Secondary_Armor = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Secondary.Armor"),
		FString("Reduces damage taken, improves Block Chance")
		);

	GameplayTags.Attributes_Secondary_ArmorPenetration = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Secondary.ArmorPenetration"),
		FString("Ignores Percentage of enemy Armor, increases Critical Hit Chance")
		);

	GameplayTags.Attributes_Secondary_BlockChance = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Secondary.BlockChance"),
		FString("Chance to cut incoming damage in half")
		);

	GameplayTags.Attributes_Secondary_CriticalHitChance = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Secondary.CriticalHitChance"),
		FString("Chance to double damage plus critical hit bonus")
		);

	GameplayTags.Attributes_Secondary_CriticalHitDamage = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Secondary.CriticalHitDamage"),
		FString("Bonus damage added when a critical hit is scored")
		);

	GameplayTags.Attributes_Secondary_CriticalHitResistance = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Secondary.CriticalHitResistance"),
		FString("Reduces Critical Hit Chance of attacking enemies")
		);

	GameplayTags.Attributes_Secondary_HealthRegeneration = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Secondary.HealthRegeneration"),
		FString("Amount of Health regenerated every 1 second")
		);

	GameplayTags.Attributes_Secondary_ManaRegeneration = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Secondary.ManaRegeneration"),
		FString("Amount of Mana regenerated every 1 second")
		);

	GameplayTags.Attributes_Secondary_MaxHealth = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Secondary.MaxHealth"),
		FString("Maximum amount of Health obtainable")
		);

	GameplayTags.Attributes_Secondary_MaxMana = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Secondary.MaxMana"),
		FString("Maximum amount of Mana obtainable")
		);

	/*
	 * 输入标签
	 * 每个标签对应一个物理输入动作（鼠标键/数字键）。
	 * GiveAbility 时将对应 InputTag 添加到 AbilitySpec.DynamicAbilityTags；
	 * AuraAbilitySystemComponent 在处理输入事件时遍历 ActivatableAbilities，
	 * 匹配 DynamicAbilityTags 中包含当前 InputTag 的 AbilitySpec 并触发它。
	 * 这套机制让输入绑定与技能完全解耦，运行时可动态重绑定。
	 */

	GameplayTags.InputTag_LMB = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("InputTag.LMB"),
		FString("Input Tag for Left Mouse Button")
		);

	GameplayTags.InputTag_RMB = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("InputTag.RMB"),
		FString("Input Tag for Right Mouse Button")
		);

	GameplayTags.InputTag_1 = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("InputTag.1"),
		FString("Input Tag for 1 key")
		);

	GameplayTags.InputTag_2 = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("InputTag.2"),
		FString("Input Tag for 2 key")
		);

	GameplayTags.InputTag_3 = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("InputTag.3"),
		FString("Input Tag for 3 key")
		);

	GameplayTags.InputTag_4 = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("InputTag.4"),
		FString("Input Tag for 4 key")
		);

	GameplayTags.InputTag_Passive_1 = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("InputTag.Passive.1"),
		FString("Input Tag Passive Ability 1")
		);

	GameplayTags.InputTag_Passive_2 = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("InputTag.Passive.2"),
		FString("Input Tag Passive Ability 2")
		);

	// Damage：父 Tag，HasTag(Damage) 可检测任意伤害类型
	GameplayTags.Damage = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Damage"),
		FString("Damage")
		);

	/*
	 * 伤害类型
	 * 四种元素/物理伤害通道的标识 Tag。
	 * 技能在 FDamageEffectParams.DamageType 中填入其中一个，
	 * ExecCalc_Damage 据此查询目标对应的抗性属性，实现多元素伤害系统。
	 * 若需新增伤害类型（如冰霜），在此处注册后同步更新两个映射表即可。
	 */

	GameplayTags.Damage_Fire = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Damage.Fire"),
		FString("Fire Damage Type")
		);
	GameplayTags.Damage_Lightning = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Damage.Lightning"),
		FString("Lightning Damage Type")
		);
	GameplayTags.Damage_Arcane = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Damage.Arcane"),
		FString("Arcane Damage Type")
		);
	GameplayTags.Damage_Physical = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Damage.Physical"),
		FString("Physical Damage Type")
		);

	/*
	 * 抗性属性标签
	 * 对应 UAuraAttributeSet 中的四个抗性浮点属性（百分比，0~100）。
	 * 这些 Tag 用于 AttributeSet.TagsToAttributes 的反向映射：
	 *   ExecCalc_Damage 持有 Tag → 在 TagsToAttributes 中找到 FGameplayAttribute
	 *   → 调用 AttemptCalculateCapturedAttributeMagnitude() 获取目标当前的属性值。
	 * 不直接硬编码属性引用的好处：新增抗性只需添加 Tag + 属性声明 + 映射条目。
	 */

	GameplayTags.Attributes_Resistance_Arcane = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Resistance.Arcane"),
		FString("Resistance to Arcane damage")
		);
	GameplayTags.Attributes_Resistance_Fire = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Resistance.Fire"),
		FString("Resistance to Fire damage")
		);
	GameplayTags.Attributes_Resistance_Lightning = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Resistance.Lightning"),
		FString("Resistance to Lightning damage")
		);
	GameplayTags.Attributes_Resistance_Physical = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Resistance.Physical"),
		FString("Resistance to Physical damage")
		);

	/*
	 * 减益效果类型标签
	 * 每种伤害类型对应一个减益效果，在 DamageTypesToDebuffs 映射中配对。
	 * 减益触发流程：
	 *   ExecCalc_Damage 随机判定 DebuffChance 通过
	 *   → 将 bIsSuccessfulDebuff = true 写入 GE 上下文
	 *   → PostGameplayEffectExecute 检查 bIsSuccessfulDebuff
	 *   → 通过 DamageType 查 DamageTypesToDebuffs 得到对应减益 Tag
	 *   → 查询 AbilitySystemLibrary 中注册的减益 GE 类
	 *   → 动态创建 GE Spec 并通过 SetByCaller 注入减益参数
	 *   → 应用到目标 ASC，产生持续减益效果
	 */

	GameplayTags.Debuff_Arcane = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Debuff.Arcane"),
		FString("Debuff for Arcane damage")
		);
	GameplayTags.Debuff_Burn = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Debuff.Burn"),
		FString("Debuff for Fire damage")
		);
	GameplayTags.Debuff_Physical = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Debuff.Physical"),
		FString("Debuff for Physical damage")
		);
	GameplayTags.Debuff_Stun = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Debuff.Stun"),
		FString("Debuff for Lightning damage")
		);

	// 减益参数 Tag（作为 SetByCaller 的键传入动态创建的减益 GE Spec）
	GameplayTags.Debuff_Chance = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Debuff.Chance"),
		FString("Debuff Chance")
		);
	GameplayTags.Debuff_Damage = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Debuff.Damage"),
		FString("Debuff Damage")
		);
	GameplayTags.Debuff_Duration = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Debuff.Duration"),
		FString("Debuff Duration")
		);
	GameplayTags.Debuff_Frequency = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Debuff.Frequency"),
		FString("Debuff Frequency")
		);

	/*
	 * 元属性
	 * IncomingXP 是一个"管道"属性：通过 GE SetByCaller 将 XP 数量传递进来，
	 * UAuraAttributeSet::PostGameplayEffectExecute 捕获该属性的变化，
	 * 将其累加到玩家总经验，再清零该属性。
	 * 使用元属性的好处：XP 增加走和伤害一样的 GE 流程，
	 * 统一了数据传递方式，且不会在角色身上留下持久的"XP 属性"值。
	 */

	GameplayTags.Attributes_Meta_IncomingXP = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Attributes.Meta.IncomingXP"),
		FString("Incoming XP Meta Attribute")
		);

	/*
	 * 伤害类型 → 抗性属性 映射表
	 *
	 * 此映射表是 ExecCalc_Damage 实现多元素抗性系统的核心数据结构。
	 * 计算伤害时，ExecCalc 遍历此映射（或直接用当前伤害类型 Key 查询），
	 * 找到对应的抗性属性 Tag，再通过 AttributeSet.TagsToAttributes 映射
	 * 得到 FGameplayAttribute，最后用 GAS 属性捕获机制获取目标实际属性值。
	 *
	 * 伤害减免公式（简化）：
	 *   FinalDamage = BaseDamage * (1 - Resistance / 100.f)
	 *
	 * 扩展性：新增伤害类型时只需在此处追加一行 Add，
	 * ExecCalc_Damage 的核心逻辑无需修改。
	 */
	GameplayTags.DamageTypesToResistances.Add(GameplayTags.Damage_Arcane, GameplayTags.Attributes_Resistance_Arcane);
	GameplayTags.DamageTypesToResistances.Add(GameplayTags.Damage_Lightning, GameplayTags.Attributes_Resistance_Lightning);
	GameplayTags.DamageTypesToResistances.Add(GameplayTags.Damage_Physical, GameplayTags.Attributes_Resistance_Physical);
	GameplayTags.DamageTypesToResistances.Add(GameplayTags.Damage_Fire, GameplayTags.Attributes_Resistance_Fire);

	/*
	 * 伤害类型 → 减益效果 映射表
	 *
	 * 此映射表决定"受到什么类型的伤害会触发什么减益"。
	 * PostGameplayEffectExecute 在确认 bIsSuccessfulDebuff 后，
	 * 以 GE 上下文中的 DamageType Tag 为 Key 查询此映射，
	 * 得到对应的减益 Tag（如 Debuff.Burn），
	 * 再在项目的减益 GE 注册表中找到对应的 UGameplayEffect 类，
	 * 动态创建 GE Spec 并将 DebuffDamage/Duration/Frequency 通过
	 * SetByCaller 注入，最终 ApplyGameplayEffectSpecToSelf() 完成减益施加。
	 *
	 * 当前映射关系：
	 *   Damage.Arcane    → Debuff.Arcane   （奥术腐蚀：持续奥术跳伤）
	 *   Damage.Lightning → Debuff.Stun     （眩晕：无法行动）
	 *   Damage.Physical  → Debuff.Physical （撕裂：持续物理跳伤）
	 *   Damage.Fire      → Debuff.Burn     （燃烧：持续火焰跳伤）
	 */
	GameplayTags.DamageTypesToDebuffs.Add(GameplayTags.Damage_Arcane, GameplayTags.Debuff_Arcane);
	GameplayTags.DamageTypesToDebuffs.Add(GameplayTags.Damage_Lightning, GameplayTags.Debuff_Stun);
	GameplayTags.DamageTypesToDebuffs.Add(GameplayTags.Damage_Physical, GameplayTags.Debuff_Physical);
	GameplayTags.DamageTypesToDebuffs.Add(GameplayTags.Damage_Fire, GameplayTags.Debuff_Burn);

	/*
	 * 效果标签
	 * Effects.HitReact：受击时由 AuraAbilitySystemComponent 授予目标的状态标签。
	 * 作用双重：
	 *   1. AI 行为树检测此 Tag 判断目标是否处于受击硬直，决定是否继续攻击；
	 *   2. AuraCharacterBase 中 bHitReacting 属性由此 Tag 的存在与否驱动，
	 *      控制移动组件是否允许角色移动（硬直期间禁止移动）。
	 */

	GameplayTags.Effects_HitReact = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Effects.HitReact"),
		FString("Tag granted when Hit Reacting")
		);

	/*
	 * 技能标签
	 * Abilities.None：技能"空槽"占位符，等同于空指针语义。
	 * SpellMenuWidgetController 用 Abilities.None 表示技能槽未装备，
	 * 避免直接比较 nullptr 导致的崩溃风险。
	 */

	GameplayTags.Abilities_None = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.None"),
		FString("No Ability - like the nullptr for Ability Tags")
		);

	// 基础战斗技能 Tag，敌人 AI 通过匹配这些 Tag 查找并激活对应技能
	GameplayTags.Abilities_Attack = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Attack"),
		FString("Attack Ability Tag")
		);

	GameplayTags.Abilities_Summon = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Summon"),
		FString("Summon Ability Tag")
		);

	/*
	 * 攻击型法术
	 * 每个法术有唯一 Tag，存储在 AbilitySpec.AbilityTags 中。
	 * SpellMenuWidgetController 通过这些 Tag 在 DA_AbilityInfo 数据资产中
	 * 查找技能的名称、描述、图标、解锁等级等信息，驱动 UI 显示。
	 * WaitCooldownChange 异步任务监听对应 Cooldown Tag 的 GE 应用/移除，
	 * 实时更新 UI 上的冷却倒计时。
	 */

	GameplayTags.Abilities_Fire_FireBolt = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Fire.FireBolt"),
		FString("FireBolt Ability Tag")
		);

	GameplayTags.Abilities_Fire_FireBlast = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Fire.FireBlast"),
		FString("FireBlast Ability Tag")
		);

	GameplayTags.Abilities_Lightning_Electrocute = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Lightning.Electrocute"),
		FString("Electrocute Ability Tag")
		);

	GameplayTags.Abilities_Arcane_ArcaneShards = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Arcane.ArcaneShards"),
		FString("Arcane Shards Ability Tag")
		);

	/*
	 * 被动法术
	 * 被动技能装备后通过 AuraAbilitySystemComponent 的被动技能激活机制
	 * 持续生效，不占用普通输入槽（InputTag.Passive.1 / InputTag.Passive.2 管理装备）。
	 * LifeSiphon/ManaSiphon 在每次造成伤害后回复生命/法力，
	 * HaloOfProtection 提供持续的护盾效果。
	 */

	GameplayTags.Abilities_Passive_LifeSiphon = UGameplayTagsManager::Get().AddNativeGameplayTag(
			FName("Abilities.Passive.LifeSiphon"),
			FString("Life Siphon")
			);
	GameplayTags.Abilities_Passive_ManaSiphon = UGameplayTagsManager::Get().AddNativeGameplayTag(
			FName("Abilities.Passive.ManaSiphon"),
			FString("Mana Siphon")
			);
	GameplayTags.Abilities_Passive_HaloOfProtection = UGameplayTagsManager::Get().AddNativeGameplayTag(
			FName("Abilities.Passive.HaloOfProtection"),
			FString("Halo Of Protection")
			);

	// 受击反应技能：被命中时由 AuraAbilitySystemComponent 自动触发，播放受击动画
	GameplayTags.Abilities_HitReact = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.HitReact"),
		FString("Hit React Ability")
		);

	/*
	 * 技能状态机标签
	 * 技能的四个生命周期状态，存储在 AbilitySpec.DynamicAbilityTags 中
	 * （DynamicAbilityTags 与 AbilityTags 不同，前者可在运行时动态增减）。
	 *
	 * 状态流转：
	 *   Locked → (玩家等级达标) → Eligible
	 *   Eligible → (消费技能点) → Unlocked
	 *   Unlocked ↔ (装备/卸装到槽位) ↔ Equipped
	 *
	 * SpellMenuWidgetController 监听 AbilityStatusChanged 委托，
	 * 收到新状态 Tag 后更新对应技能格子的 UI 外观。
	 */
	GameplayTags.Abilities_Status_Eligible = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Status.Eligible"),
		FString("Eligible Status")
		);

	GameplayTags.Abilities_Status_Equipped = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Status.Equipped"),
		FString("Equipped Status")
		);

	GameplayTags.Abilities_Status_Locked = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Status.Locked"),
		FString("Locked Status")
		);

	GameplayTags.Abilities_Status_Unlocked = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Status.Unlocked"),
		FString("Unlocked Status")
		);

	/*
	 * 技能类型标签
	 * 用于区分主动攻击技能与被动技能，SpellMenu 据此决定哪些格子显示在主动页/被动页。
	 * Abilities.Type.None 用于特殊技能（如 HitReact、Attack），
	 * 这些技能不在法术书 UI 中显示。
	 */
	GameplayTags.Abilities_Type_None = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Type.None"),
		FString("Type None")
		);

	GameplayTags.Abilities_Type_Offensive = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Type.Offensive"),
		FString("Type Offensive")
		);

	GameplayTags.Abilities_Type_Passive = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Abilities.Type.Passive"),
		FString("Type Passive")
		);

	/*
	* 冷却时间标签
	* 冷却 GE 授予此 Tag 给施法者，持续到冷却结束后 GE 自动移除。
	* UWaitCooldownChange 异步任务通过 OnAnyTagNewOrRemoved 委托监听
	* 此 Tag 的添加与移除事件，实时将剩余冷却时间推送给 UI。
	*/

	GameplayTags.Cooldown_Fire_FireBolt = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Cooldown.Fire.FireBolt"),
		FString("FireBolt Cooldown Tag")
		);

	/*
	 * 战斗插槽标签
	 * 对应骨骼网格体上的插槽名，标识技能特效和命中检测的发射点/接触点。
	 * ICombatInterface::GetCombatSocketLocation(FGameplayTag SocketTag)
	 * 根据此 Tag 通过 GetSocketLocation() 返回骨骼世界坐标，
	 * 技能类用该坐标生成投射物或检测近战碰撞球。
	 * Tag 化（而非直接用字符串 "weapon_r"）的好处：
	 * 骨骼插槽改名只需在一处修改，不影响技能层的代码。
	 */

	GameplayTags.CombatSocket_Weapon = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("CombatSocket.Weapon"),
		FString("Weapon")
		);

	GameplayTags.CombatSocket_RightHand = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("CombatSocket.RightHand"),
		FString("Right Hand")
		);

	GameplayTags.CombatSocket_LeftHand = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("CombatSocket.LeftHand"),
		FString("Left Hand")
		);

	GameplayTags.CombatSocket_Tail = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("CombatSocket.Tail"),
		FString("Tail")
		);

	/*
	 * 蒙太奇标签
	 * FTaggedMontage 结构体将蒙太奇资产与 Montage_Attack_N Tag 配对存储，
	 * ICombatInterface::GetAttackMontages() 返回此列表。
	 * 技能或 AI 随机选择其中一个蒙太奇播放，
	 * 通过 Tag 查找对应的 CombatSocket Tag，确定该次攻击的命中点。
	 * 这样一套角色可以配置多种攻击动画变体，且无需硬编码动画索引。
	 */

	GameplayTags.Montage_Attack_1 = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Montage.Attack.1"),
		FString("Attack 1")
		);

	GameplayTags.Montage_Attack_2 = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Montage.Attack.2"),
		FString("Attack 2")
		);

	GameplayTags.Montage_Attack_3 = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Montage.Attack.3"),
		FString("Attack 3")
		);

	GameplayTags.Montage_Attack_4 = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Montage.Attack.4"),
		FString("Attack 4")
		);

	/*
	 * 玩家阻塞标签
	 * 用于在特定状态下屏蔽玩家输入或鼠标光标射线检测。
	 * AuraPlayerController 每帧处理输入前检查 ASC 是否持有这些 Tag：
	 *   - Block_InputPressed/Held/Released：屏蔽所有技能输入（如死亡、过场动画期间）；
	 *   - Block_CursorTrace：屏蔽光标射线，防止 UI 操作时 3D 物体意外高亮。
	 * 使用 Tag 而非布尔变量的优势：多个系统可以同时独立地添加阻塞 Tag，
	 * 只有当所有阻塞方都移除自己的 Tag 后，输入才恢复，避免状态竞争。
	 */

	GameplayTags.Player_Block_CursorTrace = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Player.Block.CursorTrace"),
		FString("Block tracing under the cursor")
		);

	GameplayTags.Player_Block_InputHeld = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Player.Block.InputHeld"),
		FString("Block Input Held callback for input")
		);

	GameplayTags.Player_Block_InputPressed = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Player.Block.InputPressed"),
		FString("Block Input Pressed callback for input")
		);

	GameplayTags.Player_Block_InputReleased = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("Player.Block.InputReleased"),
		FString("Block Input Released callback for input")
		);

	/*
	 * 游戏玩法提示（GameplayCue）标签
	 * GameplayCue 标签的命名约定是以 "GameplayCue." 开头，
	 * GAS 系统据此自动识别并路由到对应的 UGameplayCueNotify 类。
	 *
	 * GameplayCue 的设计原则：
	 *   - 纯表现层：只触发粒子、音效、贴花，不修改任何 Attribute 或 Tag；
	 *   - 自动网络复制：GAS 框架会将 Cue 的触发/结束同步给所有客户端，
	 *     无需手动 Multicast RPC，大幅减少网络代码量；
	 *   - 与 GE 解耦：技能/GE 通过添加 GameplayCue Tag 触发效果，
	 *     美术/音效可以独立修改 Notify 资产而不影响逻辑代码。
	 */

	GameplayTags.GameplayCue_FireBlast = UGameplayTagsManager::Get().AddNativeGameplayTag(
		FName("GameplayCue.FireBlast"),
		FString("FireBlast GameplayCue Tag")
		);
}
