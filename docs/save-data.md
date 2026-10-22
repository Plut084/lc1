# 存档与继续游戏

角色、队伍、势力、装备和武器从 `runtime/saves/01/` 下的 YAML 读取。大陆地形、区域、地点和道路
仍由 `Continent` 生成，不属于存档数据。文件相对工作目录打开，和 `shaders/` 一致；
CMake 目标 `copy_saves` 只把缺失的种子文件复制进运行树，不覆盖已有文件，所以 `make run`
直接可用，也不会在重新构建时清除进度。脚本只复制六类初始数据，不复制进度文件。

按 F6 或调试面板里的 Save game 按钮保存，HUD 显示成功或失败；失败的详细原因写入日志。
启动时，目录里存在 `checkpoint.yaml` 就优先读取完整进度，否则读取六个初始 YAML。
`LC1_SAVE_ROOT` 可以覆盖默认的 `runtime/saves/01`。退出不会自动保存。

`Application` 拥有 `Save` 与 `GameSession`，会话内部的 `GameWorld` 在构造时消费 Save，
按依赖顺序装配势力、装备、武器、队伍和角色，不保留对 Save 的引用。
Save 只负责 YAML 记录、文件读写及引用校验；GameWorld 负责运行对象和快照。
两者都可以脱离 device、窗口和音频单独构造与测试。玩家和 AI 示例身份仍由 `session.yaml` 指明。

## 文件与记录

| 文件 | 记录类型 | 引用方 |
|---|---|---|
| `factions.yaml` | `SerializedFaction` | 队伍 |
| `equipments.yaml` | `SerializedEquipment` | 角色 |
| `weapons.yaml` | `SerializedWeapon` | 角色 |
| `parties.yaml` | `SerializedParty` | 角色 |
| `characters.yaml` | `SerializedCharacterConfig` | 队伍、`session.yaml` |
| `session.yaml` | `SerializedSession` | 角色 |

六个文件是初始数据。实际写回使用单个 `checkpoint.yaml`，包含版本号 `version: 2`、
`session`、`factions`、`equipments`、`weapons`、`parties`、`characters` 和 `states`，
将角色所属队伍和双方成员列表一起提交，避免多文件写到一半形成矛盾数据。
每类记录按编号排序输出。进度文件损坏时明确报错，不静默回退到初始世界。

世界数据文件是 YAML 序列，每条记录一项；`session.yaml` 是单个 YAML 映射。
字段必须写全：读取使用
`rfl::NoOptionals`，缺少任何字段都会解析失败，空列表要显式写成 `[]`。

`keyid` 是身份，名称只是显示。同类编号非零、在本文件内唯一；重排文件或改名不影响引用。
编号重复时 `Save` 在读取阶段就明确报错，不会静默保留其中一条。

### 势力 `factions.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 势力编号 |
| `name` | 势力名称 |

设计里的“独立势力”是分类而不是势力，因此每个具体势力各自一条记录、各自一个编号。

### 装备 `equipments.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 装备编号 |
| `type` | `Head`、`Body`、`Foot` |
| `armor` | 护甲值 |

### 武器 `weapons.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 武器编号 |
| `type` | `MeleeWeapon`、`RangedWeapon` |
| `attack` | 攻击力 |
| `range` | 射程，米 |

### 队伍 `parties.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 队伍编号 |
| `faction` | 所属势力编号 |
| `leader` | 队长角色编号；`0` 表示暂时没有队长 |
| `members` | 普通成员角色编号列表，不含队长 |
| `captives` | 俘虏角色编号列表 |

队伍保存的是具体角色的编号，不保存“士兵数量”；每个角色仍然只保存自己那一份数据。
队长单独放在 `leader` 中，不得同时出现在 `members` 或 `captives`。
两个列表各自无重复且互不重叠，列表内角色及队长的 `party` 必须指回本队。
每个角色必须且只能作为所属队伍的队长、普通成员或俘虏出现；不再允许未部署状态，也不会推断或自动补齐成员。
种子角色 3 明确列为队伍 1 的普通成员；已有自定义种子／旧进度若存在未列出的角色，需显式修正其队伍记录。
运行期 `Party` 的构造和链接接口也拒绝队长重叠、空指针和重复成员。

### 角色 `characters.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 角色编号，非零且唯一 |
| `name`、`sex`、`age` | 基础信息 |
| `party` | 所属队伍编号 |
| `level` | 等级 |
| `equipment` | 装备槽位，每项为 `[装备类型, 装备编号]` 对 |
| `weapon` | 武器槽位，每项为 `[武器类型, 武器编号]` 对 |

编号 `1` 和 `2` 不是特殊编号。谁是玩家由 `session.yaml` 决定，`GameWorld` 不再写死任何
编号：玩家可以是任意一条角色记录，也可以整支队伍换人。种子数据里 `runtime/saves/01/session.yaml`
把 1 号定为玩家、2 号定为 AI 示例。

### 会话 `session.yaml`

| 字段 | 含义 |
|---|---|
| `player` | 玩家控制的角色编号，非零且必须存在 |
| `example` | AI 示例角色编号；`0` 表示这份存档没有 AI 示例 |

这个文件只保存“这份存档由谁在玩”，不是世界数据：世界规则（势力、队伍、角色）仍各自成文。
身份之所以放在这里而不是当作角色记录的布尔标记，是因为它是存档槽的状态，而不是角色自身的
属性；放成标记就必须反复校验“有且仅有一个 true”。

这个文件是单个映射，而不是世界数据文件使用的序列：

```yaml
player: 1
example: 2
```

## 读取、校验与装配

`Save` 先把六个文件全部读成记录，再校验，最后才由 `GameWorld` 建对象：

1. `Save` 读六个文件，检查同类编号重复。
2. `Save::validate_references` 检查每个引用：会话的 `player`、`example`，队伍的 `faction`、
   `leader`、`members`、`captives`，以及角色的 `party`、`equipment`、`weapon`。`player`
   为 `0` 或指向不存在的角色、`example` 与玩家同一个人，都在这一步失败。悬空引用是数据错误，
   在碰任何运行期对象之前就失败。
3. `GameWorld::build_world` 按依赖顺序建对象：势力 → 装备/武器 → 队伍 → 角色。因为第 2
   步已经保证引用成立，这里的查表不会再失败。
4. `GameWorld::link_party_members` 最后链接队长、成员和俘虏。
5. `GameWorld` 用 `save.player()` 从建好的角色表里取出玩家，用 `save.example()` 取出 AI
   示例（`0` 时跳过）。随后 `GameSession` 构造 `PlayerView`；视角需要玩家的起始位置，
   所以它不能在世界建好之前初始化。

角色和队伍互相引用，所以队伍先建、成员后链；`Party` 因此允许暂时没有队长。

除了编号和引用，读取还检查装备／武器类型与所在槽位一致、队伍列表无重复、队长与普通成员
及俘虏互斥、角色与队伍关系双向一致。

## 运行进度与写入

`GameWorld::snapshot()` 从当前运行对象生成 `SerializedWorld`，不读取启动时的旧 `Save`。
`Application` 响应保存请求，将快照交给 `Save::write`，并把结果返回给界面。
`Save` 与会话仍然平级；会话不借用存档，也不持有存档路径。

每个角色必须有且只有一条 `states` 记录：

| 字段 | 含义 |
|---|---|
| `keyid` | 角色编号 |
| `x`、`y`、`z` | 脚底世界坐标，米 |
| `body_yaw` | 身体朝向，度 |
| `look_yaw`、`look_pitch` | 相对身体的观察角，度 |
| `patrol` | 是否继续示例 NPC 巡逻；只有 `session.example` 可启用 |
| `companion_order` | `None` 保持原型控制行为，`Follow` 跟随队长，`Wait` 原地等待 |

读取检查坐标与角度有限、观察角在角色允许范围内；运行期构建角色时再检查大陆边界和碰撞。
招募会停止示例巡逻并设为 `Follow`；跟随／等待与巡逻分别保存，重启后恢复同一命令。
命令只允许用于有队长的队伍中的普通成员，不能与巡逻同时启用；队长、俘虏及无队长队伍不可携带命令。
玩家角色仍由玩家控制，其保存的队员命令不驱动自动移动。

读取 `version: 1` 时按旧的必填字段解析，保留姿态和巡逻，并将所有命令迁移为 `None`；
下次写入为 version 2。version 2 的 `companion_order` 必填，其他版本明确拒绝。
成员关系校验适用于两版进度，不自动修正不一致的历史数据。
当前不保存跳跃速度、巡逻当前路点、对话和相机模式／缩放；重启默认使用斜视角，空中角色以
零初始速度继续下落，巡逻从初始路点重新开始。

写入先校验完整快照，再写 `checkpoint.yaml.tmp`，检查关闭文件成功，最后重命名替换
`checkpoint.yaml`。校验和临时文件写入失败不会改动旧进度；启动时忽略遗留的临时文件。
这里没有实现文件系统同步或断电持久性保证，也不支持多个进程同时写同一存档目录。

## reflect-cpp 约束

`std::map` 只有在键是字符串或数值时才编码成 YAML 映射（对象形式）。键不满足这个条件时
（枚举键就是这一类），reflect-cpp 改用 pair 序列，因为 YAML 映射的键必须是字符串。
角色的 `equipment`、`weapon` 因此写成 `- [Head, 1]`：

```yaml
  equipment:
    - [Head, 1]
    - [Body, 2]
  weapon:
    - [MeleeWeapon, 1]
```

`Head: 1` 这种映射写法会被拒绝（`Could not cast to sequence`）。枚举本身按名字写，
例如 `[Head, 1]` 里的 `Head`。map 形式也保证每种槽位至多出现一次。

## 尚未实现

- 游戏内存档槽位选择、自动保存和运行中读档；重启会读取最近一次手动保存的进度。
- 除 version 1 → 2 之外的世界版本迁移、角色存活状态，以及完整控制器／物理状态的恢复。

## 测试数据

`tests/fixtures/saves/01/` 保存固定的测试世界，不依赖可编辑的运行存档。
`save_data` 校验这些夹具、语义错误、进度往返、连续替换、写入失败保留旧进度及损坏进度诊断；
`runtime_save_data` 另行检查构建树中的 `runtime/saves/01/` 能否完整加载。
`character` 检查运行对象招募后的序列化结果，`seed_save_preservation` 检查重复安装不覆盖
玩家数据。以上均为 CPU 测试，不能替代窗口内保存按钮和重启画面的实机验证。
