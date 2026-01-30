# MicroReactor 待办事项

## 待实现功能

### ur_vfs - 异步文件系统

**痛点描述：**
AI 音箱需要播放本地提示音（如 `connected.mp3`）。传统文件系统（FatFS/LittleFS）的 `read()` 函数是阻塞的。如果在 Brain 实体里调用 `f_read()` 读取 4KB 数据，可能会阻塞 10-50ms（取决于 SD 卡或 Flash 速度）。这会瞬间卡死整个协作式调度系统，导致音频爆音或按键失灵。

**建议功能：** 基于回调/信号的异步 VFS

**API 构想：**

```c
// [请求] 这里的 ur_file_read 是非阻塞的，立即返回
ur_file_read_async(file_handle, buffer, 1024, my_entity_id, SIG_FILE_DATA);

// ... 实体继续处理其他事情 ...

// [回调] 当 DMA 或后台线程完成读取后，实体收到信号
void on_file_data(ur_entity_t *ent, ur_signal_t *sig) {
    if (sig->payload.result == UR_OK) {
        // 数据已在 buffer 中，推入音频管道
        ur_pipe_write(&audio_pipe, buffer, 1024);
    }
}
```

**设计要点：**

1. **文件句柄管理**
   - 静态分配的文件句柄池
   - 最大同时打开文件数可配置

2. **异步操作类型**
   ```c
   ur_err_t ur_vfs_open_async(const char *path, uint8_t mode,
                               uint16_t notify_ent, uint16_t notify_sig);
   ur_err_t ur_vfs_read_async(ur_file_t *file, void *buffer, size_t size,
                               uint16_t notify_ent, uint16_t notify_sig);
   ur_err_t ur_vfs_write_async(ur_file_t *file, const void *data, size_t size,
                                uint16_t notify_ent, uint16_t notify_sig);
   ur_err_t ur_vfs_close_async(ur_file_t *file);
   ```

3. **后端实现选项**
   - **DMA 模式**：适用于支持 DMA 的 SPI Flash/SD 卡
   - **后台任务模式**：单独的 FreeRTOS 任务处理文件 IO
   - **分片模式**：将大读取拆分成小块，穿插在主循环中

4. **完成信号 payload**
   ```c
   typedef struct {
       ur_file_t *file;      // 文件句柄
       ur_err_t result;      // 操作结果
       size_t bytes;         // 实际读/写字节数
       uint32_t offset;      // 当前文件偏移
   } ur_vfs_result_t;
   ```

5. **VFS 后端抽象**
   ```c
   typedef struct {
       ur_err_t (*mount)(const char *base_path);
       ur_err_t (*unmount)(void);
       ur_err_t (*open)(ur_file_t *file, const char *path, uint8_t mode);
       ur_err_t (*read)(ur_file_t *file, void *buf, size_t size, size_t *read);
       ur_err_t (*write)(ur_file_t *file, const void *buf, size_t size, size_t *written);
       ur_err_t (*close)(ur_file_t *file);
       ur_err_t (*stat)(const char *path, ur_file_stat_t *stat);
   } ur_vfs_backend_t;
   ```

6. **预置后端**
   - `ur_vfs_backend_fatfs` - FatFS (SD 卡)
   - `ur_vfs_backend_littlefs` - LittleFS (SPI Flash)
   - `ur_vfs_backend_spiffs` - SPIFFS (ESP-IDF)

**实现优先级：** 中等

**预计工作量：**
- 头文件设计：1 天
- 核心实现：3-4 天
- 后端适配：2 天/后端
- 测试验证：2 天

---

## 待改进功能

### ur_trace 增强

- [ ] 支持 SEGGER SystemView 格式输出
- [ ] 添加内存使用追踪
- [ ] 支持条件触发（满足条件时开始记录）

### ur_codec 增强

- [ ] 支持 MessagePack 格式
- [ ] 支持 CBOR 格式
- [ ] 添加版本协商机制

### ur_power 增强

- [ ] 支持唤醒源配置 API
- [ ] 添加功耗估算功能
- [ ] 支持多核 ESP32 的核心关闭

---

## 待实现工具

### Reactor Sim - PC 仿真环境

**目标：** 在 PC 上运行 MicroReactor 应用，无需硬件

**实现方案：**
1. Mock FreeRTOS API（队列、任务、定时器）
2. CMake 构建系统支持 MinGW/MSVC/GCC
3. Python 绑定用于自动化测试

**目录结构：**
```
tools/sim/
├── CMakeLists.txt
├── mock/
│   ├── freertos/
│   │   ├── FreeRTOS.h
│   │   ├── queue.h
│   │   └── task.h
│   └── esp_timer.h
├── src/
│   └── sim_main.c
└── python/
    └── reactor_sim.py
```

---

## 文档待完善

- [ ] API 参考文档（英文版）
- [ ] 架构设计文档
- [ ] 性能调优指南
- [ ] 安全最佳实践

---

*最后更新: 2026-01-30*
