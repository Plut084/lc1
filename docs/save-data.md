# 存档数据读取

角色、队伍、势力、装备和武器从 `runtime/saves/01/` 下的 YAML 读取。大陆地形、区域、地点和道路
仍由 `Continent` 生成，不属于存档数据。文件相对工作目录打开，和 `shaders/` 一致；
CMake 目标 `copy_saves` 把仓库里的种子数据复制进运行树，所以 `make run` 直接可用。

`Save` 与 `GameSession` 是平级的两块，都由 `Application` 拥有（`Save save_` 在
`game_` 之前）：`Save` 只负责存档数据——记录类型、YAML 读写、编号与引用校验，不持有任何
运行期对象；`GameSession` 在构造时用 `Save` 装配出自己的世界（`Faction`、`Equipment`、
`Weapon`、`Party`、`Character`），之后不再引用它。因此 `Save` 可以脱离 device、大陆和
窗口单独加载与测试。玩家和 AI 示例也是存档记录，身份由 `session.yaml` 指明，`GameSession`
不把任何编号写死。

## 文件与记录

| 文件 | 记录类型 | 引用方 |
|---|---|---|
| `factions.yaml` | `SerializedFaction` | 队伍 |
| `equipments.yaml` | `SerializedEquipment` | 角色 |
| `weapons.yaml` | `SerializedWeapon` | 角色 |
| `parties.yaml` | `SerializedParty` | 角色 |
| `characters.yaml` | `SerializedCharacterConfig` | 队伍、`session.yaml` |
| `session.yaml` | `SerializedSession` | 角色 |

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
| `members` | 成员角色编号列表 |
| `captives` | 俘虏角色编号列表 |

队伍保存的是具体角色的编号，不保存“士兵数量”；每个角色仍然只保存自己那一份数据。

### 角色 `characters.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 角色编号，非零且唯一 |
| `name`、`sex`、`age` | 基础信息 |
| `party` | 所属队伍编号 |
| `level` | 等级 |
| `equipment` | 装备槽位，每项为 `[装备类型, 装备编号]` 对 |
| `weapon` | 武器槽位，每项为 `[武器类型, 武器编号]` 对 |

编号 `1` 和 `2` 不是特殊编号。谁是玩家由 `session.yaml` 决定，`GameSession` 不再写死任何
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

`Save` 先把六个文件全部读成记录，再校验，最后才由 `GameSession` 建对象：

1. `Save` 读六个文件，检查同类编号重复。
2. `Save::validate_references` 检查每个引用：会话的 `player`、`example`，队伍的 `faction`、
   `leader`、`members`、`captives`，以及角色的 `party`、`equipment`、`weapon`。`player`
   为 `0` 或指向不存在的角色、`example` 与玩家同一个人，都在这一步失败。悬空引用是数据错误，
   在碰任何运行期对象之前就失败。
3. `GameSession::build_world` 按依赖顺序建对象：势力 → 装备/武器 → 队伍 → 角色。因为第 2
   步已经保证引用成立，这里的查表不会再失败。
4. `GameSession::link_party_members` 最后链接队长、成员和俘虏。
5. `GameSession` 用 `save.player()` 从建好的角色表里取出玩家，用 `save.example()` 取出 AI
   示例（`0` 时跳过），然后构造 `PlayerView`——视角需要玩家的起始位置，所以它不能在世界
   建好之前初始化。

角色和队伍互相引用，所以队伍先建、成员后链；`Party` 因此允许暂时没有队长。

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

- 只读：还没有把运行中的世界写回 YAML。写回时由 `GameSession` 生成记录、`Save` 负责写出，
  方向与现在相反。
- 存档目录目前固定在 `ApplicationConfig::save_root`（默认 `runtime/saves/01`），没有存档槽位选择；
  玩家身份已经由 `session.yaml` 提供，切换槽位只需改这个路径。
- 角色记录不含位置、朝向、控制器和存活状态。
- `copy_saves` 每次构建都会覆盖运行树里的存档；等游戏开始写存档后，必须改成不覆盖
  已经存在的文件。

## 测试数据

`tests/fixtures/saves/01/` 保存固定的测试世界，不依赖可编辑的运行存档。
`save_data` 校验这些夹具和错误情况；`runtime_save_data` 另行检查构建树中的
`runtime/saves/01/` 能否完整加载。
