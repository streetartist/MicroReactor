# MicroReactor 零基础入门教程

> 从零开始，一步一步带你掌握 MicroReactor 框架。不需要任何前置知识，傻子也能看懂！

---

## 目录

1. [写在前面：这个框架能帮你解决什么问题？](#1-写在前面)
2. [第一章：理解核心概念（用生活中的例子）](#2-第一章理解核心概念)
3. [第二章：你的第一个程序——LED 闪烁](#3-第二章你的第一个程序)
4. [第三章：按键控制 LED](#4-第三章按键控制-led)
5. [第四章：状态机入门——理解"状态"](#5-第四章状态机入门)
6. [第五章：信号系统——实体之间如何"说话"](#6-第五章信号系统)
7. [第六章：协程——让异步代码变简单](#7-第六章协程)
8. [第七章：中间件——信号的"安检站"](#8-第七章中间件)
9. [第八章：混入——代码复用的秘密武器](#9-第八章混入)
10. [第九章：发布订阅——广播电台模式](#10-第九章发布订阅)
11. [第十章：参数系统——记住用户的设置](#11-第十章参数系统)
12. [第十一章：实战项目——智能台灯](#12-第十一章实战项目)
13. [常见错误和解决方法](#13-常见错误和解决方法)
14. [术语表](#14-术语表)

---

## 1. 写在前面

### 1.1 你遇到过这些问题吗？

如果你写过嵌入式代码，你可能遇到过这些头疼的问题：

**问题一：代码乱成一锅粥**
```c
// 这种代码你见过吗？
if (button_pressed && !led_on && timer_expired && wifi_connected && !sleeping) {
    // 天哪，这是什么条件？？
}
```

**问题二：回调地狱**
```c
// 先做A，A完成后做B，B完成后做C...
void step_a_done() {
    start_step_b(step_b_done);
}
void step_b_done() {
    start_step_c(step_c_done);
}
void step_c_done() {
    start_step_d(step_d_done);  // 这样下去没完没了...
}
```

**问题三：全局变量满天飞**
```c
// 到处都是全局变量，改一个可能影响一片
int g_temperature;
int g_humidity;
int g_fan_speed;
int g_mode;
int g_error_code;
// ... 还有100个 ...
```

### 1.2 MicroReactor 如何解决这些问题？

MicroReactor 提供了一种**全新的代码组织方式**：

1. **状态机**：明确"我现在在干什么"
2. **信号**：用"发消息"代替"调函数"
3. **实体**：每个模块管好自己的事

听起来很抽象？没关系，接下来我们用生活中的例子来解释。

---

## 2. 第一章：理解核心概念

### 2.1 什么是"实体"？

**生活类比：实体就像一个"员工"**

想象一个公司：
- 有前台（负责接待）
- 有财务（负责算账）
- 有程序员（负责写代码）

每个员工：
- 有自己的**工位**（独立空间）
- 有自己的**职责**（状态和行为）
- 有自己的**收件箱**（接收任务）

在 MicroReactor 中，**实体（Entity）** 就是这样的"员工"：

```
┌─────────────────────────────────────┐
│            实体 (Entity)             │
├─────────────────────────────────────┤
│  ID: 1                              │  ← 工号
│  名称: "LED控制器"                   │  ← 姓名
│  当前状态: "关闭"                    │  ← 正在做什么
│  收件箱: [消息1, 消息2, ...]         │  ← 待处理任务
└─────────────────────────────────────┘
```

### 2.2 什么是"信号"？

**生活类比：信号就像"便签纸"**

员工之间怎么沟通？写便签！

```
┌─────────────────┐
│ 便签纸          │
│                 │
│ 发件人: 前台    │
│ 内容: 有客人来  │
│ 附加信息: 3人   │
└─────────────────┘
```

在 MicroReactor 中，**信号（Signal）** 就是这样的"便签"：

```c
// 一个信号包含：
// - id: 这是什么信号？（比如：按钮被按下）
// - src_id: 谁发的？
// - payload: 附带的数据
```

### 2.3 什么是"状态"？

**生活类比：状态就像"工作模式"**

想象一个客服：
- **空闲状态**：等待来电
- **通话状态**：正在接听
- **休息状态**：午休中

不同状态下，对同一件事的反应不同：
- 空闲时，电话响了 → 接听
- 通话时，电话响了 → 忽略
- 休息时，电话响了 → 转接给别人

这就是**状态机**的核心思想！

```
        ┌──────────┐
        │   空闲   │
        └────┬─────┘
             │ 电话响了
             ▼
        ┌──────────┐
        │   通话   │
        └────┬─────┘
             │ 挂断
             ▼
        ┌──────────┐
        │   空闲   │
        └──────────┘
```

### 2.4 把它们组合在一起

```
                    ┌─────────────────────────┐
  信号进来 ──────▶  │         实体            │
  (按钮按下)        │                         │
                    │  当前状态: 关闭         │
                    │                         │
                    │  规则:                  │
                    │  如果收到"按钮按下"     │
                    │  就转到"打开"状态       │
                    │  并执行"开灯"动作       │
                    └─────────────────────────┘
```

---

## 3. 第二章：你的第一个程序

### 3.1 最简单的 LED 闪烁

我们先写一个最简单的程序：让 LED 每秒闪烁一次。

```c
/**
 * 第一个 MicroReactor 程序
 * 功能：LED 每秒闪烁
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// 第一步：引入 MicroReactor 头文件
#include "ur_core.h"

// LED 引脚定义
#define LED_PIN GPIO_NUM_2

// ============================================================
// 第二步：定义信号
// 信号就像"便签"，告诉实体发生了什么事
// ============================================================

enum {
    // 系统信号从 0x0000 开始，用户信号从 0x0100 开始
    SIG_TICK = SIG_USER_BASE,  // 我们自定义的"滴答"信号
};

// ============================================================
// 第三步：定义状态
// 状态就是"我现在在干什么"
// ============================================================

enum {
    STATE_OFF = 1,  // LED 关闭状态
    STATE_ON  = 2,  // LED 打开状态
};

// ============================================================
// 第四步：定义动作函数
// 动作就是"要做的事情"
// ============================================================

// 开灯动作
static uint16_t action_turn_on(ur_entity_t *ent, const ur_signal_t *sig) {
    // ent: 是谁在执行这个动作
    // sig: 是什么信号触发的

    gpio_set_level(LED_PIN, 1);  // 点亮 LED
    printf("LED 亮了！\n");

    // 返回 0 表示：使用规则里定义的下一个状态
    return 0;
}

// 关灯动作
static uint16_t action_turn_off(ur_entity_t *ent, const ur_signal_t *sig) {
    gpio_set_level(LED_PIN, 0);  // 熄灭 LED
    printf("LED 灭了！\n");
    return 0;
}

// ============================================================
// 第五步：定义规则
// 规则就是：在什么状态下，收到什么信号，做什么事，转到什么状态
// ============================================================

// "关闭"状态的规则
static const ur_rule_t rules_off[] = {
    // 收到 SIG_TICK 信号，转到 STATE_ON 状态，执行 action_turn_on
    UR_RULE(SIG_TICK, STATE_ON, action_turn_on),
    UR_RULE_END  // 规则列表结束标记（必须有！）
};

// "打开"状态的规则
static const ur_rule_t rules_on[] = {
    // 收到 SIG_TICK 信号，转到 STATE_OFF 状态，执行 action_turn_off
    UR_RULE(SIG_TICK, STATE_OFF, action_turn_off),
    UR_RULE_END
};

// ============================================================
// 第六步：定义状态列表
// 把所有状态组装起来
// ============================================================

static const ur_state_def_t led_states[] = {
    // UR_STATE(状态ID, 父状态, 进入动作, 退出动作, 规则列表)
    UR_STATE(STATE_OFF, 0, NULL, NULL, rules_off),
    UR_STATE(STATE_ON,  0, NULL, NULL, rules_on),
};

// ============================================================
// 第七步：创建实体
// ============================================================

// 实体必须是静态或全局的！（不能是局部变量）
static ur_entity_t led_entity;

// ============================================================
// 第八步：主函数
// ============================================================

void app_main(void) {
    printf("=== MicroReactor 第一个程序 ===\n");

    // 1. 配置 GPIO
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    // 2. 配置实体
    ur_entity_config_t config = {
        .id = 1,                        // 实体ID（相当于工号）
        .name = "LED",                  // 实体名称
        .states = led_states,           // 状态列表
        .state_count = 2,               // 状态数量
        .initial_state = STATE_OFF,     // 初始状态
    };

    // 3. 初始化实体
    ur_err_t err = ur_init(&led_entity, &config);
    if (err != UR_OK) {
        printf("初始化失败！错误码: %d\n", err);
        return;
    }

    // 4. 启动实体
    ur_start(&led_entity);
    printf("LED 实体已启动，当前状态: %d\n", ur_get_state(&led_entity));

    // 5. 主循环：每秒发送一个 TICK 信号
    while (1) {
        // 创建一个信号
        ur_signal_t tick_signal = {
            .id = SIG_TICK,      // 信号类型
            .src_id = 0,         // 发送者ID（0表示系统）
        };

        // 发送信号给 LED 实体
        ur_emit(&led_entity, tick_signal);

        // 处理信号（调度）
        ur_dispatch(&led_entity, 0);

        // 等待 1 秒
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### 3.2 代码解读

让我们一步步理解这个程序：

**第一步：定义信号**
```c
enum {
    SIG_TICK = SIG_USER_BASE,  // 值是 0x0100
};
```
- `SIG_USER_BASE` 是框架预留的用户信号起始值
- 系统信号占用 0x0000 ~ 0x00FF
- 用户信号从 0x0100 开始

**第二步：定义状态**
```c
enum {
    STATE_OFF = 1,  // 不能用 0！0 是保留值
    STATE_ON  = 2,
};
```
- 状态ID从 1 开始
- 0 有特殊含义（表示"不转换状态"）

**第三步：定义动作函数**
```c
static uint16_t action_turn_on(ur_entity_t *ent, const ur_signal_t *sig) {
    gpio_set_level(LED_PIN, 1);
    return 0;  // 返回 0 = 使用规则定义的下一个状态
}
```
- 函数签名是固定的：`uint16_t func(ur_entity_t *, const ur_signal_t *)`
- 返回 0：使用规则里的 next_state
- 返回非 0：覆盖下一个状态（动态决定）

**第四步：定义规则**
```c
static const ur_rule_t rules_off[] = {
    UR_RULE(SIG_TICK, STATE_ON, action_turn_on),
    UR_RULE_END
};
```
- `UR_RULE(信号, 下一状态, 动作函数)`
- `UR_RULE_END` 必须放在最后！

**第五步：组装状态**
```c
static const ur_state_def_t led_states[] = {
    UR_STATE(STATE_OFF, 0, NULL, NULL, rules_off),
    //       状态ID   父状态 进入  退出  规则
};
```

**第六步：运行循环**
```c
while (1) {
    ur_emit(&led_entity, tick_signal);  // 发送信号
    ur_dispatch(&led_entity, 0);        // 处理信号
    vTaskDelay(pdMS_TO_TICKS(1000));    // 等待
}
```

### 3.3 运行效果

```
=== MicroReactor 第一个程序 ===
LED 实体已启动，当前状态: 1
LED 亮了！
LED 灭了！
LED 亮了！
LED 灭了！
...（持续闪烁）
```

### 3.4 小练习

试着修改代码：
1. 改变闪烁频率（比如 0.5 秒）
2. 添加第三个状态（比如"快闪"）
3. 在进入状态时打印一条消息

---

## 4. 第三章：按键控制 LED

现在我们加入按键，让程序能响应用户输入。

### 4.1 需求

- 按下按钮：LED 亮
- 松开按钮：LED 灭

### 4.2 完整代码

```c
/**
 * 第二个程序：按键控制 LED
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "ur_core.h"

#define LED_PIN    GPIO_NUM_2
#define BUTTON_PIN GPIO_NUM_0  // 通常是 BOOT 按钮

// ============================================================
// 信号定义
// ============================================================

enum {
    SIG_BUTTON_PRESS = SIG_USER_BASE,    // 按钮按下
    SIG_BUTTON_RELEASE,                   // 按钮松开
};

// ============================================================
// 状态定义
// ============================================================

enum {
    STATE_OFF = 1,
    STATE_ON,
};

// ============================================================
// 动作函数
// ============================================================

static uint16_t turn_on(ur_entity_t *ent, const ur_signal_t *sig) {
    gpio_set_level(LED_PIN, 1);
    printf("[LED] 开灯\n");
    return 0;
}

static uint16_t turn_off(ur_entity_t *ent, const ur_signal_t *sig) {
    gpio_set_level(LED_PIN, 0);
    printf("[LED] 关灯\n");
    return 0;
}

// ============================================================
// 规则定义
// ============================================================

static const ur_rule_t rules_off[] = {
    // 关闭状态 + 按下按钮 → 打开状态
    UR_RULE(SIG_BUTTON_PRESS, STATE_ON, turn_on),
    UR_RULE_END
};

static const ur_rule_t rules_on[] = {
    // 打开状态 + 松开按钮 → 关闭状态
    UR_RULE(SIG_BUTTON_RELEASE, STATE_OFF, turn_off),
    UR_RULE_END
};

// ============================================================
// 状态定义
// ============================================================

static const ur_state_def_t led_states[] = {
    UR_STATE(STATE_OFF, 0, NULL, NULL, rules_off),
    UR_STATE(STATE_ON,  0, NULL, NULL, rules_on),
};

// ============================================================
// 实体
// ============================================================

static ur_entity_t led_entity;

// ============================================================
// 中断处理（从中断发送信号）
// ============================================================

// 上一次按钮状态（用于检测变化）
static volatile int last_button_state = 1;  // 1 = 未按下（上拉）

// GPIO 中断处理函数
static void IRAM_ATTR button_isr_handler(void *arg) {
    ur_entity_t *ent = (ur_entity_t *)arg;
    BaseType_t woken = pdFALSE;

    // 读取当前按钮状态
    int current_state = gpio_get_level(BUTTON_PIN);

    // 检测变化
    if (current_state != last_button_state) {
        last_button_state = current_state;

        // 创建信号
        ur_signal_t sig;
        if (current_state == 0) {
            // 按钮按下（低电平）
            sig.id = SIG_BUTTON_PRESS;
        } else {
            // 按钮松开（高电平）
            sig.id = SIG_BUTTON_RELEASE;
        }
        sig.src_id = 0;

        // 从中断发送信号（注意：必须用 _from_isr 版本！）
        ur_emit_from_isr(ent, sig, &woken);
    }

    // 如果唤醒了更高优先级的任务，触发调度
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

// ============================================================
// 主函数
// ============================================================

void app_main(void) {
    printf("=== 按键控制 LED ===\n");

    // 配置 LED
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    // 配置按钮（带上拉电阻）
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);

    // 配置中断
    gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_ANYEDGE);  // 上升沿和下降沿都触发
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, &led_entity);

    // 初始化实体
    ur_entity_config_t config = {
        .id = 1,
        .name = "LED",
        .states = led_states,
        .state_count = 2,
        .initial_state = STATE_OFF,
    };

    ur_init(&led_entity, &config);
    ur_start(&led_entity);

    printf("按住 BOOT 按钮点亮 LED，松开熄灭\n");

    // 主循环：处理信号
    while (1) {
        // 等待并处理信号（阻塞最多100ms）
        ur_dispatch(&led_entity, 100);
    }
}
```

### 4.3 关键点解释

**1. 从中断发送信号**

```c
// 错误！不能在中断里用普通版本
ur_emit(&led_entity, sig);  // ❌ 会死机！

// 正确！必须用 _from_isr 版本
ur_emit_from_isr(ent, sig, &woken);  // ✓
```

为什么？因为中断是"紧急情况"，不能等待，必须用特殊的无阻塞版本。

**2. portYIELD_FROM_ISR 是什么？**

```c
if (woken) {
    portYIELD_FROM_ISR();
}
```

这行代码的意思是：如果发送信号唤醒了一个等待中的任务，立即让那个任务运行。

**3. 为什么用 IRAM_ATTR？**

```c
static void IRAM_ATTR button_isr_handler(void *arg) {
```

`IRAM_ATTR` 告诉编译器：把这个函数放在 RAM 里（而不是 Flash）。中断处理函数必须这样，否则可能会因为 Flash 缓存问题导致崩溃。

### 4.4 运行效果

```
=== 按键控制 LED ===
按住 BOOT 按钮点亮 LED，松开熄灭
[LED] 开灯       （按下按钮）
[LED] 关灯       （松开按钮）
[LED] 开灯       （按下按钮）
...
```

---

## 5. 第四章：状态机入门

### 5.1 为什么需要状态机？

看这段"普通"代码：

```c
// 不用状态机的代码 - 容易出bug
bool led_on = false;
bool button_pressed = false;
bool blink_mode = false;
int blink_count = 0;

void handle_button() {
    if (button_pressed && !led_on && !blink_mode) {
        led_on = true;
        // ...
    } else if (button_pressed && led_on && !blink_mode) {
        // ...
    }
    // 条件越来越复杂，越来越乱...
}
```

用状态机就清晰多了：

```
┌─────────┐  按下   ┌─────────┐  长按   ┌─────────┐
│   关闭  │ ──────▶ │  打开   │ ──────▶ │  闪烁   │
└─────────┘         └─────────┘         └─────────┘
     ▲                   │                   │
     │      按下         │      按下         │
     └───────────────────┴───────────────────┘
```

### 5.2 状态机的三要素

1. **状态（State）**：我现在在哪？
2. **事件（Event/Signal）**：发生了什么？
3. **转换（Transition）**：从A状态到B状态

### 5.3 设计一个红绿灯状态机

需求：红灯 → 绿灯 → 黄灯 → 红灯（循环）

```
        ┌─────────┐
        │   红灯   │
        │  停5秒   │
        └────┬────┘
             │ 超时
             ▼
        ┌─────────┐
        │   绿灯   │
        │  走10秒  │
        └────┬────┘
             │ 超时
             ▼
        ┌─────────┐
        │   黄灯   │
        │  等2秒   │
        └────┬────┘
             │ 超时
             ▼
        ┌─────────┐
        │   红灯   │
        └─────────┘
```

### 5.4 红绿灯代码

```c
/**
 * 红绿灯状态机
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "ur_core.h"

// ============================================================
// 信号定义
// ============================================================

enum {
    SIG_TIMEOUT = SIG_USER_BASE,  // 超时信号
};

// ============================================================
// 状态定义
// ============================================================

enum {
    STATE_RED = 1,      // 红灯
    STATE_GREEN,        // 绿灯
    STATE_YELLOW,       // 黄灯
};

// 每个状态的持续时间（秒）
#define RED_DURATION     5
#define GREEN_DURATION   10
#define YELLOW_DURATION  2

// ============================================================
// 动作函数
// ============================================================

static uint16_t enter_red(ur_entity_t *ent, const ur_signal_t *sig) {
    printf("\n");
    printf("  ████  \n");
    printf("  ████  ← 红灯亮！停车等待\n");
    printf("  ○○○○  \n");
    printf("  ○○○○  \n");
    printf("\n");
    return 0;
}

static uint16_t enter_green(ur_entity_t *ent, const ur_signal_t *sig) {
    printf("\n");
    printf("  ○○○○  \n");
    printf("  ○○○○  \n");
    printf("  ████  ← 绿灯亮！可以通行\n");
    printf("  ████  \n");
    printf("\n");
    return 0;
}

static uint16_t enter_yellow(ur_entity_t *ent, const ur_signal_t *sig) {
    printf("\n");
    printf("  ○○○○  \n");
    printf("  ████  ← 黄灯亮！准备停车\n");
    printf("  ████  \n");
    printf("  ○○○○  \n");
    printf("\n");
    return 0;
}

// ============================================================
// 规则定义
// ============================================================

static const ur_rule_t red_rules[] = {
    UR_RULE(SIG_TIMEOUT, STATE_GREEN, enter_green),  // 红灯超时 → 绿灯
    UR_RULE_END
};

static const ur_rule_t green_rules[] = {
    UR_RULE(SIG_TIMEOUT, STATE_YELLOW, enter_yellow),  // 绿灯超时 → 黄灯
    UR_RULE_END
};

static const ur_rule_t yellow_rules[] = {
    UR_RULE(SIG_TIMEOUT, STATE_RED, enter_red),  // 黄灯超时 → 红灯
    UR_RULE_END
};

// ============================================================
// 状态定义
// ============================================================

static const ur_state_def_t light_states[] = {
    UR_STATE(STATE_RED,    0, enter_red,    NULL, red_rules),
    UR_STATE(STATE_GREEN,  0, NULL,         NULL, green_rules),
    UR_STATE(STATE_YELLOW, 0, NULL,         NULL, yellow_rules),
};

// ============================================================
// 实体
// ============================================================

static ur_entity_t traffic_light;

// ============================================================
// 定时器回调
// ============================================================

static esp_timer_handle_t light_timer = NULL;

// 获取当前状态的持续时间
static int get_duration_for_state(uint16_t state) {
    switch (state) {
        case STATE_RED:    return RED_DURATION;
        case STATE_GREEN:  return GREEN_DURATION;
        case STATE_YELLOW: return YELLOW_DURATION;
        default:           return 1;
    }
}

// 定时器触发时发送超时信号
static void timer_callback(void *arg) {
    ur_signal_t sig = { .id = SIG_TIMEOUT, .src_id = 0 };
    ur_emit(&traffic_light, sig);
}

// 启动定时器
static void start_timer_for_current_state(void) {
    uint16_t state = ur_get_state(&traffic_light);
    int duration = get_duration_for_state(state);

    printf("定时器：%d秒后切换\n", duration);

    // 停止旧定时器
    esp_timer_stop(light_timer);
    // 启动新定时器
    esp_timer_start_once(light_timer, duration * 1000000);  // 微秒
}

// ============================================================
// 主函数
// ============================================================

void app_main(void) {
    printf("=== 红绿灯状态机 ===\n\n");

    // 创建定时器
    esp_timer_create_args_t timer_args = {
        .callback = timer_callback,
        .name = "light_timer",
    };
    esp_timer_create(&timer_args, &light_timer);

    // 初始化实体
    ur_entity_config_t config = {
        .id = 1,
        .name = "TrafficLight",
        .states = light_states,
        .state_count = 3,
        .initial_state = STATE_RED,
    };

    ur_init(&traffic_light, &config);
    ur_start(&traffic_light);

    // 启动第一个定时器
    start_timer_for_current_state();

    // 主循环
    while (1) {
        // 处理信号
        ur_err_t err = ur_dispatch(&traffic_light, 1000);

        // 如果处理了信号（状态可能变了），重新启动定时器
        if (err == UR_OK) {
            start_timer_for_current_state();
        }
    }
}
```

### 5.5 运行效果

```
=== 红绿灯状态机 ===

  ████
  ████  ← 红灯亮！停车等待
  ○○○○
  ○○○○

定时器：5秒后切换

（5秒后...）

  ○○○○
  ○○○○
  ████  ← 绿灯亮！可以通行
  ████

定时器：10秒后切换

...
```

---

## 6. 第五章：信号系统

### 6.1 信号的结构

每个信号包含以下信息：

```c
struct ur_signal_s {
    uint16_t id;           // 信号ID：这是什么信号？
    uint16_t src_id;       // 来源ID：谁发的？

    union {
        uint8_t  u8[4];    // 4个字节
        uint16_t u16[2];   // 2个16位数
        uint32_t u32[1];   // 1个32位数
        int32_t  i32[1];   // 1个有符号32位数
        float    f32;      // 1个浮点数
    } payload;             // 载荷：附带的数据

    void *ptr;             // 指针：指向更大的数据
    uint32_t timestamp;    // 时间戳：什么时候发的
};
```

### 6.2 创建信号的几种方式

```c
// 方式一：直接构造
ur_signal_t sig1 = {
    .id = SIG_BUTTON_PRESS,
    .src_id = 1,
};

// 方式二：使用工具函数
ur_signal_t sig2 = ur_signal_create(SIG_BUTTON_PRESS, 1);

// 方式三：带数据的信号
ur_signal_t sig3 = ur_signal_create_u32(SIG_TEMPERATURE, 1, 2500);  // 25.0度

// 方式四：使用宏
ur_signal_t sig4 = UR_SIGNAL_U32(SIG_TEMPERATURE, 1, 2500);
```

### 6.3 信号载荷的使用

```c
// 发送方：打包数据
ur_signal_t sig = ur_signal_create(SIG_SENSOR_DATA, ent->id);
sig.payload.u16[0] = temperature;  // 温度
sig.payload.u16[1] = humidity;     // 湿度

// 接收方：解包数据
static uint16_t handle_sensor(ur_entity_t *ent, const ur_signal_t *sig) {
    uint16_t temp = sig->payload.u16[0];
    uint16_t humid = sig->payload.u16[1];

    printf("温度: %d, 湿度: %d\n", temp, humid);
    return 0;
}
```

### 6.4 发送大数据

4字节不够用？用指针！

```c
// 定义数据结构
typedef struct {
    float values[10];
    char name[32];
    uint32_t timestamp;
} big_data_t;

// 重要：数据必须是静态或全局的！
static big_data_t shared_data;

// 发送
void send_big_data(void) {
    shared_data.values[0] = 3.14f;
    strcpy(shared_data.name, "温度传感器");
    shared_data.timestamp = ur_get_time_ms();

    ur_signal_t sig = ur_signal_create_ptr(SIG_BIG_DATA, 1, &shared_data);
    ur_emit(&receiver, sig);
}

// 接收
static uint16_t handle_big_data(ur_entity_t *ent, const ur_signal_t *sig) {
    big_data_t *data = (big_data_t *)sig->ptr;

    printf("收到数据: %s, 值: %.2f\n", data->name, data->values[0]);
    return 0;
}
```

> **警告**：`sig->ptr` 必须指向静态或全局内存！如果指向局部变量，函数返回后数据就没了！

### 6.5 系统信号

框架预定义了一些信号：

| 信号名 | 值 | 说明 |
|-------|------|------|
| `SIG_SYS_INIT` | 0x0001 | 实体启动时自动发送 |
| `SIG_SYS_ENTRY` | 0x0002 | 进入状态时 |
| `SIG_SYS_EXIT` | 0x0003 | 离开状态时 |
| `SIG_SYS_TICK` | 0x0004 | 周期性信号 |
| `SIG_SYS_TIMEOUT` | 0x0005 | 超时信号 |
| `SIG_USER_BASE` | 0x0100 | 用户信号从这开始 |

```c
// 你可以处理系统信号
static const ur_rule_t rules[] = {
    UR_RULE(SIG_SYS_INIT, 0, on_init),      // 处理初始化
    UR_RULE(SIG_BUTTON, STATE_ON, turn_on), // 处理用户信号
    UR_RULE_END
};

static uint16_t on_init(ur_entity_t *ent, const ur_signal_t *sig) {
    printf("实体初始化完成！\n");
    return 0;
}
```

---

## 7. 第六章：协程

### 7.1 什么是协程？为什么需要它？

看这个需求：LED 闪3下然后常亮。

**传统写法（回调地狱）：**
```c
void blink_sequence() {
    led_on();
    start_timer(500, step1);
}

void step1() {
    led_off();
    start_timer(500, step2);
}

void step2() {
    led_on();
    start_timer(500, step3);
}

void step3() {
    led_off();
    start_timer(500, step4);
}

void step4() {
    led_on();
    start_timer(500, step5);
}

void step5() {
    led_off();
    start_timer(500, step6);
}

void step6() {
    led_on();  // 最后常亮
}
```

天哪！如果要改成闪10下呢？

**协程写法（清晰！）：**
```c
uint16_t blink_action(ur_entity_t *ent, const ur_signal_t *sig) {
    UR_FLOW_BEGIN(ent);

    for (int i = 0; i < 3; i++) {  // 闪3下
        led_on();
        UR_AWAIT_TIME(ent, 500);   // 等500ms

        led_off();
        UR_AWAIT_TIME(ent, 500);
    }

    led_on();  // 常亮

    UR_FLOW_END(ent);
}
```

是不是清晰多了？想改成闪10下？改个数字就行！

### 7.2 协程的基本用法

```c
uint16_t my_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    // 必须以这个开始！
    UR_FLOW_BEGIN(ent);

    // 你的代码...

    // 必须以这个结束！
    UR_FLOW_END(ent);
}
```

### 7.3 协程等待方式

**等待时间：**
```c
UR_AWAIT_TIME(ent, 1000);  // 等待1000毫秒
```

**等待信号：**
```c
UR_AWAIT_SIGNAL(ent, SIG_BUTTON_PRESS);  // 等待按钮按下
// 信号来了，继续执行
printf("按钮按下了！信号数据: %d\n", sig->payload.u32[0]);
```

**等待多个信号之一：**
```c
UR_AWAIT_ANY(ent, SIG_OK, SIG_CANCEL, SIG_TIMEOUT);
// 收到三个信号中的任意一个就继续

if (sig->id == SIG_OK) {
    printf("收到确认\n");
} else if (sig->id == SIG_CANCEL) {
    printf("收到取消\n");
} else {
    printf("超时了\n");
}
```

**等待条件：**
```c
UR_AWAIT_COND(ent, temperature > 30);  // 等待温度超过30度
```

### 7.4 暂存区：协程中的变量存储

**问题：** 协程中的局部变量在 `UR_AWAIT_*` 后会丢失！

```c
// 错误示例！
uint16_t bad_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    UR_FLOW_BEGIN(ent);

    int count = 0;  // 这个变量会出问题！

    while (count < 5) {
        printf("count = %d\n", count);
        count++;
        UR_AWAIT_TIME(ent, 1000);  // 等待后，count 的值不可靠了！
    }

    UR_FLOW_END(ent);
}
```

**解决方案：使用暂存区（Scratchpad）**

```c
// 第一步：定义暂存区结构
typedef struct {
    int count;       // 计数器
    float total;     // 累计值
    bool flag;       // 标志位
} my_scratch_t;

// 第二步：编译时检查大小（防止超出限制）
UR_SCRATCH_STATIC_ASSERT(my_scratch_t);

// 第三步：在协程中使用
uint16_t good_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    // 获取暂存区指针
    my_scratch_t *s = UR_SCRATCH_PTR(ent, my_scratch_t);

    UR_FLOW_BEGIN(ent);

    // 初始化（只在第一次运行时执行）
    s->count = 0;
    s->total = 0.0f;

    while (s->count < 5) {
        printf("count = %d\n", s->count);
        s->count++;
        UR_AWAIT_TIME(ent, 1000);  // 等待后，s->count 仍然有效！
    }

    printf("循环完成！共计: %d\n", s->count);

    UR_FLOW_END(ent);
}
```

### 7.5 完整协程示例：温度监控

```c
/**
 * 温度监控协程
 * 功能：每秒读取温度，如果连续3次超过阈值就报警
 */

// 暂存区
typedef struct {
    int high_count;      // 高温计数
    float last_temp;     // 上次温度
    uint32_t start_time; // 开始时间
} temp_monitor_scratch_t;

UR_SCRATCH_STATIC_ASSERT(temp_monitor_scratch_t);

#define TEMP_THRESHOLD  35.0f  // 报警阈值
#define ALARM_COUNT     3      // 连续几次触发报警

static uint16_t monitor_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    temp_monitor_scratch_t *s = UR_SCRATCH_PTR(ent, temp_monitor_scratch_t);

    UR_FLOW_BEGIN(ent);

    // 初始化
    s->high_count = 0;
    s->start_time = ur_get_time_ms();
    printf("[温度监控] 启动！阈值: %.1f℃\n", TEMP_THRESHOLD);

    // 无限循环监控
    while (1) {
        // 读取温度（模拟）
        s->last_temp = 20.0f + (rand() % 300) / 10.0f;  // 20-50度随机

        printf("[温度监控] 当前温度: %.1f℃", s->last_temp);

        if (s->last_temp > TEMP_THRESHOLD) {
            s->high_count++;
            printf(" ⚠️ 高温！连续%d次\n", s->high_count);

            if (s->high_count >= ALARM_COUNT) {
                printf("\n🚨🚨🚨 警报！温度过高！🚨🚨🚨\n\n");
                s->high_count = 0;  // 重置计数
            }
        } else {
            s->high_count = 0;  // 温度正常，重置计数
            printf(" ✓ 正常\n");
        }

        // 等待1秒
        UR_AWAIT_TIME(ent, 1000);
    }

    UR_FLOW_END(ent);
}
```

### 7.6 协程的限制

1. **不能使用 switch 语句**（因为协程内部就是用 switch 实现的）
2. **局部变量在等待后会丢失**（必须用暂存区）
3. **协程宏只能在动作函数顶层使用**（不能在子函数里）

```c
// 错误！不能用 switch
UR_FLOW_BEGIN(ent);
switch (mode) {           // ❌ 编译会出错！
    case 1: break;
}
UR_FLOW_END(ent);

// 正确：用 if-else 代替
UR_FLOW_BEGIN(ent);
if (mode == 1) {          // ✓
    // ...
} else if (mode == 2) {
    // ...
}
UR_FLOW_END(ent);
```

---

## 8. 第七章：中间件

### 8.1 什么是中间件？

**生活类比：中间件就像"安检站"**

信号从发送到处理，要经过一系列"检查站"：

```
信号 ──▶ [日志] ──▶ [防抖] ──▶ [权限] ──▶ 状态规则
           │          │          │
           ▼          ▼          ▼
         记录       过滤重复    检查权限
```

每个检查站可以：
- **放行**：让信号继续传递
- **拦截**：丢弃信号
- **修改**：改变信号内容

### 8.2 编写中间件

```c
// 中间件函数签名
ur_mw_result_t my_middleware(
    ur_entity_t *ent,    // 目标实体
    ur_signal_t *sig,    // 信号（可修改）
    void *ctx            // 上下文数据
) {
    // 你的处理逻辑...

    return UR_MW_CONTINUE;  // 返回值决定后续行为
}
```

**返回值说明：**
- `UR_MW_CONTINUE`：放行，继续传递给下一个中间件
- `UR_MW_FILTERED`：拦截，丢弃信号
- `UR_MW_HANDLED`：已处理完毕，不传给状态规则
- `UR_MW_TRANSFORM`：已修改信号，继续传递

### 8.3 示例：日志中间件

```c
/**
 * 日志中间件：记录所有收到的信号
 */

ur_mw_result_t logger_middleware(ur_entity_t *ent, ur_signal_t *sig, void *ctx) {
    printf("[LOG] 实体'%s' 收到信号 0x%04X，来自实体 %d\n",
           ur_entity_name(ent), sig->id, sig->src_id);

    return UR_MW_CONTINUE;  // 继续传递
}

// 注册中间件
void setup() {
    // 参数：实体, 中间件函数, 上下文, 优先级(数字小先执行)
    ur_register_middleware(&my_entity, logger_middleware, NULL, 0);
}
```

### 8.4 示例：防抖中间件

按钮可能会"抖动"，短时间内产生多个信号。防抖中间件可以过滤掉重复的信号：

```c
/**
 * 防抖中间件：忽略短时间内的重复信号
 */

typedef struct {
    uint16_t signal_id;     // 要防抖的信号
    uint32_t debounce_ms;   // 防抖时间
    uint32_t last_time;     // 上次触发时间
} debounce_ctx_t;

ur_mw_result_t debounce_middleware(ur_entity_t *ent, ur_signal_t *sig, void *ctx) {
    debounce_ctx_t *deb = (debounce_ctx_t *)ctx;

    // 只处理指定的信号
    if (sig->id != deb->signal_id) {
        return UR_MW_CONTINUE;
    }

    uint32_t now = ur_get_time_ms();

    // 检查时间间隔
    if (now - deb->last_time < deb->debounce_ms) {
        printf("[防抖] 信号太频繁，已过滤\n");
        return UR_MW_FILTERED;  // 过滤掉
    }

    deb->last_time = now;
    return UR_MW_CONTINUE;  // 放行
}

// 使用
static debounce_ctx_t btn_debounce = {
    .signal_id = SIG_BUTTON_PRESS,
    .debounce_ms = 50,
    .last_time = 0,
};

void setup() {
    ur_register_middleware(&my_entity, debounce_middleware, &btn_debounce, 1);
}
```

### 8.5 示例：信号转换中间件

```c
/**
 * 温度单位转换中间件：摄氏度 → 华氏度
 */

ur_mw_result_t temp_converter(ur_entity_t *ent, ur_signal_t *sig, void *ctx) {
    if (sig->id == SIG_TEMP_CELSIUS) {
        // 获取摄氏度
        int celsius = sig->payload.i32[0];

        // 转换为华氏度
        int fahrenheit = celsius * 9 / 5 + 32;

        // 修改信号
        sig->id = SIG_TEMP_FAHRENHEIT;
        sig->payload.i32[0] = fahrenheit;

        printf("[转换] %d℃ → %d℉\n", celsius, fahrenheit);

        return UR_MW_TRANSFORM;  // 已修改，继续传递
    }

    return UR_MW_CONTINUE;
}
```

### 8.6 中间件管理

```c
// 注册
ur_register_middleware(&ent, my_middleware, ctx, priority);

// 注销
ur_unregister_middleware(&ent, my_middleware);

// 临时禁用
ur_set_middleware_enabled(&ent, my_middleware, false);

// 重新启用
ur_set_middleware_enabled(&ent, my_middleware, true);
```

---

## 9. 第八章：混入

### 9.1 什么是混入？

**生活类比：混入就像"通用技能"**

想象一个公司：
- 所有员工都要打卡
- 所有员工都要遵守安全规定
- 所有员工都要会用灭火器

这些"通用技能"不属于某个具体岗位，而是所有人都要有的。

在 MicroReactor 中，**混入（Mixin）** 就是这种"通用规则"：

```
┌─────────────────────────────────────────────────────┐
│                    电源管理混入                      │
│  - 收到关机信号 → 保存状态                          │
│  - 收到低电量信号 → 降低亮度                        │
└─────────────────────────────────────────────────────┘
                         │
         ┌───────────────┼───────────────┐
         ▼               ▼               ▼
    ┌─────────┐     ┌─────────┐     ┌─────────┐
    │ LED实体  │     │ 显示器  │     │ 传感器  │
    └─────────┘     └─────────┘     └─────────┘
```

三个实体都拥有了电源管理能力！

### 9.2 定义混入

```c
// 混入的动作函数
static uint16_t handle_power_off(ur_entity_t *ent, const ur_signal_t *sig) {
    printf("[%s] 收到关机信号，正在保存状态...\n", ur_entity_name(ent));
    // 保存状态的代码...
    return 0;
}

static uint16_t handle_low_battery(ur_entity_t *ent, const ur_signal_t *sig) {
    uint8_t level = sig->payload.u8[0];
    printf("[%s] 低电量警告！剩余 %d%%\n", ur_entity_name(ent), level);
    // 降低功耗的代码...
    return 0;
}

// 混入的规则
static const ur_rule_t power_mixin_rules[] = {
    UR_RULE(SIG_POWER_OFF, 0, handle_power_off),
    UR_RULE(SIG_LOW_BATTERY, 0, handle_low_battery),
    UR_RULE_END
};

// 定义混入
static const ur_mixin_t power_mixin = {
    .name = "PowerMixin",
    .rules = power_mixin_rules,
    .rule_count = 2,
    .priority = 10,  // 优先级（越小越先检查）
};
```

### 9.3 使用混入

```c
// 绑定混入到实体
ur_bind_mixin(&led_entity, &power_mixin);
ur_bind_mixin(&display_entity, &power_mixin);
ur_bind_mixin(&sensor_entity, &power_mixin);

// 现在，这三个实体都会响应电源相关的信号！
```

### 9.4 混入 vs 中间件

| 特性 | 混入 | 中间件 |
|------|------|--------|
| 作用 | 添加规则 | 预处理信号 |
| 时机 | 状态规则之后检查 | 状态规则之前执行 |
| 能做什么 | 处理信号、转换状态 | 过滤、修改、记录信号 |
| 适用场景 | 添加通用行为 | 日志、防抖、权限检查 |

### 9.5 信号查找顺序

当信号到来时，查找规则的顺序是：

```
1. 当前状态的规则    ← 最先检查
2. 混入的规则        ← 其次
3. 父状态的规则      ← 最后（HSM模式）
```

如果某个规则匹配了信号，就执行该规则，不再继续查找。

---

## 10. 第九章：发布订阅

### 10.1 什么是发布订阅？

**生活类比：发布订阅就像"广播电台"**

传统方式：
- 你有消息要告诉小明、小红、小刚
- 你要分别打电话给每个人

发布订阅方式：
- 你在广播电台发布消息
- 订阅了这个频道的人自动收到

```
    发布者                     订阅者
      │                    ┌───────────┐
      │   发布"温度=25"    │  显示器   │ ← 订阅了温度
      │                    └───────────┘
      ▼                    ┌───────────┐
┌──────────────┐           │  日志器   │ ← 订阅了温度
│   消息总线    │────────▶  └───────────┘
└──────────────┘           ┌───────────┐
                           │  报警器   │ ← 订阅了温度
                           └───────────┘
```

发布者不需要知道有多少订阅者，也不需要一个一个发送！

### 10.2 使用发布订阅

```c
#include "ur_bus.h"

// 定义主题（使用信号ID作为主题）
enum {
    TOPIC_TEMPERATURE = SIG_USER_BASE + 100,
    TOPIC_HUMIDITY,
    TOPIC_ALARM,
};

void setup() {
    // 初始化消息总线
    ur_bus_init();

    // 订阅主题
    ur_subscribe(&display_entity, TOPIC_TEMPERATURE);
    ur_subscribe(&logger_entity, TOPIC_TEMPERATURE);
    ur_subscribe(&alarm_entity, TOPIC_TEMPERATURE);
}

void publish_temperature(int temp) {
    // 发布消息（所有订阅者都会收到）
    int count = ur_publish_u32(TOPIC_TEMPERATURE, sensor_entity.id, temp);
    printf("温度 %d 已发布给 %d 个订阅者\n", temp, count);
}
```

### 10.3 完整示例

```c
/**
 * 发布订阅示例：温度监控系统
 *
 * 传感器 ────发布────▶ [温度主题] ────订阅────▶ 显示器
 *                                          ├────▶ 日志器
 *                                          └────▶ 报警器
 */

#include "ur_core.h"
#include "ur_bus.h"

enum {
    TOPIC_TEMP = SIG_USER_BASE + 100,
};

// ---------- 传感器实体 ----------

static uint16_t sensor_read(ur_entity_t *ent, const ur_signal_t *sig) {
    // 模拟读取温度
    int temp = 20 + (rand() % 20);  // 20-40度

    // 发布到总线
    ur_publish_u32(TOPIC_TEMP, ent->id, temp);
    printf("[传感器] 发布温度: %d℃\n", temp);

    return 0;
}

// ---------- 显示器实体 ----------

static uint16_t display_show(ur_entity_t *ent, const ur_signal_t *sig) {
    int temp = sig->payload.u32[0];
    printf("[显示器] 显示: %d℃\n", temp);
    return 0;
}

// 显示器规则：订阅温度主题
static const ur_rule_t display_rules[] = {
    UR_RULE(TOPIC_TEMP, 0, display_show),
    UR_RULE_END
};

// ---------- 报警器实体 ----------

static uint16_t alarm_check(ur_entity_t *ent, const ur_signal_t *sig) {
    int temp = sig->payload.u32[0];

    if (temp > 35) {
        printf("[报警器] 🚨 温度过高: %d℃！\n", temp);
    }

    return 0;
}

// 报警器规则
static const ur_rule_t alarm_rules[] = {
    UR_RULE(TOPIC_TEMP, 0, alarm_check),
    UR_RULE_END
};

// ---------- 主函数 ----------

void app_main() {
    // 初始化总线
    ur_bus_init();

    // 初始化实体...
    // (省略初始化代码)

    // 订阅主题
    ur_subscribe(&display_entity, TOPIC_TEMP);
    ur_subscribe(&alarm_entity, TOPIC_TEMP);

    // 启动...
}
```

### 10.4 订阅管理

```c
// 订阅
ur_subscribe(&entity, TOPIC_TEMP);

// 取消订阅
ur_unsubscribe(&entity, TOPIC_TEMP);

// 取消所有订阅
ur_unsubscribe_all(&entity);

// 检查是否已订阅
if (ur_is_subscribed(&entity, TOPIC_TEMP)) {
    printf("已订阅温度主题\n");
}

// 查询订阅者数量
size_t count = ur_bus_subscriber_count(TOPIC_TEMP);
```

---

## 11. 第十章：参数系统

### 11.1 什么是参数系统？

**生活类比：参数就像"用户设置"**

手机有很多设置：
- 音量：50%
- 亮度：80%
- WiFi密码：xxxxx
- 自动休眠：开启

这些设置需要：
- **记住**（关机后不丢失）
- **修改**（用户可以改）
- **通知**（改变时告诉相关功能）

MicroReactor 的参数系统就是干这个的！

### 11.2 定义参数

```c
#include "ur_param.h"

// 参数ID
enum {
    PARAM_VOLUME = 1,
    PARAM_BRIGHTNESS,
    PARAM_WIFI_SSID,
    PARAM_AUTO_SLEEP,
};

// 参数定义
static const ur_param_def_t my_params[] = {
    {
        .id = PARAM_VOLUME,
        .type = UR_PARAM_TYPE_U8,         // 类型：无符号8位
        .name = "volume",                  // 名称
        .default_val = { .u8 = 50 },      // 默认值
        .flags = UR_PARAM_FLAG_PERSIST    // 标志：需要保存
                | UR_PARAM_FLAG_NOTIFY,   // 变更时通知
    },
    {
        .id = PARAM_BRIGHTNESS,
        .type = UR_PARAM_TYPE_U8,
        .name = "brightness",
        .default_val = { .u8 = 100 },
        .flags = UR_PARAM_FLAG_PERSIST,
    },
    {
        .id = PARAM_WIFI_SSID,
        .type = UR_PARAM_TYPE_STR,        // 类型：字符串
        .name = "wifi_ssid",
        .size = 32,                        // 最大长度
        .default_val = { .str = "" },
        .flags = UR_PARAM_FLAG_PERSIST,
    },
    {
        .id = PARAM_AUTO_SLEEP,
        .type = UR_PARAM_TYPE_BOOL,       // 类型：布尔
        .name = "auto_sleep",
        .default_val = { .b = true },
        .flags = UR_PARAM_FLAG_PERSIST | UR_PARAM_FLAG_NOTIFY,
    },
};
```

### 11.3 初始化参数系统

```c
void setup_params() {
    // 初始化参数系统
    // 参数：参数定义数组, 参数数量, 存储后端
    ur_param_init(my_params, 4, &ur_param_storage_nvs);

    // 从 Flash 加载保存的值
    int loaded = ur_param_load_all();
    printf("加载了 %d 个参数\n", loaded);
}
```

### 11.4 读写参数

```c
// 读取参数
uint8_t volume;
ur_param_get_u8(PARAM_VOLUME, &volume);
printf("当前音量: %d\n", volume);

char ssid[32];
ur_param_get_str(PARAM_WIFI_SSID, ssid, sizeof(ssid));
printf("WiFi: %s\n", ssid);

// 写入参数
ur_param_set_u8(PARAM_VOLUME, 75);
ur_param_set_str(PARAM_WIFI_SSID, "MyWiFi");
ur_param_set_bool(PARAM_AUTO_SLEEP, false);

// 保存到 Flash
ur_param_save_all();
```

### 11.5 参数变更通知

当设置了 `UR_PARAM_FLAG_NOTIFY` 的参数变化时，框架会广播 `SIG_PARAM_CHANGED` 信号：

```c
// 在规则中处理参数变更
static const ur_rule_t rules[] = {
    UR_RULE(SIG_PARAM_CHANGED, 0, on_param_changed),
    UR_RULE_END
};

static uint16_t on_param_changed(ur_entity_t *ent, const ur_signal_t *sig) {
    // 参数ID在 payload 中
    uint16_t param_id = sig->payload.u16[0];

    switch (param_id) {
        case PARAM_VOLUME: {
            uint8_t vol;
            ur_param_get_u8(PARAM_VOLUME, &vol);
            set_speaker_volume(vol);
            printf("音量已调整为 %d\n", vol);
            break;
        }

        case PARAM_AUTO_SLEEP: {
            bool enabled;
            ur_param_get_bool(PARAM_AUTO_SLEEP, &enabled);
            printf("自动休眠: %s\n", enabled ? "开启" : "关闭");
            break;
        }
    }

    return 0;
}
```

### 11.6 批量修改

修改多个参数时，可以用批量操作减少保存次数：

```c
// 开始批量操作
ur_param_batch_begin();

// 修改多个参数（不会立即保存）
ur_param_set_u8(PARAM_VOLUME, 60);
ur_param_set_u8(PARAM_BRIGHTNESS, 80);
ur_param_set_bool(PARAM_AUTO_SLEEP, true);

// 提交（一次性保存所有修改）
int saved = ur_param_commit();
printf("保存了 %d 个参数\n", saved);
```

---

## 12. 第十一章：实战项目

### 12.1 项目：智能台灯

**功能需求：**
1. 短按按钮：开/关灯
2. 长按按钮：进入调光模式
3. 调光模式下：旋转编码器调节亮度
4. 记住亮度设置（断电不丢失）
5. 支持远程控制（通过信号）

### 12.2 状态设计

```
                    ┌──────────────────┐
                    │      关闭        │
                    │   (STATE_OFF)    │
                    └────────┬─────────┘
                             │ 短按
                             ▼
                    ┌──────────────────┐
         ┌─────────│      开启        │─────────┐
         │         │   (STATE_ON)     │         │
         │         └────────┬─────────┘         │
         │                  │ 长按              │ 短按
         │                  ▼                   │
         │         ┌──────────────────┐         │
         │         │     调光中       │         │
         │         │  (STATE_DIMMING) │         │
         │         └────────┬─────────┘         │
         │                  │ 超时/短按         │
         │                  ▼                   │
         │         ┌──────────────────┐         │
         └────────▶│      开启        │◀────────┘
                   └──────────────────┘
```

### 12.3 完整代码

```c
/**
 * 智能台灯 - MicroReactor 实战项目
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

#include "ur_core.h"
#include "ur_flow.h"
#include "ur_param.h"
#include "ur_bus.h"

// ============================================================
// 硬件配置
// ============================================================

#define LED_PIN         GPIO_NUM_2
#define BUTTON_PIN      GPIO_NUM_0
#define LED_CHANNEL     LEDC_CHANNEL_0

// ============================================================
// 信号定义
// ============================================================

enum {
    // 本地信号
    SIG_BTN_SHORT = SIG_USER_BASE,  // 短按
    SIG_BTN_LONG,                    // 长按
    SIG_ENCODER_CW,                  // 编码器顺时针
    SIG_ENCODER_CCW,                 // 编码器逆时针
    SIG_DIMMING_TIMEOUT,             // 调光超时

    // 远程控制信号（可通过总线发布）
    TOPIC_LAMP_ON = SIG_USER_BASE + 100,
    TOPIC_LAMP_OFF,
    TOPIC_LAMP_TOGGLE,
    TOPIC_LAMP_SET_BRIGHTNESS,
};

// ============================================================
// 状态定义
// ============================================================

enum {
    STATE_OFF = 1,       // 关闭
    STATE_ON,            // 开启
    STATE_DIMMING,       // 调光中
};

// ============================================================
// 参数定义
// ============================================================

enum {
    PARAM_BRIGHTNESS = 1,
};

static const ur_param_def_t lamp_params[] = {
    {
        .id = PARAM_BRIGHTNESS,
        .type = UR_PARAM_TYPE_U8,
        .name = "brightness",
        .default_val = { .u8 = 100 },
        .flags = UR_PARAM_FLAG_PERSIST | UR_PARAM_FLAG_NOTIFY,
    },
};

// ============================================================
// 暂存区（调光协程用）
// ============================================================

typedef struct {
    uint8_t brightness;       // 当前亮度
    uint8_t target;           // 目标亮度
    uint32_t dimming_start;   // 调光开始时间
} lamp_scratch_t;

UR_SCRATCH_STATIC_ASSERT(lamp_scratch_t);

// ============================================================
// 硬件操作函数
// ============================================================

static void led_init(void) {
    // 配置 LEDC 定时器
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    // 配置 LEDC 通道
    ledc_channel_config_t ch_cfg = {
        .gpio_num = LED_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LED_CHANNEL,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_cfg);
}

static void led_set_brightness(uint8_t brightness) {
    uint32_t duty = (brightness * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_CHANNEL);
}

// ============================================================
// 动作函数
// ============================================================

// 开灯
static uint16_t action_turn_on(ur_entity_t *ent, const ur_signal_t *sig) {
    lamp_scratch_t *s = UR_SCRATCH_PTR(ent, lamp_scratch_t);

    // 从参数读取亮度
    ur_param_get_u8(PARAM_BRIGHTNESS, &s->brightness);

    led_set_brightness(s->brightness);
    printf("💡 灯已打开，亮度: %d%%\n", s->brightness);

    return 0;
}

// 关灯
static uint16_t action_turn_off(ur_entity_t *ent, const ur_signal_t *sig) {
    led_set_brightness(0);
    printf("💡 灯已关闭\n");
    return 0;
}

// 切换开关
static uint16_t action_toggle(ur_entity_t *ent, const ur_signal_t *sig) {
    uint16_t current = ur_get_state(ent);

    if (current == STATE_OFF) {
        action_turn_on(ent, sig);
        return STATE_ON;
    } else {
        action_turn_off(ent, sig);
        return STATE_OFF;
    }
}

// 进入调光模式
static uint16_t action_enter_dimming(ur_entity_t *ent, const ur_signal_t *sig) {
    lamp_scratch_t *s = UR_SCRATCH_PTR(ent, lamp_scratch_t);

    s->dimming_start = ur_get_time_ms();
    printf("🔆 进入调光模式，当前亮度: %d%%\n", s->brightness);

    return 0;
}

// 调光协程
static uint16_t action_dimming_flow(ur_entity_t *ent, const ur_signal_t *sig) {
    lamp_scratch_t *s = UR_SCRATCH_PTR(ent, lamp_scratch_t);

    UR_FLOW_BEGIN(ent);

    while (1) {
        // 等待编码器信号或超时
        UR_AWAIT_ANY(ent, SIG_ENCODER_CW, SIG_ENCODER_CCW,
                     SIG_BTN_SHORT, SIG_DIMMING_TIMEOUT);

        if (sig->id == SIG_ENCODER_CW) {
            // 顺时针：增加亮度
            if (s->brightness < 100) {
                s->brightness += 10;
                if (s->brightness > 100) s->brightness = 100;
                led_set_brightness(s->brightness);
                printf("亮度: %d%% ▲\n", s->brightness);
            }
            s->dimming_start = ur_get_time_ms();  // 重置超时

        } else if (sig->id == SIG_ENCODER_CCW) {
            // 逆时针：降低亮度
            if (s->brightness > 10) {
                s->brightness -= 10;
                led_set_brightness(s->brightness);
                printf("亮度: %d%% ▼\n", s->brightness);
            }
            s->dimming_start = ur_get_time_ms();

        } else {
            // 超时或短按：退出调光模式
            break;
        }
    }

    // 保存亮度设置
    ur_param_set_u8(PARAM_BRIGHTNESS, s->brightness);
    ur_param_save_all();
    printf("亮度已保存: %d%%\n", s->brightness);

    UR_FLOW_GOTO(ent, STATE_ON);

    UR_FLOW_END(ent);
}

// 设置亮度（远程控制）
static uint16_t action_set_brightness(ur_entity_t *ent, const ur_signal_t *sig) {
    lamp_scratch_t *s = UR_SCRATCH_PTR(ent, lamp_scratch_t);

    s->brightness = sig->payload.u8[0];
    if (s->brightness > 100) s->brightness = 100;

    // 如果灯是开的，立即应用
    if (ur_get_state(ent) != STATE_OFF) {
        led_set_brightness(s->brightness);
    }

    // 保存
    ur_param_set_u8(PARAM_BRIGHTNESS, s->brightness);
    printf("亮度设置为: %d%%\n", s->brightness);

    return 0;
}

// ============================================================
// 规则定义
// ============================================================

static const ur_rule_t rules_off[] = {
    UR_RULE(SIG_BTN_SHORT, STATE_ON, action_turn_on),
    UR_RULE(TOPIC_LAMP_ON, STATE_ON, action_turn_on),
    UR_RULE(TOPIC_LAMP_TOGGLE, 0, action_toggle),
    UR_RULE_END
};

static const ur_rule_t rules_on[] = {
    UR_RULE(SIG_BTN_SHORT, STATE_OFF, action_turn_off),
    UR_RULE(SIG_BTN_LONG, STATE_DIMMING, action_enter_dimming),
    UR_RULE(TOPIC_LAMP_OFF, STATE_OFF, action_turn_off),
    UR_RULE(TOPIC_LAMP_TOGGLE, 0, action_toggle),
    UR_RULE(TOPIC_LAMP_SET_BRIGHTNESS, 0, action_set_brightness),
    UR_RULE_END
};

static const ur_rule_t rules_dimming[] = {
    UR_RULE(SIG_ENCODER_CW, 0, action_dimming_flow),
    UR_RULE(SIG_ENCODER_CCW, 0, action_dimming_flow),
    UR_RULE(SIG_BTN_SHORT, 0, action_dimming_flow),
    UR_RULE(SIG_DIMMING_TIMEOUT, 0, action_dimming_flow),
    UR_RULE_END
};

// ============================================================
// 状态定义
// ============================================================

static const ur_state_def_t lamp_states[] = {
    UR_STATE(STATE_OFF, 0, NULL, NULL, rules_off),
    UR_STATE(STATE_ON, 0, NULL, NULL, rules_on),
    UR_STATE(STATE_DIMMING, 0, NULL, NULL, rules_dimming),
};

// ============================================================
// 实体
// ============================================================

static ur_entity_t lamp_entity;

// ============================================================
// 按钮处理任务（检测长按）
// ============================================================

static void button_task(void *arg) {
    uint32_t press_start = 0;
    bool was_pressed = false;

    while (1) {
        bool pressed = (gpio_get_level(BUTTON_PIN) == 0);

        if (pressed && !was_pressed) {
            // 按下
            press_start = ur_get_time_ms();
        } else if (!pressed && was_pressed) {
            // 松开
            uint32_t duration = ur_get_time_ms() - press_start;

            ur_signal_t sig;
            if (duration > 1000) {
                sig.id = SIG_BTN_LONG;
                printf("长按检测\n");
            } else {
                sig.id = SIG_BTN_SHORT;
                printf("短按检测\n");
            }
            sig.src_id = 0;

            ur_emit(&lamp_entity, sig);
        }

        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================================
// 调光超时定时器
// ============================================================

static esp_timer_handle_t dimming_timer = NULL;

static void dimming_timer_callback(void *arg) {
    ur_signal_t sig = { .id = SIG_DIMMING_TIMEOUT, .src_id = 0 };
    ur_emit(&lamp_entity, sig);
}

// ============================================================
// 主函数
// ============================================================

void app_main(void) {
    printf("====================================\n");
    printf("   智能台灯 - MicroReactor 演示    \n");
    printf("====================================\n\n");

    // 初始化硬件
    led_init();

    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);

    // 初始化参数系统
    ur_param_init(lamp_params, 1, &ur_param_storage_nvs);
    ur_param_load_all();

    // 初始化消息总线
    ur_bus_init();

    // 初始化实体
    ur_entity_config_t config = {
        .id = 1,
        .name = "SmartLamp",
        .states = lamp_states,
        .state_count = 3,
        .initial_state = STATE_OFF,
    };

    ur_init(&lamp_entity, &config);
    ur_register_entity(&lamp_entity);

    // 订阅远程控制主题
    ur_subscribe(&lamp_entity, TOPIC_LAMP_ON);
    ur_subscribe(&lamp_entity, TOPIC_LAMP_OFF);
    ur_subscribe(&lamp_entity, TOPIC_LAMP_TOGGLE);
    ur_subscribe(&lamp_entity, TOPIC_LAMP_SET_BRIGHTNESS);

    ur_start(&lamp_entity);

    // 创建调光超时定时器
    esp_timer_create_args_t timer_args = {
        .callback = dimming_timer_callback,
        .name = "dimming_timer",
    };
    esp_timer_create(&timer_args, &dimming_timer);

    // 创建按钮处理任务
    xTaskCreate(button_task, "button", 2048, NULL, 5, NULL);

    printf("系统已启动！\n");
    printf("- 短按: 开/关灯\n");
    printf("- 长按: 调光模式\n\n");

    // 主循环
    ur_entity_t *entities[] = { &lamp_entity };
    while (1) {
        ur_run(entities, 1, 100);

        // 如果在调光模式，启动超时定时器
        if (ur_get_state(&lamp_entity) == STATE_DIMMING) {
            esp_timer_stop(dimming_timer);
            esp_timer_start_once(dimming_timer, 3000000);  // 3秒超时
        }
    }
}
```

---

## 13. 常见错误和解决方法

### 错误1：信号丢失

**现象：** 发送的信号没有被处理

**原因：** 收件箱满了

**解决：**
```c
// 方法1：增大收件箱
// 在 menuconfig 中设置 CONFIG_UR_INBOX_SIZE=16

// 方法2：更频繁地调度
while (1) {
    ur_run(entities, count, 10);  // 减小空闲时间
}

// 方法3：检查收件箱状态
size_t count = ur_inbox_count(&entity);
if (count > 6) {
    printf("警告：收件箱快满了！%d/8\n", count);
}
```

### 错误2：协程变量丢失

**现象：** 协程中的变量在等待后变成奇怪的值

**原因：** 使用了局部变量

**解决：** 使用暂存区
```c
// 错误 ❌
int count = 0;
UR_AWAIT_TIME(ent, 1000);
count++;  // count 可能是垃圾值！

// 正确 ✓
my_scratch_t *s = UR_SCRATCH_PTR(ent, my_scratch_t);
s->count = 0;
UR_AWAIT_TIME(ent, 1000);
s->count++;  // 正确！
```

### 错误3：中断发送信号失败

**现象：** 中断里发送信号导致死机

**原因：** 用了错误的函数

**解决：**
```c
// 错误 ❌
void IRAM_ATTR my_isr() {
    ur_emit(&entity, sig);  // 会死机！
}

// 正确 ✓
void IRAM_ATTR my_isr() {
    BaseType_t woken = pdFALSE;
    ur_emit_from_isr(&entity, sig, &woken);
    portYIELD_FROM_ISR(woken);  // 别忘了这行！
}
```

### 错误4：状态转换不生效

**现象：** 动作函数返回了新状态，但没有转换

**原因：** 规则里 next_state 不是 0

**解决：**
```c
// 规则定义
UR_RULE(SIG_EVENT, STATE_B, action)  // next_state=STATE_B

// 动作函数
static uint16_t action(...) {
    return STATE_C;  // 返回 STATE_C
}

// 结果：转到 STATE_B（规则定义的）
// 因为 next_state 不是 0，所以动作的返回值被忽略

// 如果想让动作决定状态，规则要这样写：
UR_RULE(SIG_EVENT, 0, action)  // next_state=0
```

### 错误5：指针载荷指向局部变量

**现象：** 收到的信号 ptr 指向垃圾数据

**原因：** 指针指向了栈上的局部变量

**解决：**
```c
// 错误 ❌
void send_data() {
    my_data_t data;  // 局部变量！
    sig.ptr = &data;
    ur_emit(&entity, sig);
}  // 函数返回后 data 就没了！

// 正确 ✓
static my_data_t data;  // 静态变量
void send_data() {
    sig.ptr = &data;
    ur_emit(&entity, sig);
}
```

---

## 14. 术语表

| 术语 | 英文 | 解释 |
|------|------|------|
| 实体 | Entity | 可以接收信号、有状态的对象 |
| 信号 | Signal | 实体间通信的消息 |
| 状态 | State | 实体当前所处的模式 |
| 规则 | Rule | 定义收到什么信号做什么事 |
| 动作 | Action | 处理信号时执行的函数 |
| 状态机 | State Machine | 由状态和转换组成的行为模型 |
| 协程 | Coroutine | 可以暂停和恢复的函数 |
| 暂存区 | Scratchpad | 协程用来存储变量的空间 |
| 中间件 | Middleware | 信号处理前的预处理器 |
| 混入 | Mixin | 可复用的通用规则集 |
| 发布订阅 | Pub/Sub | 基于主题的消息分发模式 |
| 参数 | Parameter | 可保存的配置项 |
| 调度 | Dispatch | 处理收件箱中的信号 |
| 收件箱 | Inbox | 实体的信号队列 |

---

## 下一步学习

恭喜你完成了入门教程！接下来可以：

1. **阅读高级教程** - `docs/tutorial_zh.md`
2. **查看 API 文档** - `docs/api_reference_zh.md`
3. **研究示例代码** - `examples/` 目录
4. **尝试自己的项目** - 用学到的知识做点东西！

记住：**熟能生巧**，多写多练才能掌握！

---

**文档版本**：v1.0
**最后更新**：2026年1月

