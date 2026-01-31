# MicroReactor <small>v3.0</small>

[![License](https://img.shields.io/badge/license-GPLv3.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20STM32-green.svg)]()
[![Standard](https://img.shields.io/badge/standard-C99-orange.svg)]()

<p align="center">
  <img src="docs/logo.png" width=256 height=256>
</p>

> **为微控制器而生的响应式架构。稳如磐石，灵动如流。**

MicroReactor v3.0 将现代异步编程范式带入嵌入式世界（目前支持esp32，逐步拓展）。在资源受限的环境下，为开发者提供最高级别的抽象与开发体验。

MicroReactor不仅仅是一个框架，也是一套工具，目前拥有：Reactor Studio - 可视化状态机设计器，Reactor Scope - 实时监控示波器，Reactor CTL - 命令行控制工具，Crash Analyzer - 黑盒解码器

---

## 简介

MicroReactor 并不是另一个普通的 RTOS，而是一个构建在裸机或 RTOS 之上的**响应式框架**。它旨在解决嵌入式开发中常见的痛点：回调地狱、状态管理混乱以及跨芯片通信的复杂性。

通过引入 **Actor 模型**、**有限状态机 (FSM)** 和 **无栈协程 (uFlow)**，MicroReactor 让您能够以同步的思维编写异步代码，同时保持系统的硬实时特性。

---

## 核心亮点

### 零动态分配 (Zero Alloc)
**把崩溃扼杀在编译期。**
所有数据结构（实体、信号、队列）均在静态区分配。彻底消除 `malloc/free` 带来的内存碎片风险，确保工业级设备 7x24 小时的长期运行稳定性。

### FSM + uFlow 混合引擎
**状态机的严谨 + 协程的直观。**
框架完美融合了两种强大的控制流模式：
- **FSM（状态机）**：负责管理复杂的设备生命周期（如：`初始化` -> `连接中` -> `就绪` -> `错误`）。
- **uFlow（无栈协程）**：负责处理线性的业务流程（如：`开灯` -> `等2秒` -> `关灯`），无需将逻辑拆分到多个回调函数中。

### 中间件管道 (Pipeline)
**面向切面编程 (AOP) 的嵌入式实现。**
在信号到达实体处理函数之前，通过管道对其进行预处理。您可以轻松插入拦截器、日志记录器、防抖逻辑或数据转换器，从而保持核心业务代码的纯净无暇。

### 虫洞 (Wormhole)
**位置透明的分布式通信。**
打破芯片的物理边界。您在 A 芯片上发出的信号，可以通过 UART/SPI 自动传输并触发 B 芯片上的实体。框架自动处理序列化与路由，无需手写解析协议，多芯片协作从未如此简单。

### 监督者 (Supervisor)
**借鉴 Erlang 的容错哲学。**
构建具备自愈能力的系统。当传感器或子模块发生致命错误时，监督者实体会自动捕获异常并按策略重置故障模块，而不是让整个 MCU 复位。

### Pub/Sub 消息总线
**高效的主题订阅机制。**
替代 O(N) 广播，实现精准的主题路由。实体可以订阅感兴趣的主题，发布者无需关心接收者是谁。

### 参数系统 (KV存储)
**类型安全的配置管理。**
支持持久化存储、变更通知和批量操作。适合设备配置、用户偏好等场景。

### 访问控制 (ACL)
**零信任的信号防火墙。**
为每个实体配置信号访问规则，阻止未授权的信号来源，保护关键实体的安全。

### 电源管理
**投票式自动低功耗。**
实体通过锁机制声明功耗需求，框架自动计算允许的最低功耗模式。支持 Active、Idle、Light Sleep、Deep Sleep 四种模式。

---

## 快速开始

### 添加为 ESP-IDF 组件

将 `components/micro_reactor` 文件夹复制到项目的 `components` 目录。

### 基本使用

```c
#include "ur_core.h"
#include "ur_flow.h"

/* 定义信号 */
enum {
    SIG_BUTTON = SIG_USER_BASE,
    SIG_TIMEOUT,
};

/* 定义状态 */
enum {
    STATE_OFF = 1,
    STATE_ON,
};

/* 定义动作函数 */
static uint16_t turn_on(ur_entity_t *ent, const ur_signal_t *sig) {
    gpio_set_level(LED_PIN, 1);
    return 0;  /* 保持当前状态 */
}

static uint16_t turn_off(ur_entity_t *ent, const ur_signal_t *sig) {
    gpio_set_level(LED_PIN, 0);
    return 0;
}

/* 定义状态规则 */
static const ur_rule_t off_rules[] = {
    UR_RULE(SIG_BUTTON, STATE_ON, turn_on),
    UR_RULE_END
};

static const ur_rule_t on_rules[] = {
    UR_RULE(SIG_BUTTON, STATE_OFF, turn_off),
    UR_RULE_END
};

static const ur_state_def_t led_states[] = {
    UR_STATE(STATE_OFF, 0, NULL, NULL, off_rules),
    UR_STATE(STATE_ON, 0, NULL, NULL, on_rules),
};

/* 创建并运行实体 */
static ur_entity_t led;

void app_main(void) {
    ur_entity_config_t cfg = {
        .id = 1,
        .name = "LED",
        .states = led_states,
        .state_count = 2,
        .initial_state = STATE_OFF,
    };

    ur_init(&led, &cfg);
    ur_start(&led);

    /* 推荐：无滴答调度循环 */
    ur_entity_t *entities[] = { &led };
    while (1) {
        ur_run(entities, 1, 100);  /* 空闲时休眠100ms */
    }
}
```

---

## 核心概念

### 实体（Entity）

实体是核心的响应式单元，每个实体包含：
- 唯一 ID
- 状态机（包含状态和转换规则）
- 信号收件箱队列
- 可选的混入（Mixin）和中间件
- 用于 uFlow 协程的暂存区（Scratchpad）

### 信号（Signal）

信号是轻量级消息，共 20 字节：
- 16 位信号 ID
- 16 位源实体 ID
- 4 字节内联载荷（u8/u16/u32/i8/i16/i32/float 联合体）
- 指向外部数据的指针
- 时间戳

```c
/* 创建信号 */
ur_signal_t sig = ur_signal_create(SIG_TEMP, entity_id);
sig.payload.u32[0] = temperature;

/* 发送到实体 */
ur_emit(&target_entity, sig);

/* 从中断发送 */
ur_emit_from_isr(&target_entity, sig, &woken);
```

### 系统信号

框架预定义的系统信号（0x0000 - 0x00FF）：

| 信号 | 值 | 描述 |
|------|------|------|
| `SIG_NONE` | 0x0000 | 空信号 |
| `SIG_SYS_INIT` | 0x0001 | 实体初始化 |
| `SIG_SYS_ENTRY` | 0x0002 | 状态进入 |
| `SIG_SYS_EXIT` | 0x0003 | 状态退出 |
| `SIG_SYS_TICK` | 0x0004 | 周期性滴答 |
| `SIG_SYS_TIMEOUT` | 0x0005 | 定时器超时 |
| `SIG_SYS_DYING` | 0x0006 | 实体挂起(监督者) |
| `SIG_SYS_REVIVE` | 0x0007 | 实体复活请求 |
| `SIG_SYS_RESET` | 0x0008 | 软复位 |
| `SIG_SYS_SUSPEND` | 0x0009 | 暂停实体 |
| `SIG_SYS_RESUME` | 0x000A | 恢复实体 |
| `SIG_PARAM_CHANGED` | 0x0020 | 参数变更通知 |
| `SIG_PARAM_READY` | 0x0021 | 参数系统就绪 |
| `SIG_USER_BASE` | 0x0100 | 用户信号起始 |

### 状态机

状态通过转换规则定义行为：

```c
/* 规则：收到 SIG_X 信号时，转换到 STATE_Y 并执行 action_fn */
UR_RULE(SIG_X, STATE_Y, action_fn)

/* 动作函数签名 */
uint16_t action_fn(ur_entity_t *ent, const ur_signal_t *sig) {
    /* 处理信号 */
    /* 返回值：0 = 使用规则的 next_state，非零 = 覆盖目标状态 */
    return 0;
}
```

### 层级状态机（HSM）

状态可以有父状态，实现信号冒泡：

```c
static const ur_state_def_t states[] = {
    UR_STATE(STATE_PARENT, 0, entry_fn, exit_fn, parent_rules),
    UR_STATE(STATE_CHILD, STATE_PARENT, NULL, NULL, child_rules),
};
```

信号查找顺序：
1. 当前状态规则
2. 混入（Mixin）规则
3. 父状态规则（HSM 冒泡）

### uFlow 协程

使用 Duff's Device 实现的无栈协程：

```c
uint16_t blink_action(ur_entity_t *ent, const ur_signal_t *sig) {
    UR_FLOW_BEGIN(ent);

    while (1) {
        led_on();
        UR_AWAIT_TIME(ent, 500);  /* 等待 500ms */

        led_off();
        UR_AWAIT_SIGNAL(ent, SIG_TICK);  /* 等待信号 */
    }

    UR_FLOW_END(ent);
}
```

跨 yield 变量必须使用暂存区：

```c
typedef struct {
    int counter;
    float value;
} my_scratch_t;

UR_SCRATCH_STATIC_ASSERT(my_scratch_t);

/* 在动作函数中 */
my_scratch_t *s = UR_SCRATCH_PTR(ent, my_scratch_t);
s->counter++;
```

### 中间件（转换器）

中间件在状态规则之前处理信号：

```c
/* 中间件函数 */
ur_mw_result_t my_middleware(ur_entity_t *ent, ur_signal_t *sig, void *ctx) {
    if (should_filter(sig)) {
        return UR_MW_FILTERED;  /* 丢弃信号 */
    }
    return UR_MW_CONTINUE;  /* 传递给下一个 */
}

/* 注册 */
ur_register_middleware(&entity, my_middleware, context, priority);
```

中间件返回值：
- `UR_MW_CONTINUE` - 继续传递给下一个中间件
- `UR_MW_HANDLED` - 已处理，停止传递
- `UR_MW_FILTERED` - 过滤丢弃该信号
- `UR_MW_TRANSFORM` - 信号已修改，继续传递

### 混入（Mixin）

与状态无关的信号处理器：

```c
static const ur_rule_t power_rules[] = {
    UR_RULE(SIG_POWER_OFF, 0, handle_power_off),
    UR_RULE_END
};

static const ur_mixin_t power_mixin = {
    .name = "Power",
    .rules = power_rules,
    .rule_count = 1,
    .priority = 10,
};

ur_bind_mixin(&entity, &power_mixin);
```

### 数据管道

高吞吐量数据流：

```c
/* 静态缓冲区 */
static uint8_t buffer[1024];
static ur_pipe_t pipe;

/* 初始化 */
ur_pipe_init(&pipe, buffer, sizeof(buffer), 64);

/* 写入（任务上下文） */
ur_pipe_write(&pipe, data, size, timeout_ms);

/* 写入（中断上下文） */
ur_pipe_write_from_isr(&pipe, data, size, &woken);

/* 读取 */
size_t read = ur_pipe_read(&pipe, buffer, size, timeout_ms);

/* 状态查询 */
size_t available = ur_pipe_available(&pipe);
size_t space = ur_pipe_space(&pipe);
```

### Pub/Sub 消息总线

主题订阅式消息分发：

```c
#include "ur_bus.h"

/* 初始化总线 */
ur_bus_init();

/* 订阅主题 */
ur_subscribe(&sensor_entity, TOPIC_TEMPERATURE);
ur_subscribe(&display_entity, TOPIC_TEMPERATURE);

/* 发布消息（所有订阅者都会收到） */
ur_publish(ur_signal_create(TOPIC_TEMPERATURE, src_id));

/* 带载荷发布 */
ur_publish_u32(TOPIC_TEMPERATURE, src_id, temperature_value);
```

### 参数系统

类型安全的 KV 存储：

```c
#include "ur_param.h"

/* 定义参数 */
enum {
    PARAM_BRIGHTNESS = 1,
    PARAM_VOLUME,
    PARAM_DEVICE_NAME,
};

static const ur_param_def_t params[] = {
    { PARAM_BRIGHTNESS, UR_PARAM_TYPE_U8, "brightness", .default_val.u8 = 50, UR_PARAM_FLAG_PERSIST },
    { PARAM_VOLUME, UR_PARAM_TYPE_U8, "volume", .default_val.u8 = 80, UR_PARAM_FLAG_PERSIST | UR_PARAM_FLAG_NOTIFY },
    { PARAM_DEVICE_NAME, UR_PARAM_TYPE_STR, "name", .default_val.str = "Device", UR_PARAM_FLAG_PERSIST },
};

/* 初始化 */
ur_param_init(params, 3, &storage_backend);

/* 读写参数 */
uint8_t brightness;
ur_param_get_u8(PARAM_BRIGHTNESS, &brightness);
ur_param_set_u8(PARAM_BRIGHTNESS, 75);

/* 持久化 */
ur_param_save_all();
```

### 访问控制列表（ACL）

信号防火墙：

```c
#include "ur_acl.h"

/* 定义 ACL 规则 */
static const ur_acl_rule_t sensor_rules[] = {
    UR_ACL_ALLOW_FROM(ENT_CONTROLLER),      /* 允许来自控制器的信号 */
    UR_ACL_DENY_FROM(UR_ACL_SRC_EXTERNAL),  /* 拒绝外部信号 */
    UR_ACL_ALLOW_SIG(SIG_SYS_TICK),         /* 允许系统tick */
};

/* 注册规则 */
ur_acl_register(&sensor_entity, sensor_rules, 3);

/* 设置默认策略 */
ur_acl_set_default(&sensor_entity, UR_ACL_DEFAULT_DENY);

/* 启用 ACL 中间件 */
ur_acl_enable_middleware(&sensor_entity);
```

### 电源管理

投票式低功耗控制：

```c
#include "ur_power.h"

/* 初始化 */
ur_power_init(&ur_power_hal_esp);

/* 获取电源锁（防止进入低功耗） */
ur_power_lock(&wifi_entity, UR_POWER_ACTIVE);

/* 释放电源锁 */
ur_power_unlock(&wifi_entity, UR_POWER_ACTIVE);

/* 进入低功耗（框架自动选择允许的最低模式） */
ur_power_mode_t allowed = ur_power_get_allowed_mode();
ur_power_enter_mode(allowed, timeout_ms, wake_sources);
```

### 虫洞（跨芯片 RPC）

通过 UART 进行分布式信号路由：

```c
#include "ur_wormhole.h"

/* 初始化 */
ur_wormhole_init(chip_id);

/* 添加路由：本地实体 1 <-> 远程实体 100 */
ur_wormhole_add_route(1, 100, UART_NUM_1);

/* 发送到远程 */
ur_wormhole_send(100, signal);
```

协议帧格式（10 字节）：
```
| 0xAA | SrcID (2B) | SigID (2B) | Payload (4B) | CRC8 |
```

### 监督者（自愈机制）

自动重启失败的实体：

```c
#include "ur_supervisor.h"

/* 创建监督者 */
ur_supervisor_create(&supervisor_entity, max_restarts);

/* 添加子实体 */
ur_supervisor_add_child(&supervisor_entity, &child1);
ur_supervisor_add_child(&supervisor_entity, &child2);

/* 报告故障（触发重启） */
ur_report_dying(&child1, error_code);
```

### 性能追踪

Chrome/Perfetto 兼容的追踪系统：

```c
#include "ur_trace.h"

/* 初始化 */
ur_trace_init();
ur_trace_set_backend(&ur_trace_backend_uart);
ur_trace_enable(true);

/* 注册元数据 */
ur_trace_register_entity_name(ENT_LED, "LED");
ur_trace_register_signal_name(SIG_BUTTON, "BUTTON");
ur_trace_register_state_name(ENT_LED, STATE_ON, "On");
ur_trace_sync_metadata();

/* 追踪宏（禁用时零开销） */
UR_TRACE_START(ent_id, sig_id);
UR_TRACE_END(ent_id, sig_id);
UR_TRACE_TRANSITION(ent_id, from_state, to_state);
```

### 信号编解码

序列化/反序列化用于 MQTT、HTTP、UART 桥接：

```c
#include "ur_codec.h"

/* 编码为二进制 */
uint8_t buffer[256];
size_t len;
ur_codec_encode_binary(&signal, buffer, sizeof(buffer), &len);

/* 编码为 JSON */
char json[256];
ur_codec_encode_json(&signal, json, sizeof(json), &len);

/* 解码 */
ur_signal_t decoded;
ur_codec_decode_json(json, &decoded);
```

---

## 配置

通过 `menuconfig` 或 `sdkconfig` 配置：

### 核心配置

```
CONFIG_UR_MAX_ENTITIES=16           # 最大实体数
CONFIG_UR_MAX_RULES_PER_STATE=16    # 每状态最大规则数
CONFIG_UR_MAX_STATES_PER_ENTITY=16  # 每实体最大状态数
CONFIG_UR_MAX_MIXINS_PER_ENTITY=4   # 每实体最大混入数
CONFIG_UR_INBOX_SIZE=8              # 收件箱队列大小
CONFIG_UR_SCRATCHPAD_SIZE=64        # 协程暂存区大小
CONFIG_UR_MAX_MIDDLEWARE=8          # 最大中间件数
CONFIG_UR_SIGNAL_PAYLOAD_SIZE=4     # 信号内联载荷大小
```

### 特性开关

```
CONFIG_UR_ENABLE_HSM=y              # 启用层级状态机
CONFIG_UR_ENABLE_LOGGING=n          # 启用调试日志
CONFIG_UR_ENABLE_TIMESTAMPS=y       # 启用时间戳
CONFIG_UR_PIPE_ENABLE=y             # 启用数据管道
CONFIG_UR_BUS_ENABLE=y              # 启用Pub/Sub总线
CONFIG_UR_PARAM_ENABLE=y            # 启用参数系统
CONFIG_UR_CODEC_ENABLE=y            # 启用编解码
CONFIG_UR_POWER_ENABLE=y            # 启用电源管理
CONFIG_UR_TRACE_ENABLE=n            # 启用性能追踪
CONFIG_UR_ACL_ENABLE=y              # 启用访问控制
CONFIG_UR_WORMHOLE_ENABLE=n         # 启用虫洞
CONFIG_UR_SUPERVISOR_ENABLE=n       # 启用监督者
```

### 扩展模块配置

```
CONFIG_UR_BUS_MAX_TOPICS=64         # 最大主题数
CONFIG_UR_BUS_MAX_SUBSCRIBERS=8     # 每主题最大订阅者
CONFIG_UR_PARAM_MAX_COUNT=32        # 最大参数数
CONFIG_UR_PARAM_MAX_STRING_LEN=64   # 参数字符串最大长度
CONFIG_UR_CODEC_MAX_SCHEMAS=32      # 最大编码模式数
CONFIG_UR_CODEC_BUFFER_SIZE=256     # 编码缓冲区大小
CONFIG_UR_ACL_MAX_RULES=32          # 最大ACL规则数
CONFIG_UR_TRACE_BUFFER_SIZE=4096    # 追踪缓冲区大小
CONFIG_UR_TRACE_MAX_ENTRIES=256     # 最大追踪事件数
```

---

## 示例

### example_basic

LED 闪烁器与按钮控制：
- 实体初始化
- 信号发送
- FSM 状态转换
- uFlow 协程定时
- GPIO 中断处理

### example_multi_entity

温度监控系统：
- 多实体协调
- 中间件链（日志、防抖）
- 混入用法（通用电源规则）
- HSM 层级状态
- 实体间通信

### example_pipe

音频风格的数据流：
- 生产者-消费者模式
- 中断安全写入
- 吞吐量监控

---

## API 参考

### 核心函数

```c
/* 实体管理 */
ur_err_t ur_init(ur_entity_t *ent, const ur_entity_config_t *config);
ur_err_t ur_start(ur_entity_t *ent);
ur_err_t ur_stop(ur_entity_t *ent);
ur_err_t ur_suspend(ur_entity_t *ent);
ur_err_t ur_resume(ur_entity_t *ent);

/* 信号发送 */
ur_err_t ur_emit(ur_entity_t *target, ur_signal_t sig);
ur_err_t ur_emit_from_isr(ur_entity_t *target, ur_signal_t sig, BaseType_t *woken);
ur_err_t ur_emit_to_id(uint16_t target_id, ur_signal_t sig);
int ur_broadcast(ur_signal_t sig);

/* 调度循环 */
ur_err_t ur_dispatch(ur_entity_t *ent, uint32_t timeout_ms);
int ur_dispatch_all(ur_entity_t *ent);
int ur_dispatch_multi(ur_entity_t **entities, size_t count);
int ur_run(ur_entity_t **entities, size_t count, uint32_t idle_ms);
```

### 状态管理

```c
uint16_t ur_get_state(const ur_entity_t *ent);
ur_err_t ur_set_state(ur_entity_t *ent, uint16_t state_id);
bool ur_in_state(const ur_entity_t *ent, uint16_t state_id);
```

### 混入/中间件

```c
ur_err_t ur_bind_mixin(ur_entity_t *ent, const ur_mixin_t *mixin);
ur_err_t ur_unbind_mixin(ur_entity_t *ent, const ur_mixin_t *mixin);
ur_err_t ur_register_middleware(ur_entity_t *ent, ur_middleware_fn_t fn, void *ctx, uint8_t priority);
ur_err_t ur_unregister_middleware(ur_entity_t *ent, ur_middleware_fn_t fn);
ur_err_t ur_set_middleware_enabled(ur_entity_t *ent, ur_middleware_fn_t fn, bool enabled);
```

### 实体注册表

```c
ur_err_t ur_register_entity(ur_entity_t *ent);
ur_err_t ur_unregister_entity(ur_entity_t *ent);
ur_entity_t *ur_get_entity(uint16_t id);
size_t ur_get_entity_count(void);
```

### 管道函数

```c
ur_err_t ur_pipe_init(ur_pipe_t *pipe, uint8_t *buffer, size_t size, size_t trigger);
ur_err_t ur_pipe_reset(ur_pipe_t *pipe);
size_t ur_pipe_write(ur_pipe_t *pipe, const void *data, size_t size, uint32_t timeout_ms);
size_t ur_pipe_write_from_isr(ur_pipe_t *pipe, const void *data, size_t size, BaseType_t *woken);
size_t ur_pipe_read(ur_pipe_t *pipe, void *buffer, size_t size, uint32_t timeout_ms);
size_t ur_pipe_read_from_isr(ur_pipe_t *pipe, void *buffer, size_t size, BaseType_t *woken);
size_t ur_pipe_available(const ur_pipe_t *pipe);
size_t ur_pipe_space(const ur_pipe_t *pipe);
bool ur_pipe_is_empty(const ur_pipe_t *pipe);
bool ur_pipe_is_full(const ur_pipe_t *pipe);
```

### 工具函数

```c
bool ur_in_isr(void);
uint32_t ur_get_time_ms(void);
size_t ur_inbox_count(const ur_entity_t *ent);
bool ur_inbox_empty(const ur_entity_t *ent);
void ur_inbox_clear(ur_entity_t *ent);
```

---

## 错误码

| 错误码 | 描述 |
|--------|------|
| `UR_OK` | 成功 |
| `UR_ERR_INVALID_ARG` | 无效参数 |
| `UR_ERR_NO_MEMORY` | 静态池已满 |
| `UR_ERR_QUEUE_FULL` | 收件箱已满 |
| `UR_ERR_NOT_FOUND` | 实体/状态未找到 |
| `UR_ERR_INVALID_STATE` | 无效状态转换 |
| `UR_ERR_TIMEOUT` | 操作超时 |
| `UR_ERR_ALREADY_EXISTS` | 项目已存在 |
| `UR_ERR_DISABLED` | 功能禁用 |

---

## 目标平台

- **ESP-IDF 5.x**（FreeRTOS v10.5+）
- 使用现代 FreeRTOS API：
  - `xPortInIsrContext()` 用于中断检测
  - `xStreamBufferCreateStatic()` 用于管道
  - `xQueueCreateStatic()` 用于收件箱

---

## 许可证

GPL v3 许可证

---

## 文档

- [详细教程（中文）](docs/tutorial_zh.md)
- [API参考（中文）](docs/api_reference_zh.md)

---

## 开发工具

MicroReactor 提供一套完整的可视化开发工具，位于 `tools/` 目录。

### 安装工具依赖

```bash
cd tools
pip install -r requirements.txt
```

### Reactor Studio - 可视化状态机设计器

```bash
python tools/studio/reactor_studio.py
```

拖拽式状态机编辑器，可生成 MicroReactor 框架兼容的 C 代码。

**核心功能：**
- 可视化编辑：拖拽创建状态，可视化连接转换线
- 层级状态机：支持 HSM 父子状态关系
- 状态编辑器：配置进入/退出动作、转换规则表
- 多实体项目：一个项目管理多个实体状态机
- 代码生成：自动生成 .h 和 .c 文件
- 项目管理：JSON 格式保存/加载
- 双语界面：中文/英文切换

**快捷键**：`S` 添加状态 | `T` 转换模式 | `Delete` 删除 | `Ctrl+S` 保存 | `Ctrl+E` 导出

---

### Reactor Scope - 实时监控示波器

```bash
python tools/scope/reactor_scope.py
```

连接设备串口，实时监控 MicroReactor 系统运行状态。

**核心功能：**
- Gantt 时序图：可视化各实体的 dispatch 时间线
  - 光标定位、悬浮提示、滚动浏览、自动暂停
  - 支持 10ms ~ 1s 时间窗口切换
- 信号流图：显示实体间信号传递序列图
  - 暂停/滚动、信号过滤（隐藏特定信号）
- 性能统计：信号速率、调度耗时、内存监控
- 元数据显示：实体名、信号名、状态名（从设备同步）
- 信号注入：手动发送信号测试系统
- 命令终端：发送 Shell 命令
- 数据导出：JSON/CSV 格式

**设备端启用 Trace：**
```c
UR_TRACE_INIT();
ur_trace_set_backend(&ur_trace_backend_uart);
ur_trace_enable(true);

// 注册元数据（可选，让 Scope 显示可读名称）
ur_trace_register_entity_name(ENT_ID_SYSTEM, "System");
ur_trace_register_signal_name(SIG_BUTTON, "BUTTON");
ur_trace_register_state_name(ENT_ID_SYSTEM, STATE_IDLE, "Idle");
ur_trace_sync_metadata();
```

---

### Reactor CTL - 命令行控制工具

```bash
python tools/rctl.py -p COM3 list          # 列出实体
python tools/rctl.py -p COM3 inject 1 0x100  # 注入信号
python tools/rctl.py -p COM3 listen         # 监听信号
```

---

### Crash Analyzer - 黑盒解码器

```bash
python tools/crash_analyzer.py dump.bin --elf firmware.elf
```

分析 MicroReactor 黑盒 dump 数据，生成事件时间线和问题诊断报告。

---

完整工具文档：[tools/README.md](tools/README.md)
