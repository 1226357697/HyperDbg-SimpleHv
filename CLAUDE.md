# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

SimpleHv 是一个基于 Intel VT-x 的教育型 Windows Hypervisor 驱动程序，实现了完整的 VMX 操作、EPT（扩展页表）虚拟化和高级 Hook 功能。项目源自 HyperDbg 架构。

## 构建和测试

### 构建命令
```bash
# 使用 Visual Studio 构建
msbuild SimpleHv.sln /p:Configuration=Release /p:Platform=x64

# 或使用 Debug 配置
msbuild SimpleHv.sln /p:Configuration=Debug /p:Platform=x64
```

### 清理构建产物
```bash
clean.bat
```

### 部署和测试
```bash
# 1. 启用测试签名（需要管理员权限）
bcdedit /set testsigning on
bcdedit /set debug on
# 重启系统

# 2. 加载驱动
sc create SimpleHv type= kernel binPath= "完整路径\SimpleHv.sys"
sc start SimpleHv

# 3. 卸载驱动
sc stop SimpleHv
sc delete SimpleHv

# 4. 运行用户模式测试程序
UsermodeTest.exe
```

### 调试
- 使用 WinDbg 进行内核调试（推荐双机调试）
- 在 `include/config/Configuration.h` 中设置 `DebugMode TRUE` 启用详细日志
- 日志输出通过 `SimpleHvLog()` 系列宏函数

## 代码架构

### 核心目录结构
```
code/
├── assembly/          # 底层汇编实现（VMX 指令、VM-Exit 入口点）
├── vmm/              # VMX 核心（VMCS、EPT、VM-Exit 处理）
├── hooks/
│   └── ept-hook/     # EPT Hook 实现（关键：EptHook.c）
├── memory/           # 内存管理和地址转换
├── broadcast/        # 多核同步（NMI 广播）
├── transparency/     # HyperEvade 反检测模块
└── interface/        # Configuration.c - 外部接口层

header/               # 头文件（镜像 code/ 结构）
include/              # SDK 头文件和配置
test.c               # EPT Hook 测试代码
```

### 关键组件

#### 1. VMX 层次结构
- **Driver.c**: 驱动入口点，初始化 VMM
- **Vmx.c**: VMX 指令包装（VMLAUNCH、VMREAD、VMWRITE）
- **Ept.c**: EPT 页表初始化和管理（4级分页：PML4→PML3→PML2→PML1）
- **Vmexit.c**: VM-Exit 事件分发器

#### 2. Hook 系统
- **EptHook.c**: EPT Hook 核心实现（~3000行）
- **Configuration.c**: 对外接口层（ConfigureEptHook、ConfigureEptHook2）

## EPT Hook 系统详解

### 两种 Hook 类型

#### ConfigureEptHook（简单断点式）
- 触发 EPT violation，每次调用都会 VM-Exit
- 用于简单监控场景
- **不需要** trampoline

#### ConfigureEptHook2（内联 Detour 式）
- 使用 19 字节 trampoline 隐藏 Hook
- 性能更好（首次触发后不再 VM-Exit）
- **需要** trampoline 地址来调用原始函数

### 19 字节 Trampoline 机制

**为什么是 19 字节？**

在 [EptHook.c:767-845](code/hooks/ept-hook/EptHook.c#L767-L845) 的 `EptHookWriteAbsoluteJump` 函数中，构造了一个 64 位绝对跳转：

```c
// 总共 19 字节：
call $ + 5           // 5 字节 - 将下一条指令地址入栈
push 低32位地址      // 5 字节 - 压入目标地址低32位
mov [rsp+4], 高32位  // 4 字节 - 修改栈上返回地址高32位
mov dword [rsp+4], X // 4 字节 - 写入高32位值
ret                  // 1 字节 - 跳转到目标地址
```

**Trampoline 工作流程：**

1. Hook 安装时，系统反汇编目标函数前 N 条指令，直到覆盖 ≥19 字节
2. 这些原始指令被复制到 trampoline buffer
3. 原函数被覆盖为 19 字节的跳转，指向 `AsmGeneralDetourHook`
4. Trampoline 执行原始指令后，跳回原函数+偏移继续执行

**关键数据结构：**

[Hooks.h:22-28](header/hooks/Hooks.h#L22-L28)
```c
typedef struct _HIDDEN_HOOKS_DETOUR_DETAILS {
    LIST_ENTRY OtherHooksList;
    PVOID      HookedFunctionAddress;  // 被 Hook 的函数地址
    PVOID      ReturnAddress;          // Trampoline 地址
} HIDDEN_HOOKS_DETOUR_DETAILS;
```

**Trampoline 存储位置：**
- 在 [EptHook.c:970](code/hooks/ept-hook/EptHook.c#L970) 创建 `DetourHookDetails` 并存储 `Hook->Trampoline`
- 添加到全局链表 `g_EptHook2sDetourListHead`（第 981 行）
- 在 [EptHook.c:2512-2583](code/hooks/ept-hook/EptHook.c#L2512-L2583) 的 `EptHook2GeneralDetourEventHandler` 中被汇编代码访问

### ConfigureEptHook2 API 使用

**函数签名**（已修改，支持 trampoline 返回）：

[Configuration.c:297-309](code/interface/Configuration.c#L297-L309)
```c
BOOLEAN ConfigureEptHook2(
    UINT32 CoreId,
    PVOID  TargetAddress,
    PVOID  HookFunction,
    UINT32 ProcessId,
    PVOID* OutTrampoline  // 输出参数：接收 trampoline 地址
);
```

**正确用法示例**（参考 test.c）：

```c
// 1. 声明函数指针和 trampoline
typedef NTSTATUS (*fnOriginalFunction)(参数列表...);
fnOriginalFunction pTrampoline = NULL;

// 2. 安装 Hook，获取 trampoline
BOOLEAN result = ConfigureEptHook2(
    KeGetCurrentProcessorNumberEx(NULL),
    pTargetFunction,
    (PVOID)MyHookFunction,
    ProcessId,
    (PVOID*)&pTrampoline  // 接收 trampoline 地址
);

// 3. 在 Hook 函数中调用原始函数
NTSTATUS MyHookFunction(...) {
    // 修改参数或执行自定义逻辑

    // 调用 trampoline（不是原始函数地址！）
    return pTrampoline(...);
}
```

**❌ 常见错误：**
```c
// 错误：保存原始函数地址
pTrampoline = pTargetFunction;  // 这会导致无限循环！

// 错误：传递 NULL 给 OutTrampoline 后还想调用原函数
ConfigureEptHook2(..., NULL);
// 现在没有 trampoline 地址，无法调用原函数
```

### Hook 调用链

```
Guest 调用目标函数
  ↓
EPT violation（执行权限为0）
  ↓
执行假页面代码："call $ + 5" + "jmp AsmGeneralDetourHook"
  ↓
AsmGeneralDetourHook（汇编）保存寄存器
  ↓
EptHook2GeneralDetourEventHandler（C）查找 Hook
  ↓
DispatchEventHiddenHookExecDetours（触发事件）
  ↓
返回 trampoline 地址给汇编
  ↓
汇编恢复寄存器，RET 到 trampoline
  ↓
Trampoline 执行原始指令并跳回原函数
```

## 关键设计决策

### 1. Split-View EPT 技术
- 读/写操作看到假页面（可修改代码）
- 执行操作看到真页面（原始代码）
- 在 [EptHook.c:1004-1100](code/hooks/ept-hook/EptHook.c#L1004-L1100) 的 `EptHookPerformPageHookMonitorAndInlineHook` 中实现

### 2. 多核同步
- 使用 NMI（不可屏蔽中断）广播
- 所有核心同步执行 Hook 操作
- 在 `code/broadcast/` 中实现

### 3. 事件驱动架构
- EPT Hook2 不是简单的函数替换
- 设计为事件分发系统
- Hook 函数作为事件处理器，不直接返回给 caller

### 4. 反检测（HyperEvade）
- CPUID 欺骗、RDTSC 调整
- 在 `code/transparency/` 中实现
- 配置项：`ActivateHyperEvadeProject`

## 重要注意事项

### EPT Hook 开发
1. **Trampoline 必需性**：ConfigureEptHook2 Hook 函数如果需要调用原函数，**必须**通过 OutTrampoline 参数获取 trampoline 地址
2. **地址区分**：原函数地址 ≠ trampoline 地址。调用原函数地址会触发 Hook 再次执行（无限循环）
3. **进程上下文**：Hook 在特定进程上下文中执行，注意 ProcessId 参数
4. **页面边界**：被 Hook 的函数不能跨越页面边界（4KB 对齐）

### 内存管理
- 使用 `PoolManagerRequestPool` 分配内存（自定义池管理器）
- Trampoline buffer 在非分页内存中
- EPT 页表结构必须物理连续

### 汇编交互
- C 代码通过 `extern` 声明汇编函数
- 汇编代码通过 `PUBLIC` 导出符号
- 参数传递遵循 x64 calling convention（RCX, RDX, R8, R9, stack）

### 调试技巧
- EPT violation 原因代码在 VM-Exit qualification 字段
- 使用 `SimpleHvLog` 输出调试信息
- 注意：过多的日志会显著降低性能

## 配置选项

[Configuration.h](include/config/Configuration.h) 中的关键配置：

```c
#define DebugMode                       FALSE  // 启用详细日志
#define ActivateHyperEvadeProject       TRUE   // 启用反检测
#define EnableInstantEventMechanism     TRUE   // 启用事件机制
```

## 依赖项

- **Zydis**（v4.1+）：指令反汇编（在 `dependencies/zydis/`）
- **Keystone**：指令汇编
- **ia32-doc**：Intel 指令定义（在 `dependencies/ia32-doc/`）

## 文档资源

详细的中文学习文档在 `learndocs/` 目录：
- `第六章-EPT-Hook技术深入.md`: EPT Hook 详细原理
- `第二章-Hyperdbg项目架构.md`: 整体架构说明
- `第三章-VMM回调机制.md`: 回调和事件系统

## 测试代码

[test.c](test.c) 包含 EPT Hook2 的实际使用示例：
- `ObpReferenceObjectByHandleWithTagHook`: Hook 内核对象引用
- `NtQuerySystemInformationHook`: Hook 系统信息查询
- 演示了如何正确使用 trampoline

## 常见问题排查

### 编译错误
- 确保安装 WDK 10.0+
- 检查 include 路径配置
- 注意 x64 平台选择

### 运行时崩溃
- 使用 WinDbg 分析 `MEMORY.DMP`
- 检查 `!analyze -v` 输出
- 常见原因：页面边界、空指针、IRQL 不匹配

### Hook 不生效
- 验证目标函数地址正确
- 检查是否在正确的进程上下文
- 确认 EPT 已正确初始化

### Trampoline 调用失败
- 确认 OutTrampoline 参数不为 NULL
- 验证 trampoline 地址已被正确赋值
- 检查函数签名是否与原函数匹配
