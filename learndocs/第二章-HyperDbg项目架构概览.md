# 第二章：HyperDbg项目架构概览

## 2.1 项目整体结构

HyperDbg是一个复杂的多模块项目，采用了清晰的模块化设计。

### 2.1.1 核心模块组成

```
HyperDbg/
├── hyperhv/              - Hypervisor核心（VMM实现）
│   ├── code/
│   │   ├── vmm/          - 虚拟机监视器
│   │   │   ├── vmx/      - VMX操作和VM-Exit处理
│   │   │   └── ept/      - EPT页表管理
│   │   ├── assembly/     - 汇编级别的关键代码
│   │   ├── interface/    - 导出接口和回调
│   │   └── broadcast/    - 多核广播机制
│   └── header/
│       ├── vmm/          - VMM头文件
│       ├── common/       - 公共定义
│       └── interface/    - 接口定义
│
├── hyperkd/              - 内核调试器逻辑
│   ├── code/
│   │   ├── debugger/     - 调试器核心功能
│   │   │   ├── kernel-level/      - 内核级调试
│   │   │   ├── communication/     - 通信层
│   │   │   ├── commands/          - 命令处理
│   │   │   ├── objects/           - 对象管理
│   │   │   └── broadcast/         - 调试器广播
│   │   └── driver/       - 驱动框架
│   └── header/
│
├── hyperlog/             - 日志和消息追踪
│   ├── code/             - 日志缓冲区管理
│   └── header/
│
├── hyperevade/           - 反检测模块（透明化）
│   ├── code/
│   │   ├── Transparency.c      - 透明模式控制
│   │   ├── VmxFootprints.c     - VMX痕迹隐藏
│   │   └── SyscallFootprints.c - 系统调用隐藏
│   └── header/
│
├── script-eval/          - 脚本引擎
│   ├── code/             - 表达式求值
│   └── header/
│
├── kdserial/             - 串口驱动
│
└── include/              - 公共头文件和SDK
    └── SDK/
        ├── imports/      - 导入函数定义
        ├── modules/      - 模块接口
        └── headers/      - 共享头文件
```

### 2.1.2 模块依赖关系

```
┌─────────────────────────────────────────────────────────┐
│                     hyperkd (调试器)                     │
│  - 调试命令处理                                          │
│  - 断点管理                                              │
│  - 进程/线程跟踪                                         │
│  - 串口通信                                              │
└──────────────────────┬──────────────────────────────────┘
                       │ 通过回调接口调用
                       ↓
┌─────────────────────────────────────────────────────────┐
│                    hyperhv (VMM核心)                     │
│  - VMX操作                                               │
│  - VM-Exit处理                                           │
│  - EPT管理                                               │
│  - 事件分发                                              │
└──────────────────────┬──────────────────────────────────┘
                       │ 使用
                       ↓
┌─────────────────────────────────────────────────────────┐
│                  hyperlog (日志系统)                     │
│  - 消息缓冲区                                            │
│  - 日志格式化                                            │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                 hyperevade (反检测)                      │
│  - 透明模式                                              │
│  - CPUID/MSR伪造                                         │
│  - 系统调用Hook                                          │
└─────────────────────────────────────────────────────────┘
```

---

## 2.2 核心文件详细索引

### 2.2.1 VMX核心文件

| 文件路径 | 主要功能 | 关键函数 |
|---------|---------|---------|
| `hyperhv/code/vmm/vmx/Vmx.c` | VMX指令包装 | VmxVmread*, VmxVmwrite*, VmxCheckVmxSupport |
| `hyperhv/code/vmm/vmx/Hv.c` | 高级虚拟化例程 | HvInitVmm, HvAdjustControls, HvSetGuestSelector |
| `hyperhv/code/vmm/vmx/Vmexit.c` | VM-Exit分发处理 | VmxVmexitHandler |
| `hyperhv/code/vmm/vmx/VmxRegions.c` | VMCS/VMXON区域管理 | VmxAllocateVmxonRegion, VmxAllocateVmcsRegion |
| `hyperhv/code/vmm/vmx/ManageRegs.c` | VMCS寄存器管理 | GetGuestCs, SetGuestCs 等 |
| `hyperhv/code/vmm/vmx/Vmcall.c` | VMCALL处理 | VmxHandleVmcall |
| `hyperhv/code/vmm/vmx/CrossVmexits.c` | 跨VM-Exit处理 | VmxHandleXsetbv, VmxHandleTripleFaults |
| `hyperhv/code/vmm/vmx/Mtf.c` | Monitor Trap Flag | MtfHandleVmexit |
| `hyperhv/code/vmm/vmx/ProtectedHv.c` | Hypervisor保护 | 异常位图、中断控制 |
| `hyperhv/code/vmm/vmx/MsrHandlers.c` | MSR处理 | MsrHandleRdmsrVmexit, MsrHandleWrmsrVmexit |
| `hyperhv/code/vmm/vmx/IoHandler.c` | I/O指令处理 | IoHandleIoVmExits |
| `hyperhv/code/vmm/vmx/Events.c` | 事件注入 | EventInjectInterruption |
| `hyperhv/code/vmm/vmx/IdtEmulation.c` | IDT仿真 | IdtEmulationHandleExceptionAndNmi |
| `hyperhv/code/vmm/vmx/VmxBroadcast.c` | VMX广播机制 | VmxBroadcastNmi |

### 2.2.2 EPT相关文件

| 文件路径 | 主要功能 | 关键函数 |
|---------|---------|---------|
| `hyperhv/code/vmm/ept/Ept.c` | EPT页表管理 | EptInitialize, EptCheckFeatures, EptGetMemoryType |
| `hyperhv/code/vmm/ept/Invept.c` | INVEPT指令 | EptInvept, EptInveptSingleContext |
| `hyperhv/code/vmm/ept/Vpid.c` | VPID管理 | VpidInvvpid系列函数 |
| `hyperhv/header/vmm/ept/Ept.h` | EPT结构定义 | EPT_PML*_ENTRY定义 |

### 2.2.3 汇编代码文件

| 文件路径 | 主要功能 | 导出函数 |
|---------|---------|---------|
| `hyperhv/code/assembly/AsmVmxOperation.asm` | VMX基础操作 | AsmEnableVmxOperation, AsmVmxVmcall |
| `hyperhv/code/assembly/AsmVmexitHandler.asm` | VM-Exit处理 | AsmVmexitHandler |
| `hyperhv/code/assembly/AsmVmxContextState.asm` | CPU状态保存恢复 | AsmVmxSaveState, AsmVmxRestoreState |
| `hyperhv/code/assembly/AsmEpt.asm` | EPT相关汇编 | AsmInvept, AsmInvvpid |
| `hyperhv/code/assembly/AsmCommon.asm` | 通用汇编工具 | 段寄存器读取等 |
| `hyperhv/code/assembly/AsmSegmentRegs.asm` | 段寄存器操作 | AsmGetCs, AsmGetGdtBase等 |

### 2.2.4 接口和回调文件

| 文件路径 | 主要功能 | 关键内容 |
|---------|---------|---------|
| `hyperhv/code/interface/Export.c` | 导出函数 | VmFuncInitVmm, VmFunc系列导出 |
| `hyperhv/code/interface/Callback.c` | 回调Wrapper | VmmCallbackTriggerEvents等 |
| `hyperhv/code/interface/Dispatch.c` | 事件分发 | DispatchEventCpuid, DispatchEventRdmsr等 |
| `hyperhv/code/interface/HyperEvade.c` | HyperEvade接口 | HyperevadeInitialize |

### 2.2.5 调试器核心文件

| 文件路径 | 主要功能 | 关键函数 |
|---------|---------|---------|
| `hyperkd/code/debugger/kernel-level/Kd.c` | 内核调试器核心 | KdHandleBreakpointAndDebugBreakpoints |
| `hyperkd/code/debugger/communication/SerialConnection.c` | 串口通信 | SerialConnectionSendBuffer, SerialConnectionRecvBuffer |
| `hyperkd/code/debugger/commands/DebuggerCommands.c` | 命令处理 | 各种调试命令实现 |
| `hyperkd/code/debugger/objects/Process.c` | 进程对象 | 进程切换、跟踪 |
| `hyperkd/code/debugger/objects/Thread.c` | 线程对象 | 线程切换、跟踪 |
| `hyperkd/code/driver/Loader.c` | 驱动加载器 | LoaderInitVmmAndDebugger |

### 2.2.6 状态和配置文件

| 文件路径 | 主要内容 |
|---------|---------|
| `hyperhv/header/common/State.h` | VIRTUAL_MACHINE_STATE, VMM_EPT_PAGE_TABLE等核心结构 |
| `include/config/Configuration.h` | 编译时配置选项 |
| `include/SDK/modules/VMM.h` | VMM_CALLBACKS回调结构定义 |

---

## 2.3 关键数据结构

### 2.3.1 虚拟机状态结构

**文件位置**：`hyperdbg/hyperhv/header/common/State.h`

```c
/**
 * @brief 虚拟机状态结构 - 每个CPU核心一个实例
 */
typedef struct _VIRTUAL_MACHINE_STATE {
    //
    // ========== VMX执行状态 ==========
    //
    BOOLEAN IsOnVmxRootMode;              // 是否在VMX根模式
    BOOLEAN HasLaunched;                  // 该核心是否已虚拟化
    BOOLEAN IncrementRip;                 // 是否增加RIP
    BOOLEAN IgnoreMtfUnset;               // 是否忽略MTF取消设置
    BOOLEAN IgnoreOneMtf;                 // 是否忽略一次MTF
    BOOLEAN RegisterBreakOnMtf;           // MTF时是否触发断点

    //
    // ========== 寄存器状态 ==========
    //
    PGUEST_REGS Regs;                     // 通用寄存器指针
    PGUEST_XMM_REGS XmmRegs;              // XMM寄存器指针

    //
    // ========== 核心标识 ==========
    //
    UINT32 CoreId;                        // 核心唯一标识符

    //
    // ========== VM-Exit信息 ==========
    //
    UINT32 ExitReason;                    // 退出原因
    UINT64 ExitQualification;             // 退出限定符
    UINT64 LastVmexitRip;                 // 最后VM-Exit的RIP

    //
    // ========== VMX区域地址 ==========
    //
    UINT64 VmxonRegionPhysicalAddress;    // VMXON区域物理地址
    UINT64 VmxonRegionVirtualAddress;     // VMXON区域虚拟地址

    UINT64 VmcsRegionPhysicalAddress;     // VMCS区域物理地址
    UINT64 VmcsRegionVirtualAddress;      // VMCS区域虚拟地址

    //
    // ========== VMM栈和位图 ==========
    //
    UINT64 VmmStack;                      // VMM栈地址
    UINT64 MsrBitmapVirtualAddress;       // MSR位图虚拟地址
    UINT64 MsrBitmapPhysicalAddress;      // MSR位图物理地址
    UINT64 IoBitmapVirtualAddressA;       // I/O位图A虚拟地址
    UINT64 IoBitmapPhysicalAddressA;      // I/O位图A物理地址
    UINT64 IoBitmapVirtualAddressB;       // I/O位图B虚拟地址
    UINT64 IoBitmapPhysicalAddressB;      // I/O位图B物理地址

    //
    // ========== EPT相关 ==========
    //
    EPT_POINTER EptPointer;               // EPT指针（VMCS字段）
    PVMM_EPT_PAGE_TABLE EptPageTable;     // EPT页表指针

    PEPT_HOOKED_PAGE_DETAIL MtfEptHookRestorePoint;  // MTF EPT Hook恢复点

    PUINT64 PmlBufferAddress;             // PML（Page Modification Log）缓冲区

    //
    // ========== 中断和事件相关 ==========
    //
    UINT32 PendingExternalInterrupts[PENDING_INTERRUPTS_BUFFER_CAPACITY];

    BOOLEAN EnableExternalInterruptsOnContinue;      // 继续时启用外部中断
    BOOLEAN EnableExternalInterruptsOnContinueMtf;   // MTF继续时启用中断

    UINT32 QueuedNmi;                     // 排队的NMI数量

    //
    // ========== 调试和透明模式相关 ==========
    //
    VM_EXIT_TRANSPARENCY TransparencyState;  // 透明模式状态
    NMI_BROADCASTING_STATE NmiBroadcastingState;  // NMI广播状态

    //
    // ========== VMXOFF状态 ==========
    //
    VMX_VMXOFF_STATE VmxoffState;         // VMXOFF时的寄存器状态

} VIRTUAL_MACHINE_STATE, *PVIRTUAL_MACHINE_STATE;

// 全局状态数组 - 每个CPU核心一个
extern VIRTUAL_MACHINE_STATE * g_GuestState;
```

### 2.3.2 EPT Hook页面详情

```c
/**
 * @brief EPT Hook页面详细信息
 */
typedef struct _EPT_HOOKED_PAGE_DETAIL {
    //
    // ========== 地址信息 ==========
    //
    UINT64 PhysicalAddress;               // 被Hook的物理地址
    UINT64 VirtualAddress;                // 对应的虚拟地址

    //
    // ========== EPT表项 ==========
    //
    EPT_PML1_ENTRY OriginalEntry;         // 原始EPT表项
    EPT_PML1_ENTRY ChangedEntry;          // 修改后的EPT表项
    EPT_PML1_ENTRY ExecuteOnlyEntry;      // 只执行表项（用于隐形Hook）

    //
    // ========== Hook页面内容 ==========
    //
    UINT64 FakePageContents;              // 假页面的虚拟地址（用于读写分离）

    //
    // ========== Hook类型标志 ==========
    //
    BOOLEAN IsExecuteHook;                // 是否是执行Hook
    BOOLEAN IsReadHook;                   // 是否是读Hook
    BOOLEAN IsWriteHook;                  // 是否是写Hook
    BOOLEAN IsHiddenBreakpoint;           // 是否是隐藏断点

    //
    // ========== 链表管理 ==========
    //
    LIST_ENTRY PageHookList;              // Hook链表节点

    //
    // ========== 断点相关 ==========
    //
    BYTE OriginalByte[MaximumHiddenBreakpointsOnPage];  // 原始字节（用于断点）
    UINT64 BreakpointAddresses[MaximumHiddenBreakpointsOnPage];  // 断点地址

} EPT_HOOKED_PAGE_DETAIL, *PEPT_HOOKED_PAGE_DETAIL;

#define MaximumHiddenBreakpointsOnPage  40  // 每页最多40个断点
```

### 2.3.3 Guest寄存器结构

```c
/**
 * @brief Guest通用寄存器结构
 */
typedef struct _GUEST_REGS {
    UINT64 rax;    // 0x00
    UINT64 rcx;    // 0x08
    UINT64 rdx;    // 0x10
    UINT64 rbx;    // 0x18
    UINT64 rsp;    // 0x20 - 从VMCS读取
    UINT64 rbp;    // 0x28
    UINT64 rsi;    // 0x30
    UINT64 rdi;    // 0x38
    UINT64 r8;     // 0x40
    UINT64 r9;     // 0x48
    UINT64 r10;    // 0x50
    UINT64 r11;    // 0x58
    UINT64 r12;    // 0x60
    UINT64 r13;    // 0x68
    UINT64 r14;    // 0x70
    UINT64 r15;    // 0x78
} GUEST_REGS, *PGUEST_REGS;

/**
 * @brief Guest XMM寄存器结构
 */
typedef struct _GUEST_XMM_REGS {
    M128A xmm0;
    M128A xmm1;
    M128A xmm2;
    M128A xmm3;
    M128A xmm4;
    M128A xmm5;
    M128A xmm6;
    M128A xmm7;
    M128A xmm8;
    M128A xmm9;
    M128A xmm10;
    M128A xmm11;
    M128A xmm12;
    M128A xmm13;
    M128A xmm14;
    M128A xmm15;
} GUEST_XMM_REGS, *PGUEST_XMM_REGS;
```

### 2.3.4 调试器状态结构

```c
/**
 * @brief 处理器调试状态 - 每个CPU核心一个
 */
typedef struct _PROCESSOR_DEBUGGING_STATE {
    //
    // ========== 调试锁 ==========
    //
    volatile LONG Lock;                   // 自旋锁
    BOOLEAN WaitingToBeLocked;           // 等待被锁定

    //
    // ========== NMI状态 ==========
    //
    NMI_BROADCASTING_STATE NmiState;     // NMI广播状态

    //
    // ========== 跟踪模式 ==========
    //
    BOOLEAN TracingMode;                 // 是否在指令跟踪模式

    //
    // ========== 单步执行状态 ==========
    //
    struct {
        UINT16 CsSel;                    // 保存的CS选择器
    } InstrumentationStepInTrace;

    //
    // ========== 线程/进程信息 ==========
    //
    UINT64 ProcessId;
    UINT64 ThreadId;

    //
    // ========== 其他调试状态 ==========
    //
    // ... 更多字段

} PROCESSOR_DEBUGGING_STATE, *PPROCESSOR_DEBUGGING_STATE;

// 全局调试状态数组
extern PROCESSOR_DEBUGGING_STATE * g_DbgState;
```

---

## 2.4 HyperDbg启动流程

### 2.4.1 驱动加载流程

```
DriverEntry()
    ├─> LoaderInitVmmAndDebugger()
    │   ├─> 1. 填充MESSAGE_TRACING_CALLBACKS
    │   ├─> 2. 填充VMM_CALLBACKS
    │   ├─> 3. LogInitialize(&MsgTracingCallbacks)
    │   │       └─> 初始化日志缓冲区
    │   ├─> 4. VmFuncInitVmm(&VmmCallbacks)
    │   │       └─> HvInitVmm()
    │   │           ├─> 保存回调到g_Callbacks
    │   │           ├─> CompatibilityCheckPerformChecks()
    │   │           ├─> GlobalGuestStateAllocateZeroedMemory()
    │   │           ├─> MemoryMapperInitialize()
    │   │           └─> VmxInitialize()
    │   │               └─> 在所有核心上启动虚拟化
    │   └─> 5. DebuggerInitialize()
    │           └─> 初始化调试器子系统
    └─> 注册设备对象和符号链接
```

### 2.4.2 每个核心的虚拟化流程

```c
// ============================================
// VmxInitialize调用的DPC例程
// ============================================
VOID DpcRoutineInitializeGuest(KDPC * Dpc, PVOID DeferredContext) {
    UINT32 CurrentCore = KeGetCurrentProcessorNumberEx(NULL);
    VIRTUAL_MACHINE_STATE * VCpu = &g_GuestState[CurrentCore];

    // ============================================
    // 1. 分配VMX所需的内存区域
    // ============================================

    // VMXON区域（4KB）
    if (!VmxAllocateVmxonRegion(VCpu)) {
        LogError("Core %d: Failed to allocate VMXON region", CurrentCore);
        return;
    }

    // VMCS区域（4KB）
    if (!VmxAllocateVmcsRegion(VCpu)) {
        LogError("Core %d: Failed to allocate VMCS region", CurrentCore);
        return;
    }

    // VMM栈（32KB）
    if (!VmxAllocateVmmStack(VCpu)) {
        LogError("Core %d: Failed to allocate VMM stack", CurrentCore);
        return;
    }

    // MSR位图（4KB）
    if (!VmxAllocateMsrBitmap(VCpu)) {
        LogError("Core %d: Failed to allocate MSR bitmap", CurrentCore);
        return;
    }

    // I/O位图A和B（各4KB）
    if (!VmxAllocateIoBitmaps(VCpu)) {
        LogError("Core %d: Failed to allocate I/O bitmaps", CurrentCore);
        return;
    }

    // ============================================
    // 2. 启用VMX操作
    // ============================================

    // 设置CR4.VMXE = 1
    AsmEnableVmxOperation();

    // 执行VMXON指令
    if (__vmx_on(&VCpu->VmxonRegionPhysicalAddress) != 0) {
        LogError("Core %d: VMXON failed", CurrentCore);
        return;
    }

    LogInfo("Core %d: VMXON successful", CurrentCore);

    // ============================================
    // 3. 初始化VMCS
    // ============================================

    // VMCLEAR - 初始化VMCS为"clear"状态
    if (__vmx_vmclear(&VCpu->VmcsRegionPhysicalAddress) != 0) {
        LogError("Core %d: VMCLEAR failed", CurrentCore);
        __vmx_off();
        return;
    }

    // VMPTRLD - 加载VMCS为"current"
    if (__vmx_vmptrld(&VCpu->VmcsRegionPhysicalAddress) != 0) {
        LogError("Core %d: VMPTRLD failed", CurrentCore);
        __vmx_off();
        return;
    }

    LogInfo("Core %d: VMCS loaded successfully", CurrentCore);

    // ============================================
    // 4. 设置VMCS字段
    // ============================================
    if (!VmxSetupVmcs(VCpu, GuestStack)) {
        LogError("Core %d: VMCS setup failed", CurrentCore);
        __vmx_off();
        return;
    }

    // ============================================
    // 5. 保存当前CPU状态
    // ============================================
    // 这个函数保存当前的寄存器状态，
    // 这些状态将成为Guest的初始状态
    AsmVmxSaveState();

    // ============================================
    // 6. 启动虚拟机
    // ============================================
    __vmx_vmlaunch();

    // 如果执行到这里，说明VMLAUNCH失败
    UINT64 ErrorCode;
    __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &ErrorCode);
    LogError("Core %d: VMLAUNCH failed with error: %llx", CurrentCore, ErrorCode);

    __vmx_off();
}

// ============================================
// AsmVmxSaveState实现（汇编）
// ============================================
AsmVmxSaveState PROC
    ; 保存所有通用寄存器
    pushfq
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; 准备栈对齐
    sub rsp, 028h

    ; 保存当前RSP到VMCS_GUEST_RSP
    mov rax, rsp
    add rax, 028h + 8 * 16  ; 调整到原始RSP
    mov rdx, VMCS_GUEST_RSP
    vmwrite rdx, rax

    ; 保存当前RIP（VMLAUNCH之后的返回点）
    lea rax, [rip + VmxRestorePoint]
    mov rdx, VMCS_GUEST_RIP
    vmwrite rdx, rax

    ; 保存RFLAGS
    pushfq
    pop rax
    mov rdx, VMCS_GUEST_RFLAGS
    vmwrite rdx, rax

    add rsp, 028h

    ; 执行VMLAUNCH（在DPC中调用）
    ret

VmxRestorePoint:
    ; VMLAUNCH成功后，Guest会从这里开始执行
    ; 实际上Guest第一次运行时会直接从这里开始

    ; 恢复寄存器
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    popfq

    ret

AsmVmxSaveState ENDP
```

---

## 2.5 完整的虚拟化生命周期

### 2.5.1 启动虚拟化

```c
// ============================================
// 入口点：驱动加载
// ============================================
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    NTSTATUS Status;

    LogInfo("HyperDbg Driver Loading...");

    // 1. 初始化VMM和调试器
    if (!LoaderInitVmmAndDebugger()) {
        LogError("Failed to initialize VMM and debugger");
        return STATUS_UNSUCCESSFUL;
    }

    // 2. 创建设备对象
    Status = IoCreateDevice(
        DriverObject,
        0,
        &DeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &DeviceObject
    );

    if (!NT_SUCCESS(Status)) {
        LogError("Failed to create device object");
        return Status;
    }

    // 3. 创建符号链接
    Status = IoCreateSymbolicLink(&SymbolicLink, &DeviceName);
    if (!NT_SUCCESS(Status)) {
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    // 4. 设置IRP处理函数
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DriverClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverDeviceControl;
    DriverObject->DriverUnload = DriverUnload;

    LogInfo("HyperDbg Driver Loaded Successfully");

    return STATUS_SUCCESS;
}
```

### 2.5.2 终止虚拟化

```c
// ============================================
// 驱动卸载
// ============================================
VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    LogInfo("HyperDbg Driver Unloading...");

    // 1. 卸载调试器
    if (g_HandleInUse) {
        DebuggerUninitialize();
        g_HandleInUse = FALSE;
    }

    // 2. 终止虚拟化
    LoaderUninitializeVmmAndDebugger();

    // 3. 删除符号链接
    IoDeleteSymbolicLink(&SymbolicLink);

    // 4. 删除设备对象
    IoDeleteDevice(DriverObject->DeviceObject);

    LogInfo("HyperDbg Driver Unloaded Successfully");
}

// ============================================
// 卸载VMM
// ============================================
VOID LoaderUninitializeVmmAndDebugger() {
    LogInfo("Unloading VMM...");

    // 在所有核心上执行VMXOFF
    VmFuncUninitVmm();

    // 卸载日志系统
    LoaderUninitializeLogTracer();
}

// ============================================
// 每个核心上的VMXOFF
// ============================================
VOID DpcRoutineTerminateGuest(KDPC * Dpc, PVOID DeferredContext) {
    UINT32 CurrentCore = KeGetCurrentProcessorNumberEx(NULL);

    LogInfo("Core %d: Terminating virtualization", CurrentCore);

    // 通过VMCALL请求VMXOFF
    UINT64 Status = AsmVmxVmcall(VMCALL_VMXOFF, 0, 0, 0);

    if (Status == STATUS_SUCCESS) {
        LogInfo("Core %d: Exited VMX successfully", CurrentCore);
    } else {
        LogError("Core %d: Failed to exit VMX", CurrentCore);
    }
}

// ============================================
// VMCALL_VMXOFF处理
// ============================================
BOOLEAN VmxHandleVmcallVmxoff(VIRTUAL_MACHINE_STATE * VCpu) {
    UINT64 GuestRsp, GuestRip, GuestRflags;

    LogInfo("Core %d: VMXOFF requested via VMCALL", VCpu->CoreId);

    // 1. 读取Guest状态（用于恢复）
    __vmx_vmread(VMCS_GUEST_RIP, &GuestRip);
    __vmx_vmread(VMCS_GUEST_RSP, &GuestRsp);
    __vmx_vmread(VMCS_GUEST_RFLAGS, &GuestRflags);

    // 2. 保存到VMXOFF状态
    VCpu->VmxoffState.GuestRip = GuestRip;
    VCpu->VmxoffState.GuestRsp = GuestRsp;
    VCpu->VmxoffState.GuestRflags = GuestRflags;

    // 3. 增加RIP（跳过VMCALL指令）
    GuestRip += VmxGetInstructionLength();
    VCpu->VmxoffState.GuestRip = GuestRip;

    // 4. 返回TRUE表示需要执行VMXOFF
    return TRUE;
}
```

---

## 2.6 重要的编译时配置

**文件位置**：`hyperdbg/include/config/Configuration.h`

```c
// ============================================
// VMX相关配置
// ============================================
#define VMCS_SIZE                   4096          // VMCS大小
#define VMXON_SIZE                  4096          // VMXON区域大小
#define VMM_STACK_SIZE              0x8000        // VMM栈大小（32KB）

// ============================================
// EPT相关配置
// ============================================
#define MaximumHiddenBreakpointsOnPage  40       // 每页最大隐藏断点数

// ============================================
// 中断相关配置
// ============================================
#define PENDING_INTERRUPTS_BUFFER_CAPACITY  64   // 待处理中断缓冲容量

// ============================================
// 日志相关配置
// ============================================
#define MaximumPacketsCapacity         1000      // 日志包容量
#define MaximumPacketsCapacityPriority 50        // 优先级日志包容量
#define PacketChunkSize                3000      // 包块大小
#define UsermodeBufferSize             0x100000  // 用户态缓冲区大小（1MB）
#define LogBufferSize                  \
    ((MaximumPacketsCapacity + MaximumPacketsCapacityPriority) * PacketChunkSize)

// ============================================
// HyperEvade相关配置
// ============================================
#define DISABLE_HYPERDBG_HYPEREVADE    FALSE     // 是否禁用反检测模块

// ============================================
// 调试器相关配置
// ============================================
#define UseDbgPrintInsteadOfUsermodeMessageTracking  FALSE
#define EnableInstantEventMechanism                  FALSE

// ============================================
// IDT相关配置
// ============================================
#define USE_DEFAULT_OS_IDT_AS_HOST_IDT  TRUE     // 使用系统IDT作为Host IDT
```

---

## 2.7 模块间交互示例

### 2.7.1 断点命中的完整流程

```
1. 用户在调试器中设置断点
   ├─> 调试器发送命令包
   ├─> hyperkd接收命令
   └─> BreakpointAddNew()
       ├─> 分配断点结构
       ├─> 通过VMCALL通知VMM
       └─> hyperhv设置异常位图拦截#BP

2. Guest执行到断点地址
   ├─> 执行INT3指令
   ├─> 触发#BP异常
   └─> 根据异常位图触发VM-Exit

3. VM-Exit处理（hyperhv）
   ├─> AsmVmexitHandler保存寄存器
   ├─> VmxVmexitHandler分发
   ├─> case VMX_EXIT_REASON_EXCEPTION_OR_NMI:
   ├─> IdtEmulationHandleExceptionAndNmi()
   └─> 调用回调：VmmCallbackHandleBreakpointException()

4. 断点回调（hyperkd）
   ├─> BreakpointHandleBreakpoints()
   ├─> 检查断点列表
   ├─> KdHandleBreakpointAndDebugBreakpoints()
   │   ├─> 锁定所有核心
   │   └─> 向调试器发送断点命中消息
   └─> KdResponsePacketToDebugger()
       └─> SerialConnectionSendBuffer()

5. 调试器接收并显示
   ├─> 显示断点位置
   ├─> 显示寄存器状态
   └─> 等待用户命令

6. 用户输入命令（如"g"继续执行）
   ├─> 调试器发送继续命令
   ├─> hyperkd接收命令
   ├─> KdContinueDebuggee()
   │   ├─> 解锁所有核心
   │   └─> 返回到VM-Exit handler
   └─> VMRESUME返回Guest
```

### 2.7.2 EPT Hook设置流程

```
1. 用户请求Hook某个函数
   ├─> 通过IOCTL发送请求
   └─> hyperkd处理IOCTL

2. hyperkd调用VMM接口
   ├─> 通过VMCALL进入VMM
   ├─> VMCALL号：VMCALL_EXEC_HOOK_PAGE
   └─> 参数：目标地址、Hook函数地址

3. hyperhv处理VMCALL
   ├─> VmxHandleVmcall()
   ├─> 调用回调：VmmCallbackVmcallHandler()
   └─> DebuggerVmcallHandler()
       ├─> 分配EPT_HOOKED_PAGE_DETAIL
       ├─> 获取目标页面的EPT表项
       ├─> 保存原始权限
       ├─> 移除执行权限
       └─> EptInveptSingleContext()

4. Guest执行到被Hook的函数
   ├─> 尝试执行
   ├─> 触发EPT Violation
   └─> VM-Exit

5. hyperhv处理EPT Violation
   ├─> EptHandleEptViolation()
   ├─> 找到对应的Hook
   ├─> 临时恢复执行权限
   ├─> 设置MTF
   └─> VMRESUME执行一条指令

6. MTF触发
   ├─> MtfHandleVmexit()
   ├─> 重新移除执行权限
   ├─> 恢复Hook状态
   └─> VMRESUME继续执行
```

---

## 2.8 内存布局

### 2.8.1 VMM栈布局

```
High Address
┌─────────────────────────────────┐
│                                 │
│      VMM Stack (32KB)          │
│                                 │
│  ┌──────────────────────────┐  │
│  │  Host RSP points here    │  │ <- VMCS_HOST_RSP
│  ├──────────────────────────┤  │
│  │  Shadow Space (32 bytes) │  │
│  ├──────────────────────────┤  │
│  │  Return Address          │  │
│  ├──────────────────────────┤  │
│  │  Local Variables         │  │
│  ├──────────────────────────┤  │
│  │  ...                     │  │
│  └──────────────────────────┘  │
│                                 │
├─────────────────────────────────┤ <- VCpu->VmmStack (base)
Low Address
```

### 2.8.2 Guest寄存器在栈上的布局

```
VM-Exit后，AsmVmexitHandler保存的寄存器在栈上的布局：

High Address
┌─────────────────────────────────┐
│  RFLAGS                         │  [rsp + 0xD0]
├─────────────────────────────────┤
│  XMM5                          │  [rsp + 0xC0]
├─────────────────────────────────┤
│  XMM4                          │  [rsp + 0xB0]
├─────────────────────────────────┤
│  XMM3                          │  [rsp + 0xA0]
├─────────────────────────────────┤
│  XMM2                          │  [rsp + 0x90]
├─────────────────────────────────┤
│  XMM1                          │  [rsp + 0x80]
├─────────────────────────────────┤
│  XMM0                          │  [rsp + 0x70]
├─────────────────────────────────┤
│  R15                           │  [rsp + 0x78]
├─────────────────────────────────┤
│  R14                           │  [rsp + 0x70]
├─────────────────────────────────┤
│  ...                           │
├─────────────────────────────────┤
│  RAX                           │  [rsp + 0x00] <- PGUEST_REGS
└─────────────────────────────────┘
Low Address
```

---

## 2.9 模块职责划分

### 2.9.1 hyperhv（Hypervisor核心）

**职责**：
- VMX操作（VMXON、VMLAUNCH、VMRESUME等）
- VM-Exit处理和分发
- EPT页表管理
- 事件注入
- 提供回调接口给上层

**不负责**：
- 调试逻辑（由hyperkd负责）
- 通信协议（由hyperkd负责）
- 用户命令解析（由hyperkd负责）

### 2.9.2 hyperkd（内核调试器）

**职责**：
- 调试命令处理
- 断点管理（软件断点、硬件断点、隐藏断点）
- 进程/线程跟踪
- 与外部调试器通信（串口/网络）
- 实现回调函数供hyperhv调用

**不负责**：
- VMX操作（由hyperhv负责）
- EPT管理（由hyperhv负责）

### 2.9.3 hyperlog（日志系统）

**职责**：
- 日志消息缓冲区管理
- 消息格式化
- 消息传输到用户态或调试器

**特点**：
- 分离VMX root和non-root的缓冲区
- 支持优先级消息
- 支持立即消息和批量消息

### 2.9.4 hyperevade（反检测）

**职责**：
- CPUID/MSR伪造
- 系统调用Hook和结果修改
- 硬件信息伪造
- 时间戳补偿

**特点**：
- 可选模块（可编译时禁用）
- 选择性应用（只对特定进程）
- 避免影响系统进程

---

## 2.10 编译和构建

### 2.10.1 项目组织

HyperDbg使用Visual Studio解决方案：

```
HyperDbg.sln
├── hyperhv.vcxproj          - Hypervisor核心（驱动）
├── hyperkd.vcxproj          - 内核调试器（驱动）
├── hyperlog.vcxproj         - 日志系统（库）
├── hyperevade.vcxproj       - 反检测模块（库）
├── kdserial.vcxproj         - 串口驱动
└── script-eval.vcxproj      - 脚本引擎（库）
```

### 2.10.2 编译输出

```
编译后的输出（x64 Release）：
├── hyperhv.sys              - Hypervisor驱动
├── hyperkd.sys              - 内核调试器驱动
├── kdserial.sys             - 串口驱动
├── hyperlog.lib             - 日志库
├── hyperevade.lib           - 反检测库
└── script-eval.lib          - 脚本引擎库
```

### 2.10.3 驱动签名

在Windows x64上运行需要：
1. 禁用驱动签名强制（测试模式）
2. 或使用测试签名证书
3. 或购买EV代码签名证书

```batch
REM 启用测试模式
bcdedit /set testsigning on

REM 重启后加载驱动
sc create hyperhv binpath= "C:\path\to\hyperhv.sys" type= kernel
sc start hyperhv
```

---

## 本章小结

本章介绍了HyperDbg项目的整体架构：

1. **模块组成**：hyperhv、hyperkd、hyperlog、hyperevade等核心模块
2. **文件索引**：各个关键文件的位置和作用
3. **数据结构**：虚拟机状态、EPT Hook、调试器状态等核心结构
4. **启动流程**：从驱动加载到虚拟化启动的完整过程
5. **模块交互**：各模块通过回调机制协作
6. **职责划分**：每个模块的明确职责范围

理解这个架构对于深入学习HyperDbg至关重要，它展示了如何将复杂的虚拟化调试器系统模块化设计。

---

[<< 上一章：Intel VT-x虚拟化技术基础](./第一章-Intel-VT-x虚拟化技术基础.md) | [下一章：VMM回调机制详解 >>](./第三章-VMM回调机制详解.md)
