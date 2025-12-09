# 应用层协议过滤系统设计文档

## 文档版本
- **版本**: 1.0
- **日期**: 2025-12-03
- **状态**: 设计完成，待开发

---

## 目录
1. [项目概述](#1-项目概述)
2. [核心设计思路](#2-核心设计思路)
3. [协议定义语言（PDEF）](#3-协议定义语言pdef)
4. [技术实现方案](#4-技术实现方案)
5. [数据结构设计](#5-数据结构设计)
6. [执行流程](#6-执行流程)
7. [性能优化策略](#7-性能优化策略)
8. [开发指南](#8-开发指南)
9. [测试方案](#9-测试方案)
10. [附录](#10-附录)

---

## 1. 项目概述

### 1.1 项目目标
开发一个高性能的应用层协议过滤系统，用于在网络抓包场景中根据自定义协议规则过滤特定报文。

### 1.2 核心特性
- **自定义协议描述语言**：通过 `.pdef` 文件定义协议结构和过滤规则
- **高性能过滤**：目标吞吐量 10Mpps+
- **灵活的匹配规则**：支持嵌套结构、多种数据类型、部分匹配
- **预编译执行**：配置加载时编译为字节码，运行时高效执行
- **零拷贝设计**：直接在原始报文上进行字段访问

### 1.3 应用场景
- 游戏协议抓包（特定玩家ID、房间ID）
- RPC协议分析（特定服务、接口）
- 自定义二进制协议调试
- 网络流量分析与故障诊断

### 1.4 性能指标
- 单条规则检查时间：50-100ns
- 支持吞吐量：10Mpps+（在现代CPU上）
- 内存占用：最小化，避免动态分配

---

## 2. 核心设计思路

### 2.1 四大核心原则

1. **简洁语法**
   - 不使用 `struct` 关键字
   - 类型声明简洁直观
   - 支持常量和嵌套定义

2. **嵌套支持**
   - 结构体可以嵌套其他结构体
   - 编译时扁平化为绝对偏移量
   - 避免运行时递归解析

3. **过滤条件**
   - 通过赋值语法指定过滤条件
   - 支持等值、范围、掩码匹配
   - 条件编译为高效字节码

4. **部分匹配**
   - 报文长度 >= 结构体定义大小即可
   - 支持变长字段（仅限末尾）
   - 边界检查保证安全性

### 2.2 设计决策

| 决策项 | 方案选择 | 原因 |
|--------|---------|------|
| 变长字段位置 | 仅允许在末尾（方案A） | 性能优先，避免动态计算偏移量 |
| 执行方式 | 预编译字节码 | 配置加载时编译，运行时零解析开销 |
| 嵌套实现 | 编译期扁平化 | 避免运行时递归，直接使用绝对偏移量 |
| 继承语法 | 不支持 | 嵌套已足够，避免增加复杂度 |
| 动态偏移 | 不支持（方案B） | 性能损耗太大，不适合高性能场景 |

---

## 3. 协议定义语言（PDEF）

### 3.1 文件格式
- **文件扩展名**: `.pdef`
- **编码格式**: UTF-8
- **注释语法**: `//` 单行注释

### 3.2 完整语法规范

#### 3.2.1 协议元信息

```pdef
@protocol {
    name = "MyGame";           // 协议名称
    ports = 7777, 7778;        // 监听端口列表
    endian = big;              // 字节序: big | little
}
```

**字段说明**：
- `name`: 协议标识符（必填）
- `ports`: TCP/UDP端口列表（可选，多个端口用逗号分隔）
- `endian`: 默认字节序（必填）

#### 3.2.2 常量定义

```pdef
@const {
    MAGIC = 0x12345678;
    VERSION_1 = 1;
    VERSION_2 = 2;
    MAX_PLAYERS = 100;
}
```

**用途**：
- 减少魔数
- 提高可读性
- 便于维护

#### 3.2.3 基础类型

| 类型 | 大小 | 说明 |
|------|------|------|
| `uint8` | 1字节 | 无符号8位整数 |
| `uint16` | 2字节 | 无符号16位整数 |
| `uint32` | 4字节 | 无符号32位整数 |
| `uint64` | 8字节 | 无符号64位整数 |
| `int8` | 1字节 | 有符号8位整数 |
| `int16` | 2字节 | 有符号16位整数 |
| `int32` | 4字节 | 有符号32位整数 |
| `int64` | 8字节 | 有符号64位整数 |
| `bytes[N]` | N字节 | 固定长度字节数组 |
| `string[N]` | N字节 | 固定长度字符串 |
| `varbytes` | 变长 | 变长字节数组（仅限末尾） |

#### 3.2.4 结构体定义

```pdef
Header {
    uint32  magic;
    uint8   version;
    uint8   packet_type;
    uint16  flags;
    uint32  player_id;
    uint32  sequence;
}
```

**语法规则**：
- 结构体名首字母大写
- 字段名小写下划线风格
- 不需要 `struct` 关键字

#### 3.2.5 嵌套结构

```pdef
Player {
    uint32  player_id;
    uint16  level;
    uint8   status;
}

GamePacket {
    Header  header;      // 嵌套Header结构
    Player  player;      // 嵌套Player结构
    uint32  room_id;
}
```

**编译结果**（扁平化）：
```
GamePacket:
  offset 0:  header.magic       (uint32)
  offset 4:  header.version     (uint8)
  offset 5:  header.packet_type (uint8)
  offset 6:  header.flags       (uint16)
  offset 8:  header.player_id   (uint32)
  offset 12: header.sequence    (uint32)
  offset 16: player.player_id   (uint32)
  offset 20: player.level       (uint16)
  offset 22: player.status      (uint8)
  offset 23: room_id            (uint32)
```

#### 3.2.6 过滤规则定义

```pdef
@filter LoginPacket {
    header.magic = MAGIC;
    header.version = VERSION_1;
    header.packet_type = 0x01;
}

@filter SpecificPlayer {
    header.magic = MAGIC;
    player.player_id = 123456;
}

@filter RoomRange {
    room_id >= 1000;
    room_id <= 2000;
}

@filter FlagMask {
    header.flags & 0x0F00 = 0x0100;  // 掩码匹配
}
```

**支持的运算符**：
- `=`: 等值匹配
- `>`, `>=`, `<`, `<=`: 范围匹配
- `&`: 位掩码匹配（格式: `field & mask = value`）

#### 3.2.7 变长字段限制

```pdef
//  正确：变长字段在末尾
Message {
    uint32    msg_id;
    uint16    msg_len;
    varbytes  payload;   // 仅限末尾
}

//  错误：变长字段在中间
InvalidMessage {
    uint32    msg_id;
    varbytes  payload;   // 不允许
    uint32    checksum;  // 无法确定offset
}
```

#### 3.2.8 字节序控制

```pdef
// 全局字节序
@protocol {
    endian = big;
}

// 字段级字节序覆盖（未来扩展）
MixedEndian {
    uint32 @big    field1;    // 大端
    uint32 @little field2;    // 小端
    uint32         field3;    // 使用全局设置
}
```

### 3.3 完整示例

```pdef
// ============================================
// 文件: my_game.pdef
// ============================================

@protocol {
    name = "MyGame";
    ports = 7777, 7778;
    endian = big;
}

@const {
    MAGIC = 0x12345678;
    TYPE_LOGIN = 0x01;
    TYPE_LOGOUT = 0x02;
    TYPE_MOVE = 0x03;
}

Header {
    uint32  magic;
    uint8   version;
    uint8   packet_type;
    uint16  flags;
    uint32  player_id;
    uint32  sequence;
}

Player {
    uint32  player_id;
    uint16  level;
    uint8   status;
    bytes[16] nickname;
}

GamePacket {
    Header  header;
    Player  player;
    uint32  room_id;
    uint16  payload_len;
    varbytes payload;
}

// 过滤规则
@filter LoginPacket {
    header.magic = MAGIC;
    header.packet_type = TYPE_LOGIN;
}

@filter SpecificPlayer {
    header.magic = MAGIC;
    header.player_id = 123456;
}

@filter HighLevelPlayers {
    header.magic = MAGIC;
    player.level >= 50;
}
```

---

## 4. 技术实现方案

### 4.1 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                    PDEF文件 (my_game.pdef)               │
└─────────────────────┬───────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────┐
│                  PDEF解析器 (Parser)                     │
│  - 词法分析                                              │
│  - 语法分析                                              │
│  - 结构体扁平化                                          │
│  - 常量替换                                              │
└─────────────────────┬───────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────┐
│                 字节码编译器 (Compiler)                  │
│  - 条件编译为字节码指令                                  │
│  - 偏移量计算                                            │
│  - 字节序处理                                            │
└─────────────────────┬───────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────┐
│              字节码执行引擎 (VM/Executor)                │
│  - 边界检查                                              │
│  - 字段提取                                              │
│  - 条件评估                                              │
│  - 匹配结果返回                                          │
└─────────────────────┬───────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────┐
│                  数据包捕获 (Capture)                    │
│  - 接收网络报文                                          │
│  - 调用过滤引擎                                          │
│  - 匹配成功 → 写入PCAP                                   │
└─────────────────────────────────────────────────────────┘
```

### 4.2 编译流程

```
PDEF文件
  ↓
[词法分析] → Tokens
  ↓
[语法分析] → AST
  ↓
[语义分析] → 类型检查、常量替换
  ↓
[结构扁平化] → 计算绝对偏移量
  ↓
[字节码生成] → 过滤规则 → 字节码指令序列
  ↓
[优化] → 指令合并、常量折叠
  ↓
字节码 (加载到内存)
```

### 4.3 运行时流程

```
网络报文到达
  ↓
1. 提取传输层信息（端口、协议）
  ↓
2. 查找匹配的协议定义（基于端口）
  ↓
3. 遍历所有过滤规则
  ↓
4. 对每条规则：
   a. 检查报文最小长度
   b. 执行字节码指令序列
   c. 直接访问offset，提取字段值
   d. 应用字节序转换
   e. 比较判断
   f. 任一条件失败 → 跳转到下一规则
  ↓
5. 任一规则匹配 → 捕获报文
  ↓
6. 写入PCAP文件
```

---

## 5. 数据结构设计

### 5.1 核心数据结构

#### 5.1.1 字段定义

```c
typedef enum {
    TYPE_UINT8,
    TYPE_UINT16,
    TYPE_UINT32,
    TYPE_UINT64,
    TYPE_INT8,
    TYPE_INT16,
    TYPE_INT32,
    TYPE_INT64,
    TYPE_BYTES,
    TYPE_STRING,
    TYPE_VARBYTES,
} FieldType;

typedef enum {
    ENDIAN_BIG,
    ENDIAN_LITTLE,
} Endian;

typedef struct {
    char        name[64];       // 字段名（含嵌套路径，如 "header.magic"）
    FieldType   type;           // 字段类型
    uint32_t    offset;         // 绝对偏移量（字节）
    uint32_t    size;           // 字段大小（字节）
    Endian      endian;         // 字节序
    bool        is_variable;    // 是否变长字段
} Field;
```

#### 5.1.2 结构体定义

```c
typedef struct {
    char        name[64];       // 结构体名称
    Field*      fields;         // 字段数组（已扁平化）
    uint32_t    field_count;    // 字段数量
    uint32_t    min_size;       // 最小报文长度（不含变长部分）
    bool        has_variable;   // 是否包含变长字段
} StructDef;
```

#### 5.1.3 字节码指令

```c
typedef enum {
    OP_LOAD_U8,         // 加载 uint8
    OP_LOAD_U16_BE,     // 加载 uint16 (大端)
    OP_LOAD_U16_LE,     // 加载 uint16 (小端)
    OP_LOAD_U32_BE,     // 加载 uint32 (大端)
    OP_LOAD_U32_LE,     // 加载 uint32 (小端)
    OP_LOAD_U64_BE,     // 加载 uint64 (大端)
    OP_LOAD_U64_LE,     // 加载 uint64 (小端)

    OP_CMP_EQ,          // 比较相等
    OP_CMP_GT,          // 大于
    OP_CMP_GE,          // 大于等于
    OP_CMP_LT,          // 小于
    OP_CMP_LE,          // 小于等于
    OP_CMP_MASK,        // 掩码匹配 (value & mask == expected)

    OP_JUMP_IF_FALSE,   // 条件跳转
    OP_RETURN_TRUE,     // 返回匹配成功
    OP_RETURN_FALSE,    // 返回匹配失败
} OpCode;

typedef struct {
    OpCode      opcode;         // 操作码
    uint32_t    offset;         // 数据偏移量（用于LOAD指令）
    uint64_t    operand;        // 操作数（比较值、掩码等）
    uint32_t    jump_target;    // 跳转目标（指令索引）
} Instruction;
```

#### 5.1.4 过滤规则

```c
typedef struct {
    char            name[64];           // 规则名称
    char            struct_name[64];    // 关联的结构体名称
    Instruction*    bytecode;           // 字节码指令序列
    uint32_t        bytecode_len;       // 指令数量
    uint32_t        min_packet_size;    // 最小报文长度要求
} FilterRule;
```

#### 5.1.5 协议定义

```c
typedef struct {
    char            name[64];           // 协议名称
    uint16_t*       ports;              // 端口列表
    uint32_t        port_count;         // 端口数量
    Endian          default_endian;     // 默认字节序

    StructDef*      structs;            // 结构体数组
    uint32_t        struct_count;       // 结构体数量

    FilterRule*     filters;            // 过滤规则数组
    uint32_t        filter_count;       // 规则数量

    // 常量表（可选）
    HashMap*        constants;          // name -> value
} ProtocolDef;
```

### 5.2 字节码示例

**PDEF规则**：
```pdef
@filter SpecificPlayer {
    header.magic = 0x12345678;
    player.player_id = 123456;
}
```

**编译后的字节码**：
```c
Instruction bytecode[] = {
    // 检查 header.magic == 0x12345678
    { OP_LOAD_U32_BE,     0,  0,              0 },  // 从offset=0加载uint32(大端)
    { OP_CMP_EQ,          0,  0x12345678,     0 },  // 比较是否等于0x12345678
    { OP_JUMP_IF_FALSE,   0,  0,              7 },  // 不匹配则跳到指令7(返回false)

    // 检查 player.player_id == 123456
    { OP_LOAD_U32_BE,     16, 0,              0 },  // 从offset=16加载uint32(大端)
    { OP_CMP_EQ,          0,  123456,         0 },  // 比较是否等于123456
    { OP_JUMP_IF_FALSE,   0,  0,              7 },  // 不匹配则跳到指令7(返回false)

    // 所有条件都满足
    { OP_RETURN_TRUE,     0,  0,              0 },  // 返回匹配成功

    // 跳转目标
    { OP_RETURN_FALSE,    0,  0,              0 },  // 返回匹配失败
};
```

---

## 6. 执行流程

### 6.1 初始化阶段

```c
// 伪代码
function init_protocol_filter(pdef_file):
    1. parser = new PDEFParser()
    2. ast = parser.parse(pdef_file)

    3. analyzer = new SemanticAnalyzer()
    4. analyzer.check_types(ast)
    5. analyzer.replace_constants(ast)

    6. flattener = new StructFlattener()
    7. flat_structs = flattener.flatten(ast.structs)

    8. compiler = new BytecodeCompiler()
    9. bytecode = compiler.compile(ast.filters, flat_structs)

    10. protocol = new ProtocolDef(ast.meta, flat_structs, bytecode)
    11. return protocol
```

### 6.2 报文过滤阶段

```c
// 伪代码
function filter_packet(packet, packet_len, protocol):
    // 1. 检查端口（如果有）
    if protocol.ports and packet.port not in protocol.ports:
        return false

    // 2. 遍历所有过滤规则
    for rule in protocol.filters:
        // 3. 边界检查
        if packet_len < rule.min_packet_size:
            continue

        // 4. 执行字节码
        if execute_bytecode(packet, packet_len, rule.bytecode):
            return true  // 匹配成功

    return false  // 所有规则都不匹配
```

### 6.3 字节码执行引擎

```c
// 伪代码
function execute_bytecode(packet, packet_len, bytecode):
    ip = 0  // 指令指针
    value = 0  // 累加器

    while ip < bytecode.length:
        ins = bytecode[ip]

        switch ins.opcode:
            case OP_LOAD_U8:
                if ins.offset + 1 > packet_len:
                    return false  // 越界
                value = packet[ins.offset]

            case OP_LOAD_U32_BE:
                if ins.offset + 4 > packet_len:
                    return false  // 越界
                value = read_u32_be(packet, ins.offset)

            case OP_CMP_EQ:
                if value != ins.operand:
                    ip = ins.jump_target - 1  // 跳转

            case OP_CMP_MASK:
                if (value & ins.operand) != ins.operand2:
                    ip = ins.jump_target - 1  // 跳转

            case OP_RETURN_TRUE:
                return true

            case OP_RETURN_FALSE:
                return false

        ip++

    return false
```

### 6.4 字节序处理

```c
// 内联函数，编译器会优化为单条指令
static inline uint16_t read_u16_be(const uint8_t* data, uint32_t offset) {
    return ((uint16_t)data[offset] << 8) |
           ((uint16_t)data[offset + 1]);
}

static inline uint16_t read_u16_le(const uint8_t* data, uint32_t offset) {
    return ((uint16_t)data[offset]) |
           ((uint16_t)data[offset + 1] << 8);
}

static inline uint32_t read_u32_be(const uint8_t* data, uint32_t offset) {
    return ((uint32_t)data[offset] << 24) |
           ((uint32_t)data[offset + 1] << 16) |
           ((uint32_t)data[offset + 2] << 8) |
           ((uint32_t)data[offset + 3]);
}

static inline uint32_t read_u32_le(const uint8_t* data, uint32_t offset) {
    return ((uint32_t)data[offset]) |
           ((uint32_t)data[offset + 1] << 8) |
           ((uint32_t)data[offset + 2] << 16) |
           ((uint32_t)data[offset + 3] << 24);
}
```

---

## 7. 性能优化策略

### 7.1 编译期优化

1. **结构体扁平化**
   - 嵌套结构在编译时展开为绝对偏移量
   - 避免运行时递归查找

2. **常量替换**
   - 编译时替换所有常量引用
   - 避免运行时查表

3. **指令优化**
   - 常量折叠
   - 死代码消除
   - 指令合并（如连续的LOAD+CMP）

4. **跳转优化**
   - 短路评估（任一条件失败立即返回）
   - 减少不必要的指令执行

### 7.2 运行时优化

1. **零拷贝**
   - 直接在原始报文上进行字段访问
   - 避免内存分配和拷贝

2. **内联函数**
   - 字节序转换函数使用 `inline`
   - 编译器优化为单条CPU指令（如 `bswap`）

3. **边界检查最小化**
   - 预先检查报文最小长度
   - 字段访问时仅在必要时检查

4. **分支预测友好**
   - 热路径代码保持顺序执行
   - 减少不可预测的分支

5. **缓存友好**
   - 字节码指令紧凑存储
   - 顺序访问，提高缓存命中率

### 7.3 数据结构优化

1. **字节码紧凑性**
   ```c
   // 紧凑版本（16字节）
   struct Instruction {
       uint8_t  opcode;       // 1字节
       uint8_t  reserved[3];  // 3字节（对齐）
       uint32_t offset;       // 4字节
       uint64_t operand;      // 8字节
   } __attribute__((packed));
   ```

2. **规则索引**
   - 使用哈希表按端口快速查找协议
   - 减少无效规则的遍历

### 7.4 性能测试基准

| 测试场景 | 预期性能 | 备注 |
|---------|---------|------|
| 单条规则单字段匹配 | 50ns | 最简单场景 |
| 单条规则多字段匹配 | 100ns | 3-5个字段 |
| 多条规则遍历 | 200ns | 假设5条规则 |
| 嵌套结构深度3 | 100ns | 与扁平化后相同 |
| 变长字段（不访问） | 100ns | 仅访问固定部分 |

**吞吐量计算**：
- 假设平均每报文100ns
- 吞吐量 = 1 / 100ns = 10M pps (packets per second)

---

## 8. 开发指南

### 8.1 项目结构

```
rxtracenetcap/
├── src/
│   ├── pdef/
│   │   ├── parser.c          # PDEF解析器
│   │   ├── parser.h
│   │   ├── lexer.c           # 词法分析
│   │   ├── lexer.h
│   │   ├── ast.c             # 抽象语法树
│   │   ├── ast.h
│   │   ├── semantic.c        # 语义分析
│   │   ├── semantic.h
│   │   └── flattener.c       # 结构体扁平化
│   │
│   ├── compiler/
│   │   ├── bytecode.c        # 字节码编译器
│   │   ├── bytecode.h
│   │   └── optimizer.c       # 字节码优化器
│   │
│   ├── runtime/
│   │   ├── executor.c        # 字节码执行引擎
│   │   ├── executor.h
│   │   ├── protocol.c        # 协议管理
│   │   └── protocol.h
│   │
│   ├── capture/
│   │   ├── packet_filter.c   # 报文过滤主逻辑
│   │   └── packet_filter.h
│   │
│   └── utils/
│       ├── endian.h          # 字节序处理
│       ├── hashmap.c         # 哈希表实现
│       └── hashmap.h
│
├── tests/
│   ├── test_parser.c
│   ├── test_compiler.c
│   ├── test_executor.c
│   └── samples/
│       ├── simple.pdef
│       ├── game.pdef
│       └── rpc.pdef
│
├── docs/
│   └── PROTOCOL_FILTER_DESIGN.md  # 本文档
│
└── CMakeLists.txt
```

### 8.2 开发步骤

#### 阶段1：基础设施（Week 1-2）

1. **数据结构定义**
   - [ ] 定义 `Field`, `StructDef`, `Instruction`, `FilterRule`, `ProtocolDef`
   - [ ] 实现哈希表工具（用于常量表、结构体查找）
   - [ ] 实现动态数组（用于存储字段、指令）

2. **字节序工具**
   - [ ] 实现 `read_u16_be/le`, `read_u32_be/le`, `read_u64_be/le`
   - [ ] 编写单元测试验证正确性

#### 阶段2：PDEF解析器（Week 3-4）

1. **词法分析器**
   - [ ] 识别关键字（`@protocol`, `@const`, `@filter`）
   - [ ] 识别类型名（`uint8`, `uint16`, etc.）
   - [ ] 识别标识符、数字、运算符、注释

2. **语法分析器**
   - [ ] 解析 `@protocol` 块
   - [ ] 解析 `@const` 块
   - [ ] 解析结构体定义
   - [ ] 解析 `@filter` 块
   - [ ] 构建AST

3. **语义分析器**
   - [ ] 类型检查（字段类型有效性）
   - [ ] 常量引用检查
   - [ ] 结构体引用检查
   - [ ] 变长字段位置检查

4. **结构体扁平化**
   - [ ] 递归展开嵌套结构
   - [ ] 计算绝对偏移量
   - [ ] 计算最小结构体大小

#### 阶段3：字节码编译器（Week 5-6）

1. **基础编译**
   - [ ] 条件表达式 → 字节码指令
   - [ ] 字段访问 → LOAD指令
   - [ ] 比较运算符 → CMP指令
   - [ ] 生成跳转指令

2. **优化器**
   - [ ] 常量折叠
   - [ ] 死代码消除
   - [ ] 指令合并

#### 阶段4：执行引擎（Week 7-8）

1. **字节码解释器**
   - [ ] 实现所有操作码
   - [ ] 边界检查
   - [ ] 字节序处理
   - [ ] 返回匹配结果

2. **协议管理器**
   - [ ] 加载PDEF文件
   - [ ] 按端口索引协议
   - [ ] 管理多个协议定义

#### 阶段5：集成与测试（Week 9-10）

1. **集成到抓包系统**
   - [ ] 在报文接收路径插入过滤调用
   - [ ] 匹配成功 → 写入PCAP
   - [ ] 匹配失败 → 丢弃报文

2. **性能测试**
   - [ ] 单元测试（各模块）
   - [ ] 集成测试（完整流程）
   - [ ] 性能基准测试
   - [ ] 压力测试（10Mpps+）

### 8.3 关键API设计

```c
// ========== PDEF解析器 ==========
ProtocolDef* pdef_parse_file(const char* filename);
void pdef_free(ProtocolDef* proto);

// ========== 字节码编译器 ==========
FilterRule* compile_filter(
    const ASTFilter* ast_filter,
    const StructDef* struct_def
);

// ========== 执行引擎 ==========
bool execute_filter(
    const uint8_t* packet,
    uint32_t packet_len,
    const FilterRule* rule
);

// ========== 报文过滤主接口 ==========
bool packet_filter_match(
    const uint8_t* packet,
    uint32_t packet_len,
    uint16_t port,
    const ProtocolDef* proto
);
```

### 8.4 编码规范

1. **命名约定**
   - 结构体：`PascalCase`（如 `ProtocolDef`）
   - 函数：`snake_case`（如 `execute_filter`）
   - 常量：`UPPER_CASE`（如 `OP_LOAD_U32`）

2. **错误处理**
   - 编译期错误：打印详细错误信息+行号
   - 运行时错误：返回false，避免崩溃

3. **内存管理**
   - 所有 `malloc` 都需要对应的 `free`
   - 提供统一的释放函数（如 `pdef_free`）

4. **性能关键路径**
   - 使用 `inline` 标记
   - 避免函数调用开销
   - 使用 `likely/unlikely` 宏（GCC）

---

## 9. 测试方案

### 9.1 单元测试

| 模块 | 测试项 | 预期结果 |
|------|--------|---------|
| Lexer | 关键字识别 | 正确识别所有关键字 |
| Lexer | 数字解析 | 正确解析十进制、十六进制 |
| Parser | 简单结构体 | 正确构建AST |
| Parser | 嵌套结构体 | 正确表示嵌套关系 |
| Flattener | 偏移量计算 | 正确计算所有字段偏移量 |
| Compiler | 等值条件 | 生成正确的LOAD+CMP指令 |
| Compiler | 范围条件 | 生成正确的比较指令序列 |
| Executor | 匹配成功 | 返回true |
| Executor | 匹配失败 | 返回false |
| Executor | 边界检查 | 越界访问返回false |

### 9.2 集成测试

**测试用例1：简单协议**
```pdef
@protocol { name = "Test"; endian = big; }

SimplePacket {
    uint32 magic;
}

@filter MagicTest {
    magic = 0x12345678;
}
```

**测试数据**：
- 输入：`12 34 56 78` → 期望匹配
- 输入：`00 00 00 00` → 期望不匹配
- 输入：`12 34 56` → 期望不匹配（长度不足）

**测试用例2：嵌套结构**
```pdef
Header { uint32 magic; }
Packet { Header header; uint32 data; }

@filter HeaderMagic {
    header.magic = 0xAABBCCDD;
}
```

**测试数据**：
- 输入：`AA BB CC DD 12 34 56 78` → 期望匹配

### 9.3 性能测试

**测试环境**：
- CPU: Intel Xeon / AMD EPYC
- 内存：16GB+
- 编译器：GCC 9+ / Clang 10+
- 优化选项：`-O3 -march=native`

**测试方法**：
```c
// 生成100万个测试报文
uint8_t packets[1000000][64];
// 预热
for (int i = 0; i < 1000; i++) {
    execute_filter(packets[i], 64, rule);
}
// 测试
uint64_t start = rdtsc();  // CPU时钟周期
for (int i = 0; i < 1000000; i++) {
    execute_filter(packets[i], 64, rule);
}
uint64_t end = rdtsc();
// 计算平均每报文耗时
uint64_t cycles_per_packet = (end - start) / 1000000;
```

**性能目标**：
- 单字段匹配：< 100 CPU周期
- 多字段匹配：< 300 CPU周期
- 10Mpps @ 3GHz CPU ≈ 300周期/报文

---

## 10. 附录

### 10.1 常见问题（FAQ）

**Q1: 为什么不支持动态偏移量？**
A: 动态偏移量需要运行时计算前序字段长度，严重影响性能。我们选择方案A（变长字段仅限末尾）来保证高性能。

**Q2: 如何处理复杂的协议（如TLV格式）？**
A: 对于TLV等动态格式，建议：
- 定义固定头部
- 过滤规则仅匹配头部字段
- 复杂解析留给后续工具处理

**Q3: 是否支持多线程？**
A: 字节码执行引擎是无状态的，天然支持多线程。只需确保：
- 每个线程使用独立的报文缓冲区
- `ProtocolDef` 只读，可共享

**Q4: 如何调试字节码？**
A: 提供字节码反汇编工具：
```bash
$ pdef-disasm my_game.pdef
Filter: SpecificPlayer
  0: LOAD_U32_BE  offset=0
  1: CMP_EQ       value=0x12345678
  2: JUMP_IF_FALSE target=7
  ...
```

### 10.2 性能优化技巧

1. **使用CPU亲和性**
   ```c
   cpu_set_t cpuset;
   CPU_ZERO(&cpuset);
   CPU_SET(0, &cpuset);
   pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
   ```

2. **预取数据**
   ```c
   __builtin_prefetch(packet + next_offset, 0, 3);
   ```

3. **使用SIMD指令（高级）**
   - 批量处理多个报文
   - 使用AVX2/AVX512加速字段比较

### 10.3 未来扩展方向

1. **即时编译（JIT）**
   - 将字节码编译为本地机器码
   - 使用LLVM或自定义JIT引擎
   - 预期性能提升：2-5x

2. **GPU加速**
   - 使用CUDA/OpenCL批量过滤
   - 适合离线分析场景

3. **更多字段类型**
   - `float`, `double`
   - `ipv4`, `ipv6`
   - `mac`

4. **正则表达式匹配**
   ```pdef
   @filter HTTPRequest {
       payload ~= "GET /api/.*";
   }
   ```

5. **统计功能**
   ```pdef
   @stats {
       count(packet_type);
       avg(player.level);
   }
   ```

### 10.4 参考资料

- **BPF (Berkeley Packet Filter)**: 经典的包过滤字节码设计
- **eBPF**: Linux内核的扩展BPF，支持更复杂的过滤逻辑
- **DPDK**: 高性能数据包处理框架
- **Protocol Buffers**: Google的结构化数据定义语言

### 10.5 术语表

| 术语 | 定义 |
|------|------|
| PDEF | Protocol Definition Format，协议定义格式 |
| AST | Abstract Syntax Tree，抽象语法树 |
| 字节码 | Bytecode，中间表示形式，由虚拟机执行 |
| 扁平化 | Flattening，将嵌套结构展开为平坦结构 |
| 零拷贝 | Zero-copy，避免不必要的内存拷贝 |
| 短路评估 | Short-circuit evaluation，条件失败立即返回 |
| 内联 | Inline，函数体直接插入调用点 |
| 分支预测 | Branch prediction，CPU预测分支走向 |
| 缓存行 | Cache line，CPU缓存的最小单位（通常64字节） |

---

## 变更历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|---------|
| 1.0 | 2025-12-03 | 团队 | 初始版本，完整设计文档 |

---

**文档结束**
