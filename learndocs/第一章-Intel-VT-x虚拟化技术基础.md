# 第一章：Intel VT-x虚拟化技术基础

## 1.1 VMM（虚拟机监视器）概念

### 1.1.1 什么是VMM？

VMM (Virtual Machine Monitor)，也称为Hypervisor，是运行在物理硬件和操作系统之间的一层软件，负责管理和控制虚拟机的执行。在Intel VT-x技术中，VMM运行在VMX Root模式，而Guest OS运行在VMX Non-root模式。

### 1.1.2 运行模式架构

```
┌─────────────────────────────────┐
│      Guest OS (Ring 0-3)        │  VMX Non-root Mode
│   运行在虚拟机中的操作系统        │  (被监控的环境)
├─────────────────────────────────┤
│         VM-Exit ↓ ↑ VM-Entry    │  <- 模式切换
├─────────────────────────────────┤
│      VMM/Hypervisor (Ring -1)   │  VMX Root Mode
│        虚拟机监视器              │  (监控者)
├─────────────────────────────────┤
│      Physical Hardware          │  物理硬件
└─────────────────────────────────┘
```

**关键概念**：
- **VMX Root Mode**：VMM运行的模式，拥有完全的硬件控制权
- **VMX Non-root Mode**：Guest运行的模式，某些操作会被拦截
- **VM-Exit**：从Guest切换到VMM的过程
- **VM-Entry**：从VMM返回到Guest的过程

### 1.1.3 HyperDbg中VMM的实现

在HyperDbg中，VMM的核心实现位于 `hyperdbg/hyperhv/code/vmm/vmx/Hv.c`：

```c
// 虚拟机状态结构 - 每个CPU核心一个
typedef struct _VIRTUAL_MACHINE_STATE {
    BOOLEAN IsOnVmxRootMode;        // 是否在VMX根模式
    BOOLEAN HasLaunched;            // VMCS是否已启动
    GUEST_REGS * Regs;              // Guest寄存器
    GUEST_XMM_REGS * XmmRegs;       // XMM寄存器
    UINT32 CoreId;                  // CPU核心ID

    // VMX区域物理地址
    UINT64 VmxonRegionPhysicalAddress;
    UINT64 VmcsRegionPhysicalAddress;

    // VMM栈
    UINT64 VmmStack;

    // EPT相关
    PVMM_EPT_PAGE_TABLE EptPageTable;

    // Exit信息
    UINT32 ExitReason;
    UINT32 ExitQualification;
    UINT64 LastVmexitRip;

    // ... 更多状态信息
} VIRTUAL_MACHINE_STATE;

// 全局状态数组 - 每个核心一个
VIRTUAL_MACHINE_STATE * g_GuestState;
```

### 1.1.4 VMM的主要职责

1. **资源虚拟化**
   - 虚拟化CPU（寄存器、指令集）
   - 虚拟化内存（通过EPT）
   - 虚拟化I/O设备

2. **VM调度与管理**
   - 管理多个虚拟机的执行
   - 处理VM-Exit和VM-Entry
   - 维护每个CPU核心的虚拟机状态

3. **拦截与处理**
   - 拦截敏感指令（CPUID、RDMSR、WRMSR等）
   - 拦截特权操作（CR访问、I/O操作等）
   - 拦截内存访问（通过EPT Violation）

4. **内存管理**
   - 通过EPT管理Guest物理内存到Host物理内存的映射
   - 实现内存隔离和保护

5. **事件注入**
   - 向Guest注入中断
   - 向Guest注入异常
   - 处理Guest的外部中断

---

## 1.2 VMX（虚拟机扩展）操作

### 1.2.1 VMX指令集概览

Intel VT-x提供了一组专门的VMX指令来管理虚拟化：

| 指令 | 功能 | 使用场景 | 特点 |
|------|------|---------|------|
| **VMXON** | 进入VMX操作模式 | 启动虚拟化 | 需要物理地址作为参数 |
| **VMXOFF** | 退出VMX操作模式 | 关闭虚拟化 | 只能在VMX root模式执行 |
| **VMLAUNCH** | 首次启动虚拟机 | 初始VM进入 | 只能执行一次 |
| **VMRESUME** | 恢复虚拟机执行 | VM-Exit后返回 | 必须在VMLAUNCH后使用 |
| **VMREAD** | 读取VMCS字段 | 获取VM配置 | 需要字段编码 |
| **VMWRITE** | 写入VMCS字段 | 设置VM配置 | 需要字段编码 |
| **VMCLEAR** | 清除VMCS | 使VMCS无效 | 释放VMCS前调用 |
| **VMPTRLD** | 加载VMCS指针 | 切换当前VMCS | 设置当前操作的VMCS |
| **VMPTRST** | 存储VMCS指针 | 保存当前VMCS | 获取当前VMCS地址 |
| **VMCALL** | 从Guest调用VMM | Hypercall | Guest主动触发VM-Exit |
| **INVEPT** | 使EPT缓存无效 | 刷新EPT TLB | 修改EPT后必须调用 |
| **INVVPID** | 使VPID缓存无效 | 刷新VPID TLB | 修改VPID后调用 |

### 1.2.2 HyperDbg中的VMX实现

**文件位置**：`hyperdbg/hyperhv/code/vmm/vmx/Vmx.c`

#### VMX指令包装函数

```c
// ============================================
// VMREAD - 读取VMCS字段
// ============================================
inline UCHAR VmxVmread64(size_t Field, UINT64 * FieldValue) {
    return __vmx_vmread(Field, FieldValue);
}

inline UCHAR VmxVmread32(size_t Field, UINT32 * FieldValue) {
    UINT64 TempValue;
    UCHAR Result = __vmx_vmread(Field, &TempValue);
    *FieldValue = (UINT32)TempValue;
    return Result;
}

inline UCHAR VmxVmread16(size_t Field, UINT16 * FieldValue) {
    UINT64 TempValue;
    UCHAR Result = __vmx_vmread(Field, &TempValue);
    *FieldValue = (UINT16)TempValue;
    return Result;
}

// ============================================
// VMWRITE - 写入VMCS字段
// ============================================
inline UCHAR VmxVmwrite64(size_t Field, UINT64 FieldValue) {
    return __vmx_vmwrite(Field, FieldValue);
}

inline UCHAR VmxVmwrite32(size_t Field, UINT32 FieldValue) {
    return __vmx_vmwrite(Field, FieldValue);
}

inline UCHAR VmxVmwrite16(size_t Field, UINT16 FieldValue) {
    return __vmx_vmwrite(Field, FieldValue);
}

// ============================================
// 检查VMX支持
// ============================================
BOOLEAN VmxCheckVmxSupport() {
    CPUID Data = {0};

    // 1. 检查CPUID.1:ECX.VMX[bit 5]
    __cpuid((int *)&Data, 1);
    if ((Data.ecx & (1 << 5)) == 0) {
        LogError("VMX is not supported by CPU");
        return FALSE;
    }

    // 2. 检查IA32_FEATURE_CONTROL MSR
    IA32_FEATURE_CONTROL_REGISTER FeatureControlMsr = {0};
    FeatureControlMsr.AsUInt = __readmsr(IA32_FEATURE_CONTROL);

    // 检查是否锁定
    if (FeatureControlMsr.Lock == FALSE) {
        LogError("IA32_FEATURE_CONTROL MSR is not locked");
        return FALSE;
    }

    // 检查VMX是否启用
    if (FeatureControlMsr.VmxEnable == FALSE) {
        LogError("VMX is not enabled in IA32_FEATURE_CONTROL");
        return FALSE;
    }

    return TRUE;
}

// ============================================
// VMXON区域分配
// ============================================
BOOLEAN VmxAllocateVmxonRegion(VIRTUAL_MACHINE_STATE * VCpu) {
    // 分配4KB对齐的内存
    UINT64 VmxonRegion = (UINT64)PlatformMemAllocateContiguousZeroedMemory(VMXON_SIZE);

    if (VmxonRegion == NULL) {
        LogError("Failed to allocate VMXON region");
        return FALSE;
    }

    // 读取VMX Basic MSR获取版本标识符
    IA32_VMX_BASIC_REGISTER VmxBasicMsr = {0};
    VmxBasicMsr.AsUInt = __readmsr(IA32_VMX_BASIC);

    // 设置版本标识符（VMXON区域的第一个DWORD）
    *(UINT32 *)VmxonRegion = VmxBasicMsr.VmcsRevisionId;

    // 保存到VCpu结构
    VCpu->VmxonRegionVirtualAddress = VmxonRegion;
    VCpu->VmxonRegionPhysicalAddress = VirtualAddressToPhysicalAddress((PVOID)VmxonRegion);

    LogInfo("VMXON region allocated at PA: %llx", VCpu->VmxonRegionPhysicalAddress);

    return TRUE;
}

// ============================================
// VMCS区域分配
// ============================================
BOOLEAN VmxAllocateVmcsRegion(VIRTUAL_MACHINE_STATE * VCpu) {
    // 分配4KB对齐的内存
    UINT64 VmcsRegion = (UINT64)PlatformMemAllocateContiguousZeroedMemory(VMCS_SIZE);

    if (VmcsRegion == NULL) {
        LogError("Failed to allocate VMCS region");
        return FALSE;
    }

    // 读取VMX Basic MSR
    IA32_VMX_BASIC_REGISTER VmxBasicMsr = {0};
    VmxBasicMsr.AsUInt = __readmsr(IA32_VMX_BASIC);

    // 设置版本标识符
    *(UINT32 *)VmcsRegion = VmxBasicMsr.VmcsRevisionId;

    // 保存到VCpu结构
    VCpu->VmcsRegionVirtualAddress = VmcsRegion;
    VCpu->VmcsRegionPhysicalAddress = VirtualAddressToPhysicalAddress((PVOID)VmcsRegion);

    LogInfo("VMCS region allocated at PA: %llx", VCpu->VmcsRegionPhysicalAddress);

    return TRUE;
}
```

### 1.2.3 VMX操作流程

#### 完整的虚拟化启动流程

```
1. 启用VMX操作
   ├─ 检查CPU是否支持VMX
   ├─ 检查IA32_FEATURE_CONTROL MSR
   ├─ 设置CR4.VMXE = 1（启用VMX）
   ├─ 分配VMXON区域（4KB，4KB对齐）
   └─ 执行VMXON指令

2. 准备VMCS
   ├─ 分配VMCS区域（4KB，4KB对齐）
   ├─ 执行VMCLEAR初始化VMCS
   ├─ 执行VMPTRLD加载VMCS
   └─ 使用VMWRITE配置VMCS字段

3. 配置VMCS
   ├─ 配置Guest-state area（Guest状态）
   ├─ 配置Host-state area（Host状态）
   ├─ 配置VM-execution controls（执行控制）
   ├─ 配置VM-exit controls（退出控制）
   └─ 配置VM-entry controls（进入控制）

4. 启动虚拟机
   ├─ 保存当前CPU状态作为Guest状态
   └─ 执行VMLAUNCH首次启动

5. VM-Exit发生
   ├─ CPU自动保存Guest状态到VMCS
   ├─ CPU自动加载Host状态从VMCS
   ├─ 跳转到Host RIP（VM-Exit handler）
   └─ VMM处理VM-Exit

6. 返回Guest
   ├─ VMM完成处理
   ├─ 更新VMCS字段（如果需要）
   └─ 执行VMRESUME返回Guest

7. 终止虚拟化
   ├─ 从Guest触发特殊VMCALL
   ├─ VMM执行VMXOFF
   └─ 恢复到正常模式
```

#### 代码示例：启动虚拟化

```c
// ============================================
// 主虚拟化初始化函数
// ============================================
BOOLEAN HvVmxInitialize() {
    // 1. 检查VMX支持
    if (!VmxCheckVmxSupport()) {
        LogError("VMX is not supported on this system");
        return FALSE;
    }

    // 2. 检查EPT支持
    if (!EptCheckFeatures()) {
        LogError("EPT is not supported on this system");
        return FALSE;
    }

    // 3. 为每个CPU核心启动虚拟化
    KeGenericCallDpc(DpcRoutineInitializeGuest, NULL);

    return TRUE;
}

// ============================================
// 每个核心的初始化DPC
// ============================================
VOID DpcRoutineInitializeGuest(KDPC * Dpc, PVOID Context) {
    UINT32 CurrentCore = KeGetCurrentProcessorNumberEx(NULL);
    VIRTUAL_MACHINE_STATE * VCpu = &g_GuestState[CurrentCore];

    LogInfo("Initializing virtualization on core %d", CurrentCore);

    // 1. 分配VMX区域
    if (!VmxAllocateVmxonRegion(VCpu)) {
        LogError("Failed to allocate VMXON region");
        return;
    }

    if (!VmxAllocateVmcsRegion(VCpu)) {
        LogError("Failed to allocate VMCS region");
        return;
    }

    if (!VmxAllocateVmmStack(VCpu)) {
        LogError("Failed to allocate VMM stack");
        return;
    }

    if (!VmxAllocateMsrBitmap(VCpu)) {
        LogError("Failed to allocate MSR bitmap");
        return;
    }

    // 2. 进入VMX操作模式
    AsmEnableVmxOperation();  // 设置CR4.VMXE = 1

    // 3. 执行VMXON
    if (__vmx_on(&VCpu->VmxonRegionPhysicalAddress) != 0) {
        LogError("VMXON failed");
        return;
    }

    LogInfo("VMXON successful on core %d", CurrentCore);

    // 4. 初始化VMCS
    if (__vmx_vmclear(&VCpu->VmcsRegionPhysicalAddress) != 0) {
        LogError("VMCLEAR failed");
        return;
    }

    if (__vmx_vmptrld(&VCpu->VmcsRegionPhysicalAddress) != 0) {
        LogError("VMPTRLD failed");
        return;
    }

    // 5. 初始化EPT
    if (!EptInitialize()) {
        LogError("EPT initialization failed");
        return;
    }

    // 6. 设置VMCS字段
    if (!VmxSetupVmcs(VCpu, GuestStack)) {
        LogError("VMCS setup failed");
        return;
    }

    // 7. 保存当前状态并启动虚拟机
    AsmVmxSaveState();    // 保存当前状态作为Guest状态

    // 8. 执行VMLAUNCH
    __vmx_vmlaunch();

    // 如果到达这里说明VMLAUNCH失败
    UINT64 ErrorCode = 0;
    __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &ErrorCode);
    LogError("VMLAUNCH failed with error: %llx", ErrorCode);
}
```

---

## 1.3 VMCS（虚拟机控制结构）

### 1.3.1 VMCS概述

**VMCS (Virtual Machine Control Structure)** 是一个4KB大小的内存区域，包含了虚拟机的完整状态和配置信息。每个逻辑处理器可以有多个VMCS，但同时只能有一个是"current VMCS"。

**重要特性**：
- 大小固定为4KB
- 必须4KB对齐
- 必须在物理内存中
- 每个CPU核心独立维护
- 通过VMPTRLD设置当前VMCS
- 通过VMREAD/VMWRITE访问字段

### 1.3.2 VMCS字段组成

VMCS包含6个逻辑组：

```
┌─────────────────────────────────────────┐
│ 1. Guest-state area                    │ <- Guest的CPU状态
│    - 寄存器（CR0, CR3, CR4, RSP, RIP等）│
│    - 段寄存器（CS, DS, ES, SS, FS, GS）  │
│    - GDTR, IDTR, LDTR, TR               │
│    - MSRs                               │
├─────────────────────────────────────────┤
│ 2. Host-state area                     │ <- VM-Exit后加载的状态
│    - RIP (VM-Exit handler地址)          │
│    - RSP (Host栈指针)                   │
│    - 段选择器                            │
│    - 控制寄存器                          │
├─────────────────────────────────────────┤
│ 3. VM-execution control fields         │ <- 控制哪些操作触发VM-Exit
│    - Pin-based controls                 │
│    - Processor-based controls           │
│    - Exception bitmap                   │
│    - MSR bitmap                         │
│    - I/O bitmap                         │
│    - EPT pointer                        │
├─────────────────────────────────────────┤
│ 4. VM-exit control fields              │ <- 控制VM-Exit行为
│    - VM-exit controls                   │
│    - MSR-store/load addresses           │
├─────────────────────────────────────────┤
│ 5. VM-entry control fields             │ <- 控制VM-Entry行为
│    - VM-entry controls                  │
│    - Event injection                    │
├─────────────────────────────────────────┤
│ 6. VM-exit information fields          │ <- VM-Exit原因和信息
│    - Exit reason                        │
│    - Exit qualification                 │
│    - Guest linear/physical address      │
│    - Instruction information            │
└─────────────────────────────────────────┘
```

### 1.3.3 重要的VMCS字段定义

**文件位置**：`hyperdbg/hyperhv/header/vmm/vmx/Vmx.h`

```c
// ============================================
// Guest State 字段
// ============================================
#define VMCS_GUEST_ES_SELECTOR                  0x00000800
#define VMCS_GUEST_CS_SELECTOR                  0x00000802
#define VMCS_GUEST_SS_SELECTOR                  0x00000804
#define VMCS_GUEST_DS_SELECTOR                  0x00000806
#define VMCS_GUEST_FS_SELECTOR                  0x00000808
#define VMCS_GUEST_GS_SELECTOR                  0x0000080A
#define VMCS_GUEST_LDTR_SELECTOR                0x0000080C
#define VMCS_GUEST_TR_SELECTOR                  0x0000080E

#define VMCS_GUEST_CR0                          0x00006800
#define VMCS_GUEST_CR3                          0x00006802
#define VMCS_GUEST_CR4                          0x00006804
#define VMCS_GUEST_DR7                          0x0000681A

#define VMCS_GUEST_RSP                          0x0000681C
#define VMCS_GUEST_RIP                          0x0000681E
#define VMCS_GUEST_RFLAGS                       0x00006820

#define VMCS_GUEST_GDTR_BASE                    0x00006816
#define VMCS_GUEST_IDTR_BASE                    0x00006818

// ============================================
// Host State 字段
// ============================================
#define VMCS_HOST_CR0                           0x00006C00
#define VMCS_HOST_CR3                           0x00006C02
#define VMCS_HOST_CR4                           0x00006C04

#define VMCS_HOST_RSP                           0x00006C14
#define VMCS_HOST_RIP                           0x00006C16

#define VMCS_HOST_CS_SELECTOR                   0x00000C02
#define VMCS_HOST_SS_SELECTOR                   0x00000C04
#define VMCS_HOST_DS_SELECTOR                   0x00000C06
#define VMCS_HOST_ES_SELECTOR                   0x00000C00
#define VMCS_HOST_FS_SELECTOR                   0x00000C08
#define VMCS_HOST_GS_SELECTOR                   0x00000C0A
#define VMCS_HOST_TR_SELECTOR                   0x00000C0C

// ============================================
// VM-Execution Control 字段
// ============================================
#define VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS           0x00004000
#define VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS     0x00004002
#define VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS 0x0000401E

#define VMCS_CTRL_EXCEPTION_BITMAP              0x00004004
#define VMCS_CTRL_MSR_BITMAP                    0x00002004
#define VMCS_CTRL_IO_BITMAP_A                   0x00002000
#define VMCS_CTRL_IO_BITMAP_B                   0x00002002

#define VMCS_CTRL_EPT_POINTER                   0x0000201A
#define VMCS_CTRL_VPID                          0x00000000

// ============================================
// VM-Exit Information 字段
// ============================================
#define VMCS_EXIT_REASON                        0x00004402
#define VMCS_EXIT_QUALIFICATION                 0x00006400
#define VMCS_EXIT_INSTRUCTION_LENGTH            0x0000440C
#define VMCS_EXIT_INSTRUCTION_INFO              0x0000440E

#define VMCS_GUEST_PHYSICAL_ADDRESS             0x00002400
#define VMCS_GUEST_LINEAR_ADDRESS               0x0000640A

#define VMCS_VM_INSTRUCTION_ERROR               0x00004400

// ============================================
// Control Bits 定义
// ============================================

// Pin-Based VM-Execution Controls
#define PIN_BASED_VM_EXECUTION_CONTROLS_EXTERNAL_INTERRUPT   0x00000001
#define PIN_BASED_VM_EXECUTION_CONTROLS_NMI_EXITING          0x00000008

// Processor-Based VM-Execution Controls
#define CPU_BASED_INTERRUPT_WINDOW_EXITING      0x00000004
#define CPU_BASED_USE_TSC_OFFSETTING            0x00000008
#define CPU_BASED_HLT_EXITING                   0x00000080
#define CPU_BASED_INVLPG_EXITING                0x00000200
#define CPU_BASED_MWAIT_EXITING                 0x00000400
#define CPU_BASED_RDPMC_EXITING                 0x00000800
#define CPU_BASED_RDTSC_EXITING                 0x00001000
#define CPU_BASED_CR3_LOAD_EXITING              0x00008000
#define CPU_BASED_CR3_STORE_EXITING             0x00010000
#define CPU_BASED_CR8_LOAD_EXITING              0x00080000
#define CPU_BASED_CR8_STORE_EXITING             0x00100000
#define CPU_BASED_MOV_DR_EXITING                0x00800000
#define CPU_BASED_UNCONDITIONAL_IO_EXITING      0x01000000
#define CPU_BASED_ACTIVATE_MSR_BITMAP           0x10000000
#define CPU_BASED_ACTIVATE_SECONDARY_CONTROLS   0x80000000

// Secondary Processor-Based VM-Execution Controls
#define CPU_BASED_CTL2_ENABLE_EPT               0x00000002
#define CPU_BASED_CTL2_ENABLE_RDTSCP            0x00000008
#define CPU_BASED_CTL2_ENABLE_VPID              0x00000020
#define CPU_BASED_CTL2_UNRESTRICTED_GUEST       0x00000080
#define CPU_BASED_CTL2_ENABLE_INVPCID           0x00001000
#define CPU_BASED_CTL2_ENABLE_XSAVES_XRSTORS    0x00100000
```

### 1.3.4 VMCS配置实现

**文件位置**：`hyperdbg/hyperhv/code/vmm/vmx/Hv.c`

```c
// ============================================
// 完整的VMCS设置函数
// ============================================
BOOLEAN VmxSetupVmcs(VIRTUAL_MACHINE_STATE * VCpu, PVOID GuestStack) {
    UINT64 GdtBase = 0;
    SEGMENT_SELECTOR SegmentSelector = {0};

    LogInfo("Setting up VMCS for core %d", VCpu->CoreId);

    // ============================================
    // 1. Guest State Area 配置
    // ============================================

    // 1.1 配置段寄存器
    AsmGetGdtBase(&GdtBase);

    // CS段
    SegmentSelector.Selector = AsmGetCs();
    HvFillGuestSelectorData((PVOID)GdtBase, VMCS_GUEST_CS_SELECTOR, SegmentSelector.Selector);

    // SS段
    SegmentSelector.Selector = AsmGetSs();
    HvFillGuestSelectorData((PVOID)GdtBase, VMCS_GUEST_SS_SELECTOR, SegmentSelector.Selector);

    // DS段
    SegmentSelector.Selector = AsmGetDs();
    HvFillGuestSelectorData((PVOID)GdtBase, VMCS_GUEST_DS_SELECTOR, SegmentSelector.Selector);

    // ES段
    SegmentSelector.Selector = AsmGetEs();
    HvFillGuestSelectorData((PVOID)GdtBase, VMCS_GUEST_ES_SELECTOR, SegmentSelector.Selector);

    // FS段
    SegmentSelector.Selector = AsmGetFs();
    HvFillGuestSelectorData((PVOID)GdtBase, VMCS_GUEST_FS_SELECTOR, SegmentSelector.Selector);

    // GS段
    SegmentSelector.Selector = AsmGetGs();
    HvFillGuestSelectorData((PVOID)GdtBase, VMCS_GUEST_GS_SELECTOR, SegmentSelector.Selector);

    // LDTR
    SegmentSelector.Selector = AsmGetLdtr();
    HvFillGuestSelectorData((PVOID)GdtBase, VMCS_GUEST_LDTR_SELECTOR, SegmentSelector.Selector);

    // TR
    SegmentSelector.Selector = AsmGetTr();
    HvFillGuestSelectorData((PVOID)GdtBase, VMCS_GUEST_TR_SELECTOR, SegmentSelector.Selector);

    // 1.2 配置控制寄存器
    __vmx_vmwrite(VMCS_GUEST_CR0, __readcr0());
    __vmx_vmwrite(VMCS_GUEST_CR3, __readcr3());
    __vmx_vmwrite(VMCS_GUEST_CR4, __readcr4());
    __vmx_vmwrite(VMCS_GUEST_DR7, 0x400);  // 调试寄存器

    // 1.3 配置描述符表
    __vmx_vmwrite(VMCS_GUEST_GDTR_BASE, AsmGetGdtBase());
    __vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, AsmGetGdtLimit());
    __vmx_vmwrite(VMCS_GUEST_IDTR_BASE, AsmGetIdtBase());
    __vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, AsmGetIdtLimit());

    // 1.4 配置RIP/RSP/RFLAGS
    __vmx_vmwrite(VMCS_GUEST_RIP, (UINT64)AsmVmxRestoreState);  // Guest返回点
    __vmx_vmwrite(VMCS_GUEST_RSP, (UINT64)GuestStack);
    __vmx_vmwrite(VMCS_GUEST_RFLAGS, __readeflags());

    // 1.5 配置MSRs
    __vmx_vmwrite(VMCS_GUEST_DEBUGCTL, __readmsr(IA32_DEBUGCTL));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_CS, __readmsr(IA32_SYSENTER_CS));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
    __vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));

    // 1.6 配置活动状态
    __vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, 0);  // Active
    __vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY_STATE, 0);

    // ============================================
    // 2. Host State Area 配置
    // ============================================

    // 2.1 Host段选择器
    __vmx_vmwrite(VMCS_HOST_ES_SELECTOR, AsmGetEs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_CS_SELECTOR, AsmGetCs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_SS_SELECTOR, AsmGetSs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_DS_SELECTOR, AsmGetDs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_FS_SELECTOR, AsmGetFs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_GS_SELECTOR, AsmGetGs() & 0xF8);
    __vmx_vmwrite(VMCS_HOST_TR_SELECTOR, AsmGetTr() & 0xF8);

    // 2.2 Host控制寄存器
    __vmx_vmwrite(VMCS_HOST_CR0, __readcr0());
    __vmx_vmwrite(VMCS_HOST_CR3, __readcr3());
    __vmx_vmwrite(VMCS_HOST_CR4, __readcr4());

    // 2.3 Host描述符表
    __vmx_vmwrite(VMCS_HOST_GDTR_BASE, AsmGetGdtBase());
    __vmx_vmwrite(VMCS_HOST_IDTR_BASE, AsmGetIdtBase());

    // 2.4 Host RIP/RSP - VM-Exit处理函数
    __vmx_vmwrite(VMCS_HOST_RIP, (UINT64)AsmVmexitHandler);
    __vmx_vmwrite(VMCS_HOST_RSP, (UINT64)VCpu->VmmStack + VMM_STACK_SIZE - 1);

    // 2.5 Host MSRs
    __vmx_vmwrite(VMCS_HOST_SYSENTER_CS, __readmsr(IA32_SYSENTER_CS));
    __vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
    __vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));

    // ============================================
    // 3. VM-Execution Control Fields 配置
    // ============================================

    // 3.1 Pin-Based Controls
    UINT32 PinBasedControls = 0;
    PinBasedControls |= PIN_BASED_VM_EXECUTION_CONTROLS_EXTERNAL_INTERRUPT;
    PinBasedControls |= PIN_BASED_VM_EXECUTION_CONTROLS_NMI_EXITING;
    __vmx_vmwrite(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS,
        HvAdjustControls(PinBasedControls, IA32_VMX_PINBASED_CTLS));

    // 3.2 Processor-Based Controls
    UINT32 ProcessorBasedControls = 0;
    ProcessorBasedControls |= CPU_BASED_USE_TSC_OFFSETTING;
    ProcessorBasedControls |= CPU_BASED_HLT_EXITING;
    ProcessorBasedControls |= CPU_BASED_INVLPG_EXITING;
    ProcessorBasedControls |= CPU_BASED_CR3_LOAD_EXITING;
    ProcessorBasedControls |= CPU_BASED_CR3_STORE_EXITING;
    ProcessorBasedControls |= CPU_BASED_ACTIVATE_MSR_BITMAP;
    ProcessorBasedControls |= CPU_BASED_ACTIVATE_SECONDARY_CONTROLS;
    __vmx_vmwrite(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
        HvAdjustControls(ProcessorBasedControls, IA32_VMX_PROCBASED_CTLS));

    // 3.3 Secondary Processor-Based Controls
    UINT32 SecondaryControls = 0;
    SecondaryControls |= CPU_BASED_CTL2_ENABLE_EPT;
    SecondaryControls |= CPU_BASED_CTL2_ENABLE_RDTSCP;
    SecondaryControls |= CPU_BASED_CTL2_ENABLE_VPID;
    SecondaryControls |= CPU_BASED_CTL2_ENABLE_INVPCID;
    SecondaryControls |= CPU_BASED_CTL2_ENABLE_XSAVES_XRSTORS;
    __vmx_vmwrite(VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS,
        HvAdjustControls(SecondaryControls, IA32_VMX_PROCBASED_CTLS2));

    // 3.4 异常位图 - 初始为0，后续动态设置
    __vmx_vmwrite(VMCS_CTRL_EXCEPTION_BITMAP, 0);

    // 3.5 MSR位图
    __vmx_vmwrite(VMCS_CTRL_MSR_BITMAP, VCpu->MsrBitmapPhysicalAddress);

    // 3.6 I/O位图
    __vmx_vmwrite(VMCS_CTRL_IO_BITMAP_A, VCpu->IoBitmapPhysicalAddressA);
    __vmx_vmwrite(VMCS_CTRL_IO_BITMAP_B, VCpu->IoBitmapPhysicalAddressB);

    // 3.7 EPT指针
    __vmx_vmwrite(VMCS_CTRL_EPT_POINTER, VCpu->EptPointer.AsUInt);

    // 3.8 VPID
    __vmx_vmwrite(VMCS_CTRL_VPID, VCpu->CoreId + 1);  // VPID不能为0

    // 3.9 TSC Offset
    __vmx_vmwrite(VMCS_CTRL_TSC_OFFSET, 0);

    // ============================================
    // 4. VM-Exit Control Fields 配置
    // ============================================

    UINT32 VmExitControls = 0;
    VmExitControls |= VM_EXIT_HOST_ADDRESS_SPACE_SIZE;  // 64-bit host
    VmExitControls |= VM_EXIT_ACK_INTERRUPT_ON_EXIT;
    __vmx_vmwrite(VMCS_CTRL_VMEXIT_CONTROLS,
        HvAdjustControls(VmExitControls, IA32_VMX_EXIT_CTLS));

    // ============================================
    // 5. VM-Entry Control Fields 配置
    // ============================================

    UINT32 VmEntryControls = 0;
    VmEntryControls |= VM_ENTRY_IA32E_MODE_GUEST;  // 64-bit guest
    __vmx_vmwrite(VMCS_CTRL_VMENTRY_CONTROLS,
        HvAdjustControls(VmEntryControls, IA32_VMX_ENTRY_CTLS));

    LogInfo("VMCS setup completed successfully");

    return TRUE;
}

// ============================================
// 辅助函数：调整控制字段
// ============================================
UINT32 HvAdjustControls(UINT32 Ctl, UINT32 Msr) {
    MSR MsrValue = {0};

    MsrValue.Flags = __readmsr(Msr);
    Ctl &= MsrValue.Fields.High; /* bit == 0 in high word ==> must be zero */
    Ctl |= MsrValue.Fields.Low;  /* bit == 1 in low word  ==> must be one  */
    return Ctl;
}

// ============================================
// 辅助函数：填充Guest段选择器数据
// ============================================
BOOLEAN HvFillGuestSelectorData(PVOID GdtBase, UINT32 SegmentRegister, UINT16 Selector) {
    VMX_SEGMENT_SELECTOR SegmentSelector = {0};

    SegmentGetDescriptor(GdtBase, Selector, &SegmentSelector);

    if (Selector == 0x0) {
        SegmentSelector.Attributes.Unusable = TRUE;
    }

    __vmx_vmwrite(VMCS_GUEST_ES_SELECTOR + SegmentRegister * 2, Selector);
    __vmx_vmwrite(VMCS_GUEST_ES_LIMIT + SegmentRegister * 2, SegmentSelector.Limit);
    __vmx_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS + SegmentRegister * 2,
                  SegmentSelector.Attributes.AsUInt);
    __vmx_vmwrite(VMCS_GUEST_ES_BASE + SegmentRegister * 2, SegmentSelector.Base);

    return TRUE;
}
```

### 1.3.5 VMCS字段访问示例

```c
// ============================================
// 读取Guest状态
// ============================================
VOID ReadGuestState() {
    UINT64 GuestRip, GuestRsp, GuestCr3;

    __vmx_vmread(VMCS_GUEST_RIP, &GuestRip);
    __vmx_vmread(VMCS_GUEST_RSP, &GuestRsp);
    __vmx_vmread(VMCS_GUEST_CR3, &GuestCr3);

    LogInfo("Guest RIP: %llx, RSP: %llx, CR3: %llx",
            GuestRip, GuestRsp, GuestCr3);
}

// ============================================
// 修改Guest RIP（跳过指令）
// ============================================
VOID SkipCurrentInstruction() {
    UINT64 GuestRip;
    UINT32 InstructionLength;

    __vmx_vmread(VMCS_GUEST_RIP, &GuestRip);
    __vmx_vmread(VMCS_EXIT_INSTRUCTION_LENGTH, &InstructionLength);

    __vmx_vmwrite(VMCS_GUEST_RIP, GuestRip + InstructionLength);
}

// ============================================
// 设置异常拦截
// ============================================
VOID SetExceptionBitmap(UINT32 ExceptionVector) {
    UINT32 ExceptionBitmap;

    __vmx_vmread(VMCS_CTRL_EXCEPTION_BITMAP, &ExceptionBitmap);
    ExceptionBitmap |= (1 << ExceptionVector);
    __vmx_vmwrite(VMCS_CTRL_EXCEPTION_BITMAP, ExceptionBitmap);
}
```

---

## 1.4 VM Entry/Exit机制

### 1.4.1 VM-Exit概述

**VM-Exit** 是从VMX Non-root模式（Guest）切换到VMX Root模式（VMM）的过程，是虚拟化技术的核心机制。

#### VM-Exit触发条件分类

1. **无条件VM-Exit指令**（始终触发）
   - CPUID
   - GETSEC
   - INVD
   - XSETBV
   - VMCALL, VMCLEAR, VMLAUNCH, VMPTRLD, VMPTRST, VMRESUME, VMXOFF, VMXON
   - INVEPT, INVVPID

2. **有条件VM-Exit**（根据VMCS配置）
   - CR访问（MOV to/from CR0, CR3, CR4, CR8）
   - MSR访问（RDMSR, WRMSR）
   - I/O指令（IN, OUT, INS, OUTS）
   - RDTSC, RDTSCP
   - HLT, MWAIT, MONITOR
   - INVLPG

3. **异常和中断**
   - 异常（#GP, #PF, #BP, #DB等）- 根据Exception Bitmap
   - 外部中断
   - NMI

4. **EPT违规**
   - EPT Violation（权限不足）
   - EPT Misconfiguration（配置错误）

5. **特殊事件**
   - MTF (Monitor Trap Flag)
   - Triple Fault
   - INIT信号
   - SIPI信号

### 1.4.2 VM-Exit硬件行为

当VM-Exit发生时，CPU自动执行以下操作：

```
1. 保存Guest状态到VMCS
   ├─ Guest通用寄存器保持不变（需要软件保存）
   ├─ Guest RIP -> VMCS_GUEST_RIP
   ├─ Guest RSP -> VMCS_GUEST_RSP
   ├─ Guest RFLAGS -> VMCS_GUEST_RFLAGS
   ├─ Guest段寄存器 -> VMCS相应字段
   └─ Guest控制寄存器 -> VMCS相应字段

2. 记录VM-Exit信息到VMCS
   ├─ Exit Reason -> VMCS_EXIT_REASON
   ├─ Exit Qualification -> VMCS_EXIT_QUALIFICATION
   ├─ Guest Linear Address -> VMCS_GUEST_LINEAR_ADDRESS
   ├─ Guest Physical Address -> VMCS_GUEST_PHYSICAL_ADDRESS
   └─ Instruction Information -> VMCS_EXIT_INSTRUCTION_INFO

3. 加载Host状态从VMCS
   ├─ VMCS_HOST_CR0 -> CR0
   ├─ VMCS_HOST_CR3 -> CR3
   ├─ VMCS_HOST_CR4 -> CR4
   ├─ VMCS_HOST_RIP -> RIP（跳转到VM-Exit handler）
   ├─ VMCS_HOST_RSP -> RSP
   ├─ VMCS_HOST_CS/SS/DS/ES/FS/GS -> 相应段寄存器
   └─ VMCS_HOST_SYSENTER_* -> 相应MSRs

4. 进入VMX Root模式
   └─ CPU现在在VMM的控制下
```

### 1.4.3 VM-Exit处理流程

**文件位置**：`hyperdbg/hyperhv/code/vmm/vmx/Vmexit.c`

#### C语言处理函数

```c
/**
 * @brief VM-Exit handler for different exit reasons
 *
 * @param GuestRegs 通用寄存器（由汇编handler保存）
 * @return BOOLEAN TRUE表示执行了VMXOFF，FALSE表示继续虚拟化
 */
BOOLEAN VmxVmexitHandler(_Inout_ PGUEST_REGS GuestRegs) {
    UINT32                  ExitReason = 0;
    BOOLEAN                 Result     = FALSE;
    VIRTUAL_MACHINE_STATE * VCpu       = NULL;

    // ============================================
    // 1. 获取当前CPU核心的虚拟机状态
    // ============================================
    VCpu = &g_GuestState[KeGetCurrentProcessorNumberEx(NULL)];

    // ============================================
    // 2. 保存Guest寄存器状态
    // ============================================
    // 通用寄存器（由汇编代码保存到栈上）
    VCpu->Regs = GuestRegs;

    // XMM寄存器（紧跟在通用寄存器后面）
    VCpu->XmmRegs = (GUEST_XMM_REGS *)(((CHAR *)GuestRegs) + sizeof(GUEST_REGS));

    // ============================================
    // 3. 标记进入VMX Root模式
    // ============================================
    VCpu->IsOnVmxRootMode = TRUE;

    // ============================================
    // 4. 读取VM-Exit原因
    // ============================================
    VmxVmread32P(VMCS_EXIT_REASON, &ExitReason);
    ExitReason &= 0xffff;  // 只取低16位

    // 保存Exit Reason
    VCpu->ExitReason = ExitReason;

    // ============================================
    // 5. 设置默认行为：增加RIP
    // ============================================
    VCpu->IncrementRip = TRUE;

    // ============================================
    // 6. 读取Guest状态信息
    // ============================================
    // 保存当前RIP
    __vmx_vmread(VMCS_GUEST_RIP, &VCpu->LastVmexitRip);

    // 读取RSP到寄存器结构
    __vmx_vmread(VMCS_GUEST_RSP, &VCpu->Regs->rsp);

    // 读取Exit Qualification（提供额外信息）
    VmxVmread32P(VMCS_EXIT_QUALIFICATION, &VCpu->ExitQualification);

    // ============================================
    // 7. 根据Exit Reason分派处理
    // ============================================
    switch (ExitReason) {

        // --------------------------------------------
        // 三重故障处理
        // --------------------------------------------
        case VMX_EXIT_REASON_TRIPLE_FAULT:
        {
            VmxHandleTripleFaults(VCpu);
            break;
        }

        // --------------------------------------------
        // 无条件VM-Exit指令
        // --------------------------------------------
        case VMX_EXIT_REASON_EXECUTE_VMCLEAR:
        case VMX_EXIT_REASON_EXECUTE_VMPTRLD:
        case VMX_EXIT_REASON_EXECUTE_VMPTRST:
        case VMX_EXIT_REASON_EXECUTE_VMREAD:
        case VMX_EXIT_REASON_EXECUTE_VMRESUME:
        case VMX_EXIT_REASON_EXECUTE_VMWRITE:
        case VMX_EXIT_REASON_EXECUTE_VMXOFF:
        case VMX_EXIT_REASON_EXECUTE_VMXON:
        case VMX_EXIT_REASON_EXECUTE_VMLAUNCH:
        case VMX_EXIT_REASON_EXECUTE_INVEPT:
        case VMX_EXIT_REASON_EXECUTE_INVVPID:
        case VMX_EXIT_REASON_EXECUTE_GETSEC:
        case VMX_EXIT_REASON_EXECUTE_INVD:
        {
            // 这些指令在Guest中不应该执行
            // 注入#UD（未定义指令异常）
            EventInjectUndefinedOpcode(VCpu);
            break;
        }

        // --------------------------------------------
        // 控制寄存器访问
        // --------------------------------------------
        case VMX_EXIT_REASON_MOV_CR:
        {
            // 处理CR0, CR3, CR4, CR8的访问
            DispatchEventMovToFromControlRegisters(VCpu);
            break;
        }

        // --------------------------------------------
        // MSR读写
        // --------------------------------------------
        case VMX_EXIT_REASON_EXECUTE_RDMSR:
        {
            DispatchEventRdmsr(VCpu);
            break;
        }

        case VMX_EXIT_REASON_EXECUTE_WRMSR:
        {
            DispatchEventWrmsr(VCpu);
            break;
        }

        // --------------------------------------------
        // CPUID指令
        // --------------------------------------------
        case VMX_EXIT_REASON_EXECUTE_CPUID:
        {
            DispatchEventCpuid(VCpu);
            break;
        }

        // --------------------------------------------
        // I/O指令
        // --------------------------------------------
        case VMX_EXIT_REASON_EXECUTE_IO_INSTRUCTION:
        {
            DispatchEventIO(VCpu);
            break;
        }

        // --------------------------------------------
        // EPT Violation（EPT页面访问违规）
        // --------------------------------------------
        case VMX_EXIT_REASON_EPT_VIOLATION:
        {
            if (EptHandleEptViolation(VCpu) == FALSE) {
                LogError("Error in handling EPT violation");
            }
            break;
        }

        // --------------------------------------------
        // EPT Misconfiguration（EPT配置错误）
        // --------------------------------------------
        case VMX_EXIT_REASON_EPT_MISCONFIGURATION:
        {
            LogError("EPT Misconfiguration!");
            LogError("Guest Physical Address: %llx",
                VCpu->ExitQualification);

            // 这是严重错误，通常需要终止虚拟化
            Result = TRUE;
            break;
        }

        // --------------------------------------------
        // VMCALL（Guest主动调用VMM）
        // --------------------------------------------
        case VMX_EXIT_REASON_EXECUTE_VMCALL:
        {
            Result = VmxHandleVmcall(VCpu);
            break;
        }

        // --------------------------------------------
        // 异常或NMI
        // --------------------------------------------
        case VMX_EXIT_REASON_EXCEPTION_OR_NMI:
        {
            IdtEmulationHandleExceptionAndNmi(VCpu);
            break;
        }

        // --------------------------------------------
        // 外部中断
        // --------------------------------------------
        case VMX_EXIT_REASON_EXTERNAL_INTERRUPT:
        {
            IdtEmulationHandleExternalInterrupt(VCpu);
            break;
        }

        // --------------------------------------------
        // 中断窗口
        // --------------------------------------------
        case VMX_EXIT_REASON_INTERRUPT_WINDOW:
        {
            IdtEmulationHandleInterruptWindow(VCpu);
            break;
        }

        // --------------------------------------------
        // NMI窗口
        // --------------------------------------------
        case VMX_EXIT_REASON_NMI_WINDOW:
        {
            IdtEmulationHandleNmiWindow(VCpu);
            break;
        }

        // --------------------------------------------
        // Monitor Trap Flag（单步执行）
        // --------------------------------------------
        case VMX_EXIT_REASON_MONITOR_TRAP_FLAG:
        {
            MtfHandleVmexit(VCpu);
            break;
        }

        // --------------------------------------------
        // HLT指令
        // --------------------------------------------
        case VMX_EXIT_REASON_EXECUTE_HLT:
        {
            // 直接让它继续执行
            break;
        }

        // --------------------------------------------
        // XSETBV指令
        // --------------------------------------------
        case VMX_EXIT_REASON_EXECUTE_XSETBV:
        {
            VmxHandleXsetbv(VCpu);
            break;
        }

        // --------------------------------------------
        // RDTSC/RDTSCP
        // --------------------------------------------
        case VMX_EXIT_REASON_EXECUTE_RDTSC:
        {
            DispatchEventTsc(VCpu, FALSE);
            break;
        }

        case VMX_EXIT_REASON_EXECUTE_RDTSCP:
        {
            DispatchEventTsc(VCpu, TRUE);
            break;
        }

        // --------------------------------------------
        // RDPMC
        // --------------------------------------------
        case VMX_EXIT_REASON_EXECUTE_RDPMC:
        {
            DispatchEventPmc(VCpu);
            break;
        }

        // --------------------------------------------
        // INVLPG
        // --------------------------------------------
        case VMX_EXIT_REASON_EXECUTE_INVLPG:
        {
            // 使TLB项无效
            break;
        }

        // --------------------------------------------
        // VMX抢占式计时器
        // --------------------------------------------
        case VMX_EXIT_REASON_VMX_PREEMPTION_TIMER_EXPIRED:
        {
            VmxHandleVmxPreemptionTimerVmexit(VCpu);
            break;
        }

        // --------------------------------------------
        // 其他未处理的Exit Reason
        // --------------------------------------------
        default:
        {
            LogError("Unknown VM-Exit Reason: %x", ExitReason);
            LogError("Exit Qualification: %llx", VCpu->ExitQualification);
            Result = TRUE;  // 终止虚拟化
            break;
        }
    }

    // ============================================
    // 8. 处理RIP增量
    // ============================================
    if (!Result && VCpu->IncrementRip) {
        // 增加RIP，跳过导致VM-Exit的指令
        VCpu->LastVmexitRip += VmxGetInstructionLength();
        __vmx_vmwrite(VMCS_GUEST_RIP, VCpu->LastVmexitRip);
    }

    // ============================================
    // 9. 标记离开VMX Root模式
    // ============================================
    VCpu->IsOnVmxRootMode = FALSE;

    // ============================================
    // 10. 返回结果
    // ============================================
    // TRUE: 执行了VMXOFF，需要恢复到非虚拟化状态
    // FALSE: 继续虚拟化，将执行VMRESUME
    return Result;
}

// ============================================
// 辅助函数：获取指令长度
// ============================================
UINT32 VmxGetInstructionLength() {
    UINT32 Length = 0;
    __vmx_vmread(VMCS_EXIT_INSTRUCTION_LENGTH, &Length);
    return Length;
}
```

#### 汇编处理函数

**文件位置**：`hyperdbg/hyperhv/code/assembly/AsmVmexitHandler.asm`

```asm
; ============================================
; VM-Exit汇编处理函数
; ============================================
AsmVmexitHandler PROC

    ; --------------------------------------------
    ; 1. 保存Guest RFLAGS
    ; --------------------------------------------
    pushfq

    ; --------------------------------------------
    ; 2. 保存XMM寄存器（用于浮点/SIMD运算）
    ; --------------------------------------------
    sub rsp, 60h                          ; 为6个XMM寄存器分配空间
    movaps xmmword ptr [rsp], xmm0
    movaps xmmword ptr [rsp+10h], xmm1
    movaps xmmword ptr [rsp+20h], xmm2
    movaps xmmword ptr [rsp+30h], xmm3
    movaps xmmword ptr [rsp+40h], xmm4
    movaps xmmword ptr [rsp+50h], xmm5

    ; --------------------------------------------
    ; 3. 保存所有通用寄存器
    ; --------------------------------------------
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    push rbp                              ; 占位符（RSP会从VMCS读取）
    push rbx
    push rdx
    push rcx
    push rax

    ; 此时栈布局：
    ; [rsp+00h] = RAX
    ; [rsp+08h] = RCX
    ; [rsp+10h] = RDX
    ; [rsp+18h] = RBX
    ; [rsp+20h] = RSP (占位)
    ; [rsp+28h] = RBP
    ; [rsp+30h] = RSI
    ; [rsp+38h] = RDI
    ; [rsp+40h] = R8
    ; [rsp+48h] = R9
    ; [rsp+50h] = R10
    ; [rsp+58h] = R11
    ; [rsp+60h] = R12
    ; [rsp+68h] = R13
    ; [rsp+70h] = R14
    ; [rsp+78h] = R15
    ; [rsp+80h] = XMM0
    ; [rsp+90h] = XMM1
    ; ...
    ; [rsp+D0h] = RFLAGS

    ; --------------------------------------------
    ; 4. 调用C处理函数
    ; --------------------------------------------
    mov rcx, rsp                          ; 第一个参数：PGUEST_REGS
    sub rsp, 028h                         ; 栈对齐 + Shadow Space (Windows x64调用约定)

    call VmxVmexitHandler                 ; 调用C函数

    add rsp, 028h                         ; 清理栈

    ; --------------------------------------------
    ; 5. 检查返回值
    ; --------------------------------------------
    cmp al, 1                             ; TRUE表示执行了VMXOFF
    je AsmVmxoffHandler                   ; 跳转到VMXOFF处理

    ; --------------------------------------------
    ; 6. 恢复Guest状态并VMRESUME
    ; --------------------------------------------
RestoreState:
    pop rax
    pop rcx
    pop rdx
    pop rbx
    pop rbp                               ; 跳过RSP占位符
    pop rbp                               ; 恢复真正的RBP
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; 恢复XMM寄存器
    movaps xmm0, xmmword ptr [rsp]
    movaps xmm1, xmmword ptr [rsp+10h]
    movaps xmm2, xmmword ptr [rsp+20h]
    movaps xmm3, xmmword ptr [rsp+30h]
    movaps xmm4, xmmword ptr [rsp+40h]
    movaps xmm5, xmmword ptr [rsp+50h]
    add rsp, 60h

    ; 恢复RFLAGS
    popfq

    ; --------------------------------------------
    ; 7. VMRESUME返回Guest
    ; --------------------------------------------
    vmresume

    ; 如果VMRESUME失败，会继续执行到这里
    ; 这种情况不应该发生
    jmp VmresumeError

; ============================================
; VMXOFF处理（终止虚拟化）
; ============================================
AsmVmxoffHandler:
    ; 恢复寄存器
    pop rax
    pop rcx
    pop rdx
    pop rbx
    pop rbp
    pop rbp
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; 恢复XMM
    movaps xmm0, xmmword ptr [rsp]
    movaps xmm1, xmmword ptr [rsp+10h]
    movaps xmm2, xmmword ptr [rsp+20h]
    movaps xmm3, xmmword ptr [rsp+30h]
    movaps xmm4, xmmword ptr [rsp+40h]
    movaps xmm5, xmmword ptr [rsp+50h]
    add rsp, 60h

    ; 恢复RFLAGS
    popfq

    ; 执行VMXOFF
    vmxoff

    ; 跳转到恢复点
    jmp RestoreState

; ============================================
; 错误处理
; ============================================
VmresumeError:
    ; VMRESUME失败，这是严重错误
    ; 通常需要调试
    int 3                                 ; 触发断点
    ret

AsmVmexitHandler ENDP
```

### 1.4.4 VM-Entry机制

**VM-Entry** 是从VMX Root模式（VMM）返回到VMX Non-root模式（Guest）的过程。

#### VM-Entry类型

1. **VMLAUNCH**：首次启动虚拟机
   - 只能执行一次
   - VMCS必须是"clear"状态
   - 成功后VMCS变为"launched"状态

2. **VMRESUME**：恢复虚拟机执行
   - 用于VM-Exit后返回
   - VMCS必须是"launched"状态
   - 这是最常用的方式

#### VM-Entry硬件行为

```
1. 检查VMCS状态
   ├─ VMLAUNCH：检查VMCS是否clear
   └─ VMRESUME：检查VMCS是否launched

2. 保存Host状态（部分寄存器）
   └─ Host的某些状态保存到VMCS

3. 从VMCS加载Guest状态
   ├─ VMCS_GUEST_CR0/CR3/CR4 -> CR0/CR3/CR4
   ├─ VMCS_GUEST_RIP -> RIP
   ├─ VMCS_GUEST_RSP -> RSP
   ├─ VMCS_GUEST_RFLAGS -> RFLAGS
   ├─ VMCS_GUEST_CS/SS/DS/ES/FS/GS -> 段寄存器
   └─ VMCS_GUEST_* -> 其他Guest状态

4. 事件注入（如果有）
   ├─ 检查VM-Entry Interruption Information字段
   └─ 如果设置，注入相应的中断/异常

5. 进入VMX Non-root模式
   └─ 从Guest RIP开始执行
```

#### VM-Entry代码示例

```c
// ============================================
// 首次启动虚拟机
// ============================================
BOOLEAN VmxLaunchVm() {
    UINT64 ErrorCode;

    LogInfo("Launching VM...");

    // 执行VMLAUNCH
    __vmx_vmlaunch();

    // 如果执行到这里，说明VMLAUNCH失败
    __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &ErrorCode);
    LogError("VMLAUNCH failed with error code: %llx", ErrorCode);

    return FALSE;
}

// ============================================
// VM-Exit后返回Guest（在汇编中执行）
// ============================================
; 在AsmVmexitHandler中
vmresume                              ; 返回Guest
; 如果执行到下一行，说明VMRESUME失败
jmp VmresumeError
```

### 1.4.5 完整的VM-Exit/Entry循环

```
                    ┌─────────────────────┐
                    │    Guest 执行       │
                    │  (VMX Non-root)     │
                    └──────────┬──────────┘
                               │
                 触发VM-Exit   │
               (CPUID/EPT/等) │
                               ↓
                    ┌─────────────────────┐
                    │  CPU保存Guest状态   │
                    │  到VMCS             │
                    └──────────┬──────────┘
                               │
                               ↓
                    ┌─────────────────────┐
                    │  CPU加载Host状态    │
                    │  从VMCS             │
                    └──────────┬──────────┘
                               │
                               ↓
                    ┌─────────────────────┐
                    │  跳转到Host RIP     │
                    │  (AsmVmexitHandler) │
                    └──────────┬──────────┘
                               │
                               ↓
                    ┌─────────────────────┐
                    │  汇编保存通用寄存器  │
                    │  和XMM寄存器        │
                    └──────────┬──────────┘
                               │
                               ↓
                    ┌─────────────────────┐
                    │  调用C函数          │
                    │  VmxVmexitHandler() │
                    └──────────┬──────────┘
                               │
                               ↓
                    ┌─────────────────────┐
                    │  读取Exit Reason    │
                    │  和Qualification    │
                    └──────────┬──────────┘
                               │
                               ↓
                    ┌─────────────────────┐
                    │  Switch分派处理     │
                    │  根据Exit Reason    │
                    └──────────┬──────────┘
                               │
         ┌─────────────────────┼─────────────────────┐
         │                     │                     │
         ↓                     ↓                     ↓
    ┌────────┐          ┌────────┐            ┌────────┐
    │ CPUID  │          │  EPT   │            │ VMCALL │
    │ 处理   │          │ 处理   │     ...    │ 处理   │
    └───┬────┘          └───┬────┘            └───┬────┘
        │                   │                     │
        └───────────────────┼─────────────────────┘
                            │
                            ↓
                 ┌─────────────────────┐
                 │  更新VMCS字段       │
                 │  (如增加RIP)        │
                 └──────────┬──────────┘
                            │
                            ↓
                 ┌─────────────────────┐
                 │  C函数返回FALSE     │
                 │  (继续虚拟化)       │
                 └──────────┬──────────┘
                            │
                            ↓
                 ┌─────────────────────┐
                 │  汇编恢复寄存器     │
                 └──────────┬──────────┘
                            │
                            ↓
                 ┌─────────────────────┐
                 │  执行VMRESUME       │
                 └──────────┬──────────┘
                            │
                            ↓
                 ┌─────────────────────┐
                 │  CPU保存Host状态    │
                 │  到VMCS             │
                 └──────────┬──────────┘
                            │
                            ↓
                 ┌─────────────────────┐
                 │  CPU加载Guest状态   │
                 │  从VMCS             │
                 └──────────┬──────────┘
                            │
                            ↓
                 ┌─────────────────────┐
                 │  Guest继续执行      │
                 │  (从Guest RIP)      │
                 └─────────────────────┘
```

---

## 1.5 EPT（扩展页表）

### 1.5.1 EPT基本概念

**EPT (Extended Page Tables)** 是Intel VT-x提供的硬件辅助内存虚拟化技术，也称为Second Level Address Translation (SLAT)。

#### 地址转换层级

没有EPT时：
```
Guest Virtual Address (GVA)
        ↓
   [Guest页表 - 软件模拟]
        ↓
Guest Physical Address (GPA) = Host Physical Address (HPA)
```

有EPT时：
```
Guest Virtual Address (GVA)
        ↓
   [Guest页表 - 硬件执行]
        ↓
Guest Physical Address (GPA)
        ↓
   [EPT页表 - 硬件执行]
        ↓
Host Physical Address (HPA)
```

**关键优势**：
- Guest页表由硬件直接遍历，无需VM-Exit
- 只有EPT Violation才触发VM-Exit
- 大幅减少VM-Exit次数，提升性能

### 1.5.2 EPT页表结构

EPT使用4级页表结构（类似x64分页）：

```
EPT Paging Structure (4-Level):

Level 4 (PML4) - 512 entries, each covers 512GB
    ├─ PML4E[0] -> PML3 Table 0
    ├─ PML4E[1] -> PML3 Table 1
    ├─ ...
    └─ PML4E[511] -> PML3 Table 511

Level 3 (PML3/PDPT) - 512 entries, each covers 1GB
    ├─ PML3E[0] -> PML2 Table 0 (or 1GB page)
    ├─ PML3E[1] -> PML2 Table 1 (or 1GB page)
    ├─ ...
    └─ PML3E[511] -> PML2 Table 511 (or 1GB page)

Level 2 (PML2/PD) - 512 entries, each covers 2MB
    ├─ PML2E[0] -> PML1 Table 0 (or 2MB page)
    ├─ PML2E[1] -> PML1 Table 1 (or 2MB page)
    ├─ ...
    └─ PML2E[511] -> PML1 Table 511 (or 2MB page)

Level 1 (PML1/PT) - 512 entries, each covers 4KB
    ├─ PML1E[0] -> 4KB Physical Page 0
    ├─ PML1E[1] -> 4KB Physical Page 1
    ├─ ...
    └─ PML1E[511] -> 4KB Physical Page 511
```

#### 地址拆分

一个64位Guest Physical Address的拆分：

```
63        48 47     39 38     30 29     21 20     12 11         0
┌──────────┬─────────┬─────────┬─────────┬─────────┬────────────┐
│  Reserved│ PML4 Idx│ PML3 Idx│ PML2 Idx│ PML1 Idx│   Offset   │
└──────────┴─────────┴─────────┴─────────┴─────────┴────────────┘
            9 bits    9 bits    9 bits    9 bits    12 bits
```

### 1.5.3 EPT页表项结构

**文件位置**：`hyperdbg/hyperhv/header/vmm/ept/Ept.h`

```c
// ============================================
// EPT地址索引宏
// ============================================
#define ADDRMASK_EPT_PML4_INDEX(_VAR_)  (((_VAR_) & 0xFF8000000000ULL) >> 39)
#define ADDRMASK_EPT_PML3_INDEX(_VAR_)  (((_VAR_) & 0x7FC0000000ULL) >> 30)
#define ADDRMASK_EPT_PML2_INDEX(_VAR_)  (((_VAR_) & 0x3FE00000ULL) >> 21)
#define ADDRMASK_EPT_PML1_INDEX(_VAR_)  (((_VAR_) & 0x1FF000ULL) >> 12)
#define ADDRMASK_EPT_PML1_OFFSET(_VAR_) ((_VAR_) & 0xFFFULL)

// ============================================
// 页面大小定义
// ============================================
#define SIZE_2_MB       ((SIZE_T)(512 * PAGE_SIZE))      // 2MB
#define SIZE_1_GB       ((SIZE_T)(512 * SIZE_2_MB))      // 1GB
#define SIZE_512_GB     ((SIZE_T)(512 * SIZE_1_GB))      // 512GB

// ============================================
// EPT PML4 Entry (Level 4)
// ============================================
typedef union _EPT_PML4_POINTER {
    UINT64 AsUInt;
    struct {
        UINT64 ReadAccess : 1;              // bit 0
        UINT64 WriteAccess : 1;             // bit 1
        UINT64 ExecuteAccess : 1;           // bit 2
        UINT64 Reserved1 : 5;               // bits 3-7
        UINT64 Accessed : 1;                // bit 8
        UINT64 Reserved2 : 3;               // bits 9-11
        UINT64 PageFrameNumber : 40;        // bits 12-51 (PML3 table物理地址)
        UINT64 Reserved3 : 12;              // bits 52-63
    } Fields;
} EPT_PML4_POINTER, *PEPT_PML4_POINTER;

// ============================================
// EPT PML3 Entry (Level 3 - PDPT)
// ============================================
typedef union _EPT_PML3_POINTER {
    UINT64 AsUInt;
    struct {
        UINT64 ReadAccess : 1;              // bit 0
        UINT64 WriteAccess : 1;             // bit 1
        UINT64 ExecuteAccess : 1;           // bit 2
        UINT64 Reserved1 : 5;               // bits 3-7
        UINT64 Accessed : 1;                // bit 8
        UINT64 Reserved2 : 3;               // bits 9-11
        UINT64 PageFrameNumber : 40;        // bits 12-51 (PML2 table物理地址)
        UINT64 Reserved3 : 12;              // bits 52-63
    } Fields;
} EPT_PML3_POINTER, *PEPT_PML3_POINTER;

// EPT PML3 Entry (1GB Large Page)
typedef union _EPT_PML3_ENTRY {
    UINT64 AsUInt;
    struct {
        UINT64 ReadAccess : 1;              // bit 0
        UINT64 WriteAccess : 1;             // bit 1
        UINT64 ExecuteAccess : 1;           // bit 2
        UINT64 MemoryType : 3;              // bits 3-5 (0=UC, 6=WB)
        UINT64 IgnorePat : 1;               // bit 6
        UINT64 LargePage : 1;               // bit 7 (must be 1 for 1GB page)
        UINT64 Accessed : 1;                // bit 8
        UINT64 Dirty : 1;                   // bit 9
        UINT64 Reserved1 : 2;               // bits 10-11
        UINT64 Reserved2 : 18;              // bits 12-29 (must be 0 for 1GB)
        UINT64 PageFrameNumber : 22;        // bits 30-51 (1GB page物理地址)
        UINT64 Reserved3 : 11;              // bits 52-62
        UINT64 SuppressVe : 1;              // bit 63
    } Fields;
} EPT_PML3_ENTRY, *PEPT_PML3_ENTRY;

// ============================================
// EPT PML2 Entry (Level 2 - PD)
// ============================================
typedef union _EPT_PML2_ENTRY {
    UINT64 AsUInt;
    struct {
        UINT64 ReadAccess : 1;              // bit 0
        UINT64 WriteAccess : 1;             // bit 1
        UINT64 ExecuteAccess : 1;           // bit 2
        UINT64 MemoryType : 3;              // bits 3-5
        UINT64 IgnorePat : 1;               // bit 6
        UINT64 LargePage : 1;               // bit 7 (1=2MB page, 0=PML1 pointer)
        UINT64 Accessed : 1;                // bit 8
        UINT64 Dirty : 1;                   // bit 9
        UINT64 Reserved1 : 2;               // bits 10-11
        UINT64 PageFrameNumber : 40;        // bits 12-51
        UINT64 Reserved2 : 11;              // bits 52-62
        UINT64 SuppressVe : 1;              // bit 63
    } Fields;
} EPT_PML2_ENTRY, *PEPT_PML2_ENTRY;

// ============================================
// EPT PML1 Entry (Level 1 - PT, 4KB Page)
// ============================================
typedef union _EPT_PML1_ENTRY {
    UINT64 AsUInt;
    struct {
        UINT64 ReadAccess : 1;              // bit 0 - 读权限
        UINT64 WriteAccess : 1;             // bit 1 - 写权限
        UINT64 ExecuteAccess : 1;           // bit 2 - 执行权限
        UINT64 MemoryType : 3;              // bits 3-5 - 内存类型
        UINT64 IgnorePat : 1;               // bit 6 - 忽略PAT
        UINT64 Reserved1 : 1;               // bit 7
        UINT64 Accessed : 1;                // bit 8 - 已访问
        UINT64 Dirty : 1;                   // bit 9 - 已修改
        UINT64 UserModeExecute : 1;         // bit 10 - 用户模式执行
        UINT64 Reserved2 : 1;               // bit 11
        UINT64 PageFrameNumber : 40;        // bits 12-51 - 4KB页物理地址
        UINT64 Reserved3 : 11;              // bits 52-62
        UINT64 SuppressVe : 1;              // bit 63 - 抑制#VE
    } Fields;
} EPT_PML1_ENTRY, *PEPT_PML1_ENTRY;

// ============================================
// EPT内存类型
// ============================================
#define MEMORY_TYPE_UNCACHEABLE                 0x00
#define MEMORY_TYPE_WRITE_COMBINING             0x01
#define MEMORY_TYPE_WRITE_THROUGH               0x04
#define MEMORY_TYPE_WRITE_PROTECTED             0x05
#define MEMORY_TYPE_WRITE_BACK                  0x06
#define MEMORY_TYPE_INVALID                     0xFF
```

### 1.5.4 EPT页表结构体

**文件位置**：`hyperdbg/hyperhv/header/common/State.h`

```c
// ============================================
// EPT页表结构
// ============================================
#define VMM_EPT_PML4E_COUNT     512
#define VMM_EPT_PML3E_COUNT     512
#define VMM_EPT_PML2E_COUNT     512
#define VMM_EPT_PML1E_COUNT     512

typedef struct _VMM_EPT_PAGE_TABLE {
    // Level 4 - PML4 (512个表项，每个覆盖512GB)
    DECLSPEC_ALIGN(PAGE_SIZE)
    EPT_PML4_POINTER PML4[VMM_EPT_PML4E_COUNT];

    // Level 3 - PML3 预留区域
    // 除了第一个512GB区域外的其他区域（通常不使用）
    DECLSPEC_ALIGN(PAGE_SIZE)
    EPT_PML3_ENTRY PML3_RSVD[VMM_EPT_PML4E_COUNT - 1][VMM_EPT_PML3E_COUNT];

    // Level 3 - PML3 (512个表项，每个覆盖1GB)
    DECLSPEC_ALIGN(PAGE_SIZE)
    EPT_PML3_POINTER PML3[VMM_EPT_PML3E_COUNT];

    // Level 2 - PML2 (512x512个表项，每个覆盖2MB)
    DECLSPEC_ALIGN(PAGE_SIZE)
    EPT_PML2_ENTRY PML2[VMM_EPT_PML3E_COUNT][VMM_EPT_PML2E_COUNT];

    // Level 1 - PML1动态分配（按需分配，不在这个结构中）
    // 当需要4KB粒度的权限控制时才分配PML1表

} VMM_EPT_PAGE_TABLE, *PVMM_EPT_PAGE_TABLE;

// ============================================
// EPT指针（写入VMCS_CTRL_EPT_POINTER）
// ============================================
typedef union _EPT_POINTER {
    UINT64 AsUInt;
    struct {
        UINT64 MemoryType : 3;              // bits 0-2 (通常是6=WB)
        UINT64 PageWalkLength : 3;          // bits 3-5 (3=4级页表)
        UINT64 EnableAccessAndDirtyFlags : 1; // bit 6
        UINT64 Reserved : 5;                // bits 7-11
        UINT64 PageFrameNumber : 40;        // bits 12-51 (PML4物理地址)
        UINT64 Reserved2 : 12;              // bits 52-63
    } Fields;
} EPT_POINTER, *PEPT_POINTER;
```

### 1.5.5 EPT初始化实现

**文件位置**：`hyperdbg/hyperhv/code/vmm/ept/Ept.c`

```c
// ============================================
// EPT特性检查
// ============================================
BOOLEAN EptCheckFeatures() {
    IA32_VMX_EPT_VPID_CAP_REGISTER VpidRegister = {0};
    IA32_MTRR_DEF_TYPE_REGISTER MTRRDefType = {0};

    // 读取EPT/VPID能力MSR
    VpidRegister.AsUInt = __readmsr(IA32_VMX_EPT_VPID_CAP);

    // 检查必需的EPT特性
    if (!VpidRegister.PageWalkLength4 ||        // 必须支持4级页表
        !VpidRegister.MemoryTypeWriteBack ||    // 必须支持WB内存类型
        !VpidRegister.Pde2MbPages ||            // 必须支持2MB大页
        !VpidRegister.ExecuteOnlyPages ||       // 必须支持只执行页面
        !VpidRegister.Invept ||                 // 必须支持INVEPT
        !VpidRegister.InveptSingleContext ||    // 必须支持单上下文INVEPT
        !VpidRegister.InveptAllContexts) {      // 必须支持全上下文INVEPT

        LogError("Required EPT features not supported");
        return FALSE;
    }

    // 检查MTRR是否启用
    MTRRDefType.AsUInt = __readmsr(IA32_MTRR_DEF_TYPE);
    if (!MTRRDefType.MtrrEnable) {
        LogError("MTRR is not enabled");
        return FALSE;
    }

    LogInfo("EPT features check passed");
    return TRUE;
}

// ============================================
// EPT初始化
// ============================================
BOOLEAN EptInitialize() {
    PVMM_EPT_PAGE_TABLE EptPageTable;
    EPT_POINTER EptPointer = {0};

    // 1. 分配EPT页表结构
    EptPageTable = PlatformMemAllocateZeroedNonPagedPool(sizeof(VMM_EPT_PAGE_TABLE));
    if (EptPageTable == NULL) {
        LogError("Failed to allocate EPT page table");
        return FALSE;
    }

    // 2. 初始化PML4（只使用第一个表项）
    EptPageTable->PML4[0].Fields.ReadAccess = 1;
    EptPageTable->PML4[0].Fields.WriteAccess = 1;
    EptPageTable->PML4[0].Fields.ExecuteAccess = 1;
    EptPageTable->PML4[0].Fields.PageFrameNumber =
        (VirtualAddressToPhysicalAddress(&EptPageTable->PML3[0])) >> 12;

    // 3. 初始化PML3（512个表项，每个指向一个PML2）
    for (SIZE_T i = 0; i < VMM_EPT_PML3E_COUNT; i++) {
        EptPageTable->PML3[i].Fields.ReadAccess = 1;
        EptPageTable->PML3[i].Fields.WriteAccess = 1;
        EptPageTable->PML3[i].Fields.ExecuteAccess = 1;
        EptPageTable->PML3[i].Fields.PageFrameNumber =
            (VirtualAddressToPhysicalAddress(&EptPageTable->PML2[i][0])) >> 12;
    }

    // 4. 初始化PML2（使用2MB大页映射整个物理内存）
    for (SIZE_T i = 0; i < VMM_EPT_PML3E_COUNT; i++) {
        for (SIZE_T j = 0; j < VMM_EPT_PML2E_COUNT; j++) {
            // 设置为2MB大页
            EptPageTable->PML2[i][j].Fields.ReadAccess = 1;
            EptPageTable->PML2[i][j].Fields.WriteAccess = 1;
            EptPageTable->PML2[i][j].Fields.ExecuteAccess = 1;

            // 设置为大页
            EptPageTable->PML2[i][j].Fields.LargePage = 1;

            // 设置内存类型（从MTRR获取）
            UINT64 PhysicalAddress = (i * 512 + j) * SIZE_2_MB;
            EptPageTable->PML2[i][j].Fields.MemoryType =
                EptGetMemoryType(PhysicalAddress);

            // 设置物理页帧号
            EptPageTable->PML2[i][j].Fields.PageFrameNumber =
                PhysicalAddress >> 12;
        }
    }

    // 5. 构建EPT指针（写入VMCS）
    EptPointer.Fields.MemoryType = MEMORY_TYPE_WRITE_BACK;
    EptPointer.Fields.PageWalkLength = 3;  // 4级页表-1
    EptPointer.Fields.EnableAccessAndDirtyFlags = 0;
    EptPointer.Fields.PageFrameNumber =
        (VirtualAddressToPhysicalAddress(&EptPageTable->PML4[0])) >> 12;

    // 6. 保存到全局状态
    g_EptState->EptPageTable = EptPageTable;
    g_EptState->EptPointer.AsUInt = EptPointer.AsUInt;

    LogInfo("EPT initialized successfully");
    LogInfo("EPT Pointer: %llx", EptPointer.AsUInt);

    return TRUE;
}

// ============================================
// 获取内存类型（基于MTRR）
// ============================================
UCHAR EptGetMemoryType(SIZE_T PhysicalAddress) {
    // 读取默认内存类型
    IA32_MTRR_DEF_TYPE_REGISTER MTRRDefType = {0};
    MTRRDefType.AsUInt = __readmsr(IA32_MTRR_DEF_TYPE);

    // 检查可变范围MTRR
    // （简化实现，实际需要检查所有MTRR范围）

    // 大多数内存默认为Write-Back
    return MEMORY_TYPE_WRITE_BACK;
}
```

### 1.5.6 EPT Violation处理

当Guest访问内存违反EPT权限时，触发EPT Violation：

```c
// ============================================
// EPT Violation退出限定符
// ============================================
typedef union _EPT_VIOLATION_EXIT_QUALIFICATION {
    UINT64 AsUInt;
    struct {
        UINT64 DataRead : 1;                // bit 0 - 违规是读访问
        UINT64 DataWrite : 1;               // bit 1 - 违规是写访问
        UINT64 DataExecute : 1;             // bit 2 - 违规是执行访问
        UINT64 EptReadable : 1;             // bit 3 - EPT页可读
        UINT64 EptWriteable : 1;            // bit 4 - EPT页可写
        UINT64 EptExecutable : 1;           // bit 5 - EPT页可执行
        UINT64 EptExecuteForUserMode : 1;   // bit 6
        UINT64 GuestLinearAddressValid : 1; // bit 7
        UINT64 CausedByTranslation : 1;     // bit 8
        UINT64 UserModeLinearAddress : 1;   // bit 9
        UINT64 ReadableWritablePage : 1;    // bit 10
        UINT64 ExecuteDisablePage : 1;      // bit 11
        UINT64 NmiUnblocking : 1;           // bit 12
        UINT64 Reserved : 51;               // bits 13-63
    } Fields;
} EPT_VIOLATION_EXIT_QUALIFICATION;

// ============================================
// EPT Violation处理
// ============================================
BOOLEAN EptHandleEptViolation(VIRTUAL_MACHINE_STATE * VCpu) {
    EPT_VIOLATION_EXIT_QUALIFICATION ViolationQual;
    UINT64 GuestPhysicalAddress;
    UINT64 GuestLinearAddress;

    // 1. 读取违规信息
    ViolationQual.AsUInt = VCpu->ExitQualification;

    // 读取违规的Guest物理地址
    __vmx_vmread(VMCS_GUEST_PHYSICAL_ADDRESS, &GuestPhysicalAddress);

    // 读取Guest线性地址（如果有效）
    if (ViolationQual.Fields.GuestLinearAddressValid) {
        __vmx_vmread(VMCS_GUEST_LINEAR_ADDRESS, &GuestLinearAddress);
    }

    // 2. 判断违规类型
    LogInfo("EPT Violation at GPA: %llx", GuestPhysicalAddress);

    if (ViolationQual.Fields.DataRead) {
        LogInfo("  Type: Read");
    }
    if (ViolationQual.Fields.DataWrite) {
        LogInfo("  Type: Write");
    }
    if (ViolationQual.Fields.DataExecute) {
        LogInfo("  Type: Execute");
    }

    // 3. 检查是否是我们的EPT Hook
    PEPT_HOOKED_PAGE_DETAIL HookedPage =
        EptFindHookedPageByPhysicalAddress(GuestPhysicalAddress);

    if (HookedPage == NULL) {
        // 不是Hook页面，可能是真正的错误
        LogError("EPT Violation for non-hooked page!");
        return FALSE;
    }

    // 4. 处理EPT Hook
    if (ViolationQual.Fields.DataExecute && HookedPage->IsExecuteHook) {
        // 执行Hook - 临时恢复权限并设置MTF
        return EptHandleExecuteViolation(VCpu, HookedPage);
    }
    else if (ViolationQual.Fields.DataWrite && HookedPage->IsWriteHook) {
        // 写Hook
        return EptHandleWriteViolation(VCpu, HookedPage);
    }
    else if (ViolationQual.Fields.DataRead && HookedPage->IsReadHook) {
        // 读Hook
        return EptHandleReadViolation(VCpu, HookedPage);
    }

    return TRUE;
}
```

### 1.5.7 INVEPT指令

修改EPT后必须刷新TLB：

```c
// ============================================
// INVEPT描述符
// ============================================
typedef struct _INVEPT_DESCRIPTOR {
    UINT64 EptPointer;
    UINT64 Reserved;
} INVEPT_DESCRIPTOR, *PINVEPT_DESCRIPTOR;

// ============================================
// INVEPT类型
// ============================================
#define INVEPT_SINGLE_CONTEXT       1
#define INVEPT_ALL_CONTEXTS         2

// ============================================
// 执行INVEPT
// ============================================
UCHAR EptInvept(UINT32 Type, INVEPT_DESCRIPTOR * Descriptor) {
    return AsmInvept(Type, Descriptor);  // 调用汇编实现
}

// 使单个上下文无效
UCHAR EptInveptSingleContext(UINT64 EptPointer) {
    INVEPT_DESCRIPTOR Descriptor = {0};
    Descriptor.EptPointer = EptPointer;
    return EptInvept(INVEPT_SINGLE_CONTEXT, &Descriptor);
}

// 使所有上下文无效
UCHAR EptInveptAllContexts() {
    return EptInvept(INVEPT_ALL_CONTEXTS, NULL);
}
```

**汇编实现**：`hyperdbg/hyperhv/code/assembly/AsmEpt.asm`

```asm
; INVEPT指令执行
AsmInvept PROC
    invept rcx, oword ptr [rdx]
    jz @jz                      ; ZF=1 表示失败
    jc @jc                      ; CF=1 表示失败
    xor rax, rax                ; 成功
    ret

@jz:
    mov rax, 1
    ret

@jc:
    mov rax, 2
    ret
AsmInvept ENDP
```

---

## 本章小结

在本章中，我们学习了Intel VT-x虚拟化技术的核心概念：

1. **VMM（虚拟机监视器）**
   - VMX Root模式和VMX Non-root模式
   - 虚拟机状态结构
   - VMM的职责和作用

2. **VMX操作**
   - VMX指令集（VMXON, VMXOFF, VMLAUNCH, VMRESUME等）
   - VMXON和VMCS区域的分配
   - 虚拟化启动流程

3. **VMCS（虚拟机控制结构）**
   - VMCS的6个逻辑组
   - 重要的VMCS字段
   - VMCS配置实现

4. **VM Entry/Exit机制**
   - VM-Exit的触发条件
   - VM-Exit的硬件行为
   - VM-Exit处理流程（C和汇编）
   - VM-Entry机制

5. **EPT（扩展页表）**
   - EPT的4级页表结构
   - EPT页表项的权限控制
   - EPT初始化实现
   - EPT Violation处理
   - INVEPT指令使用

这些知识是理解HyperDbg项目的基础，在后续章节中，我们将看到这些技术如何被应用于实际的调试器实现中。

---

[下一章：HyperDbg项目架构概览 >>](./第二章-HyperDbg项目架构概览.md)
