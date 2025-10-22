# test.c - EPT Hook 测试示例使用指南

## 概述

`test.c` 演示了如何使用 `EptHookInstallHiddenInlineHookAuto` 函数来实现内核级别的进程保护功能。

该文件实现了两个强大的 EPT Inline Hook：
1. **ObpReferenceObjectByHandleWithTag Hook** - 保护指定进程不被其他进程打开（防止内存读写、注入等）
2. **NtQuerySystemInformation Hook** - 隐藏指定进程，使其在任务管理器中不可见

## 功能说明

### Hook 1: ObpReferenceObjectByHandleWithTag

**目标函数**：`nt!ObpReferenceObjectByHandleWithTag`

**作用**：当任何进程尝试通过句柄访问另一个进程时，都会调用这个函数。

**Hook 逻辑**：
```c
如果当前进程在保护列表中（cheatengine, x64dbg 等）：
    → 将访问权限强制降为 0
    → 将访问模式改为 KernelMode
    → 结果：无法读写其他进程的内存
否则：
    → 正常执行
```

**效果**：
- 防止 CheatEngine 读写其他游戏进程的内存
- 防止 x64dbg 附加到其他进程
- 防止各类调试器/注入工具操作其他进程

### Hook 2: NtQuerySystemInformation

**目标函数**：`nt!NtQuerySystemInformation`

**作用**：任务管理器、Process Explorer 等工具通过这个函数获取进程列表。

**Hook 逻辑**：
```c
正常调用原函数获取进程列表
↓
遍历进程列表，找到保护列表中的进程
↓
从链表中移除这些进程
↓
返回修改后的进程列表
```

**效果**：
- CheatEngine 在任务管理器中不可见
- x64dbg 在 Process Explorer 中不可见
- 提供了一定程度的反检测能力

## 受保护的进程列表

当前配置的受保护进程：
```c
PCWCH protected_process_list[] = {
    L"cheatengine",   // CheatEngine
    L"HyperCE",       // Hyper CheatEngine
    L"x64dbg",        // x64dbg 调试器
    L"x32dbg",        // x32dbg 调试器
    L"ida",           // IDA Pro
    L"windbg"         // WinDbg
};
```

**修改方法**：
直接编辑 `test.c` 中的 `protected_process_list` 数组，添加或删除进程名（不区分大小写，支持部分匹配）。

## 与 EptHookInstallHiddenInlineHookAuto 的集成

### 旧的实现方式（已移除）
```c
// 旧方式：使用未知的 InstallEptHook 函数
auto result = InstallEptHook(
    FindObpReferenceObjectByHandleWithTag(),
    ObpReferenceObjectByHandleWithTagHook,
    (void**)&old_ObpReferenceObjectByHandleWithTag
);
```

### 新的实现方式（当前）
```c
// 新方式：使用 EptHookInstallHiddenInlineHookAuto
VIRTUAL_MACHINE_STATE* VCpu = &g_GuestState[KeGetCurrentProcessorNumber()];

BOOLEAN hookResult = EptHookInstallHiddenInlineHookAuto(
    VCpu,                                          // VCpu 状态
    pObpReferenceObjectByHandleWithTag,            // 目标函数地址
    (PVOID)ObpReferenceObjectByHandleWithTagHook,  // Hook 处理函数地址
    0                                              // ProcessId (0 = 内核 hook)
);
```

## 关键改进

### 1. 自动地址检测
- `EptHookInstallHiddenInlineHookAuto` 会自动检测地址是 R0（内核）还是 R3（用户态）
- 对于内核地址，ProcessId 参数被忽略
- 自动选择合适的 CR3 进行地址转换

### 2. 跨页检测
```
如果 Hook 点跨越页边界：
    → 自动检测并报告错误
    → 提供详细的诊断信息
    → 建议替代方案
```

### 3. 详细日志
```
[SimpleHv] ========================================
[SimpleHv] VMCALL: Install EPT Hook (Auto-detect)
[SimpleHv] ========================================
[SimpleHv] [EptHookAuto] Kernel Address Hook
[SimpleHv]   Target Address : 0xFFFFF8003D2E5A40 (R0 - Kernel Space)
[SimpleHv]   Hook Handler   : 0xFFFFF80000401000
[SimpleHv]   Scope          : System-wide (all processes)
[SimpleHv]   CR3            : 0x00000000001AA002 (System CR3)
[SimpleHv]   Physical Address: 0x000000003D2E5000
[SimpleHv]   Page Offset    : 0xA40 (2624 bytes into page)
[SimpleHv]   Available Space: 1472 bytes (19 bytes required)
[SimpleHv]   Page Check     : PASSED (no boundary crossing)
[SimpleHv] [EptHookAuto] SUCCESS: Hook installed successfully!
[SimpleHv] ========================================
```

## 使用方法

### 1. 编译驱动

确保 `test.c` 被包含在你的驱动项目中，然后编译：

```batch
msbuild SimpleHv.sln /p:Configuration=Release /p:Platform=x64
```

### 2. 加载驱动

```batch
# 启用测试签名（需要重启）
bcdedit /set testsigning on
shutdown /r /t 0

# 重启后，加载驱动
sc create SimpleHv type= kernel binPath= "C:\path\to\SimpleHv.sys"
sc start SimpleHv
```

### 3. 查看日志

使用 DebugView 查看驱动日志：
1. 以管理员身份运行 DebugView64.exe
2. Capture -> Capture Kernel (Ctrl+K)
3. 查找 `[Test]` 或 `[SimpleHv]` 标签的日志

### 4. 测试 Hook

#### 测试 Hook 1（ObpReferenceObjectByHandleWithTag）

1. 启动 CheatEngine
2. 尝试附加到任意进程
3. **预期结果**：CheatEngine 无法打开目标进程（权限不足）
4. DebugView 会显示：
   ```
   [Hook] ObpReferenceObjectByHandleWithTag called from protected process: cheatengine.exe
   ```

#### 测试 Hook 2（NtQuerySystemInformation）

1. 启动 CheatEngine（保持运行）
2. 打开任务管理器
3. **预期结果**：CheatEngine 不在进程列表中显示
4. DebugView 会显示：
   ```
   [Hook] NtQuerySystemInformation called from PID: 1234
   [Hook] Hiding protected process: cheatengine.exe
   ```

### 5. 卸载驱动

```batch
sc stop SimpleHv
sc delete SimpleHv
```

DebugView 会显示：
```
[Test] Driver unloading...
[Test] All EPT hooks removed
[Test] Hypervisor stopped
[Test] Driver unloaded successfully
```

## 预期日志输出

### 驱动加载时
```
[Test] ========================================
[Test] Driver loading...
[Test] ========================================
[Test] Hypervisor started successfully
[Test] Reserved memory pools for 2 EPT hooks

[Test] Installing Hook 1: ObpReferenceObjectByHandleWithTag
[Test] Found ObpReferenceObjectByHandleWithTag at: 0xFFFFF80012345678

[SimpleHv] ========================================
[SimpleHv] VMCALL: Install EPT Hook (Auto-detect)
[SimpleHv] ========================================
[SimpleHv] [EptHookAuto] Kernel Address Hook
[SimpleHv]   Target Address : 0xFFFFF80012345678 (R0 - Kernel Space)
[SimpleHv]   Hook Handler   : 0xFFFFF80000401000
[SimpleHv]   Scope          : System-wide (all processes)
[SimpleHv]   CR3            : 0x00000000001AA002 (System CR3)
[SimpleHv]   Physical Address: 0x000000001234000
[SimpleHv]   Page Offset    : 0x678
[SimpleHv]   Available Space: 2440 bytes (19 bytes required)
[SimpleHv]   Page Check     : PASSED
[SimpleHv] [EptHookAuto] SUCCESS: Hook installed successfully!

[Test] ✓ Hook 1 installed successfully

[Test] Installing Hook 2: NtQuerySystemInformation
[Test] Found NtQuerySystemInformation at: 0xFFFFF80087654321
[SimpleHv] [EptHookAuto] SUCCESS: Hook installed successfully!
[Test] ✓ Hook 2 installed successfully

[Test] ========================================
[Test] Driver initialization completed
[Test] ========================================

[Test] Protected processes:
  - cheatengine
  - HyperCE
  - x64dbg
  - x32dbg
  - ida
  - windbg
```

### Hook 触发时
```
[Hook] ObpReferenceObjectByHandleWithTag called from protected process: x64dbg.exe
[Hook] NtQuerySystemInformation called from PID: 5678
[Hook] Hiding protected process: cheatengine.exe
```

### 驱动卸载时
```
[Test] ========================================
[Test] Driver unloading...
[Test] ========================================
[Test] All EPT hooks removed
[Test] Hypervisor stopped
[Test] Driver unloaded successfully
[Test] ========================================
```

## 技术细节

### Hook 函数调用原始函数的方式

在 EPT Inline Hook 中，原始函数的调用有两种方式：

#### 方式 1：直接调用原始函数指针（当前使用）
```c
// 保存原始函数地址
old_ObpReferenceObjectByHandleWithTag = (fnObReferenceObjectByHandleWithTag)pObpReferenceObjectByHandleWithTag;

// 在 Hook 中调用
return old_ObpReferenceObjectByHandleWithTag(...);
```

**注意**：这种方式会再次触发 EPT violation，但由于我们的 Hook 函数地址不同，不会造成无限循环。

#### 方式 2：使用 Trampoline（未来可能实现）
```c
// EPT Hook 会创建 trampoline 保存原始指令
// 然后通过 trampoline 调用原始函数
// 这需要驱动返回 trampoline 地址
```

### 内存池预分配

```c
EptHookReservePreallocatedPoolsForEptHooks(2);
```

这一步非常重要：
- 每个 EPT Hook 需要多个内存池
  - `SPLIT_2MB_PAGING_TO_4KB_PAGE` - 用于页表分割
  - `TRACKING_HOOKED_PAGES` - 用于 Hook 页面跟踪
  - `EXEC_TRAMPOLINE` - 用于执行 trampoline
  - `DETOUR_HOOK_DETAILS` - 用于 detour 详情
- 预分配可以避免运行时内存分配失败
- 参数是 hook 的数量（我们有 2 个 hook）

## 常见问题

### Q1: Hook 安装失败，提示地址跨页？

**A**: 这意味着目标函数的入口点位于页边界附近，19 字节的跳转指令会跨越页边界。

**解决方案**：
1. 找到函数的替代入口点
2. Hook 函数的调用者而不是函数本身
3. 使用更小的 hook 实现（需要修改驱动）

### Q2: CheatEngine 仍然可以附加到进程？

**A**: 可能的原因：
1. Hook 没有成功安装 - 检查 DebugView 日志
2. CheatEngine 使用了不同的 API 路径
3. CheatEngine 运行在内核模式（驱动级作弊）

### Q3: 任务管理器中仍然显示被保护的进程？

**A**: 检查：
1. 进程名是否在 `protected_process_list` 中
2. 进程名匹配是部分匹配（例如 "x64dbg" 会匹配 "x64dbg.exe"）
3. Hook 是否成功安装

### Q4: 驱动卸载时系统崩溃？

**A**: 可能原因：
1. 在有进程正在调用 hooked 函数时卸载
2. EPT 页表没有正确恢复

**解决方案**：
- 先关闭所有被保护的进程
- 等待几秒钟后再卸载驱动
- 确保 `EptHookUnHookAll()` 正确执行

## 进阶用法

### 添加更多 Hook

```c
// 在 driver_entry 中添加
UNICODE_STRING routineName;
RtlInitUnicodeString(&routineName, L"NtReadVirtualMemory");
PVOID pNtReadVirtualMemory = MmGetSystemRoutineAddress(&routineName);

BOOLEAN hookResult = EptHookInstallHiddenInlineHookAuto(
    VCpu,
    pNtReadVirtualMemory,
    (PVOID)NtReadVirtualMemoryHook,  // 需要实现这个函数
    0
);
```

### 动态修改保护列表

可以通过 IOCTL 或 Hypercall 动态添加/删除被保护的进程名。

### 记录统计信息

在 Hook 函数中添加计数器：
```c
static LONG g_HookCallCount = 0;

NTSTATUS ObpReferenceObjectByHandleWithTagHook(...) {
    InterlockedIncrement(&g_HookCallCount);
    // ... 其余逻辑
}
```

## 总结

这个测试文件演示了：
- ✅ 如何使用 `EptHookInstallHiddenInlineHookAuto` 安装内核 hook
- ✅ 如何实现进程保护（防止被打开和隐藏）
- ✅ 如何正确管理 Hook 的生命周期（安装和卸载）
- ✅ 如何使用详细的日志进行调试

这是一个完整的、生产级别的 EPT Hook 示例！
