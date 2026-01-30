# MicroReactor 详细教程

本教程将带你从零开始学习 MicroReactor 框架，包括核心概念、设计理念和实际应用。

## 目录

1. [框架简介](#1-框架简介)
2. [核心概念详解](#2-核心概念详解)
3. [实体与状态机](#3-实体与状态机)
4. [信号系统](#4-信号系统)
5. [uFlow 协程](#5-uflow-协程)
6. [中间件管道](#6-中间件管道)
7. [混入系统](#7-混入系统)
8. [数据管道](#8-数据管道)
9. [分布式通信（虫洞）](#9-分布式通信虫洞)
10. [监督者与自愈](#10-监督者与自愈)
11. [实战案例](#11-实战案例)
12. [最佳实践](#12-最佳实践)
13. [常见问题](#13-常见问题)

---

## 1. 框架简介

### 1.1 什么是 MicroReactor？

MicroReactor 是一个专为嵌入式系统设计的响应式框架，它采用 **实体-组件-信号（ECS）** 架构，结合 **有限状态机（FSM）** 和 **无栈协程（uFlow）**，提供了一种清晰、可维护的方式来组织嵌入式代码。

### 1.2 为什么使用 MicroReactor？

传统嵌入式开发的痛点：
- 回调地狱：异步操作嵌套复杂
- 状态管理混乱：全局变量满天飞
- 代码耦合：模块间依赖难以追踪
- 内存不可控：动态分配导致碎片化

MicroReactor 的解决方案：
- **信号驱动**：用信号替代回调，解耦发送者和接收者
- **状态机**：明确的状态定义和转换规则
- **零分配**：所有内存静态分配，编译时确定
- **协程支持**：用同步风格写异步代码

### 1.3 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                      应用层                                  │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐                      │
│  │ Entity1 │  │ Entity2 │  │ Entity3 │  ...                 │
│  └────┬────┘  └────┬────┘  └────┬────┘                      │
│       │            │            │                            │
│  ┌────▼────────────▼────────────▼────┐                      │
│  │          调度器（Dispatcher）       │                      │
│  │  ┌──────────────────────────────┐ │                      │
│  │  │      中间件管道（Middleware） │ │                      │
│  │  └──────────────────────────────┘ │                      │
│  │  ┌──────────────────────────────┐ │                      │
│  │  │      规则查找（Cascading）    │ │                      │
│  │  └──────────────────────────────┘ │                      │
│  └───────────────────────────────────┘                      │
│                      │                                       │
│  ┌───────────────────▼───────────────┐                      │
│  │           信号队列                 │                      │
│  └───────────────────────────────────┘                      │
└─────────────────────────────────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                      硬件抽象层                              │
│  GPIO / UART / SPI / I2C / Timer / ADC / ...                │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. 核心概念详解

### 2.1 实体（Entity）

实体是框架的基本响应单元，可以理解为一个"活的对象"。每个实体：

- 有唯一的 ID 标识
- 维护自己的状态机
- 有独立的信号收件箱
- 可以接收和发送信号

```c
// 实体控制块结构（简化版）
struct ur_entity_s {
    uint16_t id;              // 实体 ID
    uint16_t current_state;   // 当前状态
    uint8_t flags;            // 标志位

    const ur_state_def_t *states;  // 状态定义数组
    uint8_t state_count;           // 状态数量

    QueueHandle_t inbox;      // 信号收件箱
    uint8_t scratch[64];      // 协程暂存区

    void *user_data;          // 用户数据
    const char *name;         // 实体名称
};
```

### 2.2 信号（Signal）

信号是实体间通信的唯一方式，采用"发后即忘"（fire-and-forget）模式：

```c
// 信号结构（20 字节）
struct ur_signal_s {
    uint16_t id;              // 信号 ID
    uint16_t src_id;          // 发送者实体 ID
    union {
        uint8_t  u8[4];
        uint16_t u16[2];
        uint32_t u32[1];
        float    f32;
    } payload;                // 4 字节内联载荷
    void *ptr;                // 扩展数据指针
    uint32_t timestamp;       // 时间戳
    uint32_t _reserved;       // 对齐填充
};
```

信号设计原则：
- **轻量**：固定 20 字节，适合队列传递
- **自描述**：包含来源信息
- **灵活载荷**：小数据内联，大数据用指针

### 2.3 状态（State）

状态定义了实体在特定情况下的行为：

```c
// 状态定义
struct ur_state_def_s {
    uint16_t id;              // 状态 ID
    uint16_t parent_id;       // 父状态 ID（用于 HSM）
    ur_action_fn_t on_entry;  // 进入动作
    ur_action_fn_t on_exit;   // 退出动作
    const ur_rule_t *rules;   // 转换规则数组
    uint8_t rule_count;       // 规则数量
};
```

### 2.4 规则（Rule）

规则定义了"在什么信号下做什么事"：

```c
// 转换规则
struct ur_rule_s {
    uint16_t signal_id;       // 触发信号
    uint16_t next_state;      // 目标状态（0 = 不转换）
    ur_action_fn_t action;    // 执行的动作
};
```

---

## 3. 实体与状态机

### 3.1 定义状态和规则

```c
#include "ur_core.h"

// 第一步：定义信号
enum {
    SIG_BUTTON_PRESS = SIG_USER_BASE,  // 用户信号从 0x0100 开始
    SIG_BUTTON_RELEASE,
    SIG_TIMEOUT,
    SIG_ERROR,
};

// 第二步：定义状态
enum {
    STATE_IDLE = 1,      // 状态 ID 从 1 开始，0 保留
    STATE_ACTIVE,
    STATE_ERROR,
};

// 第三步：实现动作函数
static uint16_t on_button_press(ur_entity_t *ent, const ur_signal_t *sig) {
    ESP_LOGI(TAG, "按钮按下！");
    // 返回 0 表示使用规则定义的 next_state
    // 返回非 0 值可以覆盖目标状态
    return 0;
}

static uint16_t on_timeout(ur_entity_t *ent, const ur_signal_t *sig) {
    ESP_LOGW(TAG, "操作超时");
    return STATE_ERROR;  // 动态决定转到错误状态
}

// 第四步：定义每个状态的规则
static const ur_rule_t idle_rules[] = {
    // 信号 -> 目标状态, 动作函数
    UR_RULE(SIG_BUTTON_PRESS, STATE_ACTIVE, on_button_press),
    UR_RULE(SIG_ERROR, STATE_ERROR, NULL),  // 可以没有动作
    UR_RULE_END  // 规则数组结束标记
};

static const ur_rule_t active_rules[] = {
    UR_RULE(SIG_BUTTON_RELEASE, STATE_IDLE, NULL),
    UR_RULE(SIG_TIMEOUT, 0, on_timeout),  // next_state=0，由动作决定
    UR_RULE_END
};

static const ur_rule_t error_rules[] = {
    UR_RULE(SIG_BUTTON_PRESS, STATE_IDLE, NULL),  // 任意按键恢复
    UR_RULE_END
};

// 第五步：定义状态数组
static const ur_state_def_t my_states[] = {
    // 状态ID, 父状态, 进入动作, 退出动作, 规则数组
    UR_STATE(STATE_IDLE, 0, NULL, NULL, idle_rules),
    UR_STATE(STATE_ACTIVE, 0, NULL, NULL, active_rules),
    UR_STATE(STATE_ERROR, 0, NULL, NULL, error_rules),
};
```

### 3.2 创建和启动实体

```c
// 静态分配实体
static ur_entity_t my_entity;

void setup_entity(void) {
    // 配置实体
    ur_entity_config_t config = {
        .id = 1,                    // 唯一 ID
        .name = "MyEntity",         // 调试用名称
        .states = my_states,        // 状态数组
        .state_count = 3,           // 状态数量
        .initial_state = STATE_IDLE, // 初始状态
        .user_data = NULL,          // 用户数据
    };

    // 初始化实体
    ur_err_t err = ur_init(&my_entity, &config);
    if (err != UR_OK) {
        ESP_LOGE(TAG, "初始化失败: %d", err);
        return;
    }

    // 注册到全局注册表（可选，用于 ur_emit_to_id）
    ur_register_entity(&my_entity);

    // 启动实体（进入初始状态，发送 SIG_SYS_INIT）
    ur_start(&my_entity);
}
```

### 3.3 调度循环

```c
// 方式一：阻塞式调度（推荐用于专用任务）
void dispatch_task(void *param) {
    ur_entity_t *ent = (ur_entity_t *)param;

    while (1) {
        // 阻塞等待信号，有信号时处理
        ur_dispatch(ent, portMAX_DELAY);
    }
}

// 方式二：非阻塞轮询（适合主循环）
void main_loop(void) {
    while (1) {
        // 处理所有待处理信号
        int processed = ur_dispatch_all(&my_entity);

        // 做其他事情...
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// 方式三：多实体调度
void multi_dispatch_task(void *param) {
    ur_entity_t *entities[] = {&entity1, &entity2, &entity3};

    while (1) {
        // 轮询所有实体
        ur_dispatch_multi(entities, 3);
        vTaskDelay(1);
    }
}
```

### 3.4 层级状态机（HSM）

HSM 允许状态继承，子状态处理不了的信号会"冒泡"到父状态：

```c
// 定义层级状态
enum {
    STATE_ROOT = 1,        // 根状态
    STATE_STANDBY,         // 待机（继承自 ROOT）
    STATE_RUNNING,         // 运行中（继承自 ROOT）
    STATE_RUN_NORMAL,      // 正常运行（继承自 RUNNING）
    STATE_RUN_TURBO,       // 加速运行（继承自 RUNNING）
};

// 根状态规则 - 处理全局信号
static const ur_rule_t root_rules[] = {
    UR_RULE(SIG_EMERGENCY_STOP, STATE_STANDBY, emergency_stop),
    UR_RULE(SIG_LOW_BATTERY, 0, handle_low_battery),
    UR_RULE_END
};

// 运行状态规则 - 处理运行相关信号
static const ur_rule_t running_rules[] = {
    UR_RULE(SIG_STOP, STATE_STANDBY, NULL),
    UR_RULE(SIG_SPEED_REPORT, 0, update_speed),
    UR_RULE_END
};

// 正常运行规则
static const ur_rule_t run_normal_rules[] = {
    UR_RULE(SIG_TURBO_ON, STATE_RUN_TURBO, enable_turbo),
    UR_RULE_END
};

// 状态定义（注意 parent_id）
static const ur_state_def_t hsm_states[] = {
    UR_STATE(STATE_ROOT, 0, NULL, NULL, root_rules),
    UR_STATE(STATE_STANDBY, STATE_ROOT, enter_standby, NULL, standby_rules),
    UR_STATE(STATE_RUNNING, STATE_ROOT, enter_running, exit_running, running_rules),
    UR_STATE(STATE_RUN_NORMAL, STATE_RUNNING, NULL, NULL, run_normal_rules),
    UR_STATE(STATE_RUN_TURBO, STATE_RUNNING, enter_turbo, exit_turbo, run_turbo_rules),
};
```

信号查找顺序示例（当前状态是 `STATE_RUN_NORMAL`）：
1. `STATE_RUN_NORMAL` 的规则
2. 混入规则
3. `STATE_RUNNING` 的规则（父状态）
4. `STATE_ROOT` 的规则（祖父状态）

---

## 4. 信号系统

### 4.1 系统信号

框架预定义了一些系统信号（0x0000 - 0x00FF）：

```c
typedef enum {
    SIG_NONE = 0x0000,          // 空信号
    SIG_SYS_INIT = 0x0001,      // 实体初始化
    SIG_SYS_ENTRY = 0x0002,     // 状态进入
    SIG_SYS_EXIT = 0x0003,      // 状态退出
    SIG_SYS_TICK = 0x0004,      // 周期 tick
    SIG_SYS_TIMEOUT = 0x0005,   // 超时
    SIG_SYS_DYING = 0x0006,     // 实体濒死
    SIG_SYS_REVIVE = 0x0007,    // 实体复活
    SIG_SYS_RESET = 0x0008,     // 软重置
    SIG_SYS_SUSPEND = 0x0009,   // 暂停
    SIG_SYS_RESUME = 0x000A,    // 恢复

    SIG_USER_BASE = 0x0100,     // 用户信号起始
} ur_sys_signal_t;
```

### 4.2 创建信号

```c
// 方式一：基本创建
ur_signal_t sig = ur_signal_create(SIG_MY_EVENT, sender_id);

// 方式二：带 u32 载荷
ur_signal_t sig = ur_signal_create_u32(SIG_TEMP_DATA, sender_id, temperature);

// 方式三：带指针载荷
ur_signal_t sig = ur_signal_create_ptr(SIG_BUFFER_READY, sender_id, buffer_ptr);

// 方式四：手动构造
ur_signal_t sig = {
    .id = SIG_COMPLEX_DATA,
    .src_id = my_entity.id,
    .payload.u16[0] = x_value,
    .payload.u16[1] = y_value,
    .ptr = extra_data,
    .timestamp = ur_time_ms(),
};

// 方式五：使用宏
ur_signal_t sig = UR_SIGNAL_U32(SIG_VALUE, src_id, 12345);
ur_signal_t sig = UR_SIGNAL_PTR(SIG_DATA, src_id, data_ptr);
```

### 4.3 发送信号

```c
// 发送到指定实体
ur_emit(&target_entity, signal);

// 发送到自己
ur_emit(ent, ur_signal_create(SIG_SELF_CHECK, ent->id));

// 通过 ID 发送（需要注册实体）
ur_emit_to_id(target_id, signal);

// 广播到所有实体
int count = ur_broadcast(signal);

// 从中断发送
void IRAM_ATTR gpio_isr_handler(void *arg) {
    ur_entity_t *ent = (ur_entity_t *)arg;
    BaseType_t woken = pdFALSE;

    ur_signal_t sig = {
        .id = SIG_GPIO_EVENT,
        .src_id = 0,
        .payload.u32[0] = gpio_get_level(GPIO_PIN),
    };

    ur_emit_from_isr(ent, sig, &woken);
    portYIELD_FROM_ISR(woken);
}
```

### 4.4 信号载荷使用

```c
// 发送端 - 打包数据
ur_signal_t sig = ur_signal_create(SIG_SENSOR_DATA, ent->id);
sig.payload.u16[0] = temperature;  // 温度
sig.payload.u16[1] = humidity;     // 湿度

// 接收端 - 解包数据
static uint16_t handle_sensor_data(ur_entity_t *ent, const ur_signal_t *sig) {
    uint16_t temp = sig->payload.u16[0];
    uint16_t humid = sig->payload.u16[1];

    ESP_LOGI(TAG, "温度: %d, 湿度: %d", temp, humid);
    return 0;
}

// 使用指针传递大数据
typedef struct {
    float values[10];
    uint32_t timestamp;
    char label[16];
} sensor_packet_t;

static sensor_packet_t packet;  // 必须是静态或全局的！

void send_packet(void) {
    ur_signal_t sig = ur_signal_create_ptr(SIG_PACKET, ent->id, &packet);
    ur_emit(&receiver, sig);
}

// 接收端
static uint16_t handle_packet(ur_entity_t *ent, const ur_signal_t *sig) {
    sensor_packet_t *pkt = (sensor_packet_t *)sig->ptr;
    // 使用 pkt->values, pkt->timestamp, pkt->label
    return 0;
}
```

> **重要**：指针载荷必须指向静态或全局内存，不能指向栈上的局部变量！

---

## 5. uFlow 协程

### 5.1 协程基础

uFlow 使用 Duff's Device 技术实现无栈协程，让你可以用同步风格写异步代码：

```c
// 传统回调方式（复杂）
void start_sequence(void) {
    led_on();
    start_timer(500, timer1_callback);
}
void timer1_callback(void) {
    led_off();
    start_timer(500, timer2_callback);
}
void timer2_callback(void) {
    led_on();
    // ...继续嵌套
}

// uFlow 协程方式（清晰）
uint16_t blink_sequence(ur_entity_t *ent, const ur_signal_t *sig) {
    UR_FLOW_BEGIN(ent);

    while (1) {
        led_on();
        UR_AWAIT_TIME(ent, 500);   // 等待 500ms

        led_off();
        UR_AWAIT_TIME(ent, 500);
    }

    UR_FLOW_END(ent);
}
```

### 5.2 协程宏详解

```c
// 开始协程（必须是函数第一条语句）
UR_FLOW_BEGIN(ent);

// 结束协程（必须是函数最后）
UR_FLOW_END(ent);

// 让出执行，下次任意信号时恢复
UR_YIELD(ent);

// 等待特定信号
UR_AWAIT_SIGNAL(ent, SIG_MY_EVENT);
// 恢复后，sig 参数包含收到的信号

// 等待多个信号之一
UR_AWAIT_ANY(ent, SIG_OK, SIG_CANCEL, SIG_TIMEOUT);

// 等待时间
UR_AWAIT_TIME(ent, 1000);  // 等待 1000ms

// 等待条件
UR_AWAIT_COND(ent, sensor_ready());

// 发送信号并让出
UR_EMIT_YIELD(ent, &other_entity, signal);

// 转到其他状态并退出协程
UR_FLOW_GOTO(ent, STATE_DONE);

// 重置协程（从头开始）
UR_FLOW_RESET(ent);
```

### 5.3 暂存区（Scratchpad）

协程中，跨 `UR_AWAIT_*` 的变量**不能**用局部变量，必须存在暂存区：

```c
// 定义暂存区结构
typedef struct {
    int counter;
    float accumulated;
    uint32_t start_time;
    bool flag;
} my_scratch_t;

// 静态断言：确保结构体不超过暂存区大小
UR_SCRATCH_STATIC_ASSERT(my_scratch_t);

// 在协程中使用
uint16_t counting_action(ur_entity_t *ent, const ur_signal_t *sig) {
    my_scratch_t *s = UR_SCRATCH_PTR(ent, my_scratch_t);

    UR_FLOW_BEGIN(ent);

    // 初始化（只在第一次执行）
    s->counter = 0;
    s->accumulated = 0.0f;
    s->start_time = ur_time_ms();

    while (s->counter < 10) {
        UR_AWAIT_SIGNAL(ent, SIG_DATA);

        s->accumulated += sig->payload.f32;
        s->counter++;

        ESP_LOGI(TAG, "计数: %d, 累计: %.2f", s->counter, s->accumulated);
    }

    ESP_LOGI(TAG, "完成！耗时: %d ms", ur_time_ms() - s->start_time);

    UR_FLOW_END(ent);
}
```

### 5.4 协程使用限制

1. **不能使用 switch 语句**（因为 Duff's Device 本身就是 switch）
2. **局部变量在 yield 后失效**
3. **只能在动作函数的顶层使用协程宏**
4. **协程宏不能在子函数中使用**

```c
// 错误示例
uint16_t bad_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    UR_FLOW_BEGIN(ent);

    int local_var = 0;  // 错误：局部变量在 yield 后失效

    switch (sig->payload.u32[0]) {  // 错误：不能使用 switch
        case 1: /* ... */ break;
    }

    helper_function(ent);  // 错误：不能在子函数中 yield

    UR_FLOW_END(ent);
}

// 正确示例
uint16_t good_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    my_scratch_t *s = UR_SCRATCH_PTR(ent, my_scratch_t);

    UR_FLOW_BEGIN(ent);

    s->local_var = 0;  // 正确：使用暂存区

    // 用 if-else 代替 switch
    if (sig->payload.u32[0] == 1) {
        // ...
    } else if (sig->payload.u32[0] == 2) {
        // ...
    }

    // 调用普通函数（不含协程宏）是可以的
    process_data(s);

    UR_FLOW_END(ent);
}
```

### 5.5 协程状态查询

```c
// 检查协程是否在运行
if (UR_FLOW_IS_RUNNING(ent)) {
    // 协程活跃中
}

// 检查是否在等待信号
if (UR_FLOW_IS_WAITING_SIGNAL(ent)) {
    uint16_t waiting_for = UR_FLOW_GET_WAIT_SIGNAL(ent);
}

// 检查是否在时间等待中
if (UR_FLOW_IS_WAITING_TIME(ent)) {
    // 正在等待时间超时
}
```

---

## 6. 中间件管道

### 6.1 中间件概念

中间件在信号到达状态规则之前进行预处理，可以：
- 记录日志
- 过滤信号
- 变换信号
- 实现横切关注点

```
信号 ──▶ [中间件1] ──▶ [中间件2] ──▶ [中间件N] ──▶ 状态规则
              │              │              │
              ▼              ▼              ▼
           CONTINUE      FILTERED        HANDLED
```

### 6.2 编写中间件

```c
// 中间件函数签名
ur_mw_result_t my_middleware(
    ur_entity_t *ent,    // 目标实体
    ur_signal_t *sig,    // 信号（可修改）
    void *ctx            // 上下文数据
) {
    // 处理逻辑...

    // 返回值决定后续行为
    return UR_MW_CONTINUE;   // 继续传递
    // return UR_MW_FILTERED;  // 丢弃信号
    // return UR_MW_HANDLED;   // 已处理，不传给规则
    // return UR_MW_TRANSFORM; // 信号已修改，继续传递
}
```

### 6.3 内置中间件

#### 日志中间件

```c
#include "ur_transducers.h"

typedef struct {
    uint16_t filter_signal;  // 只记录特定信号（0=全部）
    bool log_payload;        // 是否记录载荷
} ur_mw_logger_ctx_t;

static ur_mw_logger_ctx_t logger_ctx = {
    .filter_signal = 0,
    .log_payload = true,
};

ur_register_middleware(&ent, ur_mw_logger, &logger_ctx, 0);
```

#### 防抖中间件

```c
typedef struct {
    uint16_t signal_id;      // 要防抖的信号
    uint32_t debounce_ms;    // 防抖时间
    uint32_t last_time;      // 上次触发时间
} ur_mw_debounce_ctx_t;

static ur_mw_debounce_ctx_t debounce_ctx = {
    .signal_id = SIG_BUTTON,
    .debounce_ms = 50,
    .last_time = 0,
};

ur_register_middleware(&ent, ur_mw_debounce, &debounce_ctx, 1);
```

#### 节流中间件

```c
typedef struct {
    uint16_t signal_id;      // 要节流的信号
    uint32_t interval_ms;    // 最小间隔
    uint32_t last_time;
    uint32_t count_dropped;
} ur_mw_throttle_ctx_t;

static ur_mw_throttle_ctx_t throttle_ctx = {
    .signal_id = SIG_SENSOR_DATA,
    .interval_ms = 100,  // 最多每 100ms 处理一次
};

ur_register_middleware(&ent, ur_mw_throttle, &throttle_ctx, 2);
```

### 6.4 自定义中间件示例

```c
// 示例：信号计数中间件
typedef struct {
    uint32_t total_count;
    uint32_t filtered_count;
} counter_ctx_t;

static counter_ctx_t counter = {0};

ur_mw_result_t counting_middleware(ur_entity_t *ent, ur_signal_t *sig, void *ctx) {
    counter_ctx_t *c = (counter_ctx_t *)ctx;
    c->total_count++;
    return UR_MW_CONTINUE;
}

// 示例：信号变换中间件（单位转换）
ur_mw_result_t unit_converter(ur_entity_t *ent, ur_signal_t *sig, void *ctx) {
    if (sig->id == SIG_TEMP_CELSIUS) {
        // 将摄氏度转为华氏度
        float celsius = (float)sig->payload.i32[0] / 10.0f;
        float fahrenheit = celsius * 9.0f / 5.0f + 32.0f;
        sig->payload.i32[0] = (int32_t)(fahrenheit * 10);
        sig->id = SIG_TEMP_FAHRENHEIT;
        return UR_MW_TRANSFORM;
    }
    return UR_MW_CONTINUE;
}

// 示例：权限检查中间件
typedef struct {
    uint16_t protected_signals[8];
    size_t count;
    bool authorized;
} auth_ctx_t;

ur_mw_result_t auth_middleware(ur_entity_t *ent, ur_signal_t *sig, void *ctx) {
    auth_ctx_t *auth = (auth_ctx_t *)ctx;

    // 检查是否是受保护的信号
    for (size_t i = 0; i < auth->count; i++) {
        if (sig->id == auth->protected_signals[i]) {
            if (!auth->authorized) {
                ESP_LOGW(TAG, "未授权访问信号 0x%04X", sig->id);
                return UR_MW_FILTERED;
            }
        }
    }
    return UR_MW_CONTINUE;
}
```

### 6.5 中间件管理

```c
// 注册中间件（优先级越低越先执行）
ur_register_middleware(&ent, my_middleware, context, priority);

// 注销中间件
ur_unregister_middleware(&ent, my_middleware);

// 启用/禁用中间件
ur_set_middleware_enabled(&ent, my_middleware, false);  // 禁用
ur_set_middleware_enabled(&ent, my_middleware, true);   // 启用
```

---

## 7. 混入系统

### 7.1 混入概念

混入（Mixin）提供与状态无关的信号处理能力，适合：
- 共享的通用行为（电源管理、错误处理）
- 跨状态的横切关注点
- 代码复用

### 7.2 定义混入

```c
// 电源管理混入
static uint16_t handle_power_off(ur_entity_t *ent, const ur_signal_t *sig) {
    ESP_LOGI(TAG, "[%s] 收到关机信号", ur_entity_name(ent));
    // 保存状态、清理资源...
    return 0;
}

static uint16_t handle_low_battery(ur_entity_t *ent, const ur_signal_t *sig) {
    uint8_t level = sig->payload.u8[0];
    ESP_LOGW(TAG, "[%s] 低电量警告: %d%%", ur_entity_name(ent), level);
    return 0;
}

static const ur_rule_t power_mixin_rules[] = {
    UR_RULE(SIG_POWER_OFF, 0, handle_power_off),
    UR_RULE(SIG_LOW_BATTERY, 0, handle_low_battery),
    UR_RULE(SIG_POWER_SAVE, 0, enter_power_save_mode),
    UR_RULE_END
};

static const ur_mixin_t power_mixin = {
    .name = "PowerMixin",
    .rules = power_mixin_rules,
    .rule_count = 3,
    .priority = 10,  // 优先级（越小越先检查）
};

// 错误处理混入
static const ur_rule_t error_mixin_rules[] = {
    UR_RULE(SIG_ERROR, 0, handle_error),
    UR_RULE(SIG_FATAL_ERROR, 0, handle_fatal),
    UR_RULE_END
};

static const ur_mixin_t error_mixin = {
    .name = "ErrorMixin",
    .rules = error_mixin_rules,
    .rule_count = 2,
    .priority = 5,  // 更高优先级
};
```

### 7.3 使用混入

```c
// 绑定混入到实体
ur_bind_mixin(&my_entity, &power_mixin);
ur_bind_mixin(&my_entity, &error_mixin);

// 解绑混入
ur_unbind_mixin(&my_entity, &power_mixin);

// 多个实体共享同一混入
ur_bind_mixin(&entity1, &power_mixin);
ur_bind_mixin(&entity2, &power_mixin);
ur_bind_mixin(&entity3, &power_mixin);
```

### 7.4 查找顺序

信号到达时，按以下顺序查找处理规则：

1. **当前状态规则** - 状态特定的处理
2. **混入规则**（按优先级排序）- 通用处理
3. **父状态规则**（HSM 模式）- 继承的处理

```
收到 SIG_LOW_BATTERY
        │
        ▼
┌──────────────────┐
│ STATE_RUNNING    │ ─── 没有匹配规则
│ 的规则           │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ ErrorMixin       │ ─── 没有匹配规则
│ (priority=5)     │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ PowerMixin       │ ─── 找到！执行 handle_low_battery
│ (priority=10)    │
└──────────────────┘
```

---

## 8. 数据管道

### 8.1 管道概念

数据管道用于高吞吐量数据流，基于 FreeRTOS StreamBuffer 实现：

- 适合音频、视频、传感器数据流
- 支持 ISR 安全写入
- 零拷贝设计

### 8.2 创建管道

```c
#include "ur_pipe.h"

// 方式一：手动定义
static uint8_t audio_buffer[1024];
static ur_pipe_t audio_pipe;

void init_pipe(void) {
    ur_pipe_init(&audio_pipe,
                 audio_buffer,
                 sizeof(audio_buffer),
                 64);  // trigger_level: 至少 64 字节才触发读取
}

// 方式二：使用宏定义
UR_PIPE_DEFINE(sensor_pipe, 512, 1);  // 名称, 大小, 触发级别

void init_pipes(void) {
    UR_PIPE_INIT(sensor_pipe);
}
```

### 8.3 写入数据

```c
// 任务上下文写入
size_t written = ur_pipe_write(&pipe, data, data_len, timeout_ms);

if (written < data_len) {
    ESP_LOGW(TAG, "管道已满，只写入 %d/%d 字节", written, data_len);
}

// 写入单字节
ur_err_t err = ur_pipe_write_byte(&pipe, byte_value);

// ISR 上下文写入
void IRAM_ATTR adc_isr(void *arg) {
    ur_pipe_t *pipe = (ur_pipe_t *)arg;
    int16_t sample = read_adc();

    BaseType_t woken = pdFALSE;
    ur_pipe_write_from_isr(pipe, &sample, sizeof(sample), &woken);

    portYIELD_FROM_ISR(woken);
}
```

### 8.4 读取数据

```c
// 阻塞读取
uint8_t buffer[256];
size_t read = ur_pipe_read(&pipe, buffer, sizeof(buffer), portMAX_DELAY);

// 非阻塞读取
size_t read = ur_pipe_read(&pipe, buffer, sizeof(buffer), 0);

// 读取单字节
uint8_t byte;
ur_err_t err = ur_pipe_read_byte(&pipe, &byte, timeout_ms);

// ISR 中读取
size_t read = ur_pipe_read_from_isr(&pipe, buffer, size, &woken);
```

### 8.5 状态查询

```c
// 可读取的字节数
size_t available = ur_pipe_available(&pipe);

// 可写入的空间
size_t space = ur_pipe_space(&pipe);

// 是否为空
if (ur_pipe_is_empty(&pipe)) {
    // 没有数据
}

// 是否已满
if (ur_pipe_is_full(&pipe)) {
    // 无法写入更多
}

// 获取总大小
size_t total = ur_pipe_get_size(&pipe);
```

### 8.6 完整示例：音频流

```c
// 音频配置
#define SAMPLE_RATE     16000
#define SAMPLE_SIZE     2       // 16-bit
#define BUFFER_MS       100     // 100ms 缓冲
#define BUFFER_SIZE     (SAMPLE_RATE * SAMPLE_SIZE * BUFFER_MS / 1000)

// 静态分配
static uint8_t audio_buffer[BUFFER_SIZE];
static ur_pipe_t audio_pipe;

// ADC 中断 - 生产者
static void IRAM_ATTR adc_isr_handler(void *arg) {
    int16_t sample = adc_read();
    BaseType_t woken = pdFALSE;

    ur_pipe_write_from_isr(&audio_pipe, &sample, sizeof(sample), &woken);

    portYIELD_FROM_ISR(woken);
}

// 音频处理任务 - 消费者
static void audio_task(void *param) {
    int16_t samples[64];

    while (1) {
        // 等待至少 64 个样本
        size_t read = ur_pipe_read(&audio_pipe, samples,
                                    sizeof(samples), portMAX_DELAY);

        if (read > 0) {
            process_audio(samples, read / sizeof(int16_t));
        }
    }
}

// 初始化
void audio_init(void) {
    ur_pipe_init(&audio_pipe, audio_buffer, sizeof(audio_buffer),
                 64 * sizeof(int16_t));  // 64 样本触发

    // 配置 ADC 定时器中断...
    xTaskCreate(audio_task, "audio", 4096, NULL, 10, NULL);
}
```

---

## 9. 分布式通信（虫洞）

### 9.1 虫洞概念

虫洞（Wormhole）实现跨芯片的透明信号路由，让分布式系统看起来像单机系统：

```
┌─────────────┐         UART          ┌─────────────┐
│   ESP32 A   │◀──────────────────────▶│   ESP32 B   │
│             │                        │             │
│  Entity 1   │    信号自动路由         │  Entity 100 │
│  Entity 2   │◀──────────────────────▶│  Entity 101 │
└─────────────┘                        └─────────────┘
```

### 9.2 协议格式

10 字节定长帧：

```
┌──────┬─────────┬─────────┬───────────┬──────┐
│ Sync │ SrcID   │ SigID   │ Payload   │ CRC8 │
│ 0xAA │ 2 bytes │ 2 bytes │ 4 bytes   │ 1B   │
└──────┴─────────┴─────────┴───────────┴──────┘
```

CRC8 覆盖 SrcID + SigID + Payload（字节 1-8）。

### 9.3 配置虫洞

```c
#include "ur_wormhole.h"

// 在 menuconfig 中启用：
// CONFIG_UR_WORMHOLE_ENABLE=y
// CONFIG_UR_WORMHOLE_UART_NUM=1
// CONFIG_UR_WORMHOLE_BAUD_RATE=115200

void setup_wormhole(void) {
    // 初始化（chip_id 用于标识本机）
    ur_wormhole_init(1);  // 芯片 ID = 1

    // 添加路由：本地实体 1 对应远程实体 100
    ur_wormhole_add_route(1, 100, UART_NUM_1);
    ur_wormhole_add_route(2, 101, UART_NUM_1);

    // 之后，发送到远程实体的信号会自动通过 UART 传输
}
```

### 9.4 发送远程信号

```c
// 方式一：通过路由表自动发送
ur_signal_t sig = ur_signal_create(SIG_COMMAND, local_entity.id);
ur_wormhole_send(100, sig);  // 发送到远程实体 100

// 方式二：使用中间件自动路由
// 注册虫洞 TX 中间件后，发送到本地代理实体的信号会自动转发
ur_register_middleware(&proxy_entity, ur_mw_wormhole_tx, NULL, 0);
ur_emit(&proxy_entity, sig);  // 自动转发到远程
```

### 9.5 接收远程信号

虫洞 RX 任务自动运行，收到的远程信号会注入到本地实体的收件箱：

```c
// 远程芯片发送：
ur_signal_t sig = ur_signal_create(SIG_SENSOR_DATA, remote_entity.id);
ur_wormhole_send(1, sig);  // 发送到远程实体 1

// 本地芯片收到后，信号自动进入 entity 1 的收件箱
// 本地的 entity 1 正常处理，就像是本地信号一样
```

### 9.6 多芯片系统示例

```
芯片 A（主控）              芯片 B（传感器）
  ┌──────────┐               ┌──────────┐
  │ Entity 1 │◀─────────────▶│ Entity 10│ 温度传感器
  │ 显示控制 │               │          │
  └──────────┘               └──────────┘
  ┌──────────┐               ┌──────────┐
  │ Entity 2 │◀─────────────▶│ Entity 11│ 湿度传感器
  │ 数据记录 │               │          │
  └──────────┘               └──────────┘
```

```c
// 芯片 A 配置
void chip_a_init(void) {
    ur_wormhole_init(0);  // 芯片 ID = 0

    ur_wormhole_add_route(1, 10, UART_NUM_1);  // Entity 1 <-> 远程 10
    ur_wormhole_add_route(2, 11, UART_NUM_1);  // Entity 2 <-> 远程 11
}

// 芯片 B 配置
void chip_b_init(void) {
    ur_wormhole_init(1);  // 芯片 ID = 1

    ur_wormhole_add_route(10, 1, UART_NUM_1);  // Entity 10 <-> 远程 1
    ur_wormhole_add_route(11, 2, UART_NUM_1);  // Entity 11 <-> 远程 2
}
```

---

## 10. 监督者与自愈

### 10.1 监督者概念

监督者模式来自 Erlang/OTP，提供：
- 实体故障检测
- 自动重启
- 重启次数限制

```
        ┌──────────────┐
        │  Supervisor  │
        │   Entity     │
        └──────┬───────┘
               │ 监控
    ┌──────────┼──────────┐
    │          │          │
    ▼          ▼          ▼
┌───────┐  ┌───────┐  ┌───────┐
│Child 1│  │Child 2│  │Child 3│
└───────┘  └───────┘  └───────┘
```

### 10.2 配置监督者

```c
#include "ur_supervisor.h"

// 在 menuconfig 中启用：
// CONFIG_UR_SUPERVISOR_ENABLE=y
// CONFIG_UR_SUPERVISOR_MAX_CHILDREN=8
// CONFIG_UR_SUPERVISOR_RESTART_DELAY_MS=100

static ur_entity_t supervisor;
static ur_entity_t worker1, worker2, worker3;

void setup_supervisor(void) {
    // 初始化监督者实体
    ur_entity_config_t sup_cfg = {
        .id = 1,
        .name = "Supervisor",
        .states = supervisor_states,
        .state_count = 1,
        .initial_state = STATE_SUPERVISING,
    };
    ur_init(&supervisor, &sup_cfg);
    ur_start(&supervisor);

    // 创建监督者（最多重启 3 次）
    ur_supervisor_create(&supervisor, 3);

    // 初始化并添加子实体
    init_worker(&worker1, 2, "Worker1");
    init_worker(&worker2, 3, "Worker2");
    init_worker(&worker3, 4, "Worker3");

    ur_supervisor_add_child(&supervisor, &worker1);
    ur_supervisor_add_child(&supervisor, &worker2);
    ur_supervisor_add_child(&supervisor, &worker3);
}
```

### 10.3 报告故障

```c
// 工作实体检测到错误时报告
static uint16_t handle_error(ur_entity_t *ent, const ur_signal_t *sig) {
    uint32_t error_code = sig->payload.u32[0];

    ESP_LOGE(TAG, "[%s] 发生错误: 0x%08X", ur_entity_name(ent), error_code);

    // 报告故障，触发监督者重启流程
    ur_report_dying(ent, error_code);

    return 0;
}

// 或者手动触发
void trigger_failure(ur_entity_t *ent) {
    ur_report_dying(ent, ERROR_CODE_WATCHDOG_TIMEOUT);
}
```

### 10.4 重启流程

当子实体报告故障：

1. 发送 `SIG_SYS_DYING` 到监督者
2. 监督者检查重启次数
3. 如果未超限，延迟后重启子实体
4. 重启包括：`ur_stop()` → `ur_start()` → 发送 `SIG_SYS_REVIVE`

```c
// 子实体可以处理 SIG_SYS_REVIVE 进行恢复初始化
static const ur_rule_t worker_rules[] = {
    UR_RULE(SIG_SYS_REVIVE, STATE_IDLE, handle_revive),
    // ...其他规则
    UR_RULE_END
};

static uint16_t handle_revive(ur_entity_t *ent, const ur_signal_t *sig) {
    ESP_LOGI(TAG, "[%s] 已重启恢复", ur_entity_name(ent));
    // 重新初始化资源...
    return 0;
}
```

### 10.5 查询和重置

```c
// 获取重启次数
uint8_t count = ur_get_restart_count(&worker1);
ESP_LOGI(TAG, "Worker1 已重启 %d 次", count);

// 重置重启计数（例如在成功运行一段时间后）
ur_reset_restart_count(&worker1);

// 移除子实体
ur_supervisor_remove_child(&supervisor, &worker1);
```

---

## 11. 实战案例

### 11.1 智能灯控制器

```c
/**
 * 智能灯：支持开关、调光、定时关闭
 */

// 信号定义
enum {
    SIG_BUTTON_SHORT = SIG_USER_BASE,  // 短按
    SIG_BUTTON_LONG,                    // 长按
    SIG_BRIGHTNESS_UP,                  // 亮度+
    SIG_BRIGHTNESS_DOWN,                // 亮度-
    SIG_TIMER_TICK,                     // 定时器 tick
};

// 状态定义
enum {
    STATE_OFF = 1,
    STATE_ON,
    STATE_DIMMING,      // 调光中
    STATE_TIMER_ACTIVE, // 定时关闭中
};

// 暂存区
typedef struct {
    uint8_t brightness;      // 当前亮度 0-100
    uint8_t target_brightness;
    uint32_t timer_remaining; // 剩余时间（秒）
} light_scratch_t;

// 动作函数
static uint16_t turn_on(ur_entity_t *ent, const ur_signal_t *sig) {
    light_scratch_t *s = UR_SCRATCH_PTR(ent, light_scratch_t);
    s->brightness = 100;
    set_pwm_duty(s->brightness);
    ESP_LOGI(TAG, "灯已打开");
    return 0;
}

static uint16_t turn_off(ur_entity_t *ent, const ur_signal_t *sig) {
    light_scratch_t *s = UR_SCRATCH_PTR(ent, light_scratch_t);
    s->brightness = 0;
    set_pwm_duty(0);
    ESP_LOGI(TAG, "灯已关闭");
    return 0;
}

static uint16_t start_timer(ur_entity_t *ent, const ur_signal_t *sig) {
    light_scratch_t *s = UR_SCRATCH_PTR(ent, light_scratch_t);
    s->timer_remaining = 300;  // 5 分钟
    ESP_LOGI(TAG, "定时关闭已启动：%d 秒", s->timer_remaining);
    return STATE_TIMER_ACTIVE;
}

static uint16_t timer_tick(ur_entity_t *ent, const ur_signal_t *sig) {
    light_scratch_t *s = UR_SCRATCH_PTR(ent, light_scratch_t);

    if (s->timer_remaining > 0) {
        s->timer_remaining--;
        if (s->timer_remaining == 0) {
            ESP_LOGI(TAG, "定时结束，关灯");
            return STATE_OFF;
        }
    }
    return 0;
}

// 调光协程
static uint16_t dimming_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    light_scratch_t *s = UR_SCRATCH_PTR(ent, light_scratch_t);

    UR_FLOW_BEGIN(ent);

    while (s->brightness != s->target_brightness) {
        if (s->brightness < s->target_brightness) {
            s->brightness++;
        } else {
            s->brightness--;
        }
        set_pwm_duty(s->brightness);

        UR_AWAIT_TIME(ent, 20);  // 20ms 步进
    }

    ESP_LOGI(TAG, "调光完成：%d%%", s->brightness);
    UR_FLOW_GOTO(ent, STATE_ON);

    UR_FLOW_END(ent);
}

// 状态规则
static const ur_rule_t off_rules[] = {
    UR_RULE(SIG_BUTTON_SHORT, STATE_ON, turn_on),
    UR_RULE_END
};

static const ur_rule_t on_rules[] = {
    UR_RULE(SIG_BUTTON_SHORT, STATE_OFF, turn_off),
    UR_RULE(SIG_BUTTON_LONG, STATE_TIMER_ACTIVE, start_timer),
    UR_RULE(SIG_BRIGHTNESS_UP, STATE_DIMMING, start_dim_up),
    UR_RULE(SIG_BRIGHTNESS_DOWN, STATE_DIMMING, start_dim_down),
    UR_RULE_END
};

static const ur_rule_t dimming_rules[] = {
    UR_RULE(SIG_TIMER_TICK, 0, dimming_flow),  // 用 tick 驱动协程
    UR_RULE(SIG_BUTTON_SHORT, STATE_ON, NULL), // 取消调光
    UR_RULE_END
};

static const ur_rule_t timer_rules[] = {
    UR_RULE(SIG_TIMER_TICK, 0, timer_tick),
    UR_RULE(SIG_BUTTON_SHORT, STATE_ON, cancel_timer),
    UR_RULE_END
};

static const ur_state_def_t light_states[] = {
    UR_STATE(STATE_OFF, 0, NULL, turn_off, off_rules),
    UR_STATE(STATE_ON, 0, NULL, NULL, on_rules),
    UR_STATE(STATE_DIMMING, STATE_ON, NULL, NULL, dimming_rules),
    UR_STATE(STATE_TIMER_ACTIVE, STATE_ON, NULL, NULL, timer_rules),
};
```

### 11.2 传感器数据采集系统

```c
/**
 * 多传感器系统：采集、处理、上报
 */

// 实体定义
static ur_entity_t sensor_mgr;    // 传感器管理器
static ur_entity_t data_logger;   // 数据记录器
static ur_entity_t uploader;      // 数据上传器

// 传感器管理器状态
enum {
    STATE_MGR_IDLE = 1,
    STATE_MGR_SAMPLING,
    STATE_MGR_ERROR,
};

// 信号
enum {
    SIG_START_SAMPLE = SIG_USER_BASE,
    SIG_SAMPLE_DONE,
    SIG_DATA_READY,
    SIG_UPLOAD_OK,
    SIG_UPLOAD_FAIL,
    SIG_RETRY,
};

// 采样数据结构
typedef struct {
    float temperature;
    float humidity;
    float pressure;
    uint32_t timestamp;
} sensor_data_t;

static sensor_data_t current_data;

// 传感器管理器 - 采样协程
static uint16_t sampling_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    UR_FLOW_BEGIN(ent);

    ESP_LOGI(TAG, "开始采样...");

    // 采集温度
    current_data.temperature = read_temperature_sensor();
    UR_AWAIT_TIME(ent, 100);  // 传感器稳定时间

    // 采集湿度
    current_data.humidity = read_humidity_sensor();
    UR_AWAIT_TIME(ent, 100);

    // 采集气压
    current_data.pressure = read_pressure_sensor();
    current_data.timestamp = ur_time_ms();

    ESP_LOGI(TAG, "采样完成: T=%.1f H=%.1f P=%.1f",
             current_data.temperature,
             current_data.humidity,
             current_data.pressure);

    // 通知数据记录器
    ur_signal_t data_sig = ur_signal_create_ptr(SIG_DATA_READY, ent->id, &current_data);
    ur_emit(&data_logger, data_sig);
    ur_emit(&uploader, data_sig);

    UR_FLOW_GOTO(ent, STATE_MGR_IDLE);

    UR_FLOW_END(ent);
}

// 上传器 - 带重试的上传
typedef struct {
    uint8_t retry_count;
    sensor_data_t data_to_upload;
} uploader_scratch_t;

static uint16_t upload_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    uploader_scratch_t *s = UR_SCRATCH_PTR(ent, uploader_scratch_t);

    UR_FLOW_BEGIN(ent);

    // 保存要上传的数据
    if (sig->ptr != NULL) {
        memcpy(&s->data_to_upload, sig->ptr, sizeof(sensor_data_t));
    }
    s->retry_count = 0;

    while (s->retry_count < 3) {
        ESP_LOGI(TAG, "尝试上传 (第 %d 次)...", s->retry_count + 1);

        bool success = http_post_data(&s->data_to_upload);

        if (success) {
            ESP_LOGI(TAG, "上传成功！");
            ur_emit(&sensor_mgr, ur_signal_create(SIG_UPLOAD_OK, ent->id));
            UR_FLOW_GOTO(ent, STATE_UPLOAD_IDLE);
        }

        s->retry_count++;
        ESP_LOGW(TAG, "上传失败，等待重试...");

        // 指数退避
        UR_AWAIT_TIME(ent, 1000 * (1 << s->retry_count));
    }

    ESP_LOGE(TAG, "上传失败，已达最大重试次数");
    ur_emit(&sensor_mgr, ur_signal_create(SIG_UPLOAD_FAIL, ent->id));

    UR_FLOW_END(ent);
}
```

---

## 12. 最佳实践

### 12.1 内存管理

```c
// ✅ 好：静态分配
static ur_entity_t entities[MAX_ENTITIES];
static uint8_t pipe_buffer[BUFFER_SIZE];

// ❌ 坏：动态分配
ur_entity_t *ent = malloc(sizeof(ur_entity_t));  // 避免！

// ✅ 好：信号指针指向静态数据
static sensor_data_t shared_data;
sig.ptr = &shared_data;

// ❌ 坏：信号指针指向栈数据
void send_data(void) {
    sensor_data_t local_data;  // 栈上！
    sig.ptr = &local_data;     // 危险！函数返回后无效
    ur_emit(&target, sig);
}
```

### 12.2 状态机设计

```c
// ✅ 好：状态职责单一
enum {
    STATE_CONNECTING,    // 只负责连接
    STATE_AUTHENTICATING, // 只负责认证
    STATE_READY,         // 只负责正常操作
};

// ❌ 坏：状态职责混乱
enum {
    STATE_INIT_AND_CONNECT_AND_AUTH,  // 太多职责
};

// ✅ 好：使用 HSM 复用共同行为
static const ur_state_def_t states[] = {
    UR_STATE(STATE_ACTIVE, 0, NULL, NULL, active_rules),        // 父状态
    UR_STATE(STATE_NORMAL, STATE_ACTIVE, NULL, NULL, normal_rules),
    UR_STATE(STATE_TURBO, STATE_ACTIVE, NULL, NULL, turbo_rules),
};

// ✅ 好：状态进入/退出动作清理资源
UR_STATE(STATE_CONNECTED, 0, open_connection, close_connection, rules)
```

### 12.3 信号设计

```c
// ✅ 好：信号语义清晰
enum {
    SIG_BUTTON_PRESSED,
    SIG_BUTTON_RELEASED,
    SIG_TEMPERATURE_CHANGED,
    SIG_CONNECTION_LOST,
};

// ❌ 坏：信号语义模糊
enum {
    SIG_EVENT1,
    SIG_DATA,
    SIG_UPDATE,
};

// ✅ 好：使用载荷传递数据
sig.payload.u16[0] = temperature;
sig.payload.u16[1] = humidity;

// ❌ 坏：用多个信号传递相关数据
ur_emit(&target, temp_signal);
ur_emit(&target, humidity_signal);  // 可能乱序！
```

### 12.4 协程使用

```c
// ✅ 好：简单线性流程用协程
uint16_t init_sequence(ur_entity_t *ent, const ur_signal_t *sig) {
    UR_FLOW_BEGIN(ent);

    init_hardware();
    UR_AWAIT_TIME(ent, 100);

    calibrate_sensor();
    UR_AWAIT_SIGNAL(ent, SIG_CALIBRATION_DONE);

    start_normal_operation();

    UR_FLOW_END(ent);
}

// ❌ 坏：复杂分支逻辑用协程
uint16_t complex_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    UR_FLOW_BEGIN(ent);

    // 复杂的嵌套条件和循环...
    // 应该拆分成多个状态！

    UR_FLOW_END(ent);
}
```

### 12.5 中间件使用

```c
// ✅ 好：中间件职责单一
ur_register_middleware(&ent, logger_mw, NULL, 0);
ur_register_middleware(&ent, debounce_mw, &debounce_ctx, 1);
ur_register_middleware(&ent, auth_mw, &auth_ctx, 2);

// ✅ 好：中间件优先级合理
// 0: 日志（最先，记录所有）
// 1: 认证（过滤未授权）
// 2: 防抖（减少重复）
// 3: 业务处理

// ❌ 坏：在中间件中做太多事
ur_mw_result_t bad_middleware(ur_entity_t *ent, ur_signal_t *sig, void *ctx) {
    log_signal(sig);
    check_auth(sig);
    debounce(sig);
    transform(sig);
    // 应该拆分成多个中间件！
    return UR_MW_CONTINUE;
}
```

---

## 13. 常见问题

### Q1: 信号丢失怎么办？

**原因**：收件箱已满

**解决方案**：
1. 增大 `CONFIG_UR_INBOX_SIZE`
2. 提高调度频率
3. 使用中间件过滤不必要的信号
4. 检查是否有信号风暴

```c
// 监控收件箱
if (ur_inbox_count(&ent) > UR_CFG_INBOX_SIZE * 0.8) {
    ESP_LOGW(TAG, "收件箱接近满！");
}
```

### Q2: 协程不工作？

**检查清单**：
1. 是否用了 `UR_FLOW_BEGIN/END`？
2. 是否有 `switch` 语句？（不支持）
3. 局部变量是否放在暂存区？
4. 是否有信号驱动协程？

```c
// 协程需要信号驱动
static const ur_rule_t rules[] = {
    UR_RULE(SIG_TICK, 0, my_flow_action),  // 用 tick 驱动
    UR_RULE_END
};
```

### Q3: HSM 父状态规则不触发？

**检查**：
1. 确认 `CONFIG_UR_ENABLE_HSM=y`
2. 检查 `parent_id` 是否正确设置
3. 检查子状态是否有同信号的规则（会覆盖）

### Q4: 中断中发送信号失败？

**确保**：
1. 使用 `ur_emit_from_isr()` 而不是 `ur_emit()`
2. 函数标记为 `IRAM_ATTR`
3. 处理 `woken` 标志

```c
void IRAM_ATTR my_isr(void *arg) {
    BaseType_t woken = pdFALSE;
    ur_emit_from_isr(ent, sig, &woken);
    portYIELD_FROM_ISR(woken);  // 重要！
}
```

### Q5: 如何调试状态机？

```c
// 启用日志
// CONFIG_UR_ENABLE_LOGGING=y
// CONFIG_UR_LOG_LEVEL=4

// 使用日志中间件
ur_register_middleware(&ent, ur_mw_logger, &logger_ctx, 0);

// 注册 panic 钩子查看历史
void my_panic_hook(const char *reason,
                   const ur_blackbox_entry_t *history,
                   size_t count) {
    for (size_t i = 0; i < count; i++) {
        ESP_LOGE(TAG, "[%d] ent=%d sig=0x%04X state=%d",
                 i, history[i].entity_id,
                 history[i].signal_id,
                 history[i].state);
    }
}

ur_panic_set_hook(my_panic_hook);
```

### Q6: 内存不够用？

**优化建议**：
1. 减小 `CONFIG_UR_INBOX_SIZE`
2. 减小 `CONFIG_UR_SCRATCHPAD_SIZE`
3. 减少 `CONFIG_UR_MAX_MIDDLEWARE`
4. 禁用不需要的功能（HSM、Wormhole、Supervisor）

---

## 附录：配置项速查

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `UR_MAX_ENTITIES` | 16 | 最大实体数 |
| `UR_MAX_RULES_PER_STATE` | 16 | 每状态最大规则数 |
| `UR_MAX_STATES_PER_ENTITY` | 16 | 每实体最大状态数 |
| `UR_MAX_MIXINS_PER_ENTITY` | 4 | 每实体最大混入数 |
| `UR_INBOX_SIZE` | 8 | 收件箱队列大小 |
| `UR_SCRATCHPAD_SIZE` | 64 | 协程暂存区大小 |
| `UR_MAX_MIDDLEWARE` | 8 | 最大中间件数 |
| `UR_ENABLE_HSM` | y | 启用层级状态机 |
| `UR_ENABLE_LOGGING` | n | 启用调试日志 |
| `UR_ENABLE_TIMESTAMPS` | y | 启用信号时间戳 |
| `UR_WORMHOLE_ENABLE` | n | 启用虫洞 |
| `UR_SUPERVISOR_ENABLE` | n | 启用监督者 |
| `UR_PANIC_ENABLE` | y | 启用 panic 处理 |
| `UR_PIPE_ENABLE` | y | 启用数据管道 |

---

**文档版本**：v3.0
**最后更新**：2026年1月
