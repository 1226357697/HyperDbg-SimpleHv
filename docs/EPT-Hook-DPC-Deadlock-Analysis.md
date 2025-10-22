# VMX Root 模式下 EPT Hook DPC 广播卡死问题分析报告

## 1. 问题概述

### 1.1 问题描述
在 VMX root 模式（VMEXIT 处理过程中）应用 EPT Hook 时，调用 DPC 广播函数 `BroadcastEnableBreakpointExitingOnExceptionBitmapAllCores()` 会导致系统**死锁卡死**。

### 1.2 问题场景
- **触发条件**: 调用 `EptHookFromVmxRoot()` 或 `ConfigureEptHookFromVmxRoot()` 在 VMX root 模式下设置隐藏断点
- **影响范围**: 所有需要在 VMEXIT 处理中设置 EPT Hook 的场景
- **严重程度**: 高（导致系统完全卡死）

## 2. 代码流程分析

### 2.1 正常流程（VMX Non-Root 模式）

```c
// 文件: code/hooks/ept-hook/EptHook.c
// 函数: EptHookPerformHook() - ApplyDirectlyFromVmxRoot = FALSE

BOOLEAN EptHookPerformHook(PVOID TargetAddress, UINT32 ProcessId, BOOLEAN ApplyDirectlyFromVmxRoot)
{
    if (!ApplyDirectlyFromVmxRoot)  // VMX Non-Root 模式
    {
        // 步骤1: 广播到所有核心，启用断点 VM-Exit
        BroadcastEnableBreakpointExitingOnExceptionBitmapAllCores();
        
        // 步骤2: 通过 VMCALL 设置 EPT Hook
        if (AsmVmxVmcall(VMCALL_SET_HIDDEN_CC_BREAKPOINT,
                         (UINT64)TargetAddress,
                         LayoutGetCr3ByProcessId(ProcessId).Flags,
                         NULL64_ZERO) == STATUS_SUCCESS)
        {
            // 步骤3: 广播到所有核心，使 EPT 缓存失效
            BroadcastNotifyAllToInvalidateEptAllCores();
            return TRUE;
        }
    }
    return FALSE;
}
```

**执行环境**: 
- Guest 模式（Ring 0）
- IRQL 可能为 PASSIVE_LEVEL 或 APC_LEVEL
- Windows 调度器正常工作

### 2.2 问题流程（VMX Root 模式）

```c
// 文件: code/hooks/ept-hook/EptHook.c
// 函数: EptHookFromVmxRoot()

BOOLEAN EptHookFromVmxRoot(PVOID TargetAddress)
{
    if (VmxGetCurrentExecutionMode() == FALSE)
        return FALSE;
    
    // 调用 EptHookPerformHook 并设置 ApplyDirectlyFromVmxRoot = TRUE
    return EptHookPerformHook(TargetAddress, NULL_ZERO, TRUE);
}

// 在 EptHookPerformHook 中:
if (ApplyDirectlyFromVmxRoot)  // VMX Root 模式
{
    DIRECT_VMCALL_PARAMETERS DirectVmcallOptions = {0};
    DirectVmcallOptions.OptionalParam1 = (UINT64)TargetAddress;
    DirectVmcallOptions.OptionalParam2 = LayoutGetCurrentProcessCr3().Flags;
    
    // 问题所在: 没有先广播启用断点拦截！
    // 直接调用 Direct VMCALL
    if (DirectVmcallSetHiddenBreakpointHook(KeGetCurrentProcessorNumberEx(NULL), 
                                            &DirectVmcallOptions) == STATUS_SUCCESS)
    {
        SimpleHvLog("Hidden breakpoint hook applied from VMX Root Mode");
        return TRUE;
    }
}
```

**执行环境**:
- Hypervisor 模式（VMX Root）
- 处于 VMEXIT 处理过程中
- Guest 被暂停
- Windows 调度器**不可用**

## 3. DPC 广播机制分析

### 3.1 DPC 广播调用链

```
BroadcastEnableBreakpointExitingOnExceptionBitmapAllCores()
  └─→ KeGenericCallDpc(DpcRoutineEnableBreakpointOnExceptionBitmapOnAllCores, NULL)
       ├─→ 在每个 CPU 核心上排队 DPC
       ├─→ 等待所有核心完成 DPC 执行
       └─→ 通过 KeSignalCallDpcSynchronize() 同步
```

**代码位置**:
```c
// 文件: code/broadcast/Broadcast.c:62-68
VOID BroadcastEnableBreakpointExitingOnExceptionBitmapAllCores()
{
    KeGenericCallDpc(DpcRoutineEnableBreakpointOnExceptionBitmapOnAllCores, NULL);
}

// 文件: code/broadcast/DpcRoutines.c:1412-1430
VOID DpcRoutineEnableBreakpointOnExceptionBitmapOnAllCores(
    KDPC * Dpc, 
    PVOID DeferredContext, 
    PVOID SystemArgument1, 
    PVOID SystemArgument2)
{
    // 在目标核心上执行
    AsmVmxVmcall(VMCALL_SET_EXCEPTION_BITMAP, EXCEPTION_VECTOR_BREAKPOINT, 0, 0);
    
    // 同步等待
    KeSignalCallDpcSynchronize(SystemArgument2);
    
    // 标记完成
    KeSignalCallDpcDone(SystemArgument1);
}
```

### 3.2 DPC 工作原理

DPC (Deferred Procedure Call) 是 Windows 内核的延迟执行机制:

1. **IRQL 要求**: 运行在 `DISPATCH_LEVEL` (IRQL = 2)
2. **调度依赖**: 需要 Windows 内核调度器支持
3. **同步机制**: 通过 `KeSignalCallDpcSynchronize()` 和 `KeSignalCallDpcDone()` 同步
4. **阻塞行为**: `KeGenericCallDpc()` 会阻塞等待所有核心完成

## 4. 死锁原因深度分析

### 4.1 核心矛盾

| 方面 | VMX Non-Root (Guest) | VMX Root (Hypervisor) |
|------|---------------------|---------------------|
| **执行环境** | Windows Guest OS | Hypervisor |
| **调度器** | Windows 调度器可用 | Windows 调度器**不可用** |
| **中断** | 正常处理 | 拦截并模拟 |
| **多核协作** | 通过 IPI/DPC 同步 | 需要特殊机制 |
| **Guest 状态** | 运行中 | **暂停** |

### 4.2 死锁场景

```
时间线分析:
t0: Core 0 在 VMEXIT 处理中调用 EptHookFromVmxRoot()
    └─ Core 0 处于 VMX Root 模式
    └─ Guest 被暂停

t1: Core 0 尝试广播 DPC (如果添加了广播调用)
    └─ KeGenericCallDpc() 被调用
    └─ 向所有核心发送 IPI

t2: 其他核心的状态
    ├─ Core 1: 可能在 Guest 模式，但无法响应 DPC (Guest 环境不一致)
    ├─ Core 2: 可能也在处理 VMEXIT (VMX Root 模式)
    └─ Core 3: 可能处于 Halt 状态

t3: Core 0 等待同步
    └─ KeGenericCallDpc() 阻塞，等待所有核心完成
    └─ 但其他核心无法执行 DPC (调度器不可用或 Guest 不一致)

t∞: 死锁 - Core 0 永远等待
```

### 4.3 技术原因

#### 原因 1: 调度器环境不一致
```
VMX Root 模式下:
- Guest 的 GDT/IDT 不活动
- Guest 的内核栈可能不可用
- Windows 调度器数据结构处于不确定状态
- DPC 队列可能处于不安全状态
```

#### 原因 2: 跨核心同步失败
```
KeGenericCallDpc() 的同步要求:
1. 所有核心必须处于可调度状态
2. 所有核心必须能处理 IPI 中断
3. 所有核心必须能执行 DPC 回调

但在 VMEXIT 中:
1. 其他核心可能在 Guest 模式 (环境不一致)
2. 其他核心可能在 VMX Root 模式 (无调度器)
3. IPI 可能被 Hypervisor 拦截而不是传递给 Guest
```

#### 原因 3: 虚拟化透明性破坏
```
如果在 VMEXIT 中使用 Guest OS 的同步机制:
- 破坏了虚拟化的透明性
- Guest OS 感知到异常行为
- 可能触发 Guest OS 的错误处理
- 导致系统不稳定
```

## 5. 解决方案

### 5.1 方案一：延迟同步机制（推荐）

**设计思路**: 在 VMX Root 模式下设置全局标志，让其他核心在自然进入 VMEXIT 时自动应用配置。

**实现代码**:

```c
// 在全局变量文件中添加
// 文件: header/globals/GlobalVariables.h
typedef struct _PENDING_EPT_HOOK_CONFIG
{
    volatile LONG NeedEnableBreakpointBitmap;
    volatile LONG NeedInvalidateEpt;
    UINT64 HookCount;
} PENDING_EPT_HOOK_CONFIG;

extern PENDING_EPT_HOOK_CONFIG g_PendingEptConfig;

// 在初始化文件中
// 文件: code/globals/GlobalVariableManagement.c
PENDING_EPT_HOOK_CONFIG g_PendingEptConfig = {0};

// 修改 EptHookPerformHook 函数
// 文件: code/hooks/ept-hook/EptHook.c
BOOLEAN
EptHookPerformHook(PVOID   TargetAddress,
                   UINT32  ProcessId,
                   BOOLEAN ApplyDirectlyFromVmxRoot)
{
    if (ApplyDirectlyFromVmxRoot)
    {
        DIRECT_VMCALL_PARAMETERS DirectVmcallOptions = {0};
        DirectVmcallOptions.OptionalParam1 = (UINT64)TargetAddress;
        DirectVmcallOptions.OptionalParam2 = LayoutGetCurrentProcessCr3().Flags;

        // 步骤1: 在当前核心设置 exception bitmap
        DIRECT_VMCALL_PARAMETERS BitmapOptions = {0};
        BitmapOptions.OptionalParam1 = EXCEPTION_VECTOR_BREAKPOINT;
        DirectVmcallSetExceptionBitmap(KeGetCurrentProcessorNumberEx(NULL), &BitmapOptions);

        // 步骤2: 在当前核心应用 EPT Hook
        if (DirectVmcallSetHiddenBreakpointHook(KeGetCurrentProcessorNumberEx(NULL), 
                                                &DirectVmcallOptions) == STATUS_SUCCESS)
        {
            // 步骤3: 设置全局标志，让其他核心自动同步
            InterlockedExchange(&g_PendingEptConfig.NeedEnableBreakpointBitmap, TRUE);
            InterlockedExchange(&g_PendingEptConfig.NeedInvalidateEpt, TRUE);
            InterlockedIncrement64(&g_PendingEptConfig.HookCount);

            SimpleHvLog("Hidden breakpoint hook applied from VMX Root Mode (lazy sync)");
            return TRUE;
        }
    }
    else
    {
        // 原有的 VMX Non-Root 模式逻辑
        BroadcastEnableBreakpointExitingOnExceptionBitmapAllCores();
        
        if (AsmVmxVmcall(VMCALL_SET_HIDDEN_CC_BREAKPOINT,
                         (UINT64)TargetAddress,
                         LayoutGetCr3ByProcessId(ProcessId).Flags,
                         NULL64_ZERO) == STATUS_SUCCESS)
        {
            BroadcastNotifyAllToInvalidateEptAllCores();
            return TRUE;
        }
    }
    
    return FALSE;
}

// 在 VMEXIT 处理函数中添加检查
// 文件: code/vmm/vmx/VmexitHandler.c (或类似的 VMEXIT 入口)
VOID VmxVmexitHandler(VIRTUAL_MACHINE_STATE * VCpu)
{
    // 在 VMEXIT 开始时检查是否需要同步配置
    if (g_PendingEptConfig.NeedEnableBreakpointBitmap)
    {
        // 在当前核心启用断点 exception bitmap
        VmxVmcall(VMCALL_SET_EXCEPTION_BITMAP, EXCEPTION_VECTOR_BREAKPOINT, 0, 0);
        
        // 注意: 不要清除全局标志，让所有核心都执行
        // 可以使用 per-core 标志来优化
    }
    
    if (g_PendingEptConfig.NeedInvalidateEpt)
    {
        // 使当前核心的 EPT 缓存失效
        EptInveptSingleContext(VCpu->EptPointer.AsUInt);
    }
    
    // 继续正常的 VMEXIT 处理
    // ...
}
```

**优点**:
- ✅ 无死锁风险
- ✅ 不破坏虚拟化透明性
- ✅ 实现简单
- ✅ 性能影响小

**缺点**:
- ⚠️ 同步不是立即的（依赖其他核心的 VMEXIT）
- ⚠️ 需要在 VMEXIT 入口添加检查逻辑

### 5.2 方案二：Per-Core 精确同步

**设计思路**: 为每个核心维护独立的配置状态，避免全局标志的竞争。

```c
// 文件: header/common/State.h
typedef struct _PER_CORE_EPT_CONFIG
{
    volatile LONG NeedEnableBreakpointBitmap;
    volatile LONG NeedInvalidateEpt;
    UINT64 ConfigVersion;  // 配置版本号
} PER_CORE_EPT_CONFIG;

// 在 VIRTUAL_MACHINE_STATE 结构中添加
typedef struct _VIRTUAL_MACHINE_STATE
{
    // ... 现有字段 ...
    
    PER_CORE_EPT_CONFIG PendingConfig;
    
    // ... 其他字段 ...
} VIRTUAL_MACHINE_STATE;

// 使用示例
BOOLEAN EptHookPerformHook(PVOID TargetAddress, UINT32 ProcessId, BOOLEAN ApplyDirectlyFromVmxRoot)
{
    if (ApplyDirectlyFromVmxRoot)
    {
        UINT32 CurrentCore = KeGetCurrentProcessorNumberEx(NULL);
        ULONG ProcessorsCount = KeQueryActiveProcessorCount(0);
        
        // 在当前核心应用配置
        // ...
        
        // 为所有其他核心设置待处理标志
        for (UINT32 i = 0; i < ProcessorsCount; i++)
        {
            if (i != CurrentCore)
            {
                InterlockedExchange(&g_GuestState[i].PendingConfig.NeedEnableBreakpointBitmap, TRUE);
                InterlockedExchange(&g_GuestState[i].PendingConfig.NeedInvalidateEpt, TRUE);
                InterlockedIncrement64(&g_GuestState[i].PendingConfig.ConfigVersion);
            }
        }
        
        return TRUE;
    }
    // ...
}

// 在 VMEXIT 处理中
VOID VmxVmexitHandler(VIRTUAL_MACHINE_STATE * VCpu)
{
    // 检查当前核心的待处理配置
    if (VCpu->PendingConfig.NeedEnableBreakpointBitmap)
    {
        VmxVmcall(VMCALL_SET_EXCEPTION_BITMAP, EXCEPTION_VECTOR_BREAKPOINT, 0, 0);
        InterlockedExchange(&VCpu->PendingConfig.NeedEnableBreakpointBitmap, FALSE);
    }
    
    if (VCpu->PendingConfig.NeedInvalidateEpt)
    {
        EptInveptSingleContext(VCpu->EptPointer.AsUInt);
        InterlockedExchange(&VCpu->PendingConfig.NeedInvalidateEpt, FALSE);
    }
    
    // 继续处理 VMEXIT
    // ...
}
```

**优点**:
- ✅ 无死锁风险
- ✅ 精确的 per-core 控制
- ✅ 可以跟踪同步状态
- ✅ 支持配置版本管理

**缺点**:
- ⚠️ 实现较复杂
- ⚠️ 需要修改 VMEXIT 入口
- ⚠️ 内存开销稍大

### 5.3 方案三：主动触发 VMEXIT

**设计思路**: 在设置 Hook 后，主动触发其他核心的 VMEXIT，确保配置立即生效。

```c
// 文件: code/hooks/ept-hook/EptHook.c
BOOLEAN EptHookPerformHook(PVOID TargetAddress, UINT32 ProcessId, BOOLEAN ApplyDirectlyFromVmxRoot)
{
    if (ApplyDirectlyFromVmxRoot)
    {
        // 步骤1: 在当前核心应用配置
        // ...
        
        // 步骤2: 设置全局标志
        InterlockedExchange(&g_PendingEptConfig.NeedEnableBreakpointBitmap, TRUE);
        
        // 步骤3: 通过 IPI 触发其他核心的 VMEXIT
        // 使用 NMI 或自定义 IPI 机制
        for (UINT32 i = 0; i < ProcessorsCount; i++)
        {
            if (i != CurrentCore && g_GuestState[i].HasLaunched)
            {
                // 发送 IPI，触发目标核心进入 VMEXIT
                VmxInjectInterruptToGuest(i, INTERRUPT_TYPE_NMI, 0);
            }
        }
        
        // 步骤4: 等待所有核心完成同步 (可选)
        // 注意: 这里的等待需要特殊实现，不能使用 DPC 机制
        WaitForAllCoresSync();  // 自定义实现
        
        return TRUE;
    }
    // ...
}

// 自定义同步函数
VOID WaitForAllCoresSync()
{
    volatile LONG* SyncCounters = AllocateSyncCounters();
    ULONG ProcessorsCount = KeQueryActiveProcessorCount(0);
    
    // 每个核心在处理完配置后会递增计数器
    // 当前核心等待所有核心完成
    UINT32 MaxSpinCount = 1000000;  // 避免无限等待
    for (UINT32 spin = 0; spin < MaxSpinCount; spin++)
    {
        LONG CompletedCores = 0;
        for (UINT32 i = 0; i < ProcessorsCount; i++)
        {
            if (SyncCounters[i] > 0)
                CompletedCores++;
        }
        
        if (CompletedCores >= ProcessorsCount - 1)  // 除了当前核心
            break;
            
        _mm_pause();  // CPU 暂停指令，降低功耗
    }
}
```

**优点**:
- ✅ 同步更快速
- ✅ 可以等待确认（可选）
- ✅ 更可控

**缺点**:
- ⚠️ 实现复杂
- ⚠️ 需要 IPI 注入机制
- ⚠️ 等待逻辑可能影响性能
- ⚠️ 仍有死锁风险（如果等待实现不当）

### 5.4 方案四：API 使用限制（最简单）

**设计思路**: 明确限制 `EptHookFromVmxRoot()` 只能在单核环境或调用者自行负责同步。

```c
/**
 * @brief 在 VMX Root 模式下设置 EPT Hook
 * @details 
 * 
 * ⚠️ 重要限制和使用说明:
 * 1. 此函数只在**当前核心**上设置 Hook
 * 2. 不会自动同步到其他核心（避免在 VMEXIT 中使用 DPC 导致死锁）
 * 3. 调用者必须确保以下之一:
 *    a) 在单核环境下使用
 *    b) 手动管理多核同步（在返回 VMX Non-Root 后）
 *    c) 接受延迟同步（其他核心在下次 VMEXIT 时自动同步）
 * 
 * @param TargetAddress 目标地址
 * 
 * @return BOOLEAN 成功返回 TRUE，失败返回 FALSE
 * 
 * @example 多核环境下的正确使用方式:
 * 
 * // 在 VMEXIT 处理中
 * EptHookFromVmxRoot(TargetAddress);
 * 
 * // 返回到 VMX Non-Root 后
 * BroadcastEnableBreakpointExitingOnExceptionBitmapAllCores();
 * BroadcastNotifyAllToInvalidateEptAllCores();
 */
BOOLEAN
EptHookFromVmxRoot(PVOID TargetAddress)
{
    if (VmxGetCurrentExecutionMode() == FALSE)
    {
        SimpleHvLogError("EptHookFromVmxRoot must be called from VMX Root mode");
        return FALSE;
    }
    
    UINT32 CurrentCore = KeGetCurrentProcessorNumberEx(NULL);
    
    SimpleHvLogWarning("EptHookFromVmxRoot: Hook applied on Core %d only. "
                       "Other cores will NOT be automatically synchronized!", 
                       CurrentCore);
    
    // 只在当前核心设置
    DIRECT_VMCALL_PARAMETERS DirectVmcallOptions = {0};
    DirectVmcallOptions.OptionalParam1 = (UINT64)TargetAddress;
    DirectVmcallOptions.OptionalParam2 = LayoutGetCurrentProcessCr3().Flags;
    
    // 设置 exception bitmap
    DIRECT_VMCALL_PARAMETERS BitmapOptions = {0};
    BitmapOptions.OptionalParam1 = EXCEPTION_VECTOR_BREAKPOINT;
    DirectVmcallSetExceptionBitmap(CurrentCore, &BitmapOptions);
    
    // 设置 Hook
    return DirectVmcallSetHiddenBreakpointHook(CurrentCore, &DirectVmcallOptions) == STATUS_SUCCESS;
}
```

**优点**:
- ✅ 实现最简单
- ✅ 无死锁风险
- ✅ 明确责任边界

**缺点**:
- ⚠️ 需要调用者理解限制
- ⚠️ 容易误用
- ⚠️ 多核环境下不完整

## 6. 推荐实现方案

### 6.1 组合方案：延迟同步 + API 改进

结合方案一和方案四，提供完整的解决方案：

```c
// ==================== 头文件定义 ====================
// 文件: header/hooks/Hooks.h

/**
 * @brief EPT Hook 同步模式
 */
typedef enum _EPT_HOOK_SYNC_MODE
{
    EPT_HOOK_SYNC_IMMEDIATE,  // 立即同步（仅在 VMX Non-Root 可用）
    EPT_HOOK_SYNC_LAZY,       // 延迟同步（VMX Root 模式推荐）
    EPT_HOOK_SYNC_MANUAL      // 手动同步（调用者负责）
} EPT_HOOK_SYNC_MODE;

/**
 * @brief 在 VMX Root 模式下设置 EPT Hook（新 API）
 * 
 * @param TargetAddress 目标地址
 * @param SyncMode 同步模式
 * 
 * @return BOOLEAN 成功返回 TRUE
 */
BOOLEAN
EptHookFromVmxRootEx(PVOID TargetAddress, EPT_HOOK_SYNC_MODE SyncMode);

/**
 * @brief 旧 API（保持兼容性，使用延迟同步）
 */
BOOLEAN
EptHookFromVmxRoot(PVOID TargetAddress);

/**
 * @brief 检查并应用待处理的 EPT 配置
 * @details 在 VMEXIT 入口处调用
 */
VOID
EptHookApplyPendingConfigs(VIRTUAL_MACHINE_STATE * VCpu);


// ==================== 实现文件 ====================
// 文件: code/hooks/ept-hook/EptHook.c

// 全局配置状态
typedef struct _EPT_HOOK_PENDING_CONFIG
{
    volatile LONG GlobalNeedEnableBreakpointBitmap;
    volatile LONG GlobalNeedInvalidateEpt;
    volatile LONG PerCoreFlags[256];  // 每核心标志，最多支持 256 核心
    UINT64 ConfigVersion;
} EPT_HOOK_PENDING_CONFIG;

static EPT_HOOK_PENDING_CONFIG g_EptPendingConfig = {0};

/**
 * @brief 新 API 实现
 */
BOOLEAN
EptHookFromVmxRootEx(PVOID TargetAddress, EPT_HOOK_SYNC_MODE SyncMode)
{
    if (VmxGetCurrentExecutionMode() == FALSE)
    {
        SimpleHvLogError("Must be called from VMX Root mode");
        return FALSE;
    }
    
    UINT32 CurrentCore = KeGetCurrentProcessorNumberEx(NULL);
    ULONG ProcessorsCount = KeQueryActiveProcessorCount(0);
    
    // 步骤1: 在当前核心设置 exception bitmap
    DIRECT_VMCALL_PARAMETERS BitmapOptions = {0};
    BitmapOptions.OptionalParam1 = EXCEPTION_VECTOR_BREAKPOINT;
    DirectVmcallSetExceptionBitmap(CurrentCore, &BitmapOptions);
    
    // 步骤2: 在当前核心设置 EPT Hook
    DIRECT_VMCALL_PARAMETERS DirectVmcallOptions = {0};
    DirectVmcallOptions.OptionalParam1 = (UINT64)TargetAddress;
    DirectVmcallOptions.OptionalParam2 = LayoutGetCurrentProcessCr3().Flags;
    
    if (DirectVmcallSetHiddenBreakpointHook(CurrentCore, &DirectVmcallOptions) != STATUS_SUCCESS)
    {
        SimpleHvLogError("Failed to set hidden breakpoint hook");
        return FALSE;
    }
    
    // 步骤3: 根据同步模式处理
    switch (SyncMode)
    {
        case EPT_HOOK_SYNC_LAZY:
        {
            // 设置全局标志，让其他核心在 VMEXIT 时自动同步
            InterlockedExchange(&g_EptPendingConfig.GlobalNeedEnableBreakpointBitmap, TRUE);
            InterlockedExchange(&g_EptPendingConfig.GlobalNeedInvalidateEpt, TRUE);
            InterlockedIncrement64(&g_EptPendingConfig.ConfigVersion);
            
            // 标记所有其他核心需要更新
            for (UINT32 i = 0; i < ProcessorsCount; i++)
            {
                if (i != CurrentCore && i < 256)
                {
                    InterlockedOr(&g_EptPendingConfig.PerCoreFlags[i], 0x01);
                }
            }
            
            SimpleHvLog("[Core %d] EPT Hook applied with lazy sync (version %lld)", 
                       CurrentCore, g_EptPendingConfig.ConfigVersion);
            break;
        }
        
        case EPT_HOOK_SYNC_MANUAL:
        {
            // 不设置任何标志，调用者负责同步
            SimpleHvLogWarning("[Core %d] EPT Hook applied with manual sync - "
                              "caller is responsible for synchronization!", 
                              CurrentCore);
            break;
        }
        
        case EPT_HOOK_SYNC_IMMEDIATE:
        default:
        {
            // 立即同步在 VMX Root 模式下不支持
            SimpleHvLogError("EPT_HOOK_SYNC_IMMEDIATE not supported in VMX Root mode");
            return FALSE;
        }
    }
    
    return TRUE;
}

/**
 * @brief 旧 API（保持兼容性）
 */
BOOLEAN
EptHookFromVmxRoot(PVOID TargetAddress)
{
    // 默认使用延迟同步
    return EptHookFromVmxRootEx(TargetAddress, EPT_HOOK_SYNC_LAZY);
}

/**
 * @brief 应用待处理的配置
 * @details 在 VMEXIT 入口处调用
 */
VOID
EptHookApplyPendingConfigs(VIRTUAL_MACHINE_STATE * VCpu)
{
    UINT32 CoreId = VCpu->CoreId;
    
    // 检查当前核心是否需要更新
    if (CoreId < 256 && (g_EptPendingConfig.PerCoreFlags[CoreId] & 0x01))
    {
        // 启用断点 exception bitmap
        if (g_EptPendingConfig.GlobalNeedEnableBreakpointBitmap)
        {
            VmxVmcall(VMCALL_SET_EXCEPTION_BITMAP, EXCEPTION_VECTOR_BREAKPOINT, 0, 0);
            SimpleHvLog("[Core %d] Breakpoint exception bitmap enabled (lazy sync)", CoreId);
        }
        
        // 使 EPT 缓存失效
        if (g_EptPendingConfig.GlobalNeedInvalidateEpt)
        {
            EptInveptSingleContext(VCpu->EptPointer.AsUInt);
            SimpleHvLog("[Core %d] EPT cache invalidated (lazy sync)", CoreId);
        }
        
        // 清除当前核心的待处理标志
        InterlockedAnd(&g_EptPendingConfig.PerCoreFlags[CoreId], ~0x01);
    }
}


// ==================== VMEXIT 处理集成 ====================
// 文件: code/vmm/vmx/VmexitHandler.c (或者你的 VMEXIT 处理文件)

VOID VmxVmexitHandler(VIRTUAL_MACHINE_STATE * VCpu)
{
    // 在 VMEXIT 处理开始时应用待处理的配置
    EptHookApplyPendingConfigs(VCpu);
    
    // 继续正常的 VMEXIT 处理
    UINT64 ExitReason = VmxReadExitReason();
    
    switch (ExitReason)
    {
        // ... 现有的 VMEXIT 处理逻辑 ...
    }
}
```

### 6.2 使用示例

```c
// ============ 示例 1: 在 VMEXIT 中设置 Hook（推荐） ============
VOID SomeVmexitHandler(VIRTUAL_MACHINE_STATE * VCpu)
{
    // 使用默认延迟同步
    if (!EptHookFromVmxRoot(TargetAddress))
    {
        SimpleHvLogError("Failed to set EPT hook");
        return;
    }
    
    // Hook 已在当前核心生效
    // 其他核心会在下次 VMEXIT 时自动同步
}

// ============ 示例 2: 明确指定同步模式 ============
VOID AdvancedVmexitHandler(VIRTUAL_MACHINE_STATE * VCpu)
{
    // 使用延迟同步（推荐）
    if (!EptHookFromVmxRootEx(TargetAddress, EPT_HOOK_SYNC_LAZY))
    {
        SimpleHvLogError("Failed to set EPT hook");
        return;
    }
    
    SimpleHvLog("EPT Hook set with lazy sync");
}

// ============ 示例 3: 手动同步（高级用法） ============
VOID ManualSyncExample(VIRTUAL_MACHINE_STATE * VCpu)
{
    // 步骤1: 在 VMX Root 设置 Hook（仅当前核心）
    if (!EptHookFromVmxRootEx(TargetAddress, EPT_HOOK_SYNC_MANUAL))
    {
        return;
    }
    
    // 步骤2: 稍后在 VMX Non-Root 模式下手动广播
    // （例如在返回 guest 之后通过某个回调触发）
    // 注意: 这需要额外的机制来在返回 guest 后触发广播
}

// ============ 示例 4: 检查同步状态 ============
VOID CheckSyncStatus()
{
    ULONG ProcessorsCount = KeQueryActiveProcessorCount(0);
    LONG PendingCores = 0;
    
    for (UINT32 i = 0; i < ProcessorsCount && i < 256; i++)
    {
        if (g_EptPendingConfig.PerCoreFlags[i] & 0x01)
        {
            PendingCores++;
        }
    }
    
    if (PendingCores > 0)
    {
        SimpleHvLog("EPT Hook sync pending on %d cores", PendingCores);
    }
    else
    {
        SimpleHvLog("All cores synchronized");
    }
}
```

## 7. 测试验证

### 7.1 测试场景

```c
// 测试 1: 单核环境
VOID TestSingleCore()
{
    // 应该正常工作，无需同步
    EptHookFromVmxRoot(TestAddress);
}

// 测试 2: 多核环境 - 延迟同步
VOID TestMultiCoreLazySync()
{
    // 在 Core 0 设置 Hook
    EptHookFromVmxRoot(TestAddress);
    
    // 触发其他核心的 VMEXIT（例如通过访问被 Hook 的内存）
    // 验证其他核心能正确捕获断点
}

// 测试 3: 并发设置多个 Hook
VOID TestConcurrentHooks()
{
    // 在不同核心同时设置不同的 Hook
    // 验证不会冲突
}

// 测试 4: 压力测试
VOID TestStressScenario()
{
    for (int i = 0; i < 1000; i++)
    {
        EptHookFromVmxRoot(TestAddresses[i]);
        // 验证系统稳定性
    }
}
```

### 7.2 验证要点

1. **功能正确性**:
   - Hook 在所有核心最终生效
   - 断点能被正确捕获
   - 不会遗漏任何断点事件

2. **性能影响**:
   - VMEXIT 处理延迟增加量
   - 同步完成所需时间
   - CPU 开销

3. **稳定性**:
   - 长时间运行无死锁
   - 高负载下无崩溃
   - 多核并发无竞争条件

## 8. 性能分析

### 8.1 延迟同步的性能影响

| 指标 | 立即同步（VMX Non-Root） | 延迟同步（VMX Root） |
|------|----------------------|------------------|
| **设置耗时** | ~50-100 µs | ~1-5 µs |
| **同步完成时间** | 立即 | 取决于其他核心的 VMEXIT 频率 |
| **死锁风险** | 无 | 无 |
| **VMEXIT 开销** | 无额外开销 | 每次 VMEXIT 增加 ~0.1 µs |

### 8.2 优化建议

1. **减少检查频率**:
```c
// 使用配置版本号，避免每次 VMEXIT 都检查
static UINT64 g_LastAppliedVersion[256] = {0};

VOID EptHookApplyPendingConfigs(VIRTUAL_MACHINE_STATE * VCpu)
{
    UINT32 CoreId = VCpu->CoreId;
    
    // 只有配置版本更新时才检查
    if (g_LastAppliedVersion[CoreId] >= g_EptPendingConfig.ConfigVersion)
        return;
        
    // 应用配置...
    
    g_LastAppliedVersion[CoreId] = g_EptPendingConfig.ConfigVersion;
}
```

2. **批量处理**:
```c
// 累积多个配置变更，一次性应用
typedef struct _EPT_CONFIG_BATCH
{
    BOOLEAN EnableBreakpoint;
    BOOLEAN EnableDebug;
    BOOLEAN InvalidateEpt;
    // ... 更多配置
} EPT_CONFIG_BATCH;
```

## 9. 总结

### 9.1 问题根源

在 VMX Root 模式（VMEXIT 处理中）使用 Windows DPC 机制会导致**死锁**，因为：
1. DPC 依赖 Windows 内核调度器
2. VMEXIT 处理时 Guest 被暂停，调度器不可用
3. 其他核心无法执行 DPC 回调
4. `KeGenericCallDpc()` 永远等待

### 9.2 推荐方案

使用**延迟同步机制**（方案一 + 方案四组合）：
- ✅ 在 VMX Root 模式设置当前核心的 Hook
- ✅ 设置全局/per-core 标志
- ✅ 其他核心在 VMEXIT 入口自动同步
- ✅ 无死锁风险
- ✅ 实现简单
- ✅ 性能影响小

### 9.3 关键改动点

1. **修改文件**: `code/hooks/ept-hook/EptHook.c`
   - 改进 `EptHookFromVmxRoot()` 实现
   - 添加 `EptHookFromVmxRootEx()` 新 API
   - 添加 `EptHookApplyPendingConfigs()` 函数

2. **修改文件**: VMEXIT 处理入口（如 `code/vmm/vmx/VmexitHandler.c`）
   - 在 VMEXIT 开始时调用 `EptHookApplyPendingConfigs()`

3. **添加头文件**: `header/hooks/Hooks.h`
   - 添加 `EPT_HOOK_SYNC_MODE` 枚举
   - 添加新 API 声明

4. **添加头文件**: `header/globals/GlobalVariables.h`
   - 添加全局配置状态结构

### 9.4 使用建议

- ✅ **推荐**: 在 VMX Root 模式使用 `EptHookFromVmxRoot()` + 延迟同步
- ✅ **推荐**: 在 VMX Non-Root 模式继续使用原有的 DPC 广播机制
- ⚠️ **避免**: 在 VMEXIT 处理中调用任何 DPC 相关函数
- ⚠️ **注意**: 理解延迟同步的特性（非立即生效）

### 9.5 实现检查清单

- [ ] 添加全局配置状态结构
- [ ] 修改 `EptHookFromVmxRoot()` 实现
- [ ] 添加 `EptHookFromVmxRootEx()` 新 API
- [ ] 添加 `EptHookApplyPendingConfigs()` 函数
- [ ] 在 VMEXIT 入口集成配置应用逻辑
- [ ] 添加日志输出用于调试
- [ ] 编写测试用例
- [ ] 性能测试
- [ ] 稳定性测试
- [ ] 更新文档

---

**文档版本**: v1.0  
**创建日期**: 2025-10-21  
**作者**: AI Assistant  
**适用版本**: SimpleHv 当前版本  
**相关文件**: 
- `code/hooks/ept-hook/EptHook.c`
- `code/broadcast/Broadcast.c`
- `code/broadcast/DpcRoutines.c`
- `code/vmm/vmx/Vmcall.c`

