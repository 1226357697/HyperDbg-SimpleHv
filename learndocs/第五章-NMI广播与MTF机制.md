# 第五章：NMI广播与MTF机制

## 5.1 NMI（不可屏蔽中断）机制

### 5.1.1 NMI概念

**NMI (Non-Maskable Interrupt)** 是一种特殊的中断，具有以下特性：

- **不可屏蔽**：即使IF标志为0（关中断），NMI仍然会被处理
- **最高优先级**：优先级高于所有可屏蔽中断
- **用于关键事件**：硬件错误、系统崩溃、调试等
- **Vector固定**：中断向量号为2

### 5.1.2 NMI在虚拟化中的特殊性

在VMX环境中，NMI的处理更加复杂：

```
场景1：NMI在VMX Non-root模式（Guest）到达
    ↓
    自动触发VM-Exit (VMX_EXIT_REASON_EXCEPTION_OR_NMI)
    ↓
    VMM处理NMI
    ↓
    可以选择注入到Guest或自己处理

场景2：NMI在VMX Root模式（VMM）到达
    ↓
    VMM的NMI handler直接处理
    ↓
    不触发VM-Exit（因为已经在VMM中）
    ↓
    需要特殊处理以确保Guest不丢失CPU周期
```

### 5.1.3 VMCS中的NMI控制

```c
// ============================================
// NMI相关的VMCS字段
// ============================================

// Pin-Based VM-Execution Controls
#define PIN_BASED_VM_EXECUTION_CONTROLS_NMI_EXITING  0x00000008

// 设置NMI Exiting
__vmx_vmwrite(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS,
    PinBasedControls | PIN_BASED_VM_EXECUTION_CONTROLS_NMI_EXITING);

// Guest Interruptibility State
#define GUEST_INTR_STATE_NMI  0x00000008

// 检查NMI blocking
UINT32 InterruptibilityState;
__vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY_STATE, &InterruptibilityState);
if (InterruptibilityState & GUEST_INTR_STATE_NMI) {
    // NMI被阻塞
}
```

---

## 5.2 NMI广播机制

### 5.2.1 为什么需要NMI广播？

在多核调试中，当用户想暂停执行时，需要**立即暂停所有CPU核心**：

```
问题场景：用户按下Ctrl+Break暂停调试

方案1：等待自然VM-Exit（不使用NMI）
    ❌ 问题：
    - Guest可能在执行长时间循环
    - 可能数秒才有一次VM-Exit
    - 用户体验差
    - 某些核心可能永远不VM-Exit（死循环）

方案2：NMI广播（HyperDbg的方案）
    ✅ 优势：
    - NMI强制所有核心响应
    - 响应时间：1-5毫秒
    - 用户体验好，类似WinDbg
    - 可靠性高
```

### 5.2.2 NMI广播实现

**文件位置**：`hyperdbg/hyperhv/code/vmm/vmx/VmxBroadcast.c`

```c
/**
 * @brief 初始化NMI广播机制
 */
BOOLEAN VmxBroadcastInitialize() {

#if USE_DEFAULT_OS_IDT_AS_HOST_IDT == TRUE

    //
    // ========================================
    // 注册NMI回调（使用系统IDT）
    // ========================================
    //
    g_NmiHandlerForKeDeregisterNmiCallback =
        KeRegisterNmiCallback(VmxBroadcastHandleNmiCallback, NULL);

    if (g_NmiHandlerForKeDeregisterNmiCallback == NULL) {
        LogError("Failed to register NMI callback");
        return FALSE;
    }

#else

    //
    // ========================================
    // 使用自定义IDT
    // ========================================
    //
    // 在自定义IDT中设置NMI处理函数

#endif

    //
    // ========================================
    // 初始化APIC
    // ========================================
    //
    if (!ApicInitialize()) {
        LogError("Failed to initialize APIC");
        return FALSE;
    }

    //
    // ========================================
    // 在所有核心上启用NMI Exiting
    // ========================================
    //
    BroadcastEnableNmiExitingAllCores();

    g_NmiBroadcastingInitialized = TRUE;

    LogInfo("NMI broadcasting initialized successfully");

    return TRUE;
}

/**
 * @brief NMI回调处理函数
 */
BOOLEAN VmxBroadcastHandleNmiCallback(PVOID Context, BOOLEAN Handled) {
    UINT32 CurrentCore = KeGetCurrentProcessorNumberEx(NULL);
    VIRTUAL_MACHINE_STATE * VCpu = &g_GuestState[CurrentCore];

    //
    // ========================================
    // 检查是否是我们触发的NMI
    // ========================================
    //
    if (!g_NmiBroadcastingInitialized) {
        // 不是我们的NMI，让系统处理
        return FALSE;
    }

    //
    // ========================================
    // 调用NMI广播处理回调
    // ========================================
    //
    VmmCallbackNmiBroadcastRequestHandler(
        CurrentCore,
        VCpu->IsOnVmxRootMode  // 是否在VMX root模式收到NMI
    );

    // 返回TRUE表示我们已处理
    return TRUE;
}

/**
 * @brief 向所有核心广播NMI
 */
VOID VmxBroadcastNmi() {

    LogInfo("Broadcasting NMI to all cores");

    //
    // ========================================
    // 使用APIC发送NMI IPI
    // ========================================
    //
    // IPI (Inter-Processor Interrupt) 是处理器间中断
    // 通过APIC可以向其他核心发送中断

    // 设置ICR (Interrupt Command Register)
    // Destination: All excluding self
    // Delivery Mode: NMI
    // Level: Assert

    UINT64 IcrValue = 0;
    IcrValue |= APIC_ICR_DELIVERY_MODE_NMI;           // Delivery Mode = NMI (0x4)
    IcrValue |= APIC_ICR_DESTINATION_ALL_EXCLUDING_SELF;  // Destination Shorthand = All excluding self (0x3)

    // 写入ICR寄存器
    ApicWriteIcr(IcrValue);

    //
    // ========================================
    // 等待IPI发送完成
    // ========================================
    //
    while (ApicIcrIsPending()) {
        _mm_pause();
    }

    LogInfo("NMI broadcast completed");
}

/**
 * @brief 在所有核心上启用NMI Exiting
 */
VOID BroadcastEnableNmiExitingAllCores() {
    ULONG ProcessorsCount = KeQueryActiveProcessorCount(0);

    for (ULONG i = 0; i < ProcessorsCount; i++) {
        DpcRoutineRunTaskOnSingleCore(
            i,
            DpcEnableNmiExiting,
            NULL
        );
    }
}

/**
 * @brief DPC：在单个核心上启用NMI Exiting
 */
VOID DpcEnableNmiExiting(PVOID Context) {
    UINT32 PinBasedControls;

    // 读取当前Pin-Based控制
    __vmx_vmread(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS, &PinBasedControls);

    // 设置NMI Exiting位
    PinBasedControls |= PIN_BASED_VM_EXECUTION_CONTROLS_NMI_EXITING;

    // 写回VMCS
    __vmx_vmwrite(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS, PinBasedControls);

    LogInfo("Core %d: NMI Exiting enabled", KeGetCurrentProcessorNumber());
}
```

### 5.2.3 NMI广播处理回调

**文件位置**：`hyperdbg/hyperkd/code/debugger/kernel-level/Kd.c`

```c
/**
 * @brief 处理NMI广播调试中断
 *
 * @param CoreId 核心ID
 * @param IsOnVmxNmiHandler 是否在VMX root模式收到NMI
 */
VOID KdHandleNmiBroadcastDebugBreaks(UINT32 CoreId, BOOLEAN IsOnVmxNmiHandler) {
    PROCESSOR_DEBUGGING_STATE * DbgState = &g_DbgState[CoreId];

    //
    // ========================================
    // 标记等待锁定状态
    // ========================================
    //
    // 这个标志用于多个地方检查核心是否在等待被暂停
    DbgState->NmiState.WaitingToBeLocked = TRUE;

    if (IsOnVmxNmiHandler) {
        //
        // ========================================
        // 场景：NMI在VMX Root模式到达
        // ========================================
        //
        // 这种情况下，VMM正在处理某个VM-Exit
        // 我们不能直接暂停，因为没有Guest上下文

        LogInfo("Core %d: NMI received in VMX root mode", CoreId);

        // 标记NMI在VMX root中收到
        DbgState->NmiState.NmiCalledInVmxRootRelatedToHaltDebuggee = TRUE;

        //
        // ========================================
        // 设置MTF - 关键技术
        // ========================================
        //
        // 设置Monitor Trap Flag，这样Guest执行下一条指令后会VM-Exit
        // 这样做的原因：
        //
        // 1. 获取完整的Guest上下文
        //    - 当前在VMM中，没有Guest寄存器状态
        //    - 等Guest执行一条指令后VM-Exit，可以获取准确的Guest RIP、寄存器等
        //
        // 2. 确保不会错过Guest的CPU周期
        //    - 如果NMI到达时VMM正在处理一个长时间的操作
        //    - Guest可能会错过CPU时间
        //    - 通过MTF，我们确保立即返回Guest，执行一条指令后再暂停
        //
        VmFuncSetMonitorTrapFlag(TRUE);

        LogInfo("Core %d: MTF set, will halt on next guest instruction", CoreId);

    } else {
        //
        // ========================================
        // 场景：NMI在VMX Non-root模式（Guest）到达
        // ========================================
        //
        // NMI会自动触发VM-Exit，此时已经有完整的Guest上下文

        LogInfo("Core %d: NMI received in guest mode", CoreId);

        //
        // ========================================
        // 直接处理核心暂停
        // ========================================
        //
        KdHandleNmi(DbgState);

        // KdHandleNmi内部会：
        // 1. 锁定该核心（自旋锁）
        // 2. 检查是否所有核心都暂停
        // 3. 如果是第一个暂停的核心，向调试器发送paused消息
        // 4. 等待调试器命令
        // 5. 收到continue命令后解锁
    }
}

/**
 * @brief 处理NMI（在Guest模式收到）
 */
VOID KdHandleNmi(PROCESSOR_DEBUGGING_STATE * DbgState) {
    UINT32 CoreId = DbgState->CoreId;

    LogInfo("Core %d: Handling NMI", CoreId);

    //
    // ========================================
    // 1. 锁定该核心
    // ========================================
    //
    SpinlockLock(&DbgState->Lock);

    LogInfo("Core %d: Locked, waiting for other cores", CoreId);

    //
    // ========================================
    // 2. 检查是否所有核心都已锁定
    // ========================================
    //
    if (KdCheckAllCoresAreLocked()) {

        LogInfo("All cores locked, notifying debugger");

        //
        // ========================================
        // 3. 所有核心都暂停了，通知调试器
        // ========================================
        //
        KdSendPausedPacketToDebugger(
            CoreId,
            DEBUGGEE_PAUSING_REASON_DEBUGGEE_GENERAL_DEBUG_BREAK,
            NULL
        );

        //
        // ========================================
        // 4. 进入调试器主循环
        // ========================================
        //
        KdManageBreakStateOfDebugger(DbgState);

        // 这个函数会循环等待调试器命令，直到收到continue
    } else {
        //
        // ========================================
        // 还有其他核心未锁定
        // ========================================
        //

        // 自旋等待
        while (!DbgState->ContinueExecution) {
            _mm_pause();

            // 检查是否收到continue信号
            if (g_DebuggeeIsContinuing) {
                break;
            }
        }
    }

    //
    // ========================================
    // 5. 解锁核心
    // ========================================
    //
    SpinlockUnlock(&DbgState->Lock);

    LogInfo("Core %d: Unlocked, resuming execution", CoreId);
}
```

### 5.2.4 NMI广播的应用场景

#### 场景1：用户主动暂停

```
调试器运行中：
    用户按下Ctrl+Break
         ↓
    调试器发送"暂停"命令到被调试机
         ↓
    hyperkd接收命令
         ↓
    调用：VmxBroadcastNmi()
         ↓
    APIC发送NMI IPI到所有核心
         ↓
    每个核心收到NMI：
         ├─ Core 0: NMI in guest mode
         │   ↓
         │   VM-Exit
         │   ↓
         │   KdHandleNmi() -> 锁定
         │
         ├─ Core 1: NMI in VMX root mode
         │   ↓
         │   NMI handler
         │   ↓
         │   设置MTF
         │   ↓
         │   VMRESUME
         │   ↓
         │   Guest执行一条指令
         │   ↓
         │   MTF VM-Exit
         │   ↓
         │   KdHandleRegisteredMtfCallback()
         │   ↓
         │   检测到NMI waiting状态
         │   ↓
         │   锁定
         │
         └─ Core 2, 3, 4... 类似处理
         ↓
    所有核心在1-5ms内暂停
         ↓
    第一个完成的核心向调试器发送"已暂停"消息
         ↓
    所有核心进入等待状态
         ↓
    调试器显示提示符，等待用户命令
```

#### 场景2：断点命中后暂停其他核心

```c
/**
 * @brief 断点命中后的NMI广播
 */

// Core 2执行到断点
Core 2:
    执行INT3
         ↓
    VM-Exit (#BP异常)
         ↓
    BreakpointHandleBreakpoints()
         ↓
    KdHandleBreakpointAndDebugBreakpoints()
         ↓
    1. 锁定Core 2
         ↓
    2. 广播NMI到所有其他核心
         VmxBroadcastNmi()
         ↓
    3. 等待所有核心锁定
         while (!KdCheckAllCoresAreLocked()) {
             _mm_pause();
         }
         ↓
    4. 向调试器发送断点命中消息
         ↓
    5. 等待调试器命令

// 其他核心（Core 0, 1, 3, 4...）
收到NMI
         ↓
    KdHandleNmiBroadcastDebugBreaks()
         ↓
    锁定该核心
         ↓
    自旋等待continue信号

// 所有核心现在都暂停了
// 用户可以查看所有核心的状态
```

---

## 5.3 MTF（Monitor Trap Flag）机制

### 5.3.1 MTF概念

**MTF (Monitor Trap Flag)** 是VMCS中的一个控制位，当设置后，Guest执行**一条指令**就会触发VM-Exit。

**类比**：类似于x86架构的TF（Trap Flag，RFLAGS.TF），但作用于虚拟化层面。

```
RFLAGS.TF（传统调试）：
    设置TF -> 执行一条指令 -> 触发#DB异常

MTF（虚拟化调试）：
    设置MTF -> Guest执行一条指令 -> 触发VM-Exit
```

### 5.3.2 VMCS中的MTF控制

```c
// ============================================
// MTF相关的VMCS字段
// ============================================

// Processor-Based VM-Execution Controls
#define CPU_BASED_MONITOR_TRAP_FLAG  0x08000000

/**
 * @brief 设置MTF
 */
VOID HvSetMonitorTrapFlag(BOOLEAN Set) {
    UINT32 ProcessorBasedControls;

    // 读取当前控制字段
    __vmx_vmread(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
                 &ProcessorBasedControls);

    if (Set) {
        // 启用MTF
        ProcessorBasedControls |= CPU_BASED_MONITOR_TRAP_FLAG;
        LogInfo("MTF enabled");
    } else {
        // 禁用MTF
        ProcessorBasedControls &= ~CPU_BASED_MONITOR_TRAP_FLAG;
        LogInfo("MTF disabled");
    }

    // 写回VMCS
    __vmx_vmwrite(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
                  ProcessorBasedControls);
}

/**
 * @brief 检查MTF是否设置
 */
BOOLEAN HvIsMonitorTrapFlagSet() {
    UINT32 ProcessorBasedControls;

    __vmx_vmread(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
                 &ProcessorBasedControls);

    return (ProcessorBasedControls & CPU_BASED_MONITOR_TRAP_FLAG) != 0;
}
```

### 5.3.3 MTF VM-Exit处理

**文件位置**：`hyperdbg/hyperhv/code/vmm/vmx/Mtf.c`

```c
/**
 * @brief 处理MTF VM-Exit
 *
 * @param VCpu 虚拟处理器状态
 * @return BOOLEAN 处理成功返回TRUE
 */
BOOLEAN MtfHandleVmexit(VIRTUAL_MACHINE_STATE * VCpu) {

    LogInfo("Core %d: MTF VM-Exit at RIP: %llx", VCpu->CoreId, VCpu->LastVmexitRip);

    //
    // ========================================
    // 处理优先级1：断点重新应用
    // ========================================
    //
    // 当执行完断点后的一条指令，需要重新设置断点
    //
    if (g_Callbacks.BreakpointCheckAndHandleReApplyingBreakpoint != NULL &&
        g_Callbacks.BreakpointCheckAndHandleReApplyingBreakpoint(VCpu->CoreId)) {

        LogInfo("Core %d: Breakpoint reapplied", VCpu->CoreId);

        // 某些情况需要继续MTF
        goto ContinueMtfHandling;
    }

    //
    // ========================================
    // 处理优先级2：EPT Hook恢复
    // ========================================
    //
    // 执行完被Hook的指令后，恢复EPT权限限制
    //
    if (VCpu->MtfEptHookRestorePoint != NULL) {

        PEPT_HOOKED_PAGE_DETAIL HookedPage = VCpu->MtfEptHookRestorePoint;

        LogInfo("Core %d: Restoring EPT hook at GPA: %llx",
                VCpu->CoreId, HookedPage->PhysicalAddress);

        //
        // 获取EPT表项
        //
        EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(
            VCpu->EptPageTable,
            HookedPage->PhysicalAddress
        );

        //
        // 恢复权限限制
        //
        if (HookedPage->IsExecuteHook) {
            // 移除执行权限
            EptEntry->ExecuteAccess = 0;
            EptEntry->ReadAccess = 1;
            EptEntry->WriteAccess = 1;
        } else if (HookedPage->IsWriteHook) {
            // 移除写权限
            EptEntry->WriteAccess = 0;
            EptEntry->ReadAccess = 1;
            EptEntry->ExecuteAccess = 1;
        }

        //
        // 刷新EPT
        //
        EptInveptSingleContext(VCpu->EptPointer.AsUInt);

        // 清除恢复点
        VCpu->MtfEptHookRestorePoint = NULL;

        LogInfo("Core %d: EPT hook restored", VCpu->CoreId);
    }

    //
    // ========================================
    // 处理优先级3：注册的MTF处理
    // ========================================
    //
    // 调试器注册的MTF处理（单步执行等）
    //
    if (VCpu->RegisterBreakOnMtf) {

        LogInfo("Core %d: Calling registered MTF handler", VCpu->CoreId);

        // 调用注册的MTF处理回调
        VmmCallbackRegisteredMtfHandler(VCpu->CoreId);

        // 清除标志
        VCpu->RegisterBreakOnMtf = FALSE;

        goto ContinueMtfHandling;
    }

    //
    // ========================================
    // 处理优先级4：NMI相关的MTF
    // ========================================
    //
    // NMI在VMX root模式到达后设置的MTF
    //
    else if (g_Callbacks.KdCheckAndHandleNmiCallback != NULL &&
             g_Callbacks.KdCheckAndHandleNmiCallback(VCpu->CoreId)) {

        LogInfo("Core %d: NMI-related MTF handled", VCpu->CoreId);

        goto ContinueMtfHandling;
    }

    //
    // ========================================
    // 处理优先级5：忽略一次MTF
    // ========================================
    //
    // 某些情况下需要忽略MTF（如模式转换）
    //
    else if (VCpu->IgnoreOneMtf) {

        LogInfo("Core %d: Ignoring one MTF", VCpu->CoreId);

        VCpu->IgnoreOneMtf = FALSE;

        goto ContinueMtfHandling;
    }

ContinueMtfHandling:

    //
    // ========================================
    // 处理外部中断恢复
    // ========================================
    //
    if (VCpu->EnableExternalInterruptsOnContinueMtf) {
        // 恢复外部中断处理
        HvSetExternalInterruptExiting(VCpu, FALSE);
        VCpu->EnableExternalInterruptsOnContinueMtf = FALSE;

        LogInfo("Core %d: External interrupts restored", VCpu->CoreId);
    }

    //
    // ========================================
    // 关闭MTF（除非需要保持）
    // ========================================
    //
    if (!VCpu->IgnoreMtfUnset) {
        HvSetMonitorTrapFlag(FALSE);
        LogInfo("Core %d: MTF disabled", VCpu->CoreId);
    } else {
        VCpu->IgnoreMtfUnset = FALSE;
        LogInfo("Core %d: MTF kept enabled", VCpu->CoreId);
    }

    return TRUE;
}
```

### 5.3.4 MTF回调实现

**文件位置**：`hyperdbg/hyperkd/code/debugger/kernel-level/Kd.c`

```c
/**
 * @brief 注册的MTF处理回调
 *
 * @param CoreId 核心ID
 */
VOID KdHandleRegisteredMtfCallback(UINT32 CoreId) {
    PROCESSOR_DEBUGGING_STATE * DbgState = &g_DbgState[CoreId];

    if (DbgState->TracingMode) {
        //
        // ========================================
        // 场景1：指令跟踪模式（单步执行）
        // ========================================
        //
        LogInfo("Core %d: Tracing mode MTF", CoreId);

        TracingHandleMtf(DbgState);

        // TracingHandleMtf会：
        // 1. 记录执行的指令
        // 2. 递减跟踪计数
        // 3. 如果到达终点，暂停并通知调试器
        // 4. 否则保持MTF继续跟踪

    } else {
        //
        // ========================================
        // 场景2：仪表化单步（instrumentation step）
        // ========================================
        //
        LogInfo("Core %d: Instrumentation step MTF", CoreId);

        //
        // 检查CS选择器变化（用户态<->内核态切换）
        //
        UINT64 CurrentCsSel = VmFuncGetCsSelector();
        UINT16 PreviousCsSel = DbgState->InstrumentationStepInTrace.CsSel;

        if (PreviousCsSel != 0) {
            KdCheckGuestOperatingModeChanges(PreviousCsSel, (UINT16)CurrentCsSel);
        }

        // 清除保存的CS选择器
        DbgState->InstrumentationStepInTrace.CsSel = 0;

        //
        // ========================================
        // 场景3：检查软件断点
        // ========================================
        //
        UINT64 LastVmexitRip = VmFuncGetLastVmexitRip(CoreId);
        DEBUGGER_TRIGGERED_EVENT_DETAILS TargetContext = {0};

        // 检查当前RIP是否有软件断点
        if (BreakpointCheckAndHandleSoftwareDefinedBreakpoints(
                CoreId,
                LastVmexitRip,
                DEBUGGEE_PAUSING_REASON_DEBUGGEE_STEPPED,
                &TargetContext)) {

            LogInfo("Core %d: Software breakpoint hit after step", CoreId);

            // 暂停并通知调试器
            KdHandleBreakpointAndDebugBreakpointsCallback(
                CoreId,
                DEBUGGEE_PAUSING_REASON_DEBUGGEE_STEPPED,
                &TargetContext
            );
        }
    }
}

/**
 * @brief 处理指令跟踪
 */
VOID TracingHandleMtf(PROCESSOR_DEBUGGING_STATE * DbgState) {
    UINT32 CoreId = DbgState->CoreId;

    //
    // ========================================
    // 递减跟踪计数
    // ========================================
    //
    if (DbgState->TracingInstructionCount > 0) {
        DbgState->TracingInstructionCount--;

        LogInfo("Core %d: Traced instruction, remaining: %d",
                CoreId, DbgState->TracingInstructionCount);
    }

    //
    // ========================================
    // 检查是否到达终点
    // ========================================
    //
    if (DbgState->TracingInstructionCount == 0) {

        // 跟踪完成

        LogInfo("Core %d: Tracing completed", CoreId);

        //
        // 关闭跟踪模式
        //
        DbgState->TracingMode = FALSE;

        //
        // 暂停并通知调试器
        //
        KdHandleBreakpointAndDebugBreakpoints(
            DbgState,
            DEBUGGEE_PAUSING_REASON_DEBUGGEE_STEPPED,
            NULL
        );

        //
        // 关闭MTF
        //
        VmFuncSetMonitorTrapFlag(FALSE);

    } else {
        //
        // 继续跟踪
        //
        // MTF保持启用，Guest执行下一条指令后再次触发

        LogInfo("Core %d: Continuing trace", CoreId);
    }
}
```

### 5.3.5 MTF的应用场景

#### 场景A：EPT Hook的透明性（最重要）

```c
/**
 * @brief EPT执行Hook + MTF实现隐形Hook
 */

// ============================================
// Step 1: 设置EPT Hook
// ============================================
BOOLEAN SetupHiddenEptHook(UINT64 TargetFunction) {
    EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(TargetFunction);

    // 移除执行权限，保留读权限
    EptEntry->ExecuteAccess = 0;  // 不可执行
    EptEntry->ReadAccess = 1;     // 可读（隐藏Hook）
    EptEntry->WriteAccess = 1;    // 可写

    EptInveptSingleContext(EptPointer);

    LogInfo("EPT hook set at: %llx", TargetFunction);

    return TRUE;
}

// ============================================
// Step 2: Guest尝试执行被Hook的函数
// ============================================
Guest执行：call TargetFunction
    ↓
尝试从TargetFunction执行
    ↓
EPT权限检查：ExecuteAccess = 0
    ↓
触发EPT Violation
    ↓
VM-Exit (VMX_EXIT_REASON_EPT_VIOLATION)

// ============================================
// Step 3: EPT Violation处理
// ============================================
EptHandleEptViolation()
    ↓
检测到：ExecuteViolation = TRUE
    ↓
找到HookedPage
    ↓
HandleExecuteViolation():
    {
        LogInfo("Execute violation at: %llx", HookedPage->PhysicalAddress);

        // 1. 临时恢复执行权限
        EptEntry->ExecuteAccess = 1;

        // 2. 设置MTF - 关键步骤！
        VmFuncSetMonitorTrapFlag(TRUE);

        // 3. 保存恢复点
        VCpu->MtfEptHookRestorePoint = HookedPage;

        // 4. 刷新EPT
        EptInveptSingleContext(EptPointer);

        // 5. 不增加RIP（重新执行这条指令）
        VCpu->IncrementRip = FALSE;

        // 6. 可以在这里记录/修改执行
        LogInfo("Hooked function called from: %llx", VCpu->LastVmexitRip);
    }
    ↓
VMRESUME返回Guest

// ============================================
// Step 4: Guest执行被Hook的指令
// ============================================
Guest执行一条指令（现在可以执行了）
    ↓
MTF触发
    ↓
VM-Exit (VMX_EXIT_REASON_MONITOR_TRAP_FLAG)

// ============================================
// Step 5: MTF处理 - 恢复Hook
// ============================================
MtfHandleVmexit()
    ↓
检测到：VCpu->MtfEptHookRestorePoint != NULL
    ↓
    {
        // 1. 重新移除执行权限
        EptEntry->ExecuteAccess = 0;

        // 2. 刷新EPT
        EptInveptSingleContext(EptPointer);

        // 3. 清除恢复点
        VCpu->MtfEptHookRestorePoint = NULL;

        LogInfo("EPT hook restored");
    }
    ↓
    // 4. 关闭MTF
    HvSetMonitorTrapFlag(FALSE);
    ↓
VMRESUME返回Guest

// ============================================
// 结果：
// ============================================
// - Guest成功执行了被Hook的函数
// - Hook在执行前临时禁用，执行后恢复
// - Guest无法检测到Hook（读取时没有修改）
// - VMM成功拦截了执行
```

**为什么这样设计是"隐形"的？**

```
传统Hook（修改代码）：
    原始代码：48 8B 05 E1 2F 3D 00
    Hook后：  E9 XX XX XX XX 90 90  (jmp hook_function)

    Guest读取该地址：看到 E9 XX XX XX XX
    ❌ Hook可被检测

EPT Hook（不修改代码）：
    原始代码：48 8B 05 E1 2F 3D 00
    Hook后：  48 8B 05 E1 2F 3D 00  (代码未变)
    EPT：     ExecuteAccess = 0

    Guest读取该地址：看到 48 8B 05 E1 2F 3D 00
    ✅ Hook完全隐藏

    Guest尝试执行：触发EPT Violation
    ✅ VMM拦截成功
```

#### 场景B：单步调试

```c
/**
 * @brief 使用MTF实现单步调试
 */

// ============================================
// 调试器端：用户输入"t"命令
// ============================================
用户输入：t
    ↓
调试器发送STEP命令到被调试机
    ↓

// ============================================
// 被调试机端：处理STEP命令
// ============================================
KdHandleBreakpointAndDebugBreakpoints() 主循环
    ↓
收到：DEBUGGER_REMOTE_PACKET_REQUESTED_ACTION_ON_VMX_ROOT_MODE_STEP
    ↓
处理：
    {
        LogInfo("Step command received");

        // 1. 设置MTF
        VmFuncSetMonitorTrapFlag(TRUE);

        // 2. 设置跟踪模式
        DbgState->TracingMode = TRUE;
        DbgState->TracingInstructionCount = 1;  // 只执行一条

        // 3. 继续执行
        KdContinueDebuggee(DbgState, TRUE, ...);

        // 4. 退出主循环
        EscapeFromTheLoop = TRUE;
    }
    ↓
VMRESUME返回Guest

// ============================================
// Guest执行一条指令
// ============================================
Guest执行：mov rax, [rbx]
    ↓
MTF触发
    ↓
VM-Exit (VMX_EXIT_REASON_MONITOR_TRAP_FLAG)

// ============================================
// MTF处理
// ============================================
MtfHandleVmexit()
    ↓
检测到：VCpu->RegisterBreakOnMtf (或TracingMode)
    ↓
调用：VmmCallbackRegisteredMtfHandler(CoreId)
    ↓
KdHandleRegisteredMtfCallback()
    ↓
TracingHandleMtf()
    {
        // 递减计数
        DbgState->TracingInstructionCount--;  // 1 -> 0

        // 到达终点
        if (DbgState->TracingInstructionCount == 0) {
            // 关闭跟踪模式
            DbgState->TracingMode = FALSE;

            // 暂停并通知调试器
            KdHandleBreakpointAndDebugBreakpoints(
                DbgState,
                DEBUGGEE_PAUSING_REASON_DEBUGGEE_STEPPED,
                NULL
            );
        }
    }
    ↓
锁定核心，发送"已暂停"消息到调试器

// ============================================
// 调试器端：显示执行结果
// ============================================
接收"已暂停"包
    ↓
解析新的RIP和寄存器
    ↓
显示：
    0: kd> t
    nt!SomeFunction+0x5:
    fffff800`1234567d  mov rax, qword ptr [rbx]

    rax=0000000000001234 rbx=fffffa8000000000 ...

    0: kd>
```

#### 场景C：NMI后的精确暂停

```c
/**
 * @brief NMI在VMX root模式 + MTF的配合
 */

// ============================================
// 问题场景
// ============================================
/*
场景：Core 1正在VMX root模式处理一个VM-Exit（如CPUID）

此时收到NMI广播（用户按了Ctrl+Break）

问题：
  - 当前在VMM中，没有Guest上下文
  - 不能直接暂停（无法获取Guest RIP、寄存器等）
  - 如果等待当前VM-Exit处理完成：
    * 可能VMM处理很长时间
    * Guest会错过CPU时间
    * 调试器看到的状态不准确

解决方案：MTF
*/

// ============================================
// Step 1: NMI到达（在VMX root）
// ============================================
VmxBroadcastHandleNmiCallback()
    ↓
检测到：VCpu->IsOnVmxRootMode == TRUE
    ↓
调用：VmmCallbackNmiBroadcastRequestHandler(CoreId, TRUE)
    ↓
KdHandleNmiBroadcastDebugBreaks(CoreId, IsOnVmxNmiHandler=TRUE)
    {
        // 标记等待锁定
        DbgState->NmiState.WaitingToBeLocked = TRUE;
        DbgState->NmiState.NmiCalledInVmxRootRelatedToHaltDebuggee = TRUE;

        // 设置MTF
        VmFuncSetMonitorTrapFlag(TRUE);

        LogInfo("NMI in VMX root, MTF set");
    }
    ↓
返回到当前的VM-Exit handler
    ↓
VM-Exit handler完成处理
    ↓
VMRESUME返回Guest

// ============================================
// Step 2: Guest执行一条指令
// ============================================
Guest执行一条指令（如：add rax, 1）
    ↓
MTF立即触发
    ↓
VM-Exit (VMX_EXIT_REASON_MONITOR_TRAP_FLAG)

// ============================================
// Step 3: MTF处理
// ============================================
MtfHandleVmexit()
    ↓
调用：g_Callbacks.KdCheckAndHandleNmiCallback(CoreId)
    ↓
KdCheckAndHandleNmiCallback()
    {
        // 检查NMI等待状态
        if (DbgState->NmiState.WaitingToBeLocked) {

            LogInfo("Core %d: Handling delayed NMI", CoreId);

            // 现在有完整的Guest上下文了
            // 可以安全地锁定和暂停

            // 锁定核心
            SpinlockLock(&DbgState->Lock);

            // 清除NMI标志
            DbgState->NmiState.WaitingToBeLocked = FALSE;
            DbgState->NmiState.NmiCalledInVmxRootRelatedToHaltDebuggee = FALSE;

            // 检查是否所有核心都锁定
            if (KdCheckAllCoresAreLocked()) {
                // 发送"已暂停"消息
                KdSendPausedPacketToDebugger(...);

                // 进入调试器主循环
                KdManageBreakStateOfDebugger(DbgState);
            } else {
                // 等待其他核心
                while (!g_DebuggeeIsContinuing) {
                    _mm_pause();
                }
            }

            // 解锁
            SpinlockUnlock(&DbgState->Lock);

            return TRUE;
        }

        return FALSE;
    }
    ↓
关闭MTF
    ↓
VMRESUME

// ============================================
// 结果
// ============================================
✅ Guest只错过了一条指令的时间（微秒级）
✅ 获取到准确的Guest状态
✅ 所有核心都被精确暂停
```

---

## 5.4 DPC机制实现单核心任务

### 5.4.1 DPC概念

**DPC (Deferred Procedure Call)** 是Windows内核的延迟过程调用机制：

- **延迟执行**：不立即执行，而是排队等待调度
- **IRQL = DISPATCH_LEVEL**：运行在较高的IRQL
- **可指定CPU**：可以指定在特定CPU核心上执行
- **常用场景**：中断服务例程的下半部、定时器回调等

### 5.4.2 为什么需要单核心DPC？

```
问题：VMCS是per-CPU的，只能在对应核心上访问

场景：需要读取Core 2的VMCS信息

错误做法：
    当前在Core 0
    直接执行：__vmx_vmread(VMCS_GUEST_RIP, &Rip);
    ❌ 错误！读取的是Core 0的VMCS，不是Core 2的

正确做法：
    使用DPC在Core 2上执行代码
    DpcRoutineRunTaskOnSingleCore(2, ReadVmcsOnCore2, ...);
    ✅ 正确！在Core 2上读取Core 2的VMCS
```

### 5.4.3 DPC实现

**文件位置**：`hyperdbg/hyperhv/code/broadcast/DpcRoutines.c`

```c
/**
 * @brief 在指定CPU核心上运行任务
 *
 * @param CoreNumber 目标核心号
 * @param Routine 要执行的函数
 * @param DeferredContext 传递给函数的上下文
 * @return NTSTATUS 成功返回STATUS_SUCCESS
 */
NTSTATUS DpcRoutineRunTaskOnSingleCore(
    UINT32 CoreNumber,
    PVOID  Routine,
    PVOID  DeferredContext
) {
    PRKDPC Dpc;
    ULONG  ProcessorsCount;

    ProcessorsCount = KeQueryActiveProcessorCount(0);

    //
    // ========================================
    // 1. 验证核心号
    // ========================================
    //
    if (CoreNumber >= ProcessorsCount) {
        LogError("Invalid core number: %d (max: %d)", CoreNumber, ProcessorsCount - 1);
        return STATUS_INVALID_PARAMETER;
    }

    //
    // ========================================
    // 2. 分配DPC对象
    // ========================================
    //
    Dpc = PlatformMemAllocateNonPagedPool(sizeof(KDPC));

    if (!Dpc) {
        LogError("Failed to allocate DPC");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // ========================================
    // 3. 初始化DPC
    // ========================================
    //
    KeInitializeDpc(
        Dpc,                            // DPC对象
        (PKDEFERRED_ROUTINE)Routine,    // 要执行的函数
        DeferredContext                 // 上下文参数
    );

    //
    // ========================================
    // 4. 设置目标处理器
    // ========================================
    //
    KeSetTargetProcessorDpc(Dpc, (CCHAR)CoreNumber);

    //
    // ========================================
    // 5. 设置重要性（高优先级）
    // ========================================
    //
    KeSetImportanceDpc(Dpc, HighImportance);

    //
    // ========================================
    // 6. 插入DPC队列
    // ========================================
    //
    // 系统调度器会在目标CPU核心上执行这个DPC
    if (!KeInsertQueueDpc(Dpc, NULL, NULL)) {
        LogError("Failed to insert DPC");
        PlatformMemFreePool(Dpc);
        return STATUS_UNSUCCESSFUL;
    }

    LogInfo("DPC queued for core %d", CoreNumber);

    return STATUS_SUCCESS;
}

/**
 * @brief 在所有核心上运行任务
 *
 * @param Routine 要执行的函数
 * @param DeferredContext 上下文参数
 * @return NTSTATUS
 */
NTSTATUS DpcRoutineRunTaskOnAllCores(
    PVOID Routine,
    PVOID DeferredContext
) {
    ULONG ProcessorsCount = KeQueryActiveProcessorCount(0);

    LogInfo("Running task on all %d cores", ProcessorsCount);

    //
    // ========================================
    // 遍历所有核心
    // ========================================
    //
    for (ULONG i = 0; i < ProcessorsCount; i++) {

        NTSTATUS Status = DpcRoutineRunTaskOnSingleCore(
            i,
            Routine,
            DeferredContext
        );

        if (!NT_SUCCESS(Status)) {
            LogError("Failed to run task on core %d: %x", i, Status);
            return Status;
        }
    }

    //
    // ========================================
    // 等待所有DPC完成
    // ========================================
    //
    // 注意：KeInsertQueueDpc是异步的，需要等待完成
    KeWaitForAllDpcs();

    LogInfo("All DPC tasks completed");

    return STATUS_SUCCESS;
}
```

### 5.4.4 DPC应用场景

#### 场景A：读取特定核心的VMCS

```c
/**
 * @brief 读取指定核心的Guest RIP
 */
typedef struct _READ_RIP_CONTEXT {
    UINT64 GuestRip;
    BOOLEAN Success;
} READ_RIP_CONTEXT;

// DPC函数（在目标核心上执行）
VOID DpcReadGuestRip(PVOID Context) {
    READ_RIP_CONTEXT * Ctx = (READ_RIP_CONTEXT *)Context;

    // 现在在目标核心上，可以安全读取其VMCS
    UINT64 Rip;
    if (__vmx_vmread(VMCS_GUEST_RIP, &Rip) == 0) {
        Ctx->GuestRip = Rip;
        Ctx->Success = TRUE;

        LogInfo("Core %d: Guest RIP = %llx",
                KeGetCurrentProcessorNumber(), Rip);
    } else {
        Ctx->Success = FALSE;
        LogError("Core %d: Failed to read Guest RIP",
                 KeGetCurrentProcessorNumber());
    }
}

// 调用示例
UINT64 GetGuestRipOfCore(UINT32 TargetCore) {
    READ_RIP_CONTEXT Context = {0};

    // 在目标核心上执行DPC
    DpcRoutineRunTaskOnSingleCore(
        TargetCore,
        DpcReadGuestRip,
        &Context
    );

    // 等待DPC完成
    // （实际需要同步机制，这里简化）

    if (Context.Success) {
        return Context.GuestRip;
    } else {
        return 0;
    }
}
```

#### 场景B：在特定核心设置断点

```c
/**
 * @brief 在特定核心设置异常拦截
 */
typedef struct _SET_EXCEPTION_BITMAP_CONTEXT {
    UINT32 ExceptionVector;
    BOOLEAN Enable;
} SET_EXCEPTION_BITMAP_CONTEXT;

// DPC函数
VOID DpcSetExceptionBitmap(PVOID Context) {
    SET_EXCEPTION_BITMAP_CONTEXT * Ctx = (SET_EXCEPTION_BITMAP_CONTEXT *)Context;
    UINT32 ExceptionBitmap;

    // 读取当前异常位图
    __vmx_vmread(VMCS_CTRL_EXCEPTION_BITMAP, &ExceptionBitmap);

    if (Ctx->Enable) {
        // 设置位
        ExceptionBitmap |= (1 << Ctx->ExceptionVector);
    } else {
        // 清除位
        ExceptionBitmap &= ~(1 << Ctx->ExceptionVector);
    }

    // 写回VMCS
    __vmx_vmwrite(VMCS_CTRL_EXCEPTION_BITMAP, ExceptionBitmap);

    LogInfo("Core %d: Exception bitmap updated for vector %d",
            KeGetCurrentProcessorNumber(), Ctx->ExceptionVector);
}

// 使用示例：只在Core 2上拦截#BP
SET_EXCEPTION_BITMAP_CONTEXT Context;
Context.ExceptionVector = EXCEPTION_VECTOR_BREAKPOINT;
Context.Enable = TRUE;

DpcRoutineRunTaskOnSingleCore(2, DpcSetExceptionBitmap, &Context);
```

#### 场景C：同步所有核心的配置

```c
/**
 * @brief 在所有核心上启用CPUID拦截
 */

// DPC函数
VOID DpcEnableCpuidVmexit(PVOID Context) {
    UINT32 ProcessorBasedControls;

    // 读取当前控制字段
    __vmx_vmread(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
                 &ProcessorBasedControls);

    // 启用CPUID Exiting（实际上CPUID总是触发VM-Exit，这里是示例）
    // 在实际中可能是其他控制位，如RDTSC Exiting

    // 写回VMCS
    __vmx_vmwrite(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
                  ProcessorBasedControls);

    LogInfo("Core %d: CPUID exiting configured",
            KeGetCurrentProcessorNumber());
}

// 在所有核心上执行
VOID EnableCpuidVmexitOnAllCores() {
    ULONG ProcessorsCount = KeQueryActiveProcessorCount(0);

    LogInfo("Enabling CPUID VM-Exit on all cores");

    for (UINT32 i = 0; i < ProcessorsCount; i++) {
        DpcRoutineRunTaskOnSingleCore(i, DpcEnableCpuidVmexit, NULL);
    }

    LogInfo("CPUID VM-Exit enabled on all %d cores", ProcessorsCount);
}
```

---

## 5.5 多核调试的同步机制

### 5.5.1 调试锁结构

```c
/**
 * @brief 处理器调试状态
 * @details 每个CPU核心一个实例
 */
typedef struct _PROCESSOR_DEBUGGING_STATE {

    //
    // ========================================
    // 调试锁
    // ========================================
    //
    volatile LONG Lock;                   // 自旋锁
    BOOLEAN WaitingToBeLocked;           // 等待被锁定标志
    BOOLEAN ContinueExecution;           // 继续执行标志

    //
    // ========================================
    // NMI状态
    // ========================================
    //
    struct {
        BOOLEAN WaitingToBeLocked;                          // NMI等待锁定
        BOOLEAN NmiCalledInVmxRootRelatedToHaltDebuggee;   // NMI在VMX root中收到
    } NmiState;

    //
    // ========================================
    // 跟踪模式
    // ========================================
    //
    BOOLEAN TracingMode;                 // 是否在指令跟踪模式
    UINT64  TracingInstructionCount;     // 要跟踪的指令数量

    //
    // ========================================
    // 仪表化单步
    // ========================================
    //
    struct {
        UINT16 CsSel;                    // 保存的CS选择器（检测模式切换）
    } InstrumentationStepInTrace;

    //
    // ========================================
    // 其他调试状态
    // ========================================
    //
    UINT64 ProcessId;
    UINT64 ThreadId;
    BOOLEAN DoNotNmiNotifyOtherCoresByThisCore;

} PROCESSOR_DEBUGGING_STATE, *PPROCESSOR_DEBUGGING_STATE;

// 全局调试状态数组
PROCESSOR_DEBUGGING_STATE * g_DbgState;
```

### 5.5.2 自旋锁实现

```c
/**
 * @brief 获取调试锁
 */
VOID SpinlockLock(volatile LONG * Lock) {
    LONG OldValue;

    // 自旋直到获取锁
    while (TRUE) {
        // 尝试原子交换
        OldValue = InterlockedCompareExchange(Lock, 1, 0);

        if (OldValue == 0) {
            // 成功获取锁
            break;
        }

        // 失败，继续自旋
        _mm_pause();  // 减少总线竞争
    }
}

/**
 * @brief 释放调试锁
 */
VOID SpinlockUnlock(volatile LONG * Lock) {
    // 原子设置为0
    InterlockedExchange(Lock, 0);
}

/**
 * @brief 检查锁状态
 */
BOOLEAN SpinlockCheckLock(volatile LONG * Lock) {
    return (*Lock == 1);
}
```

### 5.5.3 暂停所有核心的完整流程

```
┌────────────────────────────────────────────────────────────┐
│  Step 1: 调试器发送暂停命令                                │
└────────────────────────────────────────────────────────────┘

用户在调试器端按下Ctrl+Break
    ↓
调试器构建暂停命令包
    ↓
通过串口发送到被调试机
    ↓

┌────────────────────────────────────────────────────────────┐
│  Step 2: Core 0处理暂停命令                                │
└────────────────────────────────────────────────────────────┘

Core 0的某个VM-Exit中：
    SerialConnectionRecvBuffer()接收到暂停命令
    ↓
解析命令：DEBUGGER_REMOTE_PACKET_REQUESTED_ACTION_ON_VMX_ROOT_MODE_PAUSE
    ↓
调用：VmxBroadcastNmi()
    ↓

┌────────────────────────────────────────────────────────────┐
│  Step 3: APIC广播NMI到所有核心                            │
└────────────────────────────────────────────────────────────┘

ApicWriteIcr(APIC_ICR_DELIVERY_MODE_NMI |
             APIC_ICR_DESTINATION_ALL_EXCLUDING_SELF)
    ↓
硬件向所有其他核心发送NMI
    ↓

┌────────────────────────────────────────────────────────────┐
│  Step 4: 各个核心收到NMI                                   │
└────────────────────────────────────────────────────────────┘

Core 0: 触发NMI的核心
    设置：DbgState[0].NmiState.WaitingToBeLocked = TRUE
    锁定：SpinlockLock(&DbgState[0].Lock)
    等待其他核心

Core 1: 在Guest模式
    NMI到达
    ↓
    VM-Exit (VMX_EXIT_REASON_EXCEPTION_OR_NMI)
    ↓
    VmxBroadcastHandleNmiCallback()
    ↓
    KdHandleNmiBroadcastDebugBreaks(1, FALSE)
    ↓
    KdHandleNmi(&DbgState[1])
    ↓
    SpinlockLock(&DbgState[1].Lock)

Core 2: 在VMX Root模式（正在处理CPUID VM-Exit）
    NMI到达
    ↓
    NMI handler（不触发VM-Exit）
    ↓
    VmxBroadcastHandleNmiCallback()
    ↓
    KdHandleNmiBroadcastDebugBreaks(2, TRUE)
    ↓
    设置：DbgState[2].NmiState.NmiCalledInVmxRootRelatedToHaltDebuggee = TRUE
    ↓
    VmFuncSetMonitorTrapFlag(TRUE)
    ↓
    返回到当前VM-Exit handler
    ↓
    VMRESUME
    ↓
    Guest执行一条指令
    ↓
    MTF VM-Exit
    ↓
    KdCheckAndHandleNmiCallback(2)
    ↓
    SpinlockLock(&DbgState[2].Lock)

Core 3, 4, 5... 类似处理

┌────────────────────────────────────────────────────────────┐
│  Step 5: 所有核心锁定完成                                  │
└────────────────────────────────────────────────────────────┘

最后一个锁定的核心检测到：
    KdCheckAllCoresAreLocked() == TRUE
    ↓
向调试器发送"已暂停"消息：
    KdSendPausedPacketToDebugger(
        CoreId,
        DEBUGGEE_PAUSING_REASON_DEBUGGEE_GENERAL_DEBUG_BREAK,
        NULL
    )
    ↓

┌────────────────────────────────────────────────────────────┐
│  Step 6: 调试器显示状态                                    │
└────────────────────────────────────────────────────────────┘

调试器接收"已暂停"包
    ↓
显示：
    Break instruction exception - code 80000003 (first chance)

    0: kd>

    rax=0000000000000001 rbx=0000000000000000 rcx=fffffa8012345678
    ...
    ↓
等待用户命令

┌────────────────────────────────────────────────────────────┐
│  Step 7: 用户输入继续命令                                  │
└────────────────────────────────────────────────────────────┘

用户输入：g
    ↓
调试器发送CONTINUE命令
    ↓

┌────────────────────────────────────────────────────────────┐
│  Step 8: 所有核心同步解锁                                  │
└────────────────────────────────────────────────────────────┘

某个核心接收到CONTINUE命令
    ↓
设置全局标志：g_DebuggeeIsContinuing = TRUE
    ↓
所有核心检测到标志
    ↓
依次解锁：
    Core 0: SpinlockUnlock(&DbgState[0].Lock)
    Core 1: SpinlockUnlock(&DbgState[1].Lock)
    Core 2: SpinlockUnlock(&DbgState[2].Lock)
    ...
    ↓
所有核心VMRESUME返回Guest
    ↓
系统继续正常运行
```

### 5.5.4 锁状态检查

```c
/**
 * @brief 检查所有核心是否都已锁定
 */
BOOLEAN KdCheckAllCoresAreLocked() {
    ULONG ProcessorsCount = KeQueryActiveProcessorCount(0);

    //
    // ========================================
    // 遍历所有核心
    // ========================================
    //
    for (UINT32 i = 0; i < ProcessorsCount; i++) {

        if (!SpinlockCheckLock(&g_DbgState[i].Lock)) {
            // 发现一个未锁定的核心
            LogInfo("Core %d is not locked yet", i);
            return FALSE;
        }
    }

    //
    // 所有核心都锁定了
    //
    LogInfo("All cores are locked");
    return TRUE;
}

/**
 * @brief 检查特定核心是否锁定
 */
BOOLEAN KdCheckTargetCoreIsLocked(UINT32 CoreNumber) {
    return SpinlockCheckLock(&g_DbgState[CoreNumber].Lock);
}

/**
 * @brief 等待所有核心锁定
 */
VOID KdWaitForAllCoresLocked() {
    UINT32 SpinCount = 0;
    const UINT32 MaxSpinCount = 1000000;  // 防止死锁

    LogInfo("Waiting for all cores to lock...");

    while (!KdCheckAllCoresAreLocked()) {
        _mm_pause();

        SpinCount++;
        if (SpinCount >= MaxSpinCount) {
            LogWarning("Timeout waiting for all cores to lock");
            break;
        }
    }

    if (SpinCount < MaxSpinCount) {
        LogInfo("All cores locked in %d iterations", SpinCount);
    }
}
```

---

## 5.6 NMI vs MTF 对比分析

### 5.6.1 特性对比

| 特性 | NMI | MTF |
|------|-----|-----|
| **触发方式** | 外部中断（APIC） | VMCS标志位 |
| **作用范围** | 所有核心（广播） | 单个核心 |
| **可屏蔽性** | 不可屏蔽 | N/A（总是生效） |
| **粒度** | 中断级别 | 单条指令 |
| **主要用途** | 暂停所有核心调试 | 单步执行、Hook透明化 |
| **延迟** | 1-5毫秒 | 单条指令执行时间（纳秒级） |
| **在Guest模式** | 触发VM-Exit | 触发VM-Exit |
| **在VMX root模式** | 触发NMI handler | N/A（MTF只影响Guest） |

### 5.6.2 配合使用

NMI和MTF经常配合使用：

```c
/**
 * @brief NMI + MTF配合实现精确暂停
 */

场景：NMI在VMX root模式到达

只用NMI（不用MTF）的问题：
    ❌ 当前在VMM中，没有Guest上下文
    ❌ 无法获取准确的Guest RIP
    ❌ 无法获取Guest寄存器状态
    ❌ 不知道Guest执行到哪里了

NMI + MTF的解决方案：
    ✅ NMI handler设置MTF
    ✅ 立即返回Guest（VMRESUME）
    ✅ Guest执行一条指令后MTF触发
    ✅ 此时有完整的Guest上下文
    ✅ 可以精确暂停

代码流程：
    NMI到达（在VMX root）
        ↓
    VmFuncSetMonitorTrapFlag(TRUE);
        ↓
    当前VM-Exit handler完成
        ↓
    VMRESUME返回Guest
        ↓
    Guest执行一条指令（微秒级）
        ↓
    MTF触发VM-Exit
        ↓
    获取完整Guest上下文
        ↓
    锁定核心
```

### 5.6.3 使用场景总结

```c
/**
 * @brief 何时使用NMI？
 */

// ✅ 交互式调试器
BOOLEAN InteractiveDebugging() {
    while (DebuggeeRunning) {
        if (UserPressedPause()) {
            // 需要立即暂停所有核心
            // 只有NMI能做到！
            BroadcastNmiToAllCores();
            WaitAllCoresHalted();

            // 进入交互模式
            while (InDebuggerPrompt) {
                ProcessUserCommands();
            }
        }
    }
}

// ❌ 自动化Hook（不需要NMI）
BOOLEAN AutomatedHook() {
    SetupEptHook(TargetFunction);

    // Hook自动触发，不需要人工干预
    // 完全不需要NMI
}

/**
 * @brief 何时使用MTF？
 */

// ✅ EPT Hook透明性（必需）
BOOLEAN HiddenEptHook() {
    // 移除执行权限
    EptEntry->ExecuteAccess = 0;

    // Guest执行时：
    // EPT Violation -> 临时恢复权限 -> 设置MTF
    // -> Guest执行一条指令 -> MTF触发 -> 恢复Hook

    // MTF是实现透明性的关键
}

// ✅ 单步调试（必需）
BOOLEAN SingleStepDebugging() {
    VmFuncSetMonitorTrapFlag(TRUE);

    // Guest执行一条指令后暂停
}

// ✅ NMI精确暂停（重要）
BOOLEAN PreciseHalt() {
    // NMI在VMX root -> 设置MTF -> 获取Guest上下文
}
```

---

## 5.7 特殊情况处理

### 5.7.1 NMI在VM-Exit处理中到达

```c
/**
 * @brief 处理VM-Exit期间的NMI
 */

场景：Core 1正在处理一个复杂的VM-Exit（如EPT Violation）
      处理过程中收到NMI

问题：
    - 当前在VMX root模式
    - VM-Exit handler可能很长（数毫秒）
    - Guest会错过很多CPU时间
    - 其他核心可能已经暂停，等待Core 1

解决方案：
    VmxBroadcastHandleNmiCallback()
        ↓
    检测到：IsOnVmxRootMode = TRUE
        ↓
    设置MTF标志
        ↓
    立即结束当前VM-Exit处理（设置快速返回标志）
        ↓
    VMRESUME
        ↓
    Guest执行一条指令
        ↓
    MTF触发
        ↓
    现在可以安全暂停
```

### 5.7.2 MTF与其他VM-Exit的竞争

```c
/**
 * @brief MTF可能与其他VM-Exit同时触发
 */

场景：设置了MTF，Guest执行的下一条指令是CPUID

可能的执行顺序：

方案1：MTF优先
    Guest: cpuid
    ↓
    MTF VM-Exit (VMX_EXIT_REASON_MONITOR_TRAP_FLAG)
    ↓
    MtfHandleVmexit()
    ↓
    关闭MTF
    ↓
    VMRESUME
    ↓
    Guest: cpuid (再次执行)
    ↓
    CPUID VM-Exit (VMX_EXIT_REASON_EXECUTE_CPUID)
    ↓
    DispatchEventCpuid()

方案2：CPUID优先（实际行为）
    Guest: cpuid
    ↓
    CPUID VM-Exit (VMX_EXIT_REASON_EXECUTE_CPUID)
    ↓
    DispatchEventCpuid()
    ↓
    增加RIP（跳过CPUID）
    ↓
    VMRESUME
    ↓
    Guest执行下一条指令
    ↓
    MTF VM-Exit
    ↓
    MtfHandleVmexit()

// Intel SDM规定：
// 无条件VM-Exit指令（如CPUID）优先于MTF
// 所以方案2是正确的
```

### 5.7.3 MTF与中断的交互

```c
/**
 * @brief MTF期间的中断处理
 */

问题：设置MTF后，Guest可能收到外部中断

场景：
    设置MTF
    ↓
    Guest执行指令：mov rax, [rbx]
    ↓
    同时有外部中断到达
    ↓
    ???

解决方案：HyperDbg的处理
    MtfHandleVmexit()中：
    {
        // 如果有待处理的外部中断
        if (VCpu->EnableExternalInterruptsOnContinueMtf) {

            // 恢复外部中断处理
            HvSetExternalInterruptExiting(VCpu, FALSE);

            // 清除标志
            VCpu->EnableExternalInterruptsOnContinueMtf = FALSE;
        }

        // 关闭MTF
        HvSetMonitorTrapFlag(FALSE);

        // 下一次VM-Exit可能是外部中断
    }
```

---

## 5.8 性能和时序考虑

### 5.8.1 NMI延迟

```c
/**
 * @brief NMI广播的时序分析
 */

典型的NMI响应时间：

Time 0: 发送NMI广播
    ↓
Time 0.001ms - 0.01ms: 第一个核心收到NMI
    ↓
Time 0.01ms - 0.1ms: 大部分核心收到NMI
    ↓
Time 0.1ms - 1ms: 在VMX root的核心设置MTF并返回Guest
    ↓
Time 1ms - 5ms: 所有核心完成锁定
    ↓

影响因素：
- 核心数量（更多核心 = 更长时间）
- APIC延迟
- VMX root模式的核心数量（需要MTF的额外延迟）
- 中断优先级
```

### 5.8.2 MTF开销

```c
/**
 * @brief MTF的性能开销
 */

单次MTF的开销：
    设置MTF
        ↓
    VMRESUME (~100 cycles)
        ↓
    Guest执行一条指令 (1-多个cycles)
        ↓
    MTF VM-Exit (~1000 cycles)
        ↓
    MtfHandleVmexit() (100-1000 cycles)
        ↓
    总开销：约2000-3000 cycles = 1-2微秒 @ 2GHz

对比：
- EPT Hook每次触发：约2000-3000 cycles
- 普通VM-Exit：约1000-2000 cycles
- 系统调用：约100-200 cycles

结论：MTF的开销可接受，但不应频繁使用
```

### 5.8.3 优化技巧

```c
/**
 * @brief 减少MTF使用
 */

// ❌ 不好的做法：频繁MTF
VOID BadExample() {
    for (int i = 0; i < 1000000; i++) {
        VmFuncSetMonitorTrapFlag(TRUE);
        // 执行一条指令
        // MTF触发
        // 处理
        VmFuncSetMonitorTrapFlag(FALSE);
    }
    // 性能损失巨大！
}

// ✅ 好的做法：批量处理
VOID GoodExample() {
    // 设置一次MTF
    VmFuncSetMonitorTrapFlag(TRUE);
    DbgState->TracingInstructionCount = 1000000;

    // TracingHandleMtf中：
    // 每次MTF只递减计数
    // 不执行复杂操作
    // 直到计数为0才暂停

    // MTF自动持续触发，无需每次设置
}

/**
 * @brief 避免不必要的MTF
 */

// ❌ 不好的做法：总是使用MTF
VOID SetBreakpoint(UINT64 Address) {
    // 移除执行权限
    EptEntry->ExecuteAccess = 0;

    // 总是设置MTF
    VmFuncSetMonitorTrapFlag(TRUE);  // 浪费！
}

// ✅ 好的做法：按需使用MTF
VOID SetBreakpoint(UINT64 Address) {
    // 移除执行权限
    EptEntry->ExecuteAccess = 0;

    // 不设置MTF
    // 只在EPT Violation时临时设置
}

VOID HandleEptViolation() {
    // 临时恢复权限
    EptEntry->ExecuteAccess = 1;

    // 只在这里设置MTF
    VmFuncSetMonitorTrapFlag(TRUE);
}
```

---

## 5.9 实际应用综合示例

### 5.9.1 完整的单步调试流程

```c
/**
 * @brief 用户执行"t 10"命令（跟踪10条指令）
 */

// ============================================
// 调试器端
// ============================================
用户输入：t 10
    ↓
解析命令：TraceCount = 10
    ↓
构建STEP包：
    StepPacket.Count = 10
    ↓
发送到被调试机

// ============================================
// 被调试机端：接收并处理
// ============================================
KdHandleBreakpointAndDebugBreakpoints() 主循环
    ↓
收到STEP命令
    ↓
    {
        // 设置跟踪模式
        DbgState->TracingMode = TRUE;
        DbgState->TracingInstructionCount = 10;

        // 设置MTF
        VmFuncSetMonitorTrapFlag(TRUE);

        // 继续执行
        KdContinueDebuggee(...);
    }
    ↓
解锁核心
    ↓
VMRESUME返回Guest

// ============================================
// Guest执行第1条指令
// ============================================
Guest: mov rax, [rbx]
    ↓
MTF触发
    ↓
MtfHandleVmexit()
    ↓
VmmCallbackRegisteredMtfHandler()
    ↓
TracingHandleMtf()
    {
        DbgState->TracingInstructionCount--;  // 10 -> 9
        LogInfo("Traced: mov rax, [rbx] at %llx", Rip);

        // 未到终点，保持MTF
        // 继续执行
    }
    ↓
VMRESUME

// ============================================
// Guest执行第2-9条指令
// ============================================
每条指令后都触发MTF，递减计数
    9 -> 8 -> 7 -> 6 -> 5 -> 4 -> 3 -> 2 -> 1

// ============================================
// Guest执行第10条指令
// ============================================
Guest: call SomeFunction
    ↓
MTF触发
    ↓
TracingHandleMtf()
    {
        DbgState->TracingInstructionCount--;  // 1 -> 0

        // 到达终点！
        LogInfo("Tracing completed");

        // 关闭跟踪模式
        DbgState->TracingMode = FALSE;

        // 关闭MTF
        VmFuncSetMonitorTrapFlag(FALSE);

        // 暂停并通知调试器
        KdHandleBreakpointAndDebugBreakpoints(
            DbgState,
            DEBUGGEE_PAUSING_REASON_DEBUGGEE_STEPPED,
            NULL
        );
    }
    ↓
锁定核心，发送"已暂停"消息

// ============================================
// 调试器端：显示结果
// ============================================
接收"已暂停"包
    ↓
显示：
    0: kd> t 10
    (traced 10 instructions)

    nt!SomeFunction:
    fffff800`12345690  push rbx

    0: kd>
```

### 5.9.2 EPT Hook + MTF的完整流程

```c
/**
 * @brief 隐藏Hook的完整生命周期
 */

// ============================================
// 初始化：设置Hook
// ============================================
UINT64 TargetFunction = GetFunctionAddress("NtCreateFile");

EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(TargetFunction);

// 保存原始权限
OriginalEntry = *EptEntry;

// 移除执行权限
EptEntry->ExecuteAccess = 0;
EptEntry->ReadAccess = 1;   // 保持可读（隐藏）
EptEntry->WriteAccess = 1;

EptInveptSingleContext(EptPointer);

LogInfo("Hidden hook installed at: %llx", TargetFunction);

// ============================================
// 触发：Guest调用被Hook的函数
// ============================================
Guest执行：call NtCreateFile
    ↓
尝试执行NtCreateFile第一条指令
    ↓
EPT权限检查失败（ExecuteAccess = 0）
    ↓
触发EPT Violation
    ↓
VM-Exit (VMX_EXIT_REASON_EPT_VIOLATION)

// ============================================
// 处理：EPT Violation handler
// ============================================
EptHandleEptViolation()
    ↓
读取违规信息：
    ViolationQual.DataExecute = 1
    GuestPhysicalAddress = (NtCreateFile的物理地址)
    ↓
查找Hook：
    HookedPage = FindHook(GuestPhysicalAddress)
    ↓
处理执行违规：
    {
        LogInfo("Hooked function called from: %llx", CallerRip);

        // === 这里可以执行Hook逻辑 ===
        // - 记录调用
        // - 修改参数
        // - 统计次数
        // - 等等

        // 1. 临时恢复执行权限
        EptEntry->ExecuteAccess = 1;
        EptEntry->ReadAccess = 0;  // 移除读权限（防止读取修改后的代码）

        // 2. 设置MTF - 关键！
        VmFuncSetMonitorTrapFlag(TRUE);

        // 3. 保存恢复点
        VCpu->MtfEptHookRestorePoint = HookedPage;

        // 4. 刷新EPT
        EptInveptSingleContext(EptPointer);

        // 5. 不增加RIP（重新执行）
        VCpu->IncrementRip = FALSE;
    }
    ↓
VMRESUME

// ============================================
// 执行：Guest执行被Hook的指令
// ============================================
Guest重新执行：第一条指令（现在可以执行了）
    例如：mov rdi, rsp
    ↓
执行成功（因为ExecuteAccess = 1）
    ↓
MTF立即触发
    ↓
VM-Exit (VMX_EXIT_REASON_MONITOR_TRAP_FLAG)

// ============================================
// 恢复：MTF handler恢复Hook
// ============================================
MtfHandleVmexit()
    ↓
检测到：VCpu->MtfEptHookRestorePoint != NULL
    ↓
    {
        // 1. 获取EPT表项
        EptEntry = EptGetPml1Entry(HookedPage->PhysicalAddress);

        // 2. 重新移除执行权限
        EptEntry->ExecuteAccess = 0;
        EptEntry->ReadAccess = 1;  // 恢复读权限

        // 3. 刷新EPT
        EptInveptSingleContext(EptPointer);

        // 4. 清除恢复点
        VCpu->MtfEptHookRestorePoint = NULL;

        LogInfo("Hook restored, ready for next call");
    }
    ↓
    // 5. 关闭MTF
    HvSetMonitorTrapFlag(FALSE);
    ↓
VMRESUME

// ============================================
// 继续：Guest正常执行
// ============================================
Guest继续执行NtCreateFile的后续指令
    ↓
Hook已恢复，下次调用会再次触发

// ============================================
// 透明性验证
// ============================================
如果Guest尝试读取NtCreateFile的代码：
    mov rax, [NtCreateFile]
    ↓
    读取成功（ReadAccess = 1）
    ↓
    看到原始代码（未修改）
    ✅ Hook完全隐藏

如果Guest尝试执行NtCreateFile：
    call NtCreateFile
    ↓
    触发EPT Violation
    ✅ Hook生效
```

---

## 本章小结

本章深入讲解了NMI广播和MTF机制：

1. **NMI机制**
   - 不可屏蔽中断的特性
   - VMCS中的NMI控制
   - NMI在VMX root和non-root的不同行为

2. **NMI广播**
   - 通过APIC向所有核心发送NMI
   - 用于立即暂停所有核心
   - 交互式调试的核心技术

3. **MTF机制**
   - Monitor Trap Flag单步执行
   - VMCS控制位设置
   - MTF VM-Exit处理

4. **MTF应用**
   - EPT Hook透明性（最重要）
   - 单步调试
   - NMI后的精确暂停
   - 断点重新应用

5. **DPC机制**
   - 在指定核心执行任务
   - 用于访问per-CPU的VMCS
   - 同步所有核心配置

6. **多核同步**
   - 调试锁结构
   - 自旋锁实现
   - 暂停/继续的协调

7. **NMI vs MTF**
   - NMI用于主动暂停（交互式调试需要）
   - MTF用于单步控制（Hook透明性需要）
   - 两者经常配合使用

**关键结论**：
- EPT Hook和隐藏断点**不需要NMI**（只需MTF）
- 交互式调试器**需要NMI**（用户体验）
- MTF是实现Hook透明性的**核心技术**

---

[<< 上一章：调试器通信机制](./第四章-调试器通信机制.md) | [下一章：EPT Hook技术深入 >>](./第六章-EPT-Hook技术深入.md)
