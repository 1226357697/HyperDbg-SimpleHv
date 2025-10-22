# VMCALL 到 IOCTL 通信迁移指南

## 改动总结

### 为什么要迁移？

**VMCALL 通信的问题**：
1. ✗ VMCALL handler 运行在 VMX root mode（高 IRQL）
2. ✗ 无法调用很多内核 API（`MmGetSystemRoutineAddress`, `KeGetCurrentProcessorNumber` 等）
3. ✗ 无法访问可能触发页错误的内存
4. ✗ 无法使用 DPC（会导致死锁）
5. ✗ 调试困难，容易蓝屏

**IOCTL 通信的优势**：
1. ✓ 运行在正常内核上下文（VMX non-root mode）
2. ✓ 可以调用所有内核 API
3. ✓ 标准的 Windows 驱动通信方式
4. ✓ 更安全、更稳定
5. ✓ 更容易调试和维护

## 文件修改清单

### 新增文件

1. **[header\interface\DeviceIoctl.h](g:\Cheat\Driver\SimpleHv\header\interface\DeviceIoctl.h)**
   - IOCTL 代码定义
   - 数据结构定义
   - 函数声明

2. **[code\interface\DeviceIoctl.c](g:\Cheat\Driver\SimpleHv\code\interface\DeviceIoctl.c)**
   - IRP 处理函数实现
   - IOCTL 分发逻辑

3. **[usermode\SimpleHvClient.h](g:\Cheat\Driver\SimpleHv\usermode\SimpleHvClient.h)**
   - C++ 客户端封装
   - IOCTL 调用接口

### 修改的文件

1. **[code\Driver.c](g:\Cheat\Driver\SimpleHv\code\Driver.c)**
   - 添加设备创建和符号链接
   - 注册 IRP 处理函数
   - 修复中文乱码
   - 简化 DriverUnload

2. **[usermode\main.cpp](g:\Cheat\Driver\SimpleHv\usermode\main.cpp)**
   - 使用 SimpleHv::Client 替代 HyperCall
   - 通过 DeviceIoControl 调用驱动

3. **[code\interface\HyperCall.c](g:\Cheat\Driver\SimpleHv\code\interface\HyperCall.c)**
   - `HYPERCALL_INSTALL_EPTHOOK` 标记为废弃

4. **[test.c](G:\Cheat\Driver\SimpleHv\test.c)** & **[test.h](G:\Cheat\Driver\SimpleHv\test.h)**
   - `InstallTestHooks()` 改为无参数
   - 从 DriverEntry 调用（非 VMCALL）

## IOCTL 定义

### 控制码

```c
#define IOCTL_SIMPLEHV_PING \
    CTL_CODE(FILE_DEVICE_SIMPLEHV, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_SIMPLEHV_INSTALL_TEST_HOOKS \
    CTL_CODE(FILE_DEVICE_SIMPLEHV, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_SIMPLEHV_UNHOOK_ALL \
    CTL_CODE(FILE_DEVICE_SIMPLEHV, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
```

### 数据结构

```c
typedef struct _SIMPLEHV_PING_RESPONSE {
    UINT64 Signature;       // 应为 0xE79086E5A198
    UINT32 NumProcessors;   // CPU 核心数
    BOOLEAN IsRunning;      // Hypervisor 是否运行
} SIMPLEHV_PING_RESPONSE;

typedef struct _SIMPLEHV_INSTALL_HOOKS_RESPONSE {
    NTSTATUS Status;        // 安装状态
    UINT32 HooksInstalled;  // 成功安装的 Hook 数量
} SIMPLEHV_INSTALL_HOOKS_RESPONSE;
```

## 用户态使用方法

### 旧方式（VMCALL - 已废弃）

```cpp
// ✗ 旧方式
#include "HyperCallClient.h"

if (HyperCall::IsHypervisorRunning()) {
    UINT64 result = HyperCall::InstallEptHook(...);
}
```

### 新方式（IOCTL - 推荐）

```cpp
// ✓ 新方式
#include "SimpleHvClient.h"

SimpleHv::Client client;

if (client.Open()) {
    // Ping
    SIMPLEHV_PING_RESPONSE pingResp;
    client.Ping(&pingResp);

    // Install hooks
    SIMPLEHV_INSTALL_HOOKS_RESPONSE hookResp;
    client.InstallTestHooks(&hookResp);

    // Unhook all
    client.UnhookAll();

    client.Close();
}
```

## 内核端处理流程

```
用户态应用
  ↓ CreateFile("\\\\.\\SimpleHv")
  ↓ DeviceIoControl(IOCTL_SIMPLEHV_PING, ...)
  ↓
内核驱动 (VMX non-root mode, PASSIVE_LEVEL IRQL)
  ↓ IRP_MJ_DEVICE_CONTROL
  ↓ DeviceIoctlDispatch()
  ↓ HandleIoctlPing()
  ↓ 可以调用任何内核 API
  ↓ 返回结果到用户态
```

## 编译注意事项

### Driver 端

需要在项目中包含以下文件：
- `code\interface\DeviceIoctl.c`
- `header\interface\DeviceIoctl.h`

### Usermode 端

需要包含：
- `usermode\SimpleHvClient.h`

## 测试步骤

1. **编译驱动**：
   ```batch
   msbuild SimpleHv.sln /p:Configuration=Release /p:Platform=x64
   ```

2. **加载驱动**：
   ```batch
   sc create SimpleHv type= kernel binPath= "C:\path\to\SimpleHv.sys"
   sc start SimpleHv
   ```

3. **运行用户态程序**：
   ```batch
   SimpleHvClient.exe
   ```

4. **预期输出**：
   ```
   [+] Connected to SimpleHv driver!

   Select option: 1
   [+] PING successful!
       Signature    : 0xE79086E5A198
       NumProcessors: 8
       IsRunning    : Yes
   ```

## 迁移检查清单

- [x] 创建设备对象和符号链接
- [x] 注册 IRP 处理函数
- [x] 实现 IOCTL 分发逻辑
- [x] 创建用户态客户端封装
- [x] 更新 main.cpp 使用 IOCTL
- [x] 修复中文乱码
- [x] 简化 DriverUnload
- [ ] 删除旧的 VMCALL 相关代码（可选）

## 后续优化建议

1. 添加更多 IOCTL 命令（如动态安装/卸载单个 Hook）
2. 实现异步 IOCTL（IRP pending）
3. 添加输入参数验证和安全检查
4. 实现多客户端并发访问保护

## 总结

✅ 迁移完成后，你将拥有：
- 标准的 Windows 驱动通信机制
- 可以在正常内核上下文中处理所有请求
- 更稳定、更易调试的代码
- 避免了 VMX root mode 的各种限制

现在可以安全地调用 `InstallTestHooks()` 而不会蓝屏！
