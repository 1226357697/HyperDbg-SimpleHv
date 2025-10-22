# 第三章：VMM回调机制详解

## 3.1 回调机制概述

HyperDbg采用回调机制实现VMM核心（hyperhv）与调试器逻辑（hyperkd）的解耦。这是一种经典的设计模式，类似于GUI编程中的事件处理机制。

### 3.1.1 为什么需要回调机制？

**问题**：如何让VMM核心保持独立，同时支持复杂的调试功能？

**传统方案的问题**：
```c
// ❌ 紧耦合设计
void VmxVmexitHandler(PGUEST_REGS GuestRegs) {
    switch (ExitReason) {
        case VMX_EXIT_REASON_EXECUTE_CPUID:
            // 直接调用调试器函数
            DebuggerHandleCpuid();  // VMM依赖调试器代码
            break;
    }
}
```

**HyperDbg的解决方案**：
```c
// ✅ 解耦设计
void VmxVmexitHandler(PGUEST_REGS GuestRegs) {
    switch (ExitReason) {
        case VMX_EXIT_REASON_EXECUTE_CPUID:
            // 调用回调，VMM不知道具体实现
            VmmCallbackTriggerEvents(CPUID_INSTRUCTION_EXECUTION, ...);
            break;
    }
}
```

**优势**：
1. **模块独立**：hyperhv可以独立编译和测试
2. **灵活扩展**：轻松添加新功能或替换实现
3. **代码重用**：hyperhv可以用于其他项目
4. **清晰职责**：VMM负责虚拟化，调试器负责调试逻辑

### 3.1.2 回调设计模式

```
初始化阶段（启动时一次性）：
┌─────────────────────────────────────────┐
│  LoaderInitVmmAndDebugger()             │
│                                         │
│  1. 创建回调结构体                      │
│     VMM_CALLBACKS VmmCallbacks = {      │
│         .VmmCallbackTriggerEvents =     │
│             DebuggerTriggerEvents,      │
│         .DebuggingCallback...Exception =│
│             BreakpointHandleBreakpoints,│
│         ...                             │
│     };                                  │
│                                         │
│  2. 传递给VMM                           │
│     VmFuncInitVmm(&VmmCallbacks)        │
│         ↓                               │
│     HvInitVmm(&VmmCallbacks)            │
│         ↓                               │
│     RtlCopyMemory(&g_Callbacks, ...)    │
│                                         │
│  3. 回调设置完成，等待触发              │
└─────────────────────────────────────────┘

运行阶段（VM-Exit时被动触发）：
┌─────────────────────────────────────────┐
│  VM-Exit发生                            │
│         ↓                               │
│  VmxVmexitHandler()                     │
│         ↓                               │
│  DispatchEventXxx()                     │
│         ↓                               │
│  VmmCallbackXxx() [Wrapper]             │
│         ↓                               │
│  检查：if (g_Callbacks.Xxx != NULL)     │
│         ↓                               │
│  调用：g_Callbacks.Xxx(...)             │
│         ↓                               │
│  实际回调函数执行                       │
└─────────────────────────────────────────┘
```

---

## 3.2 回调结构定义

### 3.2.1 VMM_CALLBACKS结构

**文件位置**：`hyperdbg/include/SDK/modules/VMM.h`

```c
/**
 * @brief VMM模块需要的所有回调函数原型
 */
typedef struct _VMM_CALLBACKS {

    //
    // ========================================
    // 日志回调（Hyperlog Callbacks）
    // ========================================
    //

    /**
     * @brief 准备并发送消息到队列
     */
    LOG_CALLBACK_PREPARE_AND_SEND_MESSAGE_TO_QUEUE
        LogCallbackPrepareAndSendMessageToQueueWrapper;

    /**
     * @brief 发送消息到消息追踪缓冲区
     */
    LOG_CALLBACK_SEND_MESSAGE_TO_QUEUE
        LogCallbackSendMessageToQueue;

    /**
     * @brief 发送缓冲区数据
     */
    LOG_CALLBACK_SEND_BUFFER
        LogCallbackSendBuffer;

    /**
     * @brief 检查缓冲区是否已满
     */
    LOG_CALLBACK_CHECK_IF_BUFFER_IS_FULL
        LogCallbackCheckIfBufferIsFull;

    //
    // ========================================
    // VMM核心回调（VMM Core Callbacks）
    // ========================================
    //

    /**
     * @brief 触发事件处理
     * @details 用于处理各种VM-Exit事件（CPUID、MSR、EPT等）
     */
    VMM_CALLBACK_TRIGGER_EVENTS
        VmmCallbackTriggerEvents;

    /**
     * @brief 设置最后错误码
     */
    VMM_CALLBACK_SET_LAST_ERROR
        VmmCallbackSetLastError;

    /**
     * @brief VMCALL处理器
     * @details 处理来自Guest的Hypercall
     */
    VMM_CALLBACK_VMCALL_HANDLER
        VmmCallbackVmcallHandler;

    /**
     * @brief 注册的MTF处理器
     * @details 当RegisterBreakOnMtf设置时调用
     */
    VMM_CALLBACK_REGISTERED_MTF_HANDLER
        VmmCallbackRegisteredMtfHandler;

    /**
     * @brief NMI广播请求处理器
     * @details 用于暂停所有核心进行调试
     */
    VMM_CALLBACK_NMI_BROADCAST_REQUEST_HANDLER
        VmmCallbackNmiBroadcastRequestHandler;

    /**
     * @brief 查询/终止受保护资源
     */
    VMM_CALLBACK_QUERY_TERMINATE_PROTECTED_RESOURCE
        VmmCallbackQueryTerminateProtectedResource;

    /**
     * @brief 恢复EPT状态
     */
    VMM_CALLBACK_RESTORE_EPT_STATE
        VmmCallbackRestoreEptState;

    /**
     * @brief 检查未处理的EPT违规
     */
    VMM_CALLBACK_CHECK_UNHANDLED_EPT_VIOLATION
        VmmCallbackCheckUnhandledEptViolations;

    //
    // ========================================
    // 调试回调（Debugging Callbacks）
    // ========================================
    //

    /**
     * @brief 处理断点异常（#BP）
     */
    DEBUGGING_CALLBACK_HANDLE_BREAKPOINT_EXCEPTION
        DebuggingCallbackHandleBreakpointException;

    /**
     * @brief 处理调试断点异常（硬件断点）
     */
    DEBUGGING_CALLBACK_HANDLE_DEBUG_BREAKPOINT_EXCEPTION
        DebuggingCallbackHandleDebugBreakpointException;

    /**
     * @brief 检查线程拦截（用户态调试器）
     */
    DEBUGGING_CALLBACK_CHECK_THREAD_INTERCEPTION
        DebuggingCallbackCheckThreadInterception;

    /**
     * @brief 检查并处理断点重新应用
     * @details MTF后重新应用断点
     */
    BREAKPOINT_CHECK_AND_HANDLE_REAPPLYING_BREAKPOINT
        BreakpointCheckAndHandleReApplyingBreakpoint;

    /**
     * @brief 检查进程或线程变化
     */
    INTERCEPTION_CALLBACK_TRIGGER_CLOCK_AND_IPI
        DebuggerCheckProcessOrThreadChange;

    /**
     * @brief 检查并处理NMI回调
     */
    KD_CHECK_AND_HANDLE_NMI_CALLBACK
        KdCheckAndHandleNmiCallback;

    /**
     * @brief 查询调试器的线程/进程跟踪详情
     */
    KD_QUERY_DEBUGGER_THREAD_OR_PROCESS_TRACING_DETAILS_BY_CORE_ID
        KdQueryDebuggerQueryThreadOrProcessTracingDetailsByCoreId;

    //
    // ========================================
    // 拦截回调（Interception Callbacks）
    // ========================================
    //

    /**
     * @brief CR3变化触发（进程切换）
     */
    INTERCEPTION_CALLBACK_TRIGGER_CR3_CHANGE
        InterceptionCallbackTriggerCr3ProcessChange;

} VMM_CALLBACKS, *PVMM_CALLBACKS;
```

### 3.2.2 回调函数类型定义

```c
//
// ========================================
// 日志回调类型
// ========================================
//

typedef BOOLEAN (*LOG_CALLBACK_PREPARE_AND_SEND_MESSAGE_TO_QUEUE)(
    UINT32       OperationCode,
    BOOLEAN      IsImmediateMessage,
    BOOLEAN      ShowCurrentSystemTime,
    BOOLEAN      Priority,
    const char * Fmt,
    va_list      ArgList
);

typedef BOOLEAN (*LOG_CALLBACK_SEND_MESSAGE_TO_QUEUE)(
    UINT32  OperationCode,
    BOOLEAN IsImmediateMessage,
    CHAR *  LogMessage,
    UINT32  BufferLen,
    BOOLEAN Priority
);

typedef BOOLEAN (*LOG_CALLBACK_SEND_BUFFER)(
    _In_ UINT32                          OperationCode,
    _In_reads_bytes_(BufferLength) PVOID Buffer,
    _In_ UINT32                          BufferLength,
    _In_ BOOLEAN                         Priority
);

typedef BOOLEAN (*LOG_CALLBACK_CHECK_IF_BUFFER_IS_FULL)(
    BOOLEAN Priority
);

//
// ========================================
// VMM回调类型
// ========================================
//

typedef VMM_CALLBACK_TRIGGERING_EVENT_STATUS_TYPE (*VMM_CALLBACK_TRIGGER_EVENTS)(
    VMM_EVENT_TYPE_ENUM                   EventType,
    VMM_CALLBACK_EVENT_CALLING_STAGE_TYPE CallingStage,
    PVOID                                 Context,
    BOOLEAN *                             PostEventRequired,
    GUEST_REGS *                          Regs
);

typedef VOID (*VMM_CALLBACK_SET_LAST_ERROR)(
    UINT32 LastError
);

typedef BOOLEAN (*VMM_CALLBACK_VMCALL_HANDLER)(
    UINT32 CoreId,
    UINT64 VmcallNumber,
    UINT64 OptionalParam1,
    UINT64 OptionalParam2,
    UINT64 OptionalParam3
);

typedef VOID (*VMM_CALLBACK_REGISTERED_MTF_HANDLER)(
    UINT32 CoreId
);

typedef VOID (*VMM_CALLBACK_NMI_BROADCAST_REQUEST_HANDLER)(
    UINT32  CoreId,
    BOOLEAN IsOnVmxNmiHandler
);

typedef BOOLEAN (*VMM_CALLBACK_QUERY_TERMINATE_PROTECTED_RESOURCE)(
    UINT32                               CoreId,
    PROTECTED_HV_RESOURCES_TYPE          ResourceType,
    PVOID                                Context,
    PROTECTED_HV_RESOURCES_PASSING_OVERS PassOver
);

typedef BOOLEAN (*VMM_CALLBACK_RESTORE_EPT_STATE)(
    UINT32 CoreId
);

typedef BOOLEAN (*VMM_CALLBACK_CHECK_UNHANDLED_EPT_VIOLATION)(
    UINT32 CoreId,
    UINT64 ViolationQualification,
    UINT64 GuestPhysicalAddr
);

//
// ========================================
// 调试回调类型
// ========================================
//

typedef BOOLEAN (*DEBUGGING_CALLBACK_HANDLE_BREAKPOINT_EXCEPTION)(
    UINT32 CoreId
);

typedef BOOLEAN (*DEBUGGING_CALLBACK_HANDLE_DEBUG_BREAKPOINT_EXCEPTION)(
    UINT32 CoreId
);

typedef BOOLEAN (*DEBUGGING_CALLBACK_CHECK_THREAD_INTERCEPTION)(
    UINT32 CoreId
);

typedef BOOLEAN (*BREAKPOINT_CHECK_AND_HANDLE_REAPPLYING_BREAKPOINT)(
    UINT32 CoreId
);

//
// ========================================
// 拦截回调类型
// ========================================
//

typedef VOID (*INTERCEPTION_CALLBACK_TRIGGER_CR3_CHANGE)(
    UINT32 CoreId
);
```

---

## 3.3 回调初始化

### 3.3.1 完整的初始化代码

**文件位置**：`hyperdbg/hyperkd/code/driver/Loader.c`

```c
/**
 * @brief 初始化VMM和调试器
 *
 * @return BOOLEAN 成功返回TRUE，失败返回FALSE
 */
BOOLEAN LoaderInitVmmAndDebugger() {
    MESSAGE_TRACING_CALLBACKS MsgTracingCallbacks = {0};
    VMM_CALLBACKS             VmmCallbacks        = {0};

    // 允许接收IOCTL
    g_AllowIOCTLFromUsermode = TRUE;

    //
    // ========================================
    // 填充消息追踪回调
    // ========================================
    //

    /**
     * @brief 检查当前执行模式（VMX root或non-root）
     */
    MsgTracingCallbacks.VmxOperationCheck = VmFuncVmxGetCurrentExecutionMode;

    /**
     * @brief 检查是否应该立即发送消息
     * @details 在某些情况下（如断点命中），消息需要立即发送
     */
    MsgTracingCallbacks.CheckImmediateMessageSending = KdCheckImmediateMessagingMechanism;

    /**
     * @brief 发送立即消息到调试器
     * @details 通过串口直接发送，不经过缓冲区
     */
    MsgTracingCallbacks.SendImmediateMessage = KdLoggingResponsePacketToDebugger;

    //
    // ========================================
    // 填充日志回调（用于hyperlog在VMM中使用）
    // ========================================
    //

    /**
     * @brief 准备并发送消息到队列的包装函数
     */
    VmmCallbacks.LogCallbackPrepareAndSendMessageToQueueWrapper =
        LogCallbackPrepareAndSendMessageToQueueWrapper;

    /**
     * @brief 发送消息到队列
     */
    VmmCallbacks.LogCallbackSendMessageToQueue =
        LogCallbackSendMessageToQueue;

    /**
     * @brief 发送缓冲区数据
     */
    VmmCallbacks.LogCallbackSendBuffer =
        LogCallbackSendBuffer;

    /**
     * @brief 检查缓冲区是否已满
     */
    VmmCallbacks.LogCallbackCheckIfBufferIsFull =
        LogCallbackCheckIfBufferIsFull;

    //
    // ========================================
    // 填充VMM核心回调
    // ========================================
    //

    /**
     * @brief 触发事件处理
     * @details 这是最重要的回调，处理各种VM-Exit事件
     */
    VmmCallbacks.VmmCallbackTriggerEvents =
        DebuggerTriggerEvents;

    /**
     * @brief 设置最后的错误状态
     */
    VmmCallbacks.VmmCallbackSetLastError =
        DebuggerSetLastError;

    /**
     * @brief 调试器的VMCALL处理器
     * @details 处理调试器特定的VMCALL
     */
    VmmCallbacks.VmmCallbackVmcallHandler =
        DebuggerVmcallHandler;

    /**
     * @brief 注册的MTF处理器
     * @details 用于单步执行、指令跟踪等
     */
    VmmCallbacks.VmmCallbackRegisteredMtfHandler =
        KdHandleRegisteredMtfCallback;

    /**
     * @brief NMI广播请求处理器
     * @details 暂停所有核心进行调试
     */
    VmmCallbacks.VmmCallbackNmiBroadcastRequestHandler =
        KdHandleNmiBroadcastDebugBreaks;

    /**
     * @brief 查询/终止受保护资源
     */
    VmmCallbacks.VmmCallbackQueryTerminateProtectedResource =
        TerminateQueryDebuggerResource;

    /**
     * @brief 恢复EPT状态
     * @details 检查用户态访问模块详情
     */
    VmmCallbacks.VmmCallbackRestoreEptState =
        UserAccessCheckForLoadedModuleDetails;

    /**
     * @brief 检查未处理的EPT违规
     */
    VmmCallbacks.VmmCallbackCheckUnhandledEptViolations =
        AttachingCheckUnhandledEptViolation;

    //
    // ========================================
    // 填充调试回调
    // ========================================
    //

    /**
     * @brief 处理断点异常（INT3）
     */
    VmmCallbacks.DebuggingCallbackHandleBreakpointException =
        BreakpointHandleBreakpoints;

    /**
     * @brief 处理调试断点异常（硬件断点）
     */
    VmmCallbacks.DebuggingCallbackHandleDebugBreakpointException =
        BreakpointCheckAndHandleDebugBreakpoint;

    /**
     * @brief 检查并处理断点重新应用
     * @details MTF后重新设置断点
     */
    VmmCallbacks.BreakpointCheckAndHandleReApplyingBreakpoint =
        BreakpointCheckAndHandleReApplyingBreakpoint;

    /**
     * @brief 检查进程或线程变化
     */
    VmmCallbacks.DebuggerCheckProcessOrThreadChange =
        DebuggerCheckProcessOrThreadChange;

    /**
     * @brief 检查线程拦截（用户态调试器）
     */
    VmmCallbacks.DebuggingCallbackCheckThreadInterception =
        AttachingCheckThreadInterceptionWithUserDebugger;

    /**
     * @brief 检查并处理NMI回调
     */
    VmmCallbacks.KdCheckAndHandleNmiCallback =
        KdCheckAndHandleNmiCallback;

    /**
     * @brief 查询线程或进程跟踪详情
     */
    VmmCallbacks.KdQueryDebuggerQueryThreadOrProcessTracingDetailsByCoreId =
        KdQueryDebuggerQueryThreadOrProcessTracingDetailsByCoreId;

    //
    // ========================================
    // 填充拦截回调
    // ========================================
    //

    /**
     * @brief CR3进程变化触发
     * @details 监控进程切换
     */
    VmmCallbacks.InterceptionCallbackTriggerCr3ProcessChange =
        ProcessTriggerCr3ProcessChange;

    //
    // ========================================
    // 初始化各个模块
    // ========================================
    //

    // 1. 初始化消息追踪模块
    if (LogInitialize(&MsgTracingCallbacks)) {

        // 2. 初始化VMM
        if (VmFuncInitVmm(&VmmCallbacks)) {
            LogDebugInfo("HyperDbg's hypervisor loaded successfully");

            // 3. 初始化调试器
            if (DebuggerInitialize()) {
                LogDebugInfo("HyperDbg's debugger loaded successfully");

                // 设置标志，阻止其他进程获取句柄
                g_HandleInUse = TRUE;

                return TRUE;
            } else {
                LogError("Err, HyperDbg's debugger was not loaded");
            }
        } else {
            LogError("Err, HyperDbg's hypervisor was not loaded");
        }
    } else {
        LogError("Err, HyperDbg's message tracing module was not loaded");
    }

    // 初始化失败
    g_AllowIOCTLFromUsermode = FALSE;

    return FALSE;
}
```

### 3.3.2 回调保存到VMM

**文件位置**：`hyperdbg/hyperhv/code/vmm/vmx/Hv.c`

```c
/**
 * @brief 初始化VMM
 *
 * @param VmmCallbacks 回调函数结构体指针
 * @return BOOLEAN 成功返回TRUE
 */
BOOLEAN HvInitVmm(VMM_CALLBACKS * VmmCallbacks) {
    ULONG   ProcessorsCount;
    BOOLEAN Result = FALSE;

    //
    // ========================================
    // 保存回调到全局变量
    // ========================================
    //
    RtlCopyMemory(&g_Callbacks, VmmCallbacks, sizeof(VMM_CALLBACKS));

    LogInfo("VMM callbacks saved successfully");

    //
    // ========================================
    // 检查处理器兼容性
    // ========================================
    //
    CompatibilityCheckPerformChecks();

    //
    // ========================================
    // 分配虚拟机状态数组
    // ========================================
    //
    // 每个CPU核心一个VIRTUAL_MACHINE_STATE
    Result = GlobalGuestStateAllocateZeroedMemory();

    if (!Result) {
        LogError("Failed to allocate guest state");
        return FALSE;
    }

    //
    // ========================================
    // 初始化核心ID
    // ========================================
    //
    ProcessorsCount = KeQueryActiveProcessorCount(0);

    for (UINT32 i = 0; i < ProcessorsCount; i++) {
        g_GuestState[i].CoreId = i;
    }

    //
    // ========================================
    // 初始化内存映射器
    // ========================================
    //
    MemoryMapperInitialize();

    //
    // ========================================
    // 确保透明模式已禁用
    // ========================================
    //
    if (g_CheckForFootprints) {
        TransparentUnhideDebuggerWrapper(NULL);
    }

    //
    // ========================================
    // 初始化标志
    // ========================================
    //
    g_WaitingForInterruptWindowToInjectPageFault = FALSE;

    //
    // ========================================
    // 启动VMX虚拟化
    // ========================================
    //
    return VmxInitialize();
}
```

---

## 3.4 Wrapper函数实现

### 3.4.1 Wrapper函数的作用

Wrapper函数位于VMM和实际回调之间，提供：
1. **NULL检查**：防止调用未设置的回调
2. **默认行为**：回调未设置时的处理
3. **统一接口**：为VMM内部提供一致的调用接口
4. **调试支持**：可以在Wrapper中添加日志

### 3.4.2 Wrapper函数实现

**文件位置**：`hyperdbg/hyperhv/code/interface/Callback.c`

```c
//
// ========================================
// 事件触发回调Wrapper
// ========================================
//

/**
 * @brief 触发事件的回调包装函数
 */
VMM_CALLBACK_TRIGGERING_EVENT_STATUS_TYPE
VmmCallbackTriggerEvents(VMM_EVENT_TYPE_ENUM                   EventType,
                         VMM_CALLBACK_EVENT_CALLING_STAGE_TYPE CallingStage,
                         PVOID                                 Context,
                         BOOLEAN *                             PostEventRequired,
                         GUEST_REGS *                          Regs) {
    // NULL检查
    if (g_Callbacks.VmmCallbackTriggerEvents == NULL) {
        // 回调未设置，返回默认值（忽略事件）
        return VMM_CALLBACK_TRIGGERING_EVENT_STATUS_SUCCESSFUL_NO_INITIALIZED;
    }

    // 调用实际回调
    return g_Callbacks.VmmCallbackTriggerEvents(
        EventType,
        CallingStage,
        Context,
        PostEventRequired,
        Regs
    );
}

//
// ========================================
// 错误设置回调Wrapper
// ========================================
//

/**
 * @brief 设置最后错误的回调包装函数
 */
VOID VmmCallbackSetLastError(UINT32 LastError) {
    // NULL检查
    if (g_Callbacks.VmmCallbackSetLastError == NULL) {
        // 忽略设置错误
        return;
    }

    // 调用实际回调
    g_Callbacks.VmmCallbackSetLastError(LastError);
}

//
// ========================================
// VMCALL处理回调Wrapper
// ========================================
//

/**
 * @brief VMCALL处理的回调包装函数
 */
BOOLEAN VmmCallbackVmcallHandler(UINT32 CoreId,
                                 UINT64 VmcallNumber,
                                 UINT64 OptionalParam1,
                                 UINT64 OptionalParam2,
                                 UINT64 OptionalParam3) {
    // NULL检查
    if (g_Callbacks.VmmCallbackVmcallHandler == NULL) {
        // 忽略外部VMCALL
        return FALSE;
    }

    // 调用实际回调
    return g_Callbacks.VmmCallbackVmcallHandler(
        CoreId,
        VmcallNumber,
        OptionalParam1,
        OptionalParam2,
        OptionalParam3
    );
}

//
// ========================================
// MTF处理回调Wrapper
// ========================================
//

/**
 * @brief 注册的MTF处理回调包装函数
 */
VOID VmmCallbackRegisteredMtfHandler(UINT32 CoreId) {
    // NULL检查
    if (g_Callbacks.VmmCallbackRegisteredMtfHandler == NULL) {
        // 没有注册MTF处理器
        return;
    }

    // 调用实际回调
    g_Callbacks.VmmCallbackRegisteredMtfHandler(CoreId);
}

//
// ========================================
// NMI广播回调Wrapper
// ========================================
//

/**
 * @brief NMI广播请求处理的回调包装函数
 */
VOID VmmCallbackNmiBroadcastRequestHandler(UINT32 CoreId, BOOLEAN IsOnVmxNmiHandler) {
    // NULL检查
    if (g_Callbacks.VmmCallbackNmiBroadcastRequestHandler == NULL) {
        // 没有NMI处理器
        return;
    }

    // 调用实际回调
    g_Callbacks.VmmCallbackNmiBroadcastRequestHandler(CoreId, IsOnVmxNmiHandler);
}

//
// ========================================
// 受保护资源查询回调Wrapper
// ========================================
//

/**
 * @brief 查询/终止受保护资源的回调包装函数
 */
BOOLEAN VmmCallbackQueryTerminateProtectedResource(
    UINT32                               CoreId,
    PROTECTED_HV_RESOURCES_TYPE          ResourceType,
    PVOID                                Context,
    PROTECTED_HV_RESOURCES_PASSING_OVERS PassOver) {

    // NULL检查
    if (g_Callbacks.VmmCallbackQueryTerminateProtectedResource == NULL) {
        // 允许访问（默认行为）
        return TRUE;
    }

    // 调用实际回调
    return g_Callbacks.VmmCallbackQueryTerminateProtectedResource(
        CoreId,
        ResourceType,
        Context,
        PassOver
    );
}

//
// ========================================
// EPT状态恢复回调Wrapper
// ========================================
//

/**
 * @brief EPT状态恢复的回调包装函数
 */
BOOLEAN VmmCallbackRestoreEptState(UINT32 CoreId) {
    // NULL检查
    if (g_Callbacks.VmmCallbackRestoreEptState == NULL) {
        // 不需要恢复
        return FALSE;
    }

    // 调用实际回调
    return g_Callbacks.VmmCallbackRestoreEptState(CoreId);
}

//
// ========================================
// 未处理EPT违规回调Wrapper
// ========================================
//

/**
 * @brief 检查未处理EPT违规的回调包装函数
 */
BOOLEAN VmmCallbackCheckUnhandledEptViolations(
    UINT32 CoreId,
    UINT64 ViolationQualification,
    UINT64 GuestPhysicalAddr) {

    // NULL检查
    if (g_Callbacks.VmmCallbackCheckUnhandledEptViolations == NULL) {
        // 没有处理器
        return FALSE;
    }

    // 调用实际回调
    return g_Callbacks.VmmCallbackCheckUnhandledEptViolations(
        CoreId,
        ViolationQualification,
        GuestPhysicalAddr
    );
}

//
// ========================================
// 断点异常处理回调Wrapper
// ========================================
//

/**
 * @brief 断点异常处理的回调包装函数
 */
BOOLEAN VmmCallbackHandleBreakpointException(UINT32 CoreId) {
    // NULL检查
    if (g_Callbacks.DebuggingCallbackHandleBreakpointException == NULL) {
        // 没有处理器
        return FALSE;
    }

    // 调用实际回调
    return g_Callbacks.DebuggingCallbackHandleBreakpointException(CoreId);
}

//
// ========================================
// 调试断点异常处理回调Wrapper
// ========================================
//

/**
 * @brief 调试断点异常（硬件断点）处理的回调包装函数
 */
BOOLEAN VmmCallbackHandleDebugBreakpointException(UINT32 CoreId) {
    // NULL检查
    if (g_Callbacks.DebuggingCallbackHandleDebugBreakpointException == NULL) {
        // 没有处理器
        return FALSE;
    }

    // 调用实际回调
    return g_Callbacks.DebuggingCallbackHandleDebugBreakpointException(CoreId);
}

//
// ========================================
// 线程拦截检查回调Wrapper
// ========================================
//

/**
 * @brief 线程拦截检查的回调包装函数
 */
BOOLEAN VmmCallbackCheckThreadInterception(UINT32 CoreId) {
    // NULL检查
    if (g_Callbacks.DebuggingCallbackCheckThreadInterception == NULL) {
        // 没有处理器
        return FALSE;
    }

    // 调用实际回调
    return g_Callbacks.DebuggingCallbackCheckThreadInterception(CoreId);
}

//
// ========================================
// CR3变化回调Wrapper
// ========================================
//

/**
 * @brief CR3进程变化的回调包装函数
 */
VOID VmmCallbackTriggerCr3ProcessChange(UINT32 CoreId) {
    // NULL检查
    if (g_Callbacks.InterceptionCallbackTriggerCr3ProcessChange == NULL) {
        // 没有处理器
        return;
    }

    // 调用实际回调
    g_Callbacks.InterceptionCallbackTriggerCr3ProcessChange(CoreId);
}
```

---

## 3.5 回调执行流程

### 3.5.1 完整的回调调用链

以**CPUID指令拦截**为例，展示完整的调用链：

```
┌────────────────────────────────────────────────────────────┐
│ 1. Guest执行CPUID指令                                      │
└────────────────┬───────────────────────────────────────────┘
                 │
                 ↓
┌────────────────────────────────────────────────────────────┐
│ 2. CPU硬件触发VM-Exit                                      │
│    - Exit Reason: VMX_EXIT_REASON_EXECUTE_CPUID            │
│    - 自动保存Guest状态到VMCS                               │
│    - 自动加载Host状态从VMCS                                │
│    - 跳转到VMCS_HOST_RIP                                   │
└────────────────┬───────────────────────────────────────────┘
                 │
                 ↓
┌────────────────────────────────────────────────────────────┐
│ 3. 进入汇编处理器 [AsmVmexitHandler]                      │
│    hyperdbg/hyperhv/code/assembly/AsmVmexitHandler.asm     │
│                                                            │
│    pushfq                    ; 保存RFLAGS                  │
│    sub rsp, 60h              ; 为XMM寄存器分配空间         │
│    movaps [rsp], xmm0        ; 保存XMM0                    │
│    ... (保存xmm1-xmm5)                                     │
│    push r15                  ; 保存通用寄存器              │
│    ... (保存r14-rax)                                       │
│                                                            │
│    mov rcx, rsp              ; 第一个参数 = PGUEST_REGS    │
│    call VmxVmexitHandler     ; 调用C函数                  │
└────────────────┬───────────────────────────────────────────┘
                 │
                 ↓
┌────────────────────────────────────────────────────────────┐
│ 4. C处理函数 [VmxVmexitHandler]                           │
│    hyperdbg/hyperhv/code/vmm/vmx/Vmexit.c                  │
│                                                            │
│    // 获取当前核心状态                                     │
│    VCpu = &g_GuestState[KeGetCurrentProcessorNumberEx()]; │
│                                                            │
│    // 保存寄存器指针                                       │
│    VCpu->Regs = GuestRegs;                                │
│    VCpu->XmmRegs = ...;                                   │
│                                                            │
│    // 读取Exit Reason和Qualification                      │
│    VmxVmread32P(VMCS_EXIT_REASON, &ExitReason);           │
│    VmxVmread32P(VMCS_EXIT_QUALIFICATION, &Qual);          │
│                                                            │
│    // 分派处理                                             │
│    switch (ExitReason) {                                  │
│        case VMX_EXIT_REASON_EXECUTE_CPUID:                │
│            DispatchEventCpuid(VCpu);                       │
│            break;                                          │
│    }                                                       │
└────────────────┬───────────────────────────────────────────┘
                 │
                 ↓
┌────────────────────────────────────────────────────────────┐
│ 5. 事件分发函数 [DispatchEventCpuid]                      │
│    hyperdbg/hyperhv/code/interface/Dispatch.c              │
│                                                            │
│    UINT64 Context;                                         │
│    BOOLEAN PostEventTriggerReq = FALSE;                    │
│                                                            │
│    // 准备上下文（保存EAX值）                              │
│    Context = VCpu->Regs->rax & 0xffffffff;                │
│                                                            │
│    if (g_TriggerEventForCpuids) {                         │
│        // 触发PRE事件                                      │
│        EventTriggerResult = VmmCallbackTriggerEvents(      │
│            CPUID_INSTRUCTION_EXECUTION,                    │
│            VMM_CALLBACK_CALLING_STAGE_PRE_EVENT_EMULATION,│
│            (PVOID)Context,                                 │
│            &PostEventTriggerReq,                           │
│            VCpu->Regs                                      │
│        );                                                  │
│                                                            │
│        // 如果不需要短路，执行实际CPUID                    │
│        if (EventTriggerResult !=                           │
│            VMM_CALLBACK_TRIGGERING_EVENT_STATUS_...IGNORE) {│
│            HvHandleCpuid(VCpu);                            │
│        }                                                   │
│                                                            │
│        // 如果需要，触发POST事件                           │
│        if (PostEventTriggerReq) {                          │
│            VmmCallbackTriggerEvents(                       │
│                CPUID_INSTRUCTION_EXECUTION,                │
│                VMM_CALLBACK_CALLING_STAGE_POST_...,       │
│                ...);                                       │
│        }                                                   │
│    } else {                                                │
│        // 没有事件，直接处理                               │
│        HvHandleCpuid(VCpu);                                │
│    }                                                       │
└────────────────┬───────────────────────────────────────────┘
                 │
                 ↓
┌────────────────────────────────────────────────────────────┐
│ 6. Wrapper函数 [VmmCallbackTriggerEvents]                 │
│    hyperdbg/hyperhv/code/interface/Callback.c              │
│                                                            │
│    // NULL检查                                             │
│    if (g_Callbacks.VmmCallbackTriggerEvents == NULL) {    │
│        return VMM_CALLBACK_...NO_INITIALIZED;             │
│    }                                                       │
│                                                            │
│    // 调用实际回调                                         │
│    return g_Callbacks.VmmCallbackTriggerEvents(           │
│        EventType, CallingStage, Context, ...);            │
└────────────────┬───────────────────────────────────────────┘
                 │
                 ↓
┌────────────────────────────────────────────────────────────┐
│ 7. 实际回调函数 [DebuggerTriggerEvents]                   │
│    hyperdbg/hyperkd/code/debugger/core/DebuggerEvents.c    │
│                                                            │
│    // 检查是否有注册的CPUID事件                            │
│    if (HasRegisteredCpuidEvent()) {                        │
│        // 执行用户定义的事件处理                           │
│        ExecuteEventActions();                              │
│                                                            │
│        // 可能修改寄存器值                                 │
│        Regs->rax = ModifiedValue;                          │
│                                                            │
│        // 可能要求短路事件                                 │
│        return VMM_CALLBACK_...IGNORE_EVENT;               │
│    }                                                       │
│                                                            │
│    return VMM_CALLBACK_...SUCCESSFUL;                     │
└────────────────┬───────────────────────────────────────────┘
                 │
                 ↓
┌────────────────────────────────────────────────────────────┐
│ 8. 返回到DispatchEventCpuid                               │
│    - 根据返回值决定是否执行HvHandleCpuid()                 │
│    - 执行实际的CPUID处理                                   │
└────────────────┬───────────────────────────────────────────┘
                 │
                 ↓
┌────────────────────────────────────────────────────────────┐
│ 9. 返回到VmxVmexitHandler                                 │
│    - 增加RIP（如果VCpu->IncrementRip == TRUE）            │
│    - 准备VMRESUME                                          │
└────────────────┬───────────────────────────────────────────┘
                 │
                 ↓
┌────────────────────────────────────────────────────────────┐
│ 10. 返回到AsmVmexitHandler                                │
│     - 恢复所有寄存器                                       │
│     - 执行VMRESUME                                         │
└────────────────┬───────────────────────────────────────────┘
                 │
                 ↓
┌────────────────────────────────────────────────────────────┐
│ 11. Guest继续执行                                          │
│     - 从更新后的RIP继续                                    │
│     - 使用修改后的寄存器值（如果有）                       │
└────────────────────────────────────────────────────────────┘
```

### 3.5.2 事件分发函数示例

**文件位置**：`hyperdbg/hyperhv/code/interface/Dispatch.c`

```c
/**
 * @brief CPUID事件分发
 */
VOID DispatchEventCpuid(VIRTUAL_MACHINE_STATE * VCpu) {
    UINT64                                    Context;
    VMM_CALLBACK_TRIGGERING_EVENT_STATUS_TYPE EventTriggerResult;
    BOOLEAN                                   PostEventTriggerReq = FALSE;

    // 检查是否启用了CPUID事件触发
    if (g_TriggerEventForCpuids) {

        // 保存EAX作为上下文（CPUID的主功能号）
        Context = VCpu->Regs->rax & 0xffffffff;

        //
        // ========================================
        // PRE事件阶段
        // ========================================
        //
        EventTriggerResult = VmmCallbackTriggerEvents(
            CPUID_INSTRUCTION_EXECUTION,
            VMM_CALLBACK_CALLING_STAGE_PRE_EVENT_EMULATION,
            (PVOID)Context,
            &PostEventTriggerReq,
            VCpu->Regs
        );

        //
        // 检查是否需要短路事件模拟
        //
        if (EventTriggerResult !=
            VMM_CALLBACK_TRIGGERING_EVENT_STATUS_SUCCESSFUL_IGNORE_EVENT) {

            // 执行实际的CPUID处理
            HvHandleCpuid(VCpu);
        }

        //
        // ========================================
        // POST事件阶段（如果需要）
        // ========================================
        //
        if (PostEventTriggerReq) {
            VmmCallbackTriggerEvents(
                CPUID_INSTRUCTION_EXECUTION,
                VMM_CALLBACK_CALLING_STAGE_POST_EVENT_EMULATION,
                (PVOID)Context,
                NULL,  // POST阶段不需要PostEventRequired
                VCpu->Regs
            );
        }
    } else {
        // 没有注册CPUID事件，直接处理
        HvHandleCpuid(VCpu);
    }
}

/**
 * @brief RDMSR事件分发
 */
VOID DispatchEventRdmsr(VIRTUAL_MACHINE_STATE * VCpu) {
    UINT64                                    Context;
    VMM_CALLBACK_TRIGGERING_EVENT_STATUS_TYPE EventTriggerResult;
    BOOLEAN                                   PostEventTriggerReq = FALSE;

    if (g_TriggerEventForRdmsrs) {

        // MSR号在ECX中
        Context = VCpu->Regs->rcx & 0xffffffff;

        // PRE事件
        EventTriggerResult = VmmCallbackTriggerEvents(
            RDMSR_INSTRUCTION_EXECUTION,
            VMM_CALLBACK_CALLING_STAGE_PRE_EVENT_EMULATION,
            (PVOID)Context,
            &PostEventTriggerReq,
            VCpu->Regs
        );

        if (EventTriggerResult !=
            VMM_CALLBACK_TRIGGERING_EVENT_STATUS_SUCCESSFUL_IGNORE_EVENT) {

            // 执行实际的RDMSR处理
            MsrHandleRdmsrVmexit(VCpu);
        }

        // POST事件
        if (PostEventTriggerReq) {
            VmmCallbackTriggerEvents(
                RDMSR_INSTRUCTION_EXECUTION,
                VMM_CALLBACK_CALLING_STAGE_POST_EVENT_EMULATION,
                (PVOID)Context,
                NULL,
                VCpu->Regs
            );
        }
    } else {
        // 直接处理
        MsrHandleRdmsrVmexit(VCpu);
    }
}

/**
 * @brief 控制寄存器访问事件分发
 */
VOID DispatchEventMovToFromControlRegisters(VIRTUAL_MACHINE_STATE * VCpu) {
    VMX_EXIT_QUALIFICATION_MOV_CR CrExitQualification;

    // 解析Exit Qualification
    CrExitQualification.AsUInt = VCpu->ExitQualification;

    // 根据CR号处理
    if (CrExitQualification.ControlRegister == 0 ||  // CR0
        CrExitQualification.ControlRegister == 3 ||  // CR3
        CrExitQualification.ControlRegister == 4) {  // CR4

        // CR3特殊处理（进程切换）
        if (CrExitQualification.ControlRegister == 3) {
            // 调用CR3变化回调
            VmmCallbackTriggerCr3ProcessChange(VCpu->CoreId);
        }

        // 处理CR访问
        HvHandleControlRegisterAccess(VCpu, &CrExitQualification);
    }
}

/**
 * @brief I/O指令事件分发
 */
VOID DispatchEventIO(VIRTUAL_MACHINE_STATE * VCpu) {
    VMX_EXIT_QUALIFICATION_IO_INSTRUCTION IoQualification;
    RFLAGS Flags;

    IoQualification.AsUInt = VCpu->ExitQualification;
    __vmx_vmread(VMCS_GUEST_RFLAGS, &Flags.AsUInt);

    if (g_TriggerEventForIns || g_TriggerEventForOuts) {

        UINT64 Context = IoQualification.PortNumber;

        // 触发I/O事件
        VmmCallbackTriggerEvents(
            IN_INSTRUCTION_EXECUTION,  // 或OUT_INSTRUCTION_EXECUTION
            VMM_CALLBACK_CALLING_STAGE_PRE_EVENT_EMULATION,
            (PVOID)Context,
            NULL,
            VCpu->Regs
        );
    }

    // 处理I/O指令
    IoHandleIoVmExits(VCpu, IoQualification, Flags);
}
```

### 3.5.3 MTF中的回调调用

**文件位置**：`hyperdbg/hyperhv/code/vmm/vmx/Mtf.c`

```c
/**
 * @brief 处理Monitor Trap Flag VM-Exit
 */
BOOLEAN MtfHandleVmexit(VIRTUAL_MACHINE_STATE * VCpu) {

    //
    // ========================================
    // 1. 检查断点重新应用
    // ========================================
    //
    if (g_Callbacks.BreakpointCheckAndHandleReApplyingBreakpoint != NULL &&
        g_Callbacks.BreakpointCheckAndHandleReApplyingBreakpoint(VCpu->CoreId)) {

        // 断点已重新应用
        // 某些情况下可能需要保持MTF启用
        goto ContinueMtfHandling;
    }

    //
    // ========================================
    // 2. 检查EPT Hook恢复点
    // ========================================
    //
    if (VCpu->MtfEptHookRestorePoint != NULL) {

        // 恢复EPT Hook状态
        PEPT_HOOKED_PAGE_DETAIL HookedPage = VCpu->MtfEptHookRestorePoint;
        EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(...);

        // 重新移除执行权限
        EptEntry->ExecuteAccess = 0;
        EptEntry->ReadAccess = 1;

        // 刷新EPT
        EptInveptSingleContext(VCpu->EptPointer.AsUInt);

        VCpu->MtfEptHookRestorePoint = NULL;
    }

    //
    // ========================================
    // 3. 处理注册的MTF break
    // ========================================
    //
    if (VCpu->RegisterBreakOnMtf) {

        // 调用注册的MTF处理器
        VmmCallbackRegisteredMtfHandler(VCpu->CoreId);

        VCpu->RegisterBreakOnMtf = FALSE;

        goto ContinueMtfHandling;
    }

    //
    // ========================================
    // 4. 检查NMI回调
    // ========================================
    //
    else if (g_Callbacks.KdCheckAndHandleNmiCallback != NULL &&
             g_Callbacks.KdCheckAndHandleNmiCallback(VCpu->CoreId)) {

        // NMI相关的MTF处理
        goto ContinueMtfHandling;
    }

ContinueMtfHandling:

    //
    // ========================================
    // 5. 处理外部中断恢复
    // ========================================
    //
    if (VCpu->EnableExternalInterruptsOnContinueMtf) {
        // 恢复外部中断
        HvSetExternalInterruptExiting(VCpu, FALSE);
        VCpu->EnableExternalInterruptsOnContinueMtf = FALSE;
    }

    //
    // ========================================
    // 6. 关闭MTF（如果不需要继续）
    // ========================================
    //
    if (!VCpu->IgnoreMtfUnset) {
        HvSetMonitorTrapFlag(FALSE);
    } else {
        VCpu->IgnoreMtfUnset = FALSE;
    }

    return TRUE;
}
```

---

## 3.6 回调分类详解

### 3.6.1 日志传输回调（Log Callbacks）

这组回调用于VMM向外部发送日志消息。

```c
// ============================================
// 回调1：准备并发送消息
// ============================================
VmmCallbacks.LogCallbackPrepareAndSendMessageToQueueWrapper =
    LogCallbackPrepareAndSendMessageToQueueWrapper;

/**
 * @brief 在VMM中调用
 */
void VmmSomeFunction() {
    // VMM需要记录日志
    LogInfo("VM-Exit Reason: %x", ExitReason);

    // 内部会调用：
    // g_Callbacks.LogCallbackPrepareAndSendMessageToQueueWrapper(
    //     OPERATION_LOG_INFO_MESSAGE,
    //     FALSE,  // 不是立即消息
    //     TRUE,   // 显示时间戳
    //     FALSE,  // 不是优先级消息
    //     "VM-Exit Reason: %x",
    //     VaList
    // );
}

// ============================================
// 回调2：发送消息到队列
// ============================================
VmmCallbacks.LogCallbackSendMessageToQueue =
    LogCallbackSendMessageToQueue;

/**
 * @brief 发送格式化后的消息到队列
 */
BOOLEAN LogCallbackSendMessageToQueue(
    UINT32  OperationCode,
    BOOLEAN IsImmediateMessage,
    CHAR *  LogMessage,
    UINT32  BufferLen,
    BOOLEAN Priority
) {
    // 将消息放入环形缓冲区
    // 等待用户态读取或发送到调试器
}

// ============================================
// 回调3：发送二进制缓冲区
// ============================================
VmmCallbacks.LogCallbackSendBuffer =
    LogCallbackSendBuffer;

/**
 * @brief 发送二进制数据到调试器
 */
BOOLEAN LogCallbackSendBuffer(
    UINT32  OperationCode,
    PVOID   Buffer,
    UINT32  BufferLength,
    BOOLEAN Priority
) {
    // 用于发送结构化数据（如寄存器状态、内存dump等）
}

// ============================================
// 回调4：检查缓冲区状态
// ============================================
VmmCallbacks.LogCallbackCheckIfBufferIsFull =
    LogCallbackCheckIfBufferIsFull;

/**
 * @brief 在写入前检查缓冲区
 */
BOOLEAN LogCallbackCheckIfBufferIsFull(BOOLEAN Priority) {
    // 返回TRUE表示缓冲区满
    // VMM可以决定是否等待或丢弃消息
}
```

**使用场景**：
- VMM中的LogInfo/LogError/LogWarning
- 事件触发时的数据记录
- 性能统计信息输出

### 3.6.2 VMM核心回调（VMM Core Callbacks）

#### 回调1：事件触发（最重要）

```c
VmmCallbacks.VmmCallbackTriggerEvents = DebuggerTriggerEvents;

/**
 * @brief 事件类型枚举
 */
typedef enum _VMM_EVENT_TYPE_ENUM {
    HIDDEN_HOOK_READ_AND_WRITE_AND_EXECUTE,
    HIDDEN_HOOK_READ_AND_WRITE,
    HIDDEN_HOOK_READ_AND_EXECUTE,
    HIDDEN_HOOK_WRITE_AND_EXECUTE,
    HIDDEN_HOOK_READ,
    HIDDEN_HOOK_WRITE,
    HIDDEN_HOOK_EXECUTE,

    SYSCALL_HOOK_EFER,
    SYSRET_HOOK_EFER,

    CPUID_INSTRUCTION_EXECUTION,
    RDMSR_INSTRUCTION_EXECUTION,
    WRMSR_INSTRUCTION_EXECUTION,

    IN_INSTRUCTION_EXECUTION,
    OUT_INSTRUCTION_EXECUTION,

    EXCEPTION_OCCURRED,
    EXTERNAL_INTERRUPT_OCCURRED,

    DEBUG_REGISTERS_ACCESSED,
    CONTROL_REGISTER_MODIFIED,
    CONTROL_REGISTER_READ,

    TSC_INSTRUCTION_EXECUTION,
    PMC_INSTRUCTION_EXECUTION,

    VMCALL_INSTRUCTION_EXECUTION,

    // ... 更多事件类型
} VMM_EVENT_TYPE_ENUM;

/**
 * @brief 调用阶段
 */
typedef enum _VMM_CALLBACK_EVENT_CALLING_STAGE_TYPE {
    VMM_CALLBACK_CALLING_STAGE_INVALID_EVENT_EMULATION = 0,
    VMM_CALLBACK_CALLING_STAGE_PRE_EVENT_EMULATION,      // 事件执行前
    VMM_CALLBACK_CALLING_STAGE_POST_EVENT_EMULATION,     // 事件执行后
    VMM_CALLBACK_CALLING_STAGE_ALL_EVENT_EMULATION,
} VMM_CALLBACK_EVENT_CALLING_STAGE_TYPE;

/**
 * @brief 实际回调函数（在hyperkd中实现）
 */
VMM_CALLBACK_TRIGGERING_EVENT_STATUS_TYPE
DebuggerTriggerEvents(
    VMM_EVENT_TYPE_ENUM                   EventType,
    VMM_CALLBACK_EVENT_CALLING_STAGE_TYPE CallingStage,
    PVOID                                 Context,
    BOOLEAN *                             PostEventRequired,
    GUEST_REGS *                          Regs
) {
    // 1. 遍历事件列表
    for (each registered event of type EventType) {

        // 2. 检查条件是否满足
        if (EventConditionMatches()) {

            // 3. 执行事件关联的动作
            ExecuteEventActions(Regs);

            // 4. 可能修改寄存器
            Regs->rax = NewValue;

            // 5. 决定是否需要POST事件
            *PostEventRequired = TRUE;

            // 6. 返回状态
            return VMM_CALLBACK_TRIGGERING_EVENT_STATUS_SUCCESSFUL;
        }
    }

    return VMM_CALLBACK_TRIGGERING_EVENT_STATUS_SUCCESSFUL_NO_INITIALIZED;
}
```

#### 回调2：VMCALL处理

```c
VmmCallbacks.VmmCallbackVmcallHandler = DebuggerVmcallHandler;

/**
 * @brief VMCALL号定义
 */
#define VMCALL_TEST                             0x00000001
#define VMCALL_VMXOFF                           0x00000002
#define VMCALL_CHANGE_PAGE_ATTRIB               0x00000003
#define VMCALL_INVEPT_ALL_CONTEXTS              0x00000004
#define VMCALL_INVEPT_SINGLE_CONTEXT            0x00000005
#define VMCALL_UNHOOK_ALL_PAGES                 0x00000006
#define VMCALL_UNHOOK_SINGLE_PAGE               0x00000007
#define VMCALL_ENABLE_SYSCALL_HOOK_EFER         0x00000008
#define VMCALL_DISABLE_SYSCALL_HOOK_EFER        0x00000009
#define VMCALL_CHANGE_MSR_BITMAP_READ           0x0000000A
#define VMCALL_CHANGE_MSR_BITMAP_WRITE          0x0000000B
// ... 更多VMCALL号

/**
 * @brief 实际回调实现
 */
BOOLEAN DebuggerVmcallHandler(
    UINT32 CoreId,
    UINT64 VmcallNumber,
    UINT64 OptionalParam1,
    UINT64 OptionalParam2,
    UINT64 OptionalParam3
) {
    switch (VmcallNumber) {

        case VMCALL_EXEC_HOOK_PAGE:
        {
            // 设置EPT执行Hook
            UINT64 TargetAddress = OptionalParam1;
            UINT64 HookFunction = OptionalParam2;

            return DebuggerEptHook(TargetAddress, HookFunction);
        }

        case VMCALL_CHANGE_PAGE_ATTRIB:
        {
            // 修改页面属性
            UINT64 PhysicalAddress = OptionalParam1;
            UINT64 NewAttributes = OptionalParam2;

            return DebuggerChangePageAttribute(PhysicalAddress, NewAttributes);
        }

        case VMCALL_INVEPT_SINGLE_CONTEXT:
        {
            // 刷新EPT
            EptInveptSingleContext(OptionalParam1);
            return TRUE;
        }

        // ... 更多VMCALL处理
    }

    return FALSE;
}
```

#### 回调3：MTF处理

```c
VmmCallbacks.VmmCallbackRegisteredMtfHandler = KdHandleRegisteredMtfCallback;

/**
 * @brief 实际回调实现
 */
VOID KdHandleRegisteredMtfCallback(UINT32 CoreId) {
    PROCESSOR_DEBUGGING_STATE * DbgState = &g_DbgState[CoreId];

    if (DbgState->TracingMode) {
        //
        // ========================================
        // 场景1：指令跟踪模式（单步执行）
        // ========================================
        //
        TracingHandleMtf(DbgState);

        // TracingHandleMtf内部会：
        // - 记录执行的指令
        // - 更新跟踪计数
        // - 检查是否到达跟踪终点
        // - 向调试器报告状态
    } else {
        //
        // ========================================
        // 场景2：检查模式切换
        // ========================================
        //
        UINT64 CsSel = VmFuncGetCsSelector();

        // 检查CS选择器变化（用户态<->内核态切换）
        KdCheckGuestOperatingModeChanges(
            DbgState->InstrumentationStepInTrace.CsSel,
            (UINT16)CsSel
        );

        // 清除保存的CS选择器
        DbgState->InstrumentationStepInTrace.CsSel = 0;

        //
        // ========================================
        // 场景3：检查软件定义的断点
        // ========================================
        //
        UINT64 LastVmexitRip = VmFuncGetLastVmexitRip(CoreId);

        // 检查是否需要注入断点事件
        if (CheckForSoftwareBreakpoint(LastVmexitRip)) {
            // 向调试器报告断点命中
            KdHandleBreakpointAndDebugBreakpointsCallback(
                CoreId,
                DEBUGGEE_PAUSING_REASON_DEBUGGEE_STEPPED,
                NULL
            );
        }
    }
}
```

#### 回调4：NMI广播处理

```c
VmmCallbacks.VmmCallbackNmiBroadcastRequestHandler = KdHandleNmiBroadcastDebugBreaks;

/**
 * @brief 实际回调实现
 */
VOID KdHandleNmiBroadcastDebugBreaks(UINT32 CoreId, BOOLEAN IsOnVmxNmiHandler) {
    PROCESSOR_DEBUGGING_STATE * DbgState = &g_DbgState[CoreId];

    // 标记该核心等待被锁定
    DbgState->NmiState.WaitingToBeLocked = TRUE;

    if (IsOnVmxNmiHandler) {
        //
        // ========================================
        // 在VMX Root模式收到NMI
        // ========================================
        //
        DbgState->NmiState.NmiCalledInVmxRootRelatedToHaltDebuggee = TRUE;

        // 设置MTF - 在下一条Guest指令后触发VM-Exit
        // 原因：
        // 1. 获取Guest的完整上下文（寄存器）
        // 2. 确保不会错过任何Guest CPU周期
        VmFuncSetMonitorTrapFlag(TRUE);

    } else {
        //
        // ========================================
        // 在VMX Non-root模式（Guest）收到NMI
        // ========================================
        //
        // NMI本身会触发VM-Exit，直接处理
        KdHandleNmi(DbgState);

        // KdHandleNmi内部会：
        // - 锁定该核心
        // - 向调试器报告状态
        // - 等待调试器命令
    }
}
```

### 3.6.3 调试回调（Debugging Callbacks）

#### 回调1：断点异常处理

```c
VmmCallbacks.DebuggingCallbackHandleBreakpointException = BreakpointHandleBreakpoints;

/**
 * @brief 在IdtEmulation中调用
 */
VOID IdtEmulationHandleExceptionAndNmi(VIRTUAL_MACHINE_STATE * VCpu) {
    VM_EXIT_INTERRUPTION_INFORMATION InterruptExit;

    __vmx_vmread(VMCS_EXIT_INTERRUPTION_INFORMATION, &InterruptExit.AsUInt);

    if (InterruptExit.InterruptionType == INTERRUPT_TYPE_SOFTWARE_EXCEPTION ||
        InterruptExit.InterruptionType == INTERRUPT_TYPE_HARDWARE_EXCEPTION) {

        // 检查是否是断点异常
        if (InterruptExit.Vector == EXCEPTION_VECTOR_BREAKPOINT) {

            // 调用断点处理回调
            if (VmmCallbackHandleBreakpointException(VCpu->CoreId)) {
                // 断点已处理
                return;
            }
        }

        // 检查是否是调试异常
        if (InterruptExit.Vector == EXCEPTION_VECTOR_DEBUG_BREAKPOINT) {

            // 调用硬件断点处理回调
            if (VmmCallbackHandleDebugBreakpointException(VCpu->CoreId)) {
                // 硬件断点已处理
                return;
            }
        }
    }

    // 其他异常，重新注入到Guest
    EventInjectInterruption(
        InterruptExit.InterruptionType,
        InterruptExit.Vector,
        TRUE,
        0
    );
}

/**
 * @brief 实际回调实现
 */
BOOLEAN BreakpointHandleBreakpoints(UINT32 CoreId) {
    UINT64 GuestRip = VmFuncGetLastVmexitRip(CoreId);

    // 1. 查找断点列表
    BREAKPOINT * Bp = FindBreakpoint(GuestRip - 1);  // INT3是1字节

    if (Bp == NULL) {
        // 不是我们设置的断点
        return FALSE;
    }

    // 2. 暂停执行，进入调试器
    KdHandleBreakpointAndDebugBreakpoints(
        &g_DbgState[CoreId],
        DEBUGGEE_PAUSING_REASON_DEBUGGEE_SOFTWARE_BREAKPOINT_HIT,
        NULL
    );

    // 3. 调整RIP（指向断点前）
    VmFuncSuppressRipIncrement(CoreId);
    GuestRip = GuestRip - 1;
    VmFuncSetLastVmexitRip(CoreId, GuestRip);

    return TRUE;
}
```

#### 回调2：断点重新应用

```c
VmmCallbacks.BreakpointCheckAndHandleReApplyingBreakpoint =
    BreakpointCheckAndHandleReApplyingBreakpoint;

/**
 * @brief 在MTF中调用，用于重新应用断点
 */
BOOLEAN BreakpointCheckAndHandleReApplyingBreakpoint(UINT32 CoreId) {
    UINT64 GuestRip = VmFuncGetLastVmexitRip(CoreId);

    // 检查是否有需要重新应用的断点
    BREAKPOINT * Bp = FindPendingBreakpoint(CoreId);

    if (Bp == NULL) {
        return FALSE;
    }

    // 重新写入INT3指令
    MemoryMapperWriteMemorySafe(
        Bp->Address,
        &Bp->OriginalByte,  // 恢复原始字节
        1,
        Bp->ProcessId
    );

    // 标记断点已重新应用
    Bp->IsReapplied = TRUE;

    return TRUE;
}
```

### 3.6.4 拦截回调（Interception Callbacks）

#### CR3变化回调（进程切换监控）

```c
VmmCallbacks.InterceptionCallbackTriggerCr3ProcessChange = ProcessTriggerCr3ProcessChange;

/**
 * @brief 在CR3写入时调用
 */
VOID HvHandleControlRegisterAccess(
    VIRTUAL_MACHINE_STATE *         VCpu,
    VMX_EXIT_QUALIFICATION_MOV_CR * CrExitQualification
) {
    if (CrExitQualification->ControlRegister == 3) {  // CR3

        // 获取新的CR3值
        UINT64 * RegPtr = (UINT64 *)&VCpu->Regs->rax +
                         CrExitQualification->GeneralPurposeRegister;
        UINT64 NewCr3 = *RegPtr;

        // 调用CR3变化回调
        VmmCallbackTriggerCr3ProcessChange(VCpu->CoreId);

        // 写入新的CR3到Guest
        __vmx_vmwrite(VMCS_GUEST_CR3, NewCr3);
    }
}

/**
 * @brief 实际回调实现
 */
VOID ProcessTriggerCr3ProcessChange(UINT32 CoreId) {
    UINT64 NewCr3, OldCr3;

    // 读取新的CR3
    __vmx_vmread(VMCS_GUEST_CR3, &NewCr3);

    // 获取旧的CR3
    OldCr3 = g_ProcessState[CoreId].LastCr3;

    if (NewCr3 != OldCr3) {
        // 进程切换发生

        // 1. 记录新的CR3
        g_ProcessState[CoreId].LastCr3 = NewCr3;

        // 2. 更新进程ID
        g_ProcessState[CoreId].CurrentProcessId = GetProcessIdFromCr3(NewCr3);

        // 3. 触发进程切换事件
        if (g_MonitorProcessSwitch) {
            LogInfo("Process switch: %llx -> %llx", OldCr3, NewCr3);
        }

        // 4. 检查是否需要应用进程特定的Hook
        ApplyProcessSpecificHooks(NewCr3);
    }
}
```

---

## 3.7 回调在不同VM-Exit中的应用

### 3.7.1 EPT Violation中的回调

```c
/**
 * @brief EPT违规处理
 */
BOOLEAN EptHandleEptViolation(VIRTUAL_MACHINE_STATE * VCpu) {
    EPT_VIOLATION_EXIT_QUALIFICATION ViolationQual;
    UINT64 GuestPhysicalAddress;

    // 读取违规信息
    ViolationQual.AsUInt = VCpu->ExitQualification;
    __vmx_vmread(VMCS_GUEST_PHYSICAL_ADDRESS, &GuestPhysicalAddress);

    // 查找Hook
    PEPT_HOOKED_PAGE_DETAIL HookedPage =
        EptFindHookedPageByPhysicalAddress(GuestPhysicalAddress);

    if (HookedPage != NULL) {
        // 是我们的Hook，处理
        return HandleHookedPage(VCpu, HookedPage, ViolationQual);
    }

    //
    // ========================================
    // 不是预期的Hook，调用未处理EPT违规回调
    // ========================================
    //
    BOOLEAN Handled = VmmCallbackCheckUnhandledEptViolations(
        VCpu->CoreId,
        ViolationQual.AsUInt,
        GuestPhysicalAddress
    );

    if (Handled) {
        // 回调处理了这个违规
        return TRUE;
    }

    // 真正的错误
    LogError("Unhandled EPT Violation at GPA: %llx", GuestPhysicalAddress);
    return FALSE;
}

/**
 * @brief 未处理EPT违规的实际回调
 */
BOOLEAN AttachingCheckUnhandledEptViolation(
    UINT32 CoreId,
    UINT64 ViolationQualification,
    UINT64 GuestPhysicalAddr
) {
    // 检查是否是用户态调试器相关的违规
    if (IsUserDebuggerRelated(GuestPhysicalAddr)) {
        // 处理用户态调试器的EPT违规
        return TRUE;
    }

    // 未处理
    return FALSE;
}
```

### 3.7.2 异常处理中的回调

```c
/**
 * @brief 异常和NMI处理
 */
VOID IdtEmulationHandleExceptionAndNmi(VIRTUAL_MACHINE_STATE * VCpu) {
    VM_EXIT_INTERRUPTION_INFORMATION InterruptExit;
    UINT32 ErrorCode = 0;

    // 读取中断信息
    __vmx_vmread(VMCS_EXIT_INTERRUPTION_INFORMATION, &InterruptExit.AsUInt);

    //
    // ========================================
    // #BP（断点异常）
    // ========================================
    //
    if (InterruptExit.Vector == EXCEPTION_VECTOR_BREAKPOINT) {

        // 调用断点处理回调
        if (VmmCallbackHandleBreakpointException(VCpu->CoreId)) {
            // 断点已处理，不注入到Guest
            return;
        }

        // 没有处理，重新注入到Guest
        EventInjectBreakpoint();
        return;
    }

    //
    // ========================================
    // #DB（调试异常，硬件断点）
    // ========================================
    //
    if (InterruptExit.Vector == EXCEPTION_VECTOR_DEBUG_BREAKPOINT) {

        // 调用硬件断点处理回调
        if (VmmCallbackHandleDebugBreakpointException(VCpu->CoreId)) {
            // 断点已处理
            return;
        }

        // 没有处理，重新注入
        EventInjectDebugBreakpoint();
        return;
    }

    //
    // ========================================
    // #PF（页面错误）
    // ========================================
    //
    if (InterruptExit.Vector == EXCEPTION_VECTOR_PAGE_FAULT) {

        // 读取错误码
        if (InterruptExit.ErrorCodeValid) {
            __vmx_vmread(VMCS_EXIT_INTERRUPTION_ERROR_CODE, &ErrorCode);
        }

        // 读取CR2（引起错误的地址）
        UINT64 Cr2;
        __vmx_vmread(VMCS_EXIT_QUALIFICATION, &Cr2);

        // 处理页面错误（可能需要重新注入）
        EventInjectPageFault(ErrorCode, Cr2);
        return;
    }

    //
    // ========================================
    // 其他异常，重新注入到Guest
    // ========================================
    //
    EventInjectInterruption(
        InterruptExit.InterruptionType,
        InterruptExit.Vector,
        InterruptExit.ErrorCodeValid,
        ErrorCode
    );
}
```

---

## 3.8 两阶段事件模型

HyperDbg的事件系统支持PRE和POST两个阶段：

```c
/**
 * @brief 两阶段事件处理示例
 */
VOID DispatchEventCpuid(VIRTUAL_MACHINE_STATE * VCpu) {
    UINT64 Context = VCpu->Regs->rax;
    VMM_CALLBACK_TRIGGERING_EVENT_STATUS_TYPE Result;
    BOOLEAN PostEventRequired = FALSE;

    //
    // ========================================
    // PRE阶段 - 事件执行前
    // ========================================
    //
    Result = VmmCallbackTriggerEvents(
        CPUID_INSTRUCTION_EXECUTION,
        VMM_CALLBACK_CALLING_STAGE_PRE_EVENT_EMULATION,
        (PVOID)Context,
        &PostEventRequired,  // 输出参数：是否需要POST阶段
        VCpu->Regs
    );

    // PRE阶段可以：
    // 1. 修改输入参数（Regs->rax, Regs->rcx）
    // 2. 决定是否短路（跳过实际执行）
    // 3. 设置PostEventRequired为TRUE

    //
    // ========================================
    // 执行实际操作（除非被短路）
    // ========================================
    //
    if (Result != VMM_CALLBACK_TRIGGERING_EVENT_STATUS_SUCCESSFUL_IGNORE_EVENT) {
        // 执行实际的CPUID
        HvHandleCpuid(VCpu);

        // HvHandleCpuid会：
        // - 调用__cpuid()获取真实结果
        // - 可能修改结果（如隐藏hypervisor bit）
        // - 将结果写入Regs->rax/rbx/rcx/rdx
    }

    //
    // ========================================
    // POST阶段 - 事件执行后
    // ========================================
    //
    if (PostEventRequired) {
        VmmCallbackTriggerEvents(
            CPUID_INSTRUCTION_EXECUTION,
            VMM_CALLBACK_CALLING_STAGE_POST_EVENT_EMULATION,
            (PVOID)Context,
            NULL,  // POST阶段不需要这个参数
            VCpu->Regs
        );

        // POST阶段可以：
        // 1. 检查输出结果
        // 2. 修改输出值（Regs->rax/rbx/rcx/rdx）
        // 3. 记录事件日志
    }
}
```

**两阶段模型的应用场景**：

| 阶段 | 典型用途 | 可修改内容 |
|------|---------|-----------|
| **PRE** | 检查条件、修改输入 | 输入参数（如CPUID的EAX/ECX） |
| **实际执行** | VMM执行真实操作 | - |
| **POST** | 检查结果、修改输出 | 输出结果（如CPUID的EAX/EBX/ECX/EDX） |

**示例：监控特定CPUID查询**

```c
// 用户脚本：监控CPUID(EAX=1)的调用
DebuggerTriggerEvents(...) {
    if (EventType == CPUID_INSTRUCTION_EXECUTION) {

        if (CallingStage == PRE_EVENT) {
            // PRE阶段
            UINT32 CpuidLeaf = (UINT32)Context;

            if (CpuidLeaf == 1) {
                LogInfo("CPUID(1) called from RIP: %llx",
                    VmFuncGetLastVmexitRip(CoreId));

                // 要求POST阶段回调
                *PostEventRequired = TRUE;
            }
        }
        else if (CallingStage == POST_EVENT) {
            // POST阶段
            LogInfo("CPUID(1) result: EAX=%x, EBX=%x, ECX=%x, EDX=%x",
                Regs->rax, Regs->rbx, Regs->rcx, Regs->rdx);

            // 可以修改结果
            Regs->rcx &= ~(1 << 31);  // 清除hypervisor bit
        }
    }

    return VMM_CALLBACK_TRIGGERING_EVENT_STATUS_SUCCESSFUL;
}
```

---

## 3.9 回调的调用位置总结

### 3.9.1 在VM-Exit处理器中

**文件位置**：`hyperdbg/hyperhv/code/vmm/vmx/Vmexit.c`

```c
// VmmCallbackTriggerEvents - 通过Dispatch函数调用
case VMX_EXIT_REASON_EXECUTE_CPUID:
    DispatchEventCpuid(VCpu);  // -> VmmCallbackTriggerEvents
    break;

case VMX_EXIT_REASON_EXECUTE_RDMSR:
    DispatchEventRdmsr(VCpu);  // -> VmmCallbackTriggerEvents
    break;

case VMX_EXIT_REASON_MOV_CR:
    DispatchEventMovToFromControlRegisters(VCpu);  // -> VmmCallbackTriggerCr3ProcessChange
    break;

case VMX_EXIT_REASON_EXECUTE_VMCALL:
    VmxHandleVmcall(VCpu);  // -> VmmCallbackVmcallHandler
    break;

case VMX_EXIT_REASON_EPT_VIOLATION:
    EptHandleEptViolation(VCpu);  // -> VmmCallbackCheckUnhandledEptViolations
    break;

case VMX_EXIT_REASON_EXCEPTION_OR_NMI:
    IdtEmulationHandleExceptionAndNmi(VCpu);  // -> VmmCallbackHandleBreakpointException
    break;

case VMX_EXIT_REASON_MONITOR_TRAP_FLAG:
    MtfHandleVmexit(VCpu);  // -> VmmCallbackRegisteredMtfHandler
    break;
```

### 3.9.2 在MTF处理中

**文件位置**：`hyperdbg/hyperhv/code/vmm/vmx/Mtf.c`

```c
BOOLEAN MtfHandleVmexit(VIRTUAL_MACHINE_STATE * VCpu) {

    // 1. 断点重新应用回调
    if (g_Callbacks.BreakpointCheckAndHandleReApplyingBreakpoint != NULL &&
        g_Callbacks.BreakpointCheckAndHandleReApplyingBreakpoint(VCpu->CoreId)) {
        goto ContinueMtfHandling;
    }

    // 2. 注册的MTF处理器回调
    if (VCpu->RegisterBreakOnMtf) {
        VmmCallbackRegisteredMtfHandler(VCpu->CoreId);
        VCpu->RegisterBreakOnMtf = FALSE;
        goto ContinueMtfHandling;
    }

    // 3. NMI回调
    else if (g_Callbacks.KdCheckAndHandleNmiCallback != NULL &&
             g_Callbacks.KdCheckAndHandleNmiCallback(VCpu->CoreId)) {
        goto ContinueMtfHandling;
    }

    // ... 其他MTF处理
}
```

### 3.9.3 在NMI处理中

**文件位置**：`hyperdbg/hyperhv/code/vmm/vmx/VmxBroadcast.c`

```c
BOOLEAN VmxBroadcastHandleNmiCallback(PVOID Context, BOOLEAN Handled) {
    UINT32 CoreId = KeGetCurrentProcessorNumberEx(NULL);
    VIRTUAL_MACHINE_STATE * VCpu = &g_GuestState[CoreId];

    if (g_NmiBroadcastingInitialized) {

        // 调用NMI广播回调
        VmmCallbackNmiBroadcastRequestHandler(
            CoreId,
            VCpu->IsOnVmxRootMode  // 是否在VMX Root模式
        );

        return TRUE;
    }

    return FALSE;
}
```

---

## 3.10 回调的优势和设计模式

### 3.10.1 设计优势

1. **松耦合**
   ```c
   // hyperhv不依赖hyperkd
   // 可以单独编译和测试
   ```

2. **可扩展性**
   ```c
   // 添加新功能只需：
   // 1. 在hyperkd中实现新回调
   // 2. 在初始化时注册
   // 3. VMM自动调用
   ```

3. **灵活性**
   ```c
   // 可以轻松替换实现
   VmmCallbacks.VmmCallbackTriggerEvents = MyCustomHandler;
   ```

4. **可测试性**
   ```c
   // 可以注入测试回调
   VmmCallbacks.VmmCallbackTriggerEvents = TestMockHandler;
   ```

### 3.10.2 类似的设计模式

HyperDbg的回调机制类似于：

1. **GUI事件处理**
   ```c
   button.onClick = handleButtonClick;  // 注册事件处理器
   // 点击按钮时自动调用handleButtonClick
   ```

2. **Linux内核的notifier chain**
   ```c
   register_netdevice_notifier(&my_notifier);  // 注册通知器
   // 网络设备事件发生时自动调用
   ```

3. **Windows驱动的回调**
   ```c
   PsSetCreateProcessNotifyRoutine(MyProcessCallback);  // 注册进程创建回调
   // 进程创建时自动调用
   ```

---

## 本章小结

本章深入讲解了HyperDbg的VMM回调机制：

1. **回调设计理念**
   - 解耦VMM核心和调试器逻辑
   - 提供灵活的扩展接口
   - 支持模块化开发

2. **回调结构**
   - VMM_CALLBACKS包含所有回调函数指针
   - 分为日志、VMM核心、调试、拦截等类别
   - 每个回调都有明确的函数签名

3. **初始化流程**
   - 在LoaderInitVmmAndDebugger中填充回调
   - 通过VmFuncInitVmm传递给VMM
   - 保存到全局变量g_Callbacks

4. **Wrapper模式**
   - 每个回调都有Wrapper函数
   - 提供NULL检查和默认行为
   - 统一VMM内部的调用接口

5. **调用时机**
   - VM-Exit处理器中调用
   - MTF处理中调用
   - NMI处理中调用
   - EPT Violation处理中调用

6. **两阶段事件**
   - PRE阶段：事件执行前
   - POST阶段：事件执行后
   - 支持修改输入和输出

这种回调机制是HyperDbg架构的核心，使得项目能够保持清晰的模块边界，同时提供强大的功能扩展能力。

---

[<< 上一章：HyperDbg项目架构概览](./第二章-HyperDbg项目架构概览.md) | [下一章：调试器通信机制 >>](./第四章-调试器通信机制.md)
