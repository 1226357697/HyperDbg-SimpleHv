# 第七章：HyperEvade反检测模块

## 7.1 HyperEvade概述

### 7.1.1 什么是HyperEvade？

**HyperEvade** 是HyperDbg的反检测模块，目标是让虚拟化调试器对Guest操作系统**完全透明**，使得反调试和反虚拟化检测技术失效。

### 7.1.2 模块结构

```
hyperdbg/hyperevade/
├── code/
│   ├── Transparency.c         - 透明模式主控制逻辑
│   ├── VmxFootprints.c        - VMX/虚拟化痕迹隐藏
│   ├── SyscallFootprints.c    - 系统调用层面的痕迹隐藏
│   └── UnloadDll.c            - DLL卸载相关
│
└── header/
    ├── Transparency.h         - 透明模式接口
    ├── VmxFootprints.h        - VMX痕迹隐藏定义
    └── SyscallFootprints.h    - 系统调用隐藏定义
```

### 7.1.3 设计目标

HyperEvade要对抗的检测技术：

| 检测技术 | 检测原理 | HyperEvade对策 |
|---------|---------|---------------|
| **CPUID检测** | CPUID.1:ECX[31] = Hypervisor bit | 清除该位 |
| **CPUID Vendor** | CPUID.0x40000000 返回厂商ID | 返回0 |
| **RDTSC Timing** | 测量VM-Exit时间差 | TSC补偿 |
| **MSR检测** | 读取Hypervisor MSR | 注入#GP异常 |
| **系统信息查询** | NtQuerySystemInformation | 修改返回结果 |
| **驱动枚举** | 枚举系统驱动列表 | 过滤HyperDbg驱动 |
| **调试器检测** | IsDebuggerPresent等API | 返回FALSE |
| **硬件信息** | SMBIOS/ACPI查询 | 伪造真实硬件信息 |

---

## 7.2 透明模式控制

### 7.2.1 透明模式启用

**文件位置**：`hyperdbg/hyperevade/code/Transparency.c`

```c
/**
 * @brief 启用透明模式
 *
 * @param HyperevadeCallbacks HyperEvade回调函数
 * @param TransparentModeRequest 透明模式请求参数
 * @return BOOLEAN 成功返回TRUE
 */
BOOLEAN TransparentHideDebugger(
    HYPEREVADE_CALLBACKS *                        HyperevadeCallbacks,
    DEBUGGER_HIDE_AND_TRANSPARENT_DEBUGGER_MODE * TransparentModeRequest
) {
    //
    // ========================================
    // 1. 验证回调函数
    // ========================================
    //
    // 检查所有必需的回调是否都已设置
    for (UINT32 i = 0; i < sizeof(HYPEREVADE_CALLBACKS) / sizeof(UINT64); i++) {
        if (((PVOID *)HyperevadeCallbacks)[i] == NULL) {
            LogError("HyperEvade callback at index %d is NULL", i);
            TransparentModeRequest->KernelStatus =
                DEBUGGER_ERROR_UNABLE_TO_HIDE_OR_UNHIDE_DEBUGGER;
            return FALSE;
        }
    }

    //
    // ========================================
    // 2. 保存回调
    // ========================================
    //
    RtlCopyMemory(&g_Callbacks, HyperevadeCallbacks, sizeof(HYPEREVADE_CALLBACKS));

    //
    // ========================================
    // 3. 检查是否已启用
    // ========================================
    //
    if (g_TransparentMode) {
        LogWarning("Transparent mode already enabled");
        TransparentModeRequest->KernelStatus = DEBUGGER_ERROR_DEBUGGER_ALREADY_HIDE;
        return FALSE;
    }

    //
    // ========================================
    // 4. 保存系统调用号信息
    // ========================================
    //
    // 不同Windows版本的系统调用号不同
    RtlCopyBytes(
        &g_SystemCallNumbersInformation,
        &TransparentModeRequest->SystemCallNumbersInformation,
        sizeof(SYSTEM_CALL_NUMBERS_INFORMATION)
    );

    LogInfo("System call numbers:");
    LogInfo("  NtQuerySystemInformation: %x", g_SystemCallNumbersInformation.SysNtQuerySystemInformation);
    LogInfo("  NtSystemDebugControl: %x", g_SystemCallNumbersInformation.SysNtSystemDebugControl);
    // ... 其他系统调用号

#if DISABLE_HYPERDBG_HYPEREVADE == FALSE

    //
    // ========================================
    // 5. 随机选择厂商字符串
    // ========================================
    //
    // 从真实厂商列表中随机选择一个
    TRANSPARENT_GENUINE_VENDOR_STRING_INDEX = TransparentGetRand() %
        (sizeof(TRANSPARENT_LEGIT_VENDOR_STRINGS_WCHAR) /
         sizeof(TRANSPARENT_LEGIT_VENDOR_STRINGS_WCHAR[0]));

    PWCHAR SelectedVendor = TRANSPARENT_LEGIT_VENDOR_STRINGS_WCHAR[
        TRANSPARENT_GENUINE_VENDOR_STRING_INDEX
    ];

    LogInfo("Selected vendor: %ws", SelectedVendor);

#endif

    //
    // ========================================
    // 6. 启用透明模式
    // ========================================
    //
    g_TransparentMode = TRUE;
    TransparentModeRequest->KernelStatus = DEBUGGER_OPERATION_WAS_SUCCESSFUL;

    LogInfo("Transparent mode enabled successfully");

    return TRUE;
}

/**
 * @brief 禁用透明模式
 */
BOOLEAN TransparentUnhideDebugger() {
    if (!g_TransparentMode) {
        LogWarning("Transparent mode is not enabled");
        return FALSE;
    }

    //
    // 禁用透明模式
    //
    g_TransparentMode = FALSE;

    LogInfo("Transparent mode disabled");

    return TRUE;
}

/**
 * @brief 生成随机数（使用RDTSC）
 */
UINT32 TransparentGetRand() {
    UINT64 Tsc;
    UINT32 Rand;

    // 读取时间戳计数器
    Tsc = __rdtsc();

    // 取低16位作为随机数
    Rand = (UINT32)(Tsc & 0xffff);

    return Rand;
}
```

### 7.2.2 选择性透明化

```c
/**
 * @brief 透明进程结构
 */
typedef struct _TRANSPARENCY_PROCESS {
    PVOID  BufferAddress;                     // 结构本身的地址（用于释放）
    UINT64 ProcessId;                         // 进程ID
    PVOID  ProcessName;                       // 进程名（可选）
    BOOLEAN TrueIfProcessIdAndFalseIfProcessName;  // 使用PID还是名称
    LIST_ENTRY ProcessList;                   // 链表节点
} TRANSPARENCY_PROCESS, *PTRANSPARENCY_PROCESS;

// 透明进程列表
LIST_ENTRY g_TransparentProcessList;

/**
 * @brief 添加进程到透明列表
 */
BOOLEAN TransparentAddNameOrProcessIdToTheList(
    PDEBUGGER_HIDE_AND_TRANSPARENT_DEBUGGER_MODE Measurements
) {
    PTRANSPARENCY_PROCESS PidAndNameBuffer;
    SIZE_T SizeOfBuffer;

    //
    // ========================================
    // 1. 确定缓冲区大小
    // ========================================
    //
    if (Measurements->TrueIfProcessIdAndFalseIfProcessName) {
        // 使用进程ID
        SizeOfBuffer = sizeof(TRANSPARENCY_PROCESS);
    } else {
        // 使用进程名（需要额外空间存储字符串）
        SizeOfBuffer = sizeof(TRANSPARENCY_PROCESS) + Measurements->LengthOfProcessName;
    }

    //
    // ========================================
    // 2. 分配缓冲区
    // ========================================
    //
    PidAndNameBuffer = PlatformMemAllocateZeroedNonPagedPool(SizeOfBuffer);

    if (PidAndNameBuffer == NULL) {
        LogError("Failed to allocate transparency process buffer");
        return FALSE;
    }

    // 保存地址用于释放
    PidAndNameBuffer->BufferAddress = PidAndNameBuffer;

    //
    // ========================================
    // 3. 填充结构
    // ========================================
    //
    if (Measurements->TrueIfProcessIdAndFalseIfProcessName) {
        // 进程ID
        PidAndNameBuffer->ProcessId = Measurements->ProcId;
        PidAndNameBuffer->TrueIfProcessIdAndFalseIfProcessName = TRUE;

        LogInfo("Adding process to transparency list by PID: %lld", Measurements->ProcId);

    } else {
        // 进程名
        PidAndNameBuffer->TrueIfProcessIdAndFalseIfProcessName = FALSE;

        // 复制进程名到缓冲区末尾
        RtlCopyBytes(
            (void *)((UINT64)PidAndNameBuffer + sizeof(TRANSPARENCY_PROCESS)),
            (const void *)((UINT64)Measurements + sizeof(DEBUGGER_HIDE_AND_TRANSPARENT_DEBUGGER_MODE)),
            Measurements->LengthOfProcessName
        );

        // 设置进程名指针
        PidAndNameBuffer->ProcessName = (PVOID)((UINT64)PidAndNameBuffer + sizeof(TRANSPARENCY_PROCESS));

        LogInfo("Adding process to transparency list by name: %s",
                (CHAR *)PidAndNameBuffer->ProcessName);
    }

    //
    // ========================================
    // 4. 添加到链表
    // ========================================
    //
    InsertHeadList(&g_TransparentProcessList, &PidAndNameBuffer->ProcessList);

    return TRUE;
}

/**
 * @brief 检查当前进程是否需要透明化
 */
BOOLEAN TransparentCheckIfProcessIsInList() {
    PLIST_ENTRY Entry;
    PTRANSPARENCY_PROCESS TransProcess;
    PEPROCESS CurrentProcess;
    UINT64 CurrentPid;
    PCHAR CurrentProcessName;

    //
    // 获取当前进程信息
    //
    CurrentProcess = PsGetCurrentProcess();
    CurrentPid = HANDLE_TO_UINT64(PsGetCurrentProcessId());
    CurrentProcessName = g_Callbacks.CommonGetProcessNameFromProcessControlBlock(CurrentProcess);

    //
    // ========================================
    // 遍历透明进程列表
    // ========================================
    //
    Entry = g_TransparentProcessList.Flink;

    while (Entry != &g_TransparentProcessList) {
        TransProcess = CONTAINING_RECORD(Entry, TRANSPARENCY_PROCESS, ProcessList);

        if (TransProcess->TrueIfProcessIdAndFalseIfProcessName) {
            //
            // 按PID匹配
            //
            if (TransProcess->ProcessId == CurrentPid) {
                LogInfo("Current process (%lld) is in transparency list", CurrentPid);
                return TRUE;
            }

        } else {
            //
            // 按名称匹配
            //
            if (strcmp(CurrentProcessName, (PCHAR)TransProcess->ProcessName) == 0) {
                LogInfo("Current process (%s) is in transparency list", CurrentProcessName);
                return TRUE;
            }
        }

        Entry = Entry->Flink;
    }

    // 不在列表中
    return FALSE;
}
```

---

## 7.3 VMX痕迹隐藏

### 7.3.1 CPUID伪造

**文件位置**：`hyperdbg/hyperevade/code/VmxFootprints.c`

```c
/**
 * @brief 当透明模式启用时处理CPUID
 *
 * @param Regs Guest寄存器
 * @param CpuInfo CPUID结果（将被修改）
 */
VOID TransparentCheckAndModifyCpuid(PGUEST_REGS Regs, INT32 CpuInfo[]) {

    //
    // ========================================
    // 检测方法1：CPUID Leaf 1 - 特性标志
    // ========================================
    //
    if (Regs->rax == CPUID_PROCESSOR_AND_PROCESSOR_FEATURE_IDENTIFIERS) {
        //
        // 正常情况：
        // CPUID(EAX=1) -> ECX[31] = 1 表示在Hypervisor中运行
        //

        LogInfo("CPUID(1) intercepted");

        //
        // 反检测：清除Hypervisor Present位
        //
        CpuInfo[2] &= ~HYPERV_HYPERVISOR_PRESENT_BIT;  // ECX[31] = 0

        LogInfo("Hypervisor bit cleared in CPUID(1)");

        /*
         * 效果：
         * Guest执行 CPUID(EAX=1)
         *   → ECX[31] = 0
         *   → 看起来不在虚拟化环境中
         */
    }

    //
    // ========================================
    // 检测方法2：CPUID Leaf 0x40000000 - Hypervisor信息
    // ========================================
    //
    else if (Regs->rax == CPUID_HV_VENDOR_AND_MAX_FUNCTIONS ||
             Regs->rax == HYPERV_CPUID_INTERFACE) {
        //
        // 正常情况：
        // CPUID(EAX=0x40000000) -> "HyperDbg" 厂商字符串
        // CPUID(EAX=0x40000001) -> Hypervisor接口信息
        //

        LogInfo("CPUID(0x40000000+) intercepted");

        //
        // 反检测：返回全0（表示无Hypervisor）
        //
        CpuInfo[0] = 0x40000000;  // EAX
        CpuInfo[1] = 0x40000000;  // EBX
        CpuInfo[2] = 0x40000000;  // ECX
        CpuInfo[3] = 0x40000000;  // EDX

        LogInfo("Hypervisor CPUID leaves zeroed");

        /*
         * 效果：
         * Guest执行 CPUID(EAX=0x40000000)
         *   → 返回全0
         *   → 无法获取Hypervisor信息
         */
    }
}

// ============================================
// 正常模式下的CPUID处理（对比）
// ============================================
VOID HvHandleCpuid(VIRTUAL_MACHINE_STATE * VCpu) {
    INT32 CpuInfo[4];
    PGUEST_REGS Regs = VCpu->Regs;

    // 执行真实的CPUID
    __cpuidex(CpuInfo, (INT32)Regs->rax, (INT32)Regs->rcx);

    if (!g_CheckForFootprints) {
        //
        // 非透明模式：暴露Hypervisor
        //
        if (Regs->rax == CPUID_PROCESSOR_AND_PROCESSOR_FEATURE_IDENTIFIERS) {
            // 设置Hypervisor Present位
            CpuInfo[2] |= HYPERV_HYPERVISOR_PRESENT_BIT;

            LogInfo("Hypervisor bit exposed");
        }
        else if (Regs->rax == CPUID_HV_VENDOR_AND_MAX_FUNCTIONS) {
            // 返回HyperDbg厂商ID
            CpuInfo[0] = HYPERV_CPUID_INTERFACE;
            CpuInfo[1] = 'epyH';  // "HyperDbg"反向
            CpuInfo[2] = 'gbDr';
            CpuInfo[3] = 0;

            LogInfo("HyperDbg vendor ID returned");
        }
    } else {
        //
        // 透明模式：隐藏Hypervisor
        //
        TransparentCheckAndModifyCpuid(Regs, CpuInfo);
    }

    // 将结果写入Guest寄存器
    Regs->rax = CpuInfo[0];
    Regs->rbx = CpuInfo[1];
    Regs->rcx = CpuInfo[2];
    Regs->rdx = CpuInfo[3];
}
```

### 7.3.2 MSR读写伪造

```c
/**
 * @brief 处理RDMSR（透明模式）
 *
 * @param Regs Guest寄存器
 * @param TargetMsr 目标MSR号（在ECX中）
 * @return BOOLEAN TRUE表示应该短路（不执行真实RDMSR）
 */
BOOLEAN TransparentCheckAndModifyMsrRead(PGUEST_REGS Regs, UINT32 TargetMsr) {

    //
    // ========================================
    // 检测方法：读取Hypervisor保留的MSR范围
    // ========================================
    //
    // MSR范围 0x40000000 - 0x400000F0 是为Hypervisor保留的
    // 正常情况下，Guest读取这些MSR会成功（如果Hypervisor实现了）
    //

    if (TargetMsr >= RESERVED_MSR_RANGE_LOW &&
        TargetMsr <= RESERVED_MSR_RANGE_HI) {

        LogInfo("RDMSR to reserved hypervisor MSR: %x", TargetMsr);

        //
        // 反检测：注入#GP异常
        //
        // 注意：这段代码在HyperDbg中被注释掉了
        // 原因：某些嵌套虚拟化环境（如Hyper-V）需要访问这些MSR
        //
        // g_Callbacks.EventInjectGeneralProtection();
        // return TRUE;  // 短路，不执行真实RDMSR
    }

    // 不处理，正常执行
    return FALSE;
}

/**
 * @brief 处理WRMSR（透明模式）
 */
BOOLEAN TransparentCheckAndModifyMsrWrite(PGUEST_REGS Regs, UINT32 TargetMsr) {

    if (TargetMsr >= RESERVED_MSR_RANGE_LOW &&
        TargetMsr <= RESERVED_MSR_RANGE_HI) {

        LogInfo("WRMSR to reserved hypervisor MSR: %x", TargetMsr);
        LogInfo("  Value: %llx:%llx", Regs->rdx, Regs->rax);

        //
        // 反检测：注入#GP异常（被注释）
        //
        // g_Callbacks.EventInjectGeneralProtection();
        // return TRUE;
    }

    return FALSE;
}
```

### 7.3.3 Trap Flag处理

```c
/**
 * @brief 处理VM-Exit后的Trap Flag
 *
 * @details
 * 当VMM模拟一条指令（如CPUID）时，如果Guest设置了TF（Trap Flag），
 * 需要在指令执行后注入#DB异常
 */
VOID TransparentCheckAndTrapFlagAfterVmexit() {

    //
    // ========================================
    // 问题场景
    // ========================================
    //
    /*
    Guest设置了TF（单步调试自己）：
        RFLAGS.TF = 1
            ↓
        执行CPUID
            ↓
        VM-Exit到VMM
            ↓
        VMM模拟CPUID
            ↓
        增加RIP
            ↓
        VMRESUME
            ↓
        ❌ Guest期望收到#DB异常，但没有收到
        ❌ Guest检测到异常行为
    */

    //
    // ========================================
    // 解决方案
    // ========================================
    //
    // 如果RIP被增加（指令被模拟），需要处理TF

    g_Callbacks.HvHandleTrapFlag();

    // HvHandleTrapFlag会：
    // 1. 检查Guest RFLAGS.TF
    // 2. 如果TF=1，注入#DB异常
    // 3. Guest会正常收到单步异常
}

/**
 * @brief 实际的Trap Flag处理
 */
VOID HvHandleTrapFlag() {
    RFLAGS GuestRflags;

    // 读取Guest RFLAGS
    __vmx_vmread(VMCS_GUEST_RFLAGS, &GuestRflags.AsUInt);

    if (GuestRflags.TrapFlag) {
        //
        // Guest设置了TF
        //
        LogInfo("Guest TF is set, injecting #DB");

        // 注入#DB异常
        EventInjectDebugBreakpoint();

        // 清除TF（硬件会自动清除）
        // GuestRflags.TrapFlag = 0;
        // __vmx_vmwrite(VMCS_GUEST_RFLAGS, GuestRflags.AsUInt);
    }
}
```

---

## 7.4 系统调用痕迹隐藏

### 7.4.1 系统调用Hook入口

**文件位置**：`hyperdbg/hyperevade/code/SyscallFootprints.c`

```c
/**
 * @brief 系统调用号结构
 * @details 不同Windows版本的系统调用号不同
 */
typedef struct _SYSTEM_CALL_NUMBERS_INFORMATION {
    UINT32 SysNtQuerySystemInformation;      // 通常是0x36
    UINT32 SysNtQuerySystemInformationEx;
    UINT32 SysNtSystemDebugControl;
    UINT32 SysNtQueryAttributesFile;
    UINT32 SysNtOpenDirectoryObject;
    UINT32 SysNtQueryDirectoryObject;
    UINT32 SysNtQueryInformationProcess;
    UINT32 SysNtQueryInformationThread;
    UINT32 SysNtOpenFile;
    UINT32 SysNtOpenKey;
    UINT32 SysNtOpenKeyEx;
    UINT32 SysNtQueryValueKey;
    UINT32 SysNtEnumerateKey;
    // ... 更多系统调用
} SYSTEM_CALL_NUMBERS_INFORMATION;

SYSTEM_CALL_NUMBERS_INFORMATION g_SystemCallNumbersInformation;

/**
 * @brief 处理系统调用Hook
 *
 * @param Regs Guest寄存器
 * @details
 * 这个函数在系统调用入口点（KiSystemCall64）被调用
 * 通过EPT Hook拦截
 */
VOID TransparentHandleSystemCallHook(GUEST_REGS * Regs) {

    //
    // ========================================
    // 检查透明模式
    // ========================================
    //
    if (!g_TransparentMode) {
        return;
    }

    //
    // ========================================
    // 获取调用者进程名
    // ========================================
    //
    PCHAR CallingProcess = g_Callbacks.CommonGetProcessNameFromProcessControlBlock(
        PsGetCurrentProcess()
    );

    //
    // ========================================
    // 跳过Windows系统进程
    // ========================================
    //
    // 对系统进程应用透明化可能导致系统不稳定
    for (ULONG i = 0; i < (sizeof(TRANSPARENT_WIN_PROCESS_IGNORE) /
                           sizeof(TRANSPARENT_WIN_PROCESS_IGNORE[0])); i++) {

        if (strstr(CallingProcess, TRANSPARENT_WIN_PROCESS_IGNORE[i])) {
            // 是系统进程，不处理
            return;
        }
    }

    //
    // ========================================
    // 获取系统调用号
    // ========================================
    //
    // 系统调用号在RAX寄存器
    UINT64 SyscallNumber = Regs->rax;

    //
    // ========================================
    // 根据系统调用号分派处理
    // ========================================
    //
    if (SyscallNumber == g_SystemCallNumbersInformation.SysNtQuerySystemInformation ||
        SyscallNumber == g_SystemCallNumbersInformation.SysNtQuerySystemInformationEx) {

        // 处理NtQuerySystemInformation
        TransparentHandleNtQuerySystemInformationSyscall(Regs);
    }
    else if (SyscallNumber == g_SystemCallNumbersInformation.SysNtSystemDebugControl) {

        // 处理NtSystemDebugControl
        TransparentHandleNtSystemDebugControlSyscall(Regs);
    }
    else if (SyscallNumber == g_SystemCallNumbersInformation.SysNtQueryAttributesFile) {

        // 处理NtQueryAttributesFile
        TransparentHandleNtQueryAttributesFileSyscall(Regs);
    }
    else if (SyscallNumber == g_SystemCallNumbersInformation.SysNtOpenDirectoryObject) {

        // 处理NtOpenDirectoryObject
        TransparentHandleNtOpenDirectoryObjectSyscall(Regs);
    }
    else if (SyscallNumber == g_SystemCallNumbersInformation.SysNtQueryInformationProcess) {

        // 处理NtQueryInformationProcess
        TransparentHandleNtQueryInformationProcessSyscall(Regs);
    }
    else if (SyscallNumber == g_SystemCallNumbersInformation.SysNtOpenFile) {

        // 处理NtOpenFile
        TransparentHandleNtOpenFileSyscall(Regs);
    }
    else if (SyscallNumber == g_SystemCallNumbersInformation.SysNtOpenKeyEx ||
             SyscallNumber == g_SystemCallNumbersInformation.SysNtOpenKey) {

        // 处理NtOpenKey
        TransparentHandleNtOpenKeySyscall(Regs);
    }
    else if (SyscallNumber == g_SystemCallNumbersInformation.SysNtQueryValueKey) {

        // 处理NtQueryValueKey
        TransparentHandleNtQueryValueKeySyscall(Regs);
    }
    else if (SyscallNumber == g_SystemCallNumbersInformation.SysNtEnumerateKey) {

        // 处理NtEnumerateKey
        TransparentHandleNtEnumerateKeySyscall(Regs);
    }
    else {
        // 其他系统调用不处理
    }
}

/**
 * @brief 忽略的Windows进程列表
 */
static const PCHAR TRANSPARENT_WIN_PROCESS_IGNORE[] = {
    "System",
    "Registry",
    "smss.exe",
    "csrss.exe",
    "wininit.exe",
    "services.exe",
    "lsass.exe",
    "winlogon.exe",
    // ... 更多系统进程
};
```

### 7.4.2 NtQuerySystemInformation处理

```c
/**
 * @brief 处理NtQuerySystemInformation系统调用
 *
 * @param Regs Guest寄存器
 */
VOID TransparentHandleNtQuerySystemInformationSyscall(GUEST_REGS * Regs) {
    SYSCALL_CALLBACK_CONTEXT_PARAMS ContextParams = {0};

    //
    // ========================================
    // 系统调用参数（Windows x64调用约定）
    // ========================================
    //
    // NTSTATUS NtQuerySystemInformation(
    //     SYSTEM_INFORMATION_CLASS SystemInformationClass,  // RCX -> R10
    //     PVOID SystemInformation,                          // RDX
    //     ULONG SystemInformationLength,                    // R8
    //     PULONG ReturnLength                               // R9
    // );
    //
    // 注意：SYSCALL指令会将RCX保存到R11，R10复制到RCX

    UINT32 SystemInformationClass = (UINT32)Regs->r10;  // 第一个参数
    UINT64 SystemInformation = Regs->rdx;                // 第二个参数
    UINT32 SystemInformationLength = (UINT32)Regs->r8;  // 第三个参数

    switch (SystemInformationClass) {

        //
        // ========================================
        // 检测方法1：查询进程信息
        // ========================================
        //
        case SystemProcessInformation:
        case SystemExtendedProcessInformation:
        {
            LogInfo("SystemProcessInformation query intercepted");

            //
            // 反检测：设置Trap Flag，在返回后修改结果
            //
            ContextParams.OptionalParam1 = SystemProcessInformation;
            ContextParams.OptionalParam2 = SystemInformation;       // 输出缓冲区
            ContextParams.OptionalParam3 = SystemInformationLength - 0x400;

            // 设置Trap Flag（RFLAGS.TF）
            // 系统调用返回到用户态后会触发#DB
            // 在#DB handler中修改SystemInformation缓冲区
            g_Callbacks.SyscallCallbackSetTrapFlagAfterSyscall(
                Regs,
                HANDLE_TO_UINT32(PsGetCurrentProcessId()),
                HANDLE_TO_UINT32(PsGetCurrentThreadId()),
                Regs->rax,  // 系统调用号
                &ContextParams
            );

            break;
        }

        //
        // ========================================
        // 检测方法2：查询模块信息
        // ========================================
        //
        case SystemModuleInformation:
        {
            LogInfo("SystemModuleInformation query intercepted");

            //
            // 正常情况：返回所有加载的驱动模块
            // 包括：hyperhv.sys, hyperkd.sys等
            //

            //
            // 反检测：从列表中过滤HyperDbg相关驱动
            //
            ContextParams.OptionalParam1 = SystemModuleInformation;
            ContextParams.OptionalParam2 = SystemInformation;
            ContextParams.OptionalParam3 = SystemInformationLength;

            g_Callbacks.SyscallCallbackSetTrapFlagAfterSyscall(
                Regs,
                HANDLE_TO_UINT32(PsGetCurrentProcessId()),
                HANDLE_TO_UINT32(PsGetCurrentThreadId()),
                Regs->rax,
                &ContextParams
            );

            break;
        }

        //
        // ========================================
        // 检测方法3：查询内核调试器信息
        // ========================================
        //
        case SystemKernelDebuggerInformation:
        {
            LogInfo("SystemKernelDebuggerInformation query intercepted");

            //
            // 正常情况：
            // typedef struct _SYSTEM_KERNEL_DEBUGGER_INFORMATION {
            //     BOOLEAN KernelDebuggerEnabled;
            //     BOOLEAN KernelDebuggerNotPresent;
            // } SYSTEM_KERNEL_DEBUGGER_INFORMATION;
            //
            // 如果调试器运行，KernelDebuggerEnabled = TRUE
            //

            //
            // 反检测：修改返回值
            //
            ContextParams.OptionalParam1 = SystemKernelDebuggerInformation;
            ContextParams.OptionalParam2 = SystemInformation;
            ContextParams.OptionalParam3 = SystemInformationLength;

            g_Callbacks.SyscallCallbackSetTrapFlagAfterSyscall(
                Regs,
                HANDLE_TO_UINT32(PsGetCurrentProcessId()),
                HANDLE_TO_UINT32(PsGetCurrentThreadId()),
                Regs->rax,
                &ContextParams
            );

            // 在Trap Flag handler中：
            // KernelDebuggerInformation->KernelDebuggerEnabled = FALSE;
            // KernelDebuggerInformation->KernelDebuggerNotPresent = TRUE;

            break;
        }

        //
        // ========================================
        // 检测方法4：代码完整性信息
        // ========================================
        //
        case SystemCodeIntegrityInformation:
        {
            LogInfo("SystemCodeIntegrityInformation query intercepted");

            //
            // 反检测：伪造正常的完整性状态
            //
            ContextParams.OptionalParam1 = SystemCodeIntegrityInformation;
            ContextParams.OptionalParam2 = SystemInformation;
            ContextParams.OptionalParam3 = 0x8;

            g_Callbacks.SyscallCallbackSetTrapFlagAfterSyscall(
                Regs,
                HANDLE_TO_UINT32(PsGetCurrentProcessId()),
                HANDLE_TO_UINT32(PsGetCurrentThreadId()),
                Regs->rax,
                &ContextParams
            );

            break;
        }

        default:
        {
            // 其他信息类不处理
            return;
        }
    }
}
```

### 7.4.3 Trap Flag回调处理

```c
/**
 * @brief 系统调用返回后的Trap Flag处理
 *
 * @details
 * 这个回调在系统调用返回到用户态后触发
 * 此时可以安全地修改返回的数据
 */
VOID TransparentCallbackHandleAfterSyscall(
    GUEST_REGS *                      Regs,
    UINT32                            ProcessId,
    UINT32                            ThreadId,
    UINT64                            Context,
    SYSCALL_CALLBACK_CONTEXT_PARAMS * Params
) {
    UINT32 SystemInformationClass = (UINT32)Params->OptionalParam1;
    UINT64 SystemInformation = Params->OptionalParam2;
    UINT32 SystemInformationLength = (UINT32)Params->OptionalParam3;

    LogInfo("After-syscall callback: Class=%x", SystemInformationClass);

    switch (SystemInformationClass) {

        //
        // ========================================
        // 修改SystemKernelDebuggerInformation结果
        // ========================================
        //
        case SystemKernelDebuggerInformation:
        {
            SYSTEM_KERNEL_DEBUGGER_INFORMATION KernelDebugInfo;

            // 读取返回的结构
            MemoryMapperReadMemorySafe(
                SystemInformation,
                &KernelDebugInfo,
                sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION),
                ProcessId
            );

            LogInfo("Original: KernelDebuggerEnabled=%d, NotPresent=%d",
                    KernelDebugInfo.KernelDebuggerEnabled,
                    KernelDebugInfo.KernelDebuggerNotPresent);

            //
            // 修改为：无调试器
            //
            KernelDebugInfo.KernelDebuggerEnabled = FALSE;
            KernelDebugInfo.KernelDebuggerNotPresent = TRUE;

            // 写回
            MemoryMapperWriteMemorySafe(
                SystemInformation,
                &KernelDebugInfo,
                sizeof(SYSTEM_KERNEL_DEBUGGER_INFORMATION),
                ProcessId
            );

            LogInfo("Modified: KernelDebuggerEnabled=FALSE, NotPresent=TRUE");

            break;
        }

        //
        // ========================================
        // 修改SystemModuleInformation结果
        // ========================================
        //
        case SystemModuleInformation:
        {
            // 读取模块列表
            RTL_PROCESS_MODULES ModuleInfo;
            MemoryMapperReadMemorySafe(
                SystemInformation,
                &ModuleInfo,
                sizeof(RTL_PROCESS_MODULES),
                ProcessId
            );

            LogInfo("Original module count: %d", ModuleInfo.NumberOfModules);

            //
            // 过滤HyperDbg相关驱动
            //
            UINT32 NewCount = 0;
            for (UINT32 i = 0; i < ModuleInfo.NumberOfModules; i++) {
                RTL_PROCESS_MODULE_INFORMATION * Module = &ModuleInfo.Modules[i];

                // 检查模块名
                if (strstr((PCHAR)Module->FullPathName, "hyperhv.sys") ||
                    strstr((PCHAR)Module->FullPathName, "hyperkd.sys") ||
                    strstr((PCHAR)Module->FullPathName, "hyperlog.sys")) {

                    LogInfo("Filtering out module: %s", Module->FullPathName);

                    // 跳过这个模块（不复制到新位置）
                    continue;
                }

                // 保留其他模块
                if (NewCount != i) {
                    // 移动到新位置
                    RtlCopyMemory(
                        &ModuleInfo.Modules[NewCount],
                        Module,
                        sizeof(RTL_PROCESS_MODULE_INFORMATION)
                    );
                }

                NewCount++;
            }

            // 更新模块数量
            ModuleInfo.NumberOfModules = NewCount;

            LogInfo("Modified module count: %d", NewCount);

            // 写回
            MemoryMapperWriteMemorySafe(
                SystemInformation,
                &ModuleInfo,
                sizeof(RTL_PROCESS_MODULES),
                ProcessId
            );

            break;
        }

        // ... 其他信息类处理
    }
}
```

### 7.4.4 文件和注册表访问重定向

```c
/**
 * @brief 处理NtOpenFile系统调用
 */
VOID TransparentHandleNtOpenFileSyscall(GUEST_REGS * Regs) {

    //
    // ========================================
    // 获取文件路径
    // ========================================
    //
    // NTSTATUS NtOpenFile(
    //     PHANDLE FileHandle,                    // RCX -> R10
    //     ACCESS_MASK DesiredAccess,             // RDX
    //     POBJECT_ATTRIBUTES ObjectAttributes,   // R8
    //     PIO_STATUS_BLOCK IoStatusBlock,        // R9
    //     ...
    // );

    UINT64 ObjectAttributesPtr = Regs->r8;

    // 读取OBJECT_ATTRIBUTES
    PVOID ObjectName = TransparentGetObjectNameFromAttributesVirtualPointer(
        ObjectAttributesPtr
    );

    if (ObjectName != NULL) {

        PWCHAR ObjectNameWchar = (PWCHAR)ObjectName;

        LogInfo("NtOpenFile: %ws", ObjectNameWchar);

        //
        // ========================================
        // 检测方法：打开调试器相关的设备/文件
        // ========================================
        //
        // 例如：\\.\HyperDbg, \\Device\\HyperDbg等
        //

        if (wcsstr(ObjectNameWchar, L"HyperDbg") != NULL ||
            wcsstr(ObjectNameWchar, L"hyperdbg") != NULL) {

            LogWarning("Blocked access to HyperDbg device");

            //
            // 反检测：返回"文件不存在"错误
            //
            // 设置Trap Flag
            SYSCALL_CALLBACK_CONTEXT_PARAMS ContextParams = {0};
            ContextParams.OptionalParam1 = (UINT64)ObjectName;  // 保存名称用于判断

            g_Callbacks.SyscallCallbackSetTrapFlagAfterSyscall(
                Regs,
                HANDLE_TO_UINT32(PsGetCurrentProcessId()),
                HANDLE_TO_UINT32(PsGetCurrentThreadId()),
                Regs->rax,
                &ContextParams
            );

            // 在Trap Flag handler中：
            // Regs->rax = STATUS_OBJECT_NAME_NOT_FOUND;
        }

        PlatformMemFreePool(ObjectName);
    }
}

/**
 * @brief 从OBJECT_ATTRIBUTES获取对象名
 */
PVOID TransparentGetObjectNameFromAttributesVirtualPointer(UINT64 VirtPtr) {
    OBJECT_ATTRIBUTES Buf = {0};

    //
    // ========================================
    // 1. 读取OBJECT_ATTRIBUTES结构
    // ========================================
    //
    if (!g_Callbacks.MemoryMapperReadMemorySafeOnTargetProcess(
            VirtPtr,
            &Buf,
            sizeof(OBJECT_ATTRIBUTES))) {

        LogError("Failed to read OBJECT_ATTRIBUTES");
        return NULL;
    }

    //
    // ========================================
    // 2. 读取UNICODE_STRING
    // ========================================
    //
    UNICODE_STRING NameBuf = {0};

    if (!g_Callbacks.MemoryMapperReadMemorySafeOnTargetProcess(
            (UINT64)Buf.ObjectName,
            &NameBuf,
            sizeof(UNICODE_STRING))) {

        LogError("Failed to read UNICODE_STRING");
        return NULL;
    }

    //
    // ========================================
    // 3. 分配缓冲区并读取字符串
    // ========================================
    //
    PVOID ObjectNameBuf = PlatformMemAllocateZeroedNonPagedPool(
        NameBuf.Length + sizeof(WCHAR)
    );

    if (ObjectNameBuf == NULL) {
        LogError("Failed to allocate object name buffer");
        return NULL;
    }

    if (!g_Callbacks.MemoryMapperReadMemorySafeOnTargetProcess(
            (UINT64)NameBuf.Buffer,
            ObjectNameBuf,
            NameBuf.Length + sizeof(WCHAR))) {

        LogError("Failed to read object name string");
        PlatformMemFreePool(ObjectNameBuf);
        return NULL;
    }

    return ObjectNameBuf;
}

/**
 * @brief 处理NtOpenKey系统调用
 */
VOID TransparentHandleNtOpenKeySyscall(GUEST_REGS * Regs) {

    //
    // NTSTATUS NtOpenKey(
    //     PHANDLE KeyHandle,
    //     ACCESS_MASK DesiredAccess,
    //     POBJECT_ATTRIBUTES ObjectAttributes
    // );
    //

    UINT64 ObjectAttributesPtr = Regs->r8;

    PVOID KeyPath = TransparentGetObjectNameFromAttributesVirtualPointer(
        ObjectAttributesPtr
    );

    if (KeyPath != NULL) {

        PWCHAR KeyPathWchar = (PWCHAR)KeyPath;

        LogInfo("NtOpenKey: %ws", KeyPathWchar);

        //
        // ========================================
        // 检测方法：查询HyperDbg服务注册表键
        // ========================================
        //
        // 例如：HKLM\\SYSTEM\\CurrentControlSet\\Services\\HyperDbg
        //

        if (wcsstr(KeyPathWchar, L"HyperDbg") != NULL ||
            wcsstr(KeyPathWchar, L"hyperhv") != NULL ||
            wcsstr(KeyPathWchar, L"hyperkd") != NULL) {

            LogWarning("Blocked registry access to: %ws", KeyPathWchar);

            //
            // 反检测：返回"键不存在"
            //
            SYSCALL_CALLBACK_CONTEXT_PARAMS ContextParams = {0};
            ContextParams.OptionalParam1 = (UINT64)KeyPath;

            g_Callbacks.SyscallCallbackSetTrapFlagAfterSyscall(
                Regs,
                HANDLE_TO_UINT32(PsGetCurrentProcessId()),
                HANDLE_TO_UINT32(PsGetCurrentThreadId()),
                Regs->rax,
                &ContextParams
            );

            // 在Trap Flag handler中：
            // Regs->rax = STATUS_OBJECT_NAME_NOT_FOUND;
        }

        PlatformMemFreePool(KeyPath);
    }
}
```

---

## 7.5 硬件信息伪造

### 7.5.1 SMBIOS信息伪造

```c
/**
 * @brief 真实厂商字符串列表
 */
static const PWCHAR TRANSPARENT_LEGIT_VENDOR_STRINGS_WCHAR[] = {

    // ASUS
    L"ASUS",
    L"ASUSTeK Computer INC.",
    L"ASUSTek",
    L"ASUSTEK COMPUTER INC.",

    // MSI
    L"Micro-Star International Co., Ltd.",
    L"MSI",
    L"MICRO-STAR INTERNATIONAL CO., LTD",

    // Gigabyte
    L"Gigabyte Technology Co., Ltd.",
    L"GIGABYTE",
    L"Gigabyte Technology",

    // ASRock
    L"ASRock",
    L"ASRock Incorporation",
    L"ASRock Inc.",

    // Dell
    L"Dell Inc.",
    L"Dell",
    L"Dell Computer Corporation",

    // HP
    L"HP",
    L"Hewlett-Packard",
    L"HP Inc.",

    // Lenovo
    L"Lenovo",
    L"LENOVO",
    L"Lenovo Group Limited",

    // 更多厂商...
};

// 全局变量：选中的厂商索引
UINT32 TRANSPARENT_GENUINE_VENDOR_STRING_INDEX;

/**
 * @brief 初始化时随机选择厂商
 */
VOID TransparentInitializeVendorString() {
    // 使用RDTSC生成随机数
    TRANSPARENT_GENUINE_VENDOR_STRING_INDEX = TransparentGetRand() %
        (sizeof(TRANSPARENT_LEGIT_VENDOR_STRINGS_WCHAR) /
         sizeof(TRANSPARENT_LEGIT_VENDOR_STRINGS_WCHAR[0]));

    PWCHAR SelectedVendor = TRANSPARENT_LEGIT_VENDOR_STRINGS_WCHAR[
        TRANSPARENT_GENUINE_VENDOR_STRING_INDEX
    ];

    LogInfo("Selected hardware vendor: %ws", SelectedVendor);
}

/**
 * @brief 处理SMBIOS查询
 */
VOID TransparentHandleSmbiosQuery(GUEST_REGS * Regs) {

    //
    // ========================================
    // 检测方法：查询SMBIOS表
    // ========================================
    //
    // SMBIOS包含系统硬件信息：
    // - 厂商名
    // - 产品名
    // - 序列号
    // - 等等

    // 虚拟机的SMBIOS通常包含：
    // - Manufacturer: "VMware, Inc." / "Microsoft Corporation"
    // - Product: "VMware Virtual Platform" / "Virtual Machine"

    //
    // ========================================
    // 反检测：修改SMBIOS返回值
    // ========================================
    //

    // 获取选中的厂商字符串
    PWCHAR Vendor = TRANSPARENT_LEGIT_VENDOR_STRINGS_WCHAR[
        TRANSPARENT_GENUINE_VENDOR_STRING_INDEX
    ];

    // 修改返回的SMBIOS结构
    // （具体实现取决于查询方式）

    LogInfo("SMBIOS vendor modified to: %ws", Vendor);
}
```

### 7.5.2 ACPI表访问拦截

```c
/**
 * @brief 拦截ACPI表访问
 */
BOOLEAN TransparentHandleAcpiTableAccess(UINT64 TablePhysicalAddress) {

    //
    // ========================================
    // 检测方法：检查ACPI表
    // ========================================
    //
    // 虚拟机的ACPI表可能包含Hypervisor相关信息

    LogInfo("ACPI table access at PA: %llx", TablePhysicalAddress);

    //
    // ========================================
    // 反检测：EPT Hook ACPI表
    // ========================================
    //
    // 1. 设置EPT读Hook
    EptHookReadOnly(TablePhysicalAddress);

    // 2. 在读取时修改返回的数据
    // 或直接提供假的ACPI表

    return TRUE;
}
```

---

## 7.6 VT技术在HyperEvade中的应用

### 7.6.1 应用的VT技术总结

| 反检测技术 | 使用的VT机制 | 实现位置 | 难度 |
|-----------|------------|---------|------|
| **CPUID伪造** | VM-Exit on CPUID | VmxFootprints.c | ⭐⭐ |
| **MSR伪造** | VM-Exit on RDMSR/WRMSR | VmxFootprints.c | ⭐⭐⭐ |
| **时间补偿** | TSC Offsetting (VMCS) | 未实现 | ⭐⭐⭐ |
| **系统调用Hook** | EPT Hook + Trap Flag | SyscallFootprints.c | ⭐⭐⭐⭐ |
| **内存保护** | EPT权限控制 | - | ⭐⭐⭐ |
| **硬件信息伪造** | SYSCALL Hook + EPT | SyscallFootprints.c | ⭐⭐⭐⭐ |
| **Trap Flag模拟** | VM-Exit后检查TF | Transparency.c | ⭐⭐⭐ |

### 7.6.2 详细技术实现

#### 技术1：CPUID拦截和修改

```c
// ============================================
// VT机制：Primary Processor-Based Controls
// ============================================

// CPUID总是触发VM-Exit（无条件）
// 不需要在VMCS中特别设置

// VM-Exit Handler中：
case VMX_EXIT_REASON_EXECUTE_CPUID:
    DispatchEventCpuid(VCpu);
    ↓
    HvHandleCpuid(VCpu);
    ↓
    if (g_TransparentMode) {
        // 调用HyperEvade
        TransparentCheckAndModifyCpuid(Regs, CpuInfo);
    }
    ↓
    // 将修改后的值写入Guest寄存器
    Regs->rax = CpuInfo[0];
    Regs->rbx = CpuInfo[1];
    Regs->rcx = CpuInfo[2];
    Regs->rdx = CpuInfo[3];
```

#### 技术2：系统调用Hook

```c
// ============================================
// VT机制：EPT Hook + Trap Flag
// ============================================

// Step 1: 使用EPT Hook拦截KiSystemCall64
UINT64 KiSystemCall64 = GetKernelSymbol("KiSystemCall64");
EptHookExecuteOnly(VirtualToPhysical(KiSystemCall64));

// Step 2: EPT Violation时分析系统调用
EptHandleExecuteViolation() {
    // 读取RAX（系统调用号）
    UINT64 SyscallNumber = VCpu->Regs->rax;

    if (SyscallNumber == NtQuerySystemInformation) {
        // 这是敏感的系统调用

        // Step 3: 设置Trap Flag
        // Windows x64调用约定：SYSCALL返回后跳转到RCX
        // 我们设置RFLAGS.TF，在返回后触发#DB

        VmFuncSetRflagTrapFlag(TRUE);

        // 保存上下文信息
        SaveTrapFlagContext(SystemInformationClass, OutputBuffer, ...);
    }

    // 正常处理Hook（恢复权限 + MTF）
    // ...
}

// Step 4: Trap Flag触发#DB异常
Guest从系统调用返回
    ↓
执行返回地址的第一条指令
    ↓
触发#DB异常（因为TF=1）
    ↓
VM-Exit (VMX_EXIT_REASON_EXCEPTION_OR_NMI)

// Step 5: #DB Handler中修改返回值
IdtEmulationHandleExceptionAndNmi() {
    if (IsTrapFlagException()) {
        // 读取保存的上下文
        Context = GetTrapFlagContext();

        if (Context->SyscallNumber == NtQuerySystemInformation) {
            // 修改返回缓冲区
            ModifySystemInformationResult(Context);
        }

        // 清除TF
        VmFuncSetRflagTrapFlag(FALSE);

        // 不注入#DB到Guest（我们处理了）
        return;
    }
}
```

#### 技术3：TSC时间补偿

```c
// ============================================
// VT机制：TSC Offsetting
// ============================================

// VMCS字段：TSC_OFFSET
// Guest读取TSC时，实际值 = 真实TSC + TSC_OFFSET

/**
 * @brief 补偿VM-Exit造成的时间差
 */
VOID TransparentCompensateTscOffset(UINT64 VmexitCycles) {
    UINT64 CurrentOffset;

    // 读取当前TSC偏移
    __vmx_vmread(VMCS_CTRL_TSC_OFFSET, &CurrentOffset);

    //
    // ========================================
    // 减去VM-Exit消耗的时间
    // ========================================
    //
    // Guest读取TSC时：
    // 返回值 = 真实TSC + TSC_OFFSET
    //
    // 我们减去VM-Exit时间，让Guest看不到VM-Exit的延迟
    //
    CurrentOffset -= VmexitCycles;

    __vmx_vmwrite(VMCS_CTRL_TSC_OFFSET, CurrentOffset);

    LogInfo("TSC offset adjusted: -%lld cycles", VmexitCycles);
}

// 使用示例：
BOOLEAN VmxVmexitHandler(PGUEST_REGS GuestRegs) {
    UINT64 EntryTsc, ExitTsc;

    if (g_TransparentMode) {
        // 记录进入时间
        EntryTsc = __rdtsc();
    }

    // 处理VM-Exit
    // ...

    if (g_TransparentMode) {
        // 记录退出时间
        ExitTsc = __rdtsc();

        // 补偿时间差
        TransparentCompensateTscOffset(ExitTsc - EntryTsc);
    }

    return FALSE;
}
```

---

## 7.7 实际检测与对抗示例

### 7.7.1 反作弊软件的检测代码

```c
/**
 * @brief 典型的反作弊检测代码
 */
BOOLEAN DetectVirtualization() {
    BOOLEAN IsVirtualized = FALSE;

    //
    // ========================================
    // 检测1：CPUID Hypervisor bit
    // ========================================
    //
    int cpuInfo[4];
    __cpuid(cpuInfo, 1);

    if (cpuInfo[2] & (1 << 31)) {
        printf("Detected: Hypervisor Present bit is set!\n");
        IsVirtualized = TRUE;
    }

    //
    // ========================================
    // 检测2：CPUID Hypervisor厂商
    // ========================================
    //
    __cpuid(cpuInfo, 0x40000000);

    if (cpuInfo[0] > 0x40000000) {
        // 有Hypervisor CPUID叶子
        char vendor[13] = {0};
        *(int*)&vendor[0] = cpuInfo[1];
        *(int*)&vendor[4] = cpuInfo[2];
        *(int*)&vendor[8] = cpuInfo[3];

        printf("Detected: Hypervisor vendor: %s\n", vendor);
        IsVirtualized = TRUE;
    }

    //
    // ========================================
    // 检测3：时间测量（Timing Attack）
    // ========================================
    //
    UINT64 t1, t2;

    t1 = __rdtsc();
    __cpuid(cpuInfo, 0);  // 触发VM-Exit
    t2 = __rdtsc();

    if (t2 - t1 > 1000) {
        // VM-Exit通常需要1000+ cycles
        printf("Detected: CPUID took too long (%lld cycles)!\n", t2 - t1);
        IsVirtualized = TRUE;
    }

    //
    // ========================================
    // 检测4：系统调用 - 查询调试器
    // ========================================
    //
    SYSTEM_KERNEL_DEBUGGER_INFORMATION KernelDebugInfo;
    ULONG ReturnLength;

    NTSTATUS Status = NtQuerySystemInformation(
        SystemKernelDebuggerInformation,
        &KernelDebugInfo,
        sizeof(KernelDebugInfo),
        &ReturnLength
    );

    if (NT_SUCCESS(Status)) {
        if (KernelDebugInfo.KernelDebuggerEnabled) {
            printf("Detected: Kernel debugger is enabled!\n");
            IsVirtualized = TRUE;
        }
    }

    //
    // ========================================
    // 检测5：系统调用 - 枚举驱动
    // ========================================
    //
    PVOID ModuleInfo = malloc(1024 * 1024);  // 1MB buffer

    Status = NtQuerySystemInformation(
        SystemModuleInformation,
        ModuleInfo,
        1024 * 1024,
        &ReturnLength
    );

    if (NT_SUCCESS(Status)) {
        RTL_PROCESS_MODULES * Modules = (RTL_PROCESS_MODULES *)ModuleInfo;

        for (UINT32 i = 0; i < Modules->NumberOfModules; i++) {
            char * ModuleName = (char *)Modules->Modules[i].FullPathName;

            if (strstr(ModuleName, "hyper") ||
                strstr(ModuleName, "vmware") ||
                strstr(ModuleName, "vbox")) {

                printf("Detected: Hypervisor driver: %s\n", ModuleName);
                IsVirtualized = TRUE;
            }
        }
    }

    free(ModuleInfo);

    //
    // ========================================
    // 检测6：打开调试器设备
    // ========================================
    //
    HANDLE hDevice;
    OBJECT_ATTRIBUTES ObjAttr;
    UNICODE_STRING DeviceName;
    IO_STATUS_BLOCK IoStatus;

    RtlInitUnicodeString(&DeviceName, L"\\Device\\HyperDbg");
    InitializeObjectAttributes(&ObjAttr, &DeviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = NtOpenFile(
        &hDevice,
        FILE_READ_DATA,
        &ObjAttr,
        &IoStatus,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        0
    );

    if (NT_SUCCESS(Status)) {
        printf("Detected: HyperDbg device exists!\n");
        NtClose(hDevice);
        IsVirtualized = TRUE;
    }

    return IsVirtualized;
}
```

### 7.7.2 HyperEvade的完整对抗

```
当Guest运行DetectVirtualization()时：

┌────────────────────────────────────────────────────────────┐
│  检测1：CPUID Hypervisor bit                               │
└────────────────────────────────────────────────────────────┘

Guest: __cpuid(cpuInfo, 1);
    ↓
VM-Exit (VMX_EXIT_REASON_EXECUTE_CPUID)
    ↓
HvHandleCpuid()
    ↓
g_TransparentMode == TRUE
    ↓
TransparentCheckAndModifyCpuid()
    ↓
CpuInfo[2] &= ~HYPERV_HYPERVISOR_PRESENT_BIT;  // 清除bit 31
    ↓
Regs->rcx = CpuInfo[2];  // 返回修改后的值
    ↓
VMRESUME
    ↓
Guest: if (cpuInfo[2] & (1 << 31))  // 检查bit 31
         ↓
       FALSE（bit 31 = 0）
         ↓
       ✅ 检测失败

┌────────────────────────────────────────────────────────────┐
│  检测2：CPUID Hypervisor厂商                               │
└────────────────────────────────────────────────────────────┘

Guest: __cpuid(cpuInfo, 0x40000000);
    ↓
VM-Exit
    ↓
TransparentCheckAndModifyCpuid()
    ↓
CpuInfo[0] = CpuInfo[1] = CpuInfo[2] = CpuInfo[3] = 0x40000000;
    ↓
Guest收到全0
    ↓
if (cpuInfo[0] > 0x40000000)  // 检查是否有更多叶子
         ↓
       FALSE（cpuInfo[0] = 0x40000000）
         ↓
       ✅ 检测失败

┌────────────────────────────────────────────────────────────┐
│  检测3：时间测量                                           │
└────────────────────────────────────────────────────────────┘

Guest: t1 = __rdtsc();
       __cpuid(cpuInfo, 0);
       t2 = __rdtsc();
    ↓
VM-Exit (CPUID)
    ↓
处理CPUID（约2000 cycles）
    ↓
如果没有TSC补偿：
    t2 - t1 = 真实时间差 ≈ 2000 cycles
    ↓
    if (t2 - t1 > 1000)
    ❌ 检测成功

如果有TSC补偿：
    VmexitCycles = 2000
    TSC_OFFSET -= 2000
    ↓
    Guest读取TSC：真实TSC + TSC_OFFSET
    ↓
    t2 - t1 ≈ 正常的CPUID时间（约100 cycles）
    ↓
    if (t2 - t1 > 1000)
    ✅ 检测失败

┌────────────────────────────────────────────────────────────┐
│  检测4：查询调试器状态                                     │
└────────────────────────────────────────────────────────────┘

Guest: NtQuerySystemInformation(SystemKernelDebuggerInformation, ...)
    ↓
执行SYSCALL指令
    ↓
跳转到KiSystemCall64
    ↓
EPT Violation（KiSystemCall64被Hook）
    ↓
TransparentHandleSystemCallHook()
    ↓
检测到：SyscallNumber = NtQuerySystemInformation
    ↓
检测到：SystemInformationClass = SystemKernelDebuggerInformation
    ↓
设置Trap Flag：
    VmFuncSetRflagTrapFlag(TRUE);
    保存上下文：OptionalParam1 = SystemKernelDebuggerInformation
                OptionalParam2 = OutputBuffer
    ↓
继续执行系统调用
    ↓
系统调用执行完成，返回用户态
    ↓
执行返回地址的第一条指令
    ↓
触发#DB异常（TF=1）
    ↓
VM-Exit
    ↓
TransparentCallbackHandleAfterSyscall()
    ↓
读取输出缓冲区：
    KernelDebuggerEnabled = TRUE (原始值)
    ↓
修改：
    KernelDebuggerEnabled = FALSE
    KernelDebuggerNotPresent = TRUE
    ↓
写回缓冲区
    ↓
清除TF，不注入#DB到Guest
    ↓
VMRESUME
    ↓
Guest: if (KernelDebugInfo.KernelDebuggerEnabled)
         ↓
       FALSE
         ↓
       ✅ 检测失败

┌────────────────────────────────────────────────────────────┐
│  检测5：枚举驱动列表                                       │
└────────────────────────────────────────────────────────────┘

Guest: NtQuerySystemInformation(SystemModuleInformation, ...)
    ↓
（同上，通过SYSCALL Hook + Trap Flag）
    ↓
TransparentCallbackHandleAfterSyscall()
    ↓
读取模块列表：
    [0] ntoskrnl.exe
    [1] hal.dll
    [2] hyperhv.sys      ← 调试器驱动
    [3] hyperkd.sys      ← 调试器驱动
    [4] win32k.sys
    ...
    ↓
过滤：
    for each module:
        if (strstr(module.Name, "hyper"))
            跳过（不复制到新位置）
    ↓
修改后的列表：
    [0] ntoskrnl.exe
    [1] hal.dll
    [2] win32k.sys       ← hyperhv.sys和hyperkd.sys被移除
    ...
    NumberOfModules = 原数量 - 2
    ↓
写回缓冲区
    ↓
Guest遍历驱动列表：
    找不到hyperhv.sys和hyperkd.sys
    ↓
    ✅ 检测失败

┌────────────────────────────────────────────────────────────┐
│  检测6：打开调试器设备                                     │
└────────────────────────────────────────────────────────────┘

Guest: NtOpenFile(..., "\\Device\\HyperDbg", ...)
    ↓
SYSCALL
    ↓
EPT Violation
    ↓
TransparentHandleNtOpenFileSyscall()
    ↓
读取文件路径：ObjectName = "\\Device\\HyperDbg"
    ↓
检测到包含"HyperDbg"
    ↓
设置Trap Flag
    ↓
系统调用执行（实际会成功打开）
    ↓
Trap Flag触发
    ↓
TransparentCallbackHandleAfterSyscall()
    ↓
修改返回值：
    Regs->rax = STATUS_OBJECT_NAME_NOT_FOUND;  // 文件不存在
    ↓
Guest: if (NT_SUCCESS(Status))
         ↓
       FALSE（Status = STATUS_OBJECT_NAME_NOT_FOUND）
         ↓
       ✅ 检测失败

// ============================================
// 最终结果
// ============================================
DetectVirtualization() 返回 FALSE
    ↓
反作弊软件认为：不在虚拟化环境中
    ↓
游戏/程序正常运行
    ↓
✅ HyperEvade成功绕过所有检测
```

---

## 7.8 关键设计考虑

### 7.8.1 选择性应用透明化

```c
/**
 * @brief 为什么需要选择性应用？
 */

// ============================================
// 问题：对所有进程透明化
// ============================================
if (全局透明化) {
    对所有进程应用透明化
        ↓
    问题1：系统进程也被透明化
        ↓
        系统进程可能依赖真实的系统信息
        ↓
        ❌ 可能导致系统不稳定、崩溃

    问题2：性能开销
        ↓
        所有系统调用都需要Hook和修改
        ↓
        ❌ 系统性能显著下降

    问题3：调试困难
        ↓
        正常的工具也看不到调试器
        ↓
        ❌ 无法使用其他工具辅助调试
}

// ============================================
// 解决方案：选择性透明化
// ============================================
if (当前进程在透明列表中) {
    应用透明化
} else {
    不应用透明化
}

// 好处：
// ✅ 系统进程不受影响
// ✅ 性能开销小
// ✅ 灵活控制
// ✅ 可以同时运行调试工具

/**
 * @brief 实现
 */
VOID TransparentHandleSystemCallHook(GUEST_REGS * Regs) {

    // 1. 检查全局透明模式
    if (!g_TransparentMode) {
        return;
    }

    // 2. 获取当前进程名
    PCHAR ProcessName = GetCurrentProcessName();

    // 3. 检查是否是系统进程
    for (i = 0; i < TRANSPARENT_WIN_PROCESS_IGNORE_COUNT; i++) {
        if (strstr(ProcessName, TRANSPARENT_WIN_PROCESS_IGNORE[i])) {
            // 系统进程，不处理
            return;
        }
    }

    // 4. 检查是否在透明列表中
    if (!TransparentCheckIfProcessIsInList()) {
        // 不在列表中，不处理
        return;
    }

    // 5. 应用透明化
    // ...
}
```

### 7.8.2 延迟修改策略

```c
/**
 * @brief 为什么使用Trap Flag延迟修改？
 */

// ============================================
// 问题：在系统调用中直接修改
// ============================================
TransparentHandleNtQuerySystemInformation() {
    // 系统调用还未执行
    // 输出缓冲区可能未分配或无效

    if (直接修改缓冲区) {
        MemoryMapperWriteMemorySafe(OutputBuffer, ...);
        ↓
        ❌ 缓冲区可能是用户态地址
        ❌ 在内核态可能无法访问
        ❌ 可能导致页面错误
        ❌ 时机不对，系统调用会覆盖修改
    }
}

// ============================================
// 解决方案：Trap Flag延迟修改
// ============================================
TransparentHandleNtQuerySystemInformation() {
    // 1. 不立即修改，而是设置Trap Flag
    VmFuncSetRflagTrapFlag(TRUE);

    // 2. 保存上下文信息
    SaveTrapFlagContext(OutputBuffer, ...);

    // 3. 让系统调用正常执行
    // ...
}

系统调用执行完成
    ↓
返回到用户态
    ↓
执行返回地址的第一条指令
    ↓
Trap Flag触发#DB
    ↓
VM-Exit
    ↓
TransparentCallbackHandleAfterSyscall() {
    // 此时系统调用已执行完成
    // 输出缓冲区已填充有效数据
    // 现在可以安全修改

    MemoryMapperReadMemorySafe(OutputBuffer, &Data, ...);
    ModifyData(&Data);
    MemoryMapperWriteMemorySafe(OutputBuffer, &Data, ...);

    ✅ 时机正确
    ✅ 数据有效
    ✅ 不影响系统调用执行
}
```

### 7.8.3 随机化策略

```c
/**
 * @brief 为什么需要随机化？
 */

// ============================================
// 问题：使用固定的伪造值
// ============================================
TransparentHandleSmbiosQuery() {
    // 总是返回相同的厂商
    return "Dell Inc.";

    // 问题：
    // ❌ 检测软件可以识别固定模式
    // ❌ "所有运行在Dell上的用户都有相同的序列号？"
    // ❌ 容易被检测
}

// ============================================
// 解决方案：随机化
// ============================================
TransparentInitialize() {
    // 随机选择厂商
    Index = TransparentGetRand() % VendorCount;
    SelectedVendor = VendorList[Index];

    // 每次运行选择不同的厂商
    // ✅ 难以识别固定模式
    // ✅ 更接近真实环境
}

/**
 * @brief 生成随机数
 */
UINT32 TransparentGetRand() {
    // 使用TSC作为随机源
    // TSC（Time Stamp Counter）每个CPU周期递增
    // 在不同时刻调用会得到不同的值

    UINT64 Tsc = __rdtsc();

    // 取低16位
    return (UINT32)(Tsc & 0xFFFF);
}

// 使用示例：
// 第一次启动：TransparentGetRand() = 0x1234 -> 选择"ASUS"
// 第二次启动：TransparentGetRand() = 0x5678 -> 选择"Dell Inc."
// 第三次启动：TransparentGetRand() = 0x9ABC -> 选择"Lenovo"
```

---

## 7.9 HyperEvade的限制

### 7.9.1 已知的绕过方法

```c
/**
 * @brief HyperEvade无法防御的检测技术
 */

// ============================================
// 限制1：基于副作用的检测
// ============================================
BOOLEAN DetectByPagingStructure() {
    // 检查CR3（页表基地址）的高位
    UINT64 Cr3 = __readcr3();

    // EPT环境中，Guest使用的CR3可能有特殊模式
    // 或者检查页表结构的变化

    // HyperEvade难以完全隐藏这些副作用
}

// ============================================
// 限制2：基于缓存行为的检测
// ============================================
BOOLEAN DetectByCacheTiming() {
    // EPT违规会影响缓存行为
    // 通过精确的缓存时序分析可能检测到

    // HyperEvade难以完全消除缓存差异
}

// ============================================
// 限制3：多核时序检测
// ============================================
BOOLEAN DetectByMulticoreTiming() {
    // 在多核上同时执行敏感操作
    // 检测不同核心的执行时序差异

    // HyperEvade难以完全同步所有核心的时间
}

// ============================================
// 限制4：硬件特性检测
// ============================================
BOOLEAN DetectByHardwareFeatures() {
    // 检查某些硬件特性的微小差异
    // 例如：
    // - 特定的未文档化CPUID叶子
    // - 特定的MSR寄存器
    // - 硬件性能计数器行为

    // HyperEvade无法模拟所有硬件细节
}
```

### 7.9.2 性能影响

```c
/**
 * @brief HyperEvade的性能开销
 */

// ============================================
// 开销来源
// ============================================

1. CPUID拦截：
   每次CPUID -> VM-Exit -> 处理 -> VMRESUME
   开销：约2000-3000 cycles
   频率：中等（程序初始化时较多）

2. 系统调用Hook：
   每次敏感系统调用 -> EPT Violation -> 设置TF -> #DB -> 修改结果
   开销：约5000-10000 cycles
   频率：取决于程序行为

3. Trap Flag处理：
   每次TF触发 -> VM-Exit -> 修改数据 -> VMRESUME
   开销：约2000-3000 cycles
   频率：等于敏感系统调用频率

// ============================================
// 总体影响
// ============================================

对于普通程序：
    影响：< 5%
    原因：敏感操作较少

对于反调试程序：
    影响：10-30%
    原因：频繁检测，大量敏感操作

对于系统进程：
    影响：0%
    原因：HyperEvade不处理系统进程

// ============================================
// 优化建议
// ============================================

1. 选择性启用：
   只对需要的进程启用透明化

2. 缓存结果：
   某些查询结果可以缓存（如模块列表）

3. 快速路径：
   对不敏感的系统调用快速通过
```

---

## 7.10 与其他反检测技术对比

### 7.10.1 技术对比

| 技术 | 实现层次 | 隐藏性 | 性能 | 复杂度 |
|------|---------|--------|------|--------|
| **HyperEvade (VT-x)** | Hypervisor | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **API Hook** | 内核驱动 | ⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐ |
| **SSDT Hook** | 内核驱动 | ⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ |
| **Filter Driver** | 内核驱动 | ⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐ |
| **DLL Injection** | 用户态 | ⭐ | ⭐⭐⭐⭐⭐ | ⭐ |

### 7.10.2 优势分析

```c
/**
 * @brief HyperEvade的独特优势
 */

1. 硬件级别隐藏：
   - 运行在Ring -1（VMX root）
   - Guest无法直接访问或检测
   - 绕过所有内核保护（PatchGuard等）

2. 完全透明：
   - 不修改Guest内存（代码、数据）
   - 不修改Guest内核结构
   - 只在访问时动态修改结果

3. 灵活性：
   - 可以Hook任何指令（CPUID、MSR等）
   - 可以Hook任何系统调用
   - 可以实现任何自定义逻辑

4. 绕过传统防御：
   - 绕过PatchGuard（不修改内核）
   - 绕过代码签名（不修改代码）
   - 绕过完整性检查

// ============================================
// 对比：传统技术的限制
// ============================================

API Hook（用户态）：
    ❌ 容易被检测（IAT检查）
    ❌ 容易被绕过（直接SYSCALL）
    ❌ 无法Hook内核

SSDT Hook（内核态）：
    ❌ PatchGuard会检测并导致蓝屏
    ❌ 容易被检测（SSDT表检查）
    ❌ Win10 1607后基本不可用

HyperEvade（Hypervisor）：
    ✅ PatchGuard无法检测（在更底层）
    ✅ 难以检测（硬件级别）
    ✅ 所有Windows版本可用
```

---

## 7.11 实际应用场景

### 7.11.1 场景1：游戏反作弊绕过

```c
/**
 * @brief 使用HyperEvade调试受保护的游戏
 */

// ============================================
// 问题
// ============================================
现代游戏使用强大的反作弊系统：
    - BattlEye
    - EasyAntiCheat
    - Vanguard (Riot Games)
    - 等等

这些系统会检测：
    ✓ 调试器
    ✓ 虚拟化环境
    ✓ 驱动注入
    ✓ 内存修改
    ✓ 等等

传统调试器：
    ❌ 立即被检测
    ❌ 游戏拒绝运行或封号

// ============================================
// HyperEvade解决方案
// ============================================

1. 启动HyperDbg（启用透明模式）:
   hyperkd.sys加载 -> 启动虚拟化 -> 启用HyperEvade

2. 添加游戏进程到透明列表:
   透明化 game.exe

3. 启动游戏:
   游戏运行在虚拟化环境中
   但所有检测都失败
   ✅ 游戏正常运行

4. 进行调试/分析:
   - 设置断点（使用EPT Hook，完全隐藏）
   - 读取内存
   - 修改变量
   - 跟踪函数调用
   ✅ 所有操作对游戏透明

5. 反作弊系统执行检测:
   检测1：CPUID -> ✅ 绕过（bit清除）
   检测2：查询调试器 -> ✅ 绕过（返回FALSE）
   检测3：枚举驱动 -> ✅ 绕过（过滤列表）
   检测4：时间测量 -> ✅ 绕过（TSC补偿）
   检测5：打开调试器设备 -> ✅ 绕过（返回不存在）

   结果：反作弊系统认为环境正常
   ✅ 不封号，不踢出

// ============================================
// 注意事项
// ============================================
// 此示例仅用于学习和研究目的
// 绕过反作弊系统可能违反服务条款
// 仅在测试环境或自己的游戏中使用
```

### 7.11.2 场景2：恶意软件分析

```c
/**
 * @brief 使用HyperEvade分析反分析的恶意软件
 */

// ============================================
// 问题
// ============================================
现代恶意软件具有反分析能力：
    - 检测调试器
    - 检测虚拟机
    - 检测沙箱
    - 如果检测到，拒绝运行或执行假行为

传统分析方法：
    ❌ 恶意软件检测到分析环境
    ❌ 不执行恶意行为
    ❌ 无法分析真实功能

// ============================================
// HyperEvade解决方案
// ============================================

1. 准备隔离环境:
   使用HyperDbg + HyperEvade
   启用完全透明模式

2. 运行恶意软件:
   malware.exe运行
   执行各种检测
   ✅ 所有检测失败
   ✅ 恶意软件认为环境"安全"

3. 恶意软件执行真实行为:
   - 下载C&C服务器地址
   - 解密payload
   - 执行键盘记录
   - 窃取凭证
   - 等等

4. HyperDbg记录所有行为:
   - API调用（通过EPT Hook）
   - 网络连接
   - 文件操作
   - 注册表修改
   ✅ 获取完整的恶意行为日志

5. 生成分析报告:
   - 恶意软件的真实功能
   - C&C服务器地址
   - 加密算法
   - 传播机制
   ✅ 完整的威胁情报
```

---

## 7.12 HyperEvade配置和使用

### 7.12.1 启用透明模式

```c
/**
 * @brief 用户态命令启用透明模式
 */

// 从HyperDbg CLI：
hyperdbg> !hide enable

// 内部流程：
1. 构建DEBUGGER_HIDE_AND_TRANSPARENT_DEBUGGER_MODE结构
2. 通过IOCTL发送到驱动
3. hyperkd接收IOCTL
4. 调用TransparentHideDebugger()
5. 启用g_TransparentMode
6. 返回成功

// ============================================
// 添加进程到透明列表
// ============================================

// 按进程名：
hyperdbg> !hide name game.exe

// 按进程ID：
hyperdbg> !hide pid 1234

// 内部调用：
TransparentAddNameOrProcessIdToTheList(Measurements);
```

### 7.12.2 禁用透明模式

```c
/**
 * @brief 禁用透明模式
 */

// 从HyperDbg CLI：
hyperdbg> !hide disable

// 内部流程：
1. 调用TransparentUnhideDebugger()
2. 设置g_TransparentMode = FALSE
3. 清空透明进程列表（可选）

// 效果：
// - CPUID返回真实的Hypervisor信息
// - 系统调用不再修改结果
// - 调试器变得可见
```

---

## 7.13 未来改进方向

### 7.13.1 可能的增强

```c
/**
 * @brief HyperEvade的潜在改进
 */

1. TSC偏移自动调整：
   // 当前：未实现自动TSC补偿
   // 改进：每次VM-Exit后自动调整TSC_OFFSET
   VOID AutoAdjustTscOffset() {
       UINT64 VmexitCycles = CalculateVmexitCycles();
       AdjustTscOffset(-VmexitCycles);
   }

2. 更多系统调用支持：
   // 当前：只支持部分系统调用
   // 改进：支持更多敏感系统调用
   - NtQueryInformationThread
   - NtQueryDirectoryObject
   - NtQuerySystemTime
   // ...

3. 硬件信息数据库：
   // 当前：使用固定的厂商列表
   // 改进：从真实硬件采集信息，动态伪造
   HARDWARE_INFO RealHardwareProfile = {
       .Vendor = "Dell Inc.",
       .Model = "Latitude E7470",
       .SerialNumber = "XYZ123456",
       .BiosVersion = "1.2.3",
       // ...
   };

4. 行为学习：
   // 当前：静态规则
   // 改进：学习程序的检测模式，动态调整
   if (DetectAntiDebugPattern()) {
       AdaptTransparentStrategy();
   }

5. 嵌套虚拟化支持：
   // 当前：在某些嵌套环境中可能有问题
   // 改进：完全支持Hyper-V、VMware等嵌套环境
```

---

## 本章小结

本章深入分析了HyperEvade反检测模块：

1. **模块架构**
   - Transparency.c：透明模式控制
   - VmxFootprints.c：VMX痕迹隐藏
   - SyscallFootprints.c：系统调用隐藏

2. **反检测技术**
   - CPUID伪造（清除Hypervisor bit）
   - MSR访问控制（注入异常）
   - 系统调用Hook和结果修改
   - 硬件信息伪造（SMBIOS、厂商等）
   - Trap Flag处理（避免检测VM-Exit）

3. **应用的VT技术**
   - VM-Exit on CPUID/MSR
   - EPT Hook系统调用入口
   - Trap Flag延迟修改
   - TSC Offsetting时间补偿

4. **设计要点**
   - 选择性透明化（只对特定进程）
   - 延迟修改策略（Trap Flag）
   - 随机化（避免固定模式）
   - 系统进程保护（避免不稳定）

5. **实际对抗**
   - 反作弊系统绕过
   - 恶意软件分析
   - 保护敏感调试会话

6. **限制和改进**
   - 无法防御所有检测（副作用、缓存等）
   - 性能开销（10-30%对敏感程序）
   - 未来改进方向

HyperEvade展示了如何利用VT技术实现高级的反检测功能，是防御性安全技术的优秀示例。

---

[<< 上一章：EPT Hook技术深入](./第六章-EPT-Hook技术深入.md) | [下一章：实践与总结 >>](./第八章-实践与总结.md)
