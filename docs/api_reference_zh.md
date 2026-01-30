# MicroReactor API 参考手册

本文档提供 MicroReactor 框架所有公开 API 的详细说明。

## 目录

1. [核心 API (ur_core.h)](#1-核心-api)
2. [类型定义 (ur_types.h)](#2-类型定义)
3. [协程 API (ur_flow.h)](#3-协程-api)
4. [管道 API (ur_pipe.h)](#4-管道-api)
5. [工具函数 (ur_utils.h)](#5-工具函数)
6. [中间件 (ur_transducers.c)](#6-中间件)
7. [虫洞 API (ur_wormhole.c)](#7-虫洞-api)
8. [监督者 API (ur_supervisor.c)](#8-监督者-api)
9. [异常处理 (ur_panic.c)](#9-异常处理)

---

## 1. 核心 API

### 实体管理

#### ur_init
```c
ur_err_t ur_init(ur_entity_t *ent, const ur_entity_config_t *config);
```
初始化实体控制块。

**参数**：
- `ent`: 实体指针（必须是静态分配）
- `config`: 配置结构体

**返回**：`UR_OK` 成功，错误码失败

**示例**：
```c
static ur_entity_t my_entity;
ur_entity_config_t cfg = {
    .id = 1,
    .name = "MyEntity",
    .states = my_states,
    .state_count = 3,
    .initial_state = STATE_IDLE,
};
ur_init(&my_entity, &cfg);
```

---

#### ur_start
```c
ur_err_t ur_start(ur_entity_t *ent);
```
启动实体，进入初始状态并发送 `SIG_SYS_INIT`。

---

#### ur_stop
```c
ur_err_t ur_stop(ur_entity_t *ent);
```
停止实体，执行退出动作并清空收件箱。

---

#### ur_suspend / ur_resume
```c
ur_err_t ur_suspend(ur_entity_t *ent);
ur_err_t ur_resume(ur_entity_t *ent);
```
暂停/恢复实体的信号处理。

---

### 信号发送

#### ur_emit
```c
ur_err_t ur_emit(ur_entity_t *target, ur_signal_t sig);
```
发送信号到目标实体。自动检测 ISR 上下文。

**返回**：
- `UR_OK`: 成功
- `UR_ERR_QUEUE_FULL`: 收件箱已满

---

#### ur_emit_from_isr
```c
ur_err_t ur_emit_from_isr(ur_entity_t *target, ur_signal_t sig, BaseType_t *pxWoken);
```
从中断上下文发送信号。

**参数**：
- `pxWoken`: 如果唤醒了更高优先级任务，设为 `pdTRUE`

---

#### ur_emit_to_id
```c
ur_err_t ur_emit_to_id(uint16_t target_id, ur_signal_t sig);
```
通过实体 ID 发送信号（需要注册实体）。

---

#### ur_broadcast
```c
int ur_broadcast(ur_signal_t sig);
```
广播信号到所有注册的实体。

**返回**：接收成功的实体数量

---

### 调度循环

#### ur_dispatch
```c
ur_err_t ur_dispatch(ur_entity_t *ent, uint32_t timeout_ms);
```
处理一个待处理信号。

**参数**：
- `timeout_ms`: 等待超时（0=不等待，`portMAX_DELAY`=永久等待）

**返回**：
- `UR_OK`: 处理了一个信号
- `UR_ERR_TIMEOUT`: 超时无信号

---

#### ur_dispatch_all
```c
int ur_dispatch_all(ur_entity_t *ent);
```
处理所有待处理信号。

**返回**：处理的信号数量

---

#### ur_dispatch_multi
```c
int ur_dispatch_multi(ur_entity_t **entities, size_t count);
```
轮询处理多个实体。

---

### 状态管理

#### ur_get_state
```c
uint16_t ur_get_state(const ur_entity_t *ent);
```
获取当前状态 ID。

---

#### ur_set_state
```c
ur_err_t ur_set_state(ur_entity_t *ent, uint16_t state_id);
```
强制状态转换（绕过规则匹配）。

---

#### ur_in_state
```c
bool ur_in_state(const ur_entity_t *ent, uint16_t state_id);
```
检查是否在指定状态（HSM 模式下也检查父状态）。

---

### 混入管理

#### ur_bind_mixin
```c
ur_err_t ur_bind_mixin(ur_entity_t *ent, const ur_mixin_t *mixin);
```
绑定混入到实体。

---

#### ur_unbind_mixin
```c
ur_err_t ur_unbind_mixin(ur_entity_t *ent, const ur_mixin_t *mixin);
```
解绑混入。

---

### 中间件管理

#### ur_register_middleware
```c
ur_err_t ur_register_middleware(
    ur_entity_t *ent,
    ur_middleware_fn_t fn,
    void *ctx,
    uint8_t priority
);
```
注册中间件。优先级越低越先执行。

---

#### ur_unregister_middleware
```c
ur_err_t ur_unregister_middleware(ur_entity_t *ent, ur_middleware_fn_t fn);
```
注销中间件。

---

#### ur_set_middleware_enabled
```c
ur_err_t ur_set_middleware_enabled(ur_entity_t *ent, ur_middleware_fn_t fn, bool enabled);
```
启用/禁用中间件。

---

### 实体注册表

#### ur_register_entity / ur_unregister_entity
```c
ur_err_t ur_register_entity(ur_entity_t *ent);
ur_err_t ur_unregister_entity(ur_entity_t *ent);
```
在全局注册表中注册/注销实体。

---

#### ur_get_entity
```c
ur_entity_t *ur_get_entity(uint16_t id);
```
通过 ID 查找实体。

---

### 工具函数

#### ur_inbox_count / ur_inbox_empty / ur_inbox_clear
```c
size_t ur_inbox_count(const ur_entity_t *ent);
bool ur_inbox_empty(const ur_entity_t *ent);
void ur_inbox_clear(ur_entity_t *ent);
```
收件箱查询和操作。

---

#### ur_in_isr
```c
bool ur_in_isr(void);
```
检查当前是否在 ISR 上下文。

---

#### ur_get_time_ms
```c
uint32_t ur_get_time_ms(void);
```
获取当前时间戳（毫秒）。

---

## 2. 类型定义

### ur_signal_t
```c
struct ur_signal_s {
    uint16_t id;           // 信号 ID
    uint16_t src_id;       // 源实体 ID
    union {
        uint8_t  u8[4];
        uint16_t u16[2];
        uint32_t u32[1];
        float    f32;
    } payload;             // 4 字节载荷
    void *ptr;             // 扩展数据指针
    uint32_t timestamp;    // 时间戳
};
```

### ur_rule_t
```c
struct ur_rule_s {
    uint16_t signal_id;    // 触发信号
    uint16_t next_state;   // 目标状态（0=不转换）
    ur_action_fn_t action; // 动作函数
};
```

### ur_state_def_t
```c
struct ur_state_def_s {
    uint16_t id;           // 状态 ID
    uint16_t parent_id;    // 父状态 ID（HSM）
    ur_action_fn_t on_entry;
    ur_action_fn_t on_exit;
    const ur_rule_t *rules;
    uint8_t rule_count;
};
```

### ur_mixin_t
```c
struct ur_mixin_s {
    const char *name;
    const ur_rule_t *rules;
    uint8_t rule_count;
    uint8_t priority;
};
```

### ur_middleware_t
```c
typedef ur_mw_result_t (*ur_middleware_fn_t)(
    ur_entity_t *ent,
    ur_signal_t *sig,
    void *ctx
);
```

### ur_mw_result_t
```c
typedef enum {
    UR_MW_CONTINUE = 0,  // 继续传递
    UR_MW_HANDLED,       // 已处理，停止
    UR_MW_FILTERED,      // 过滤丢弃
    UR_MW_TRANSFORM,     // 已变换，继续
} ur_mw_result_t;
```

### ur_err_t
```c
typedef enum {
    UR_OK = 0,
    UR_ERR_INVALID_ARG,
    UR_ERR_NO_MEMORY,
    UR_ERR_QUEUE_FULL,
    UR_ERR_NOT_FOUND,
    UR_ERR_INVALID_STATE,
    UR_ERR_TIMEOUT,
    UR_ERR_ALREADY_EXISTS,
    UR_ERR_DISABLED,
} ur_err_t;
```

---

## 3. 协程 API

### 基本宏

#### UR_FLOW_BEGIN / UR_FLOW_END
```c
UR_FLOW_BEGIN(ent);
// ... 协程代码 ...
UR_FLOW_END(ent);
```
协程开始和结束标记。

---

#### UR_YIELD
```c
UR_YIELD(ent);
```
让出执行，下次任意信号时恢复。

---

#### UR_AWAIT_SIGNAL
```c
UR_AWAIT_SIGNAL(ent, signal_id);
```
等待特定信号。恢复后 `sig` 参数包含收到的信号。

---

#### UR_AWAIT_ANY
```c
UR_AWAIT_ANY(ent, SIG_A, SIG_B, SIG_C);
```
等待多个信号之一。

---

#### UR_AWAIT_TIME
```c
UR_AWAIT_TIME(ent, milliseconds);
```
等待指定时间。

---

#### UR_AWAIT_COND
```c
UR_AWAIT_COND(ent, condition);
```
等待条件为真。

---

#### UR_FLOW_GOTO
```c
UR_FLOW_GOTO(ent, state_id);
```
转到其他状态并退出协程。

---

#### UR_FLOW_RESET
```c
UR_FLOW_RESET(ent);
```
重置协程到开始位置。

---

### 暂存区宏

#### UR_SCRATCH_PTR
```c
my_type *ptr = UR_SCRATCH_PTR(ent, my_type);
```
获取类型化的暂存区指针。

---

#### UR_SCRATCH_CLEAR
```c
UR_SCRATCH_CLEAR(ent);
```
清零暂存区。

---

#### UR_SCRATCH_STATIC_ASSERT
```c
UR_SCRATCH_STATIC_ASSERT(my_type);
```
静态断言类型不超过暂存区大小。

---

### 状态查询宏

```c
UR_FLOW_IS_RUNNING(ent)      // 协程是否运行中
UR_FLOW_IS_WAITING_SIGNAL(ent) // 是否等待信号
UR_FLOW_IS_WAITING_TIME(ent)   // 是否等待时间
UR_FLOW_GET_WAIT_SIGNAL(ent)   // 获取等待的信号 ID
```

---

## 4. 管道 API

### ur_pipe_init
```c
ur_err_t ur_pipe_init(
    ur_pipe_t *pipe,
    uint8_t *buffer,
    size_t buffer_size,
    size_t trigger_level
);
```
初始化管道。

**参数**：
- `buffer`: 静态缓冲区
- `buffer_size`: 缓冲区大小
- `trigger_level`: 触发阻塞读取的最小字节数

---

### ur_pipe_reset
```c
ur_err_t ur_pipe_reset(ur_pipe_t *pipe);
```
重置管道到空状态。

---

### 写入操作

```c
size_t ur_pipe_write(ur_pipe_t *pipe, const void *data, size_t size, uint32_t timeout_ms);
size_t ur_pipe_write_from_isr(ur_pipe_t *pipe, const void *data, size_t size, BaseType_t *pxWoken);
ur_err_t ur_pipe_write_byte(ur_pipe_t *pipe, uint8_t byte);
```

---

### 读取操作

```c
size_t ur_pipe_read(ur_pipe_t *pipe, void *buffer, size_t size, uint32_t timeout_ms);
size_t ur_pipe_read_from_isr(ur_pipe_t *pipe, void *buffer, size_t size, BaseType_t *pxWoken);
ur_err_t ur_pipe_read_byte(ur_pipe_t *pipe, uint8_t *byte, uint32_t timeout_ms);
```

---

### 状态查询

```c
size_t ur_pipe_available(const ur_pipe_t *pipe);  // 可读字节数
size_t ur_pipe_space(const ur_pipe_t *pipe);      // 可写空间
bool ur_pipe_is_empty(const ur_pipe_t *pipe);
bool ur_pipe_is_full(const ur_pipe_t *pipe);
size_t ur_pipe_get_size(const ur_pipe_t *pipe);
```

---

### 便捷宏

```c
UR_PIPE_DEFINE(name, size, trigger);  // 定义静态管道
UR_PIPE_INIT(name);                   // 初始化定义的管道
```

---

## 5. 工具函数

### CRC8

```c
uint8_t ur_crc8(const uint8_t *data, size_t len);
uint8_t ur_crc8_update(uint8_t crc, uint8_t byte);
```

---

### 时间函数

```c
uint32_t ur_time_ms(void);
uint64_t ur_time_us(void);
bool ur_time_elapsed(uint32_t start, uint32_t duration);
uint32_t ur_time_diff(uint32_t start, uint32_t end);
```

---

### 信号创建

```c
ur_signal_t ur_signal_create(uint16_t id, uint16_t src_id);
ur_signal_t ur_signal_create_u32(uint16_t id, uint16_t src_id, uint32_t payload);
ur_signal_t ur_signal_create_ptr(uint16_t id, uint16_t src_id, void *ptr);
void ur_signal_copy(ur_signal_t *dst, const ur_signal_t *src);
```

---

### 名称函数（可覆盖）

```c
const char *ur_entity_name(const ur_entity_t *ent);
const char *ur_state_name(const ur_entity_t *ent, uint16_t state_id);  // weak
const char *ur_signal_name(uint16_t signal_id);  // weak
```

---

### 辅助宏

```c
UR_SIGNAL_U32(id, src, val)    // 创建带 u32 载荷的信号
UR_SIGNAL_PTR(id, src, ptr)    // 创建带指针的信号
UR_RULE(sig, next, action)     // 定义规则
UR_RULE_END                    // 规则数组结束标记
UR_STATE(id, parent, entry, exit, rules)  // 定义状态
UR_SCRATCH(ent, type)          // 获取暂存区指针
UR_ARRAY_SIZE(arr)             // 数组大小
UR_MIN(a, b)
UR_MAX(a, b)
UR_CLAMP(x, lo, hi)
```

---

## 6. 中间件

### ur_mw_logger
```c
ur_mw_result_t ur_mw_logger(ur_entity_t *ent, ur_signal_t *sig, void *ctx);
```
信号日志中间件。

**上下文**：
```c
typedef struct {
    uint16_t filter_signal;  // 只记录此信号（0=全部）
    bool log_payload;        // 是否记录载荷
} ur_mw_logger_ctx_t;
```

---

### ur_mw_debounce
```c
ur_mw_result_t ur_mw_debounce(ur_entity_t *ent, ur_signal_t *sig, void *ctx);
```
防抖中间件。

**上下文**：
```c
typedef struct {
    uint16_t signal_id;      // 要防抖的信号
    uint32_t debounce_ms;    // 防抖时间
    uint32_t last_time;      // 内部状态
} ur_mw_debounce_ctx_t;
```

---

### ur_mw_throttle
```c
ur_mw_result_t ur_mw_throttle(ur_entity_t *ent, ur_signal_t *sig, void *ctx);
```
节流中间件。

**上下文**：
```c
typedef struct {
    uint16_t signal_id;
    uint32_t interval_ms;
    uint32_t last_time;
    uint32_t count_dropped;
} ur_mw_throttle_ctx_t;
```

---

### ur_mw_filter
```c
ur_mw_result_t ur_mw_filter(ur_entity_t *ent, ur_signal_t *sig, void *ctx);
```
自定义过滤中间件。

**上下文**：
```c
typedef bool (*ur_filter_predicate_t)(const ur_entity_t *, const ur_signal_t *, void *);

typedef struct {
    ur_filter_predicate_t predicate;
    void *user_data;
    bool invert;
} ur_mw_filter_ctx_t;
```

---

## 7. 虫洞 API

### ur_wormhole_init
```c
ur_err_t ur_wormhole_init(uint8_t chip_id);
```
初始化虫洞子系统。

---

### ur_wormhole_deinit
```c
ur_err_t ur_wormhole_deinit(void);
```
关闭虫洞。

---

### 路由管理

```c
ur_err_t ur_wormhole_add_route(uint16_t local_id, uint16_t remote_id, uint8_t channel);
ur_err_t ur_wormhole_remove_route(uint16_t local_id, uint16_t remote_id);
```

---

### ur_wormhole_send
```c
ur_err_t ur_wormhole_send(uint16_t remote_id, ur_signal_t sig);
```
发送信号到远程实体。

---

### ur_mw_wormhole_tx
```c
ur_mw_result_t ur_mw_wormhole_tx(ur_entity_t *ent, ur_signal_t *sig, void *ctx);
```
虫洞发送中间件。

---

## 8. 监督者 API

### ur_supervisor_create
```c
ur_err_t ur_supervisor_create(ur_entity_t *supervisor_ent, uint8_t max_restarts);
```
创建监督者。

---

### 子实体管理

```c
ur_err_t ur_supervisor_add_child(ur_entity_t *supervisor_ent, ur_entity_t *child_ent);
ur_err_t ur_supervisor_remove_child(ur_entity_t *supervisor_ent, ur_entity_t *child_ent);
```

---

### ur_reset_entity
```c
ur_err_t ur_reset_entity(ur_entity_t *ent);
```
软重置实体（不停止/启动）。

---

### ur_report_dying
```c
ur_err_t ur_report_dying(ur_entity_t *ent, uint32_t reason);
```
报告实体故障，触发监督者重启流程。

---

### 重启计数

```c
uint8_t ur_get_restart_count(ur_entity_t *ent);
ur_err_t ur_reset_restart_count(ur_entity_t *ent);
```

---

## 9. 异常处理

### 黑盒记录

```c
void ur_blackbox_record(const ur_entity_t *ent, const ur_signal_t *sig);
size_t ur_blackbox_get_history(ur_blackbox_entry_t *history, size_t max_count);
void ur_blackbox_clear(void);
size_t ur_blackbox_count(void);
void ur_blackbox_dump(void);
```

---

### Panic 函数

```c
void ur_panic_set_hook(ur_panic_hook_t hook);
void ur_panic(const char *reason);
void ur_panic_with_info(const char *reason, const ur_entity_t *ent, const ur_signal_t *sig);
```

**Hook 类型**：
```c
typedef void (*ur_panic_hook_t)(
    const char *reason,
    const ur_blackbox_entry_t *history,
    size_t history_count
);
```

---

### ur_mw_blackbox
```c
ur_mw_result_t ur_mw_blackbox(ur_entity_t *ent, ur_signal_t *sig, void *ctx);
```
黑盒记录中间件。

---

**文档版本**：v3.0
