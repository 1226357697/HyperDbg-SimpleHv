# 第六章：EPT Hook技术深入

## 6.1 EPT Hook原理

### 6.1.1 什么是EPT Hook？

**EPT Hook** 是利用EPT（扩展页表）的权限控制机制来拦截内存访问的技术。与传统的代码修改Hook不同，EPT Hook不修改目标代码，而是通过修改EPT页表项的权限位来触发拦截。

### 6.1.2 EPT Hook vs 传统Hook

#### 传统Hook（Inline Hook）

```c
// ============================================
// 原始函数
// ============================================
NtCreateFile:
    48 8B 5C 24 08    mov rbx, [rsp+8]
    48 89 74 24 10    mov [rsp+10h], rsi
    57                push rdi
    ...

// ============================================
// 设置Hook（修改代码）
// ============================================
NtCreateFile:
    E9 XX XX XX XX    jmp HookFunction     ; 5字节的跳转指令
    48 89 74 24 10    mov [rsp+10h], rsi  ; 被覆盖的原始代码
    57                push rdi
    ...

// ============================================
// 问题
// ============================================
✅ 优势：
    - 性能好（直接跳转）
    - 实现简单

❌ 劣势：
    - 修改了代码，可被检测
    - Guest读取代码会看到E9 XX XX XX XX
    - 完整性检查会失败（CRC、签名等）
    - 反调试软件容易检测
```

#### EPT Hook

```c
// ============================================
// 原始函数（代码不变）
// ============================================
NtCreateFile:
    48 8B 5C 24 08    mov rbx, [rsp+8]
    48 89 74 24 10    mov [rsp+10h], rsi
    57                push rdi
    ...

// ============================================
// 设置Hook（修改EPT权限）
// ============================================
EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(NtCreateFile);

EptEntry->ReadAccess = 1;     // 仍然可读
EptEntry->WriteAccess = 1;    // 仍然可写
EptEntry->ExecuteAccess = 0;  // 移除执行权限 ← 关键

// ============================================
// Guest行为
// ============================================
Guest读取NtCreateFile的代码：
    mov rax, [NtCreateFile]
    ✅ 成功，看到原始代码：48 8B 5C 24 08
    ✅ 完全隐藏，无法检测

Guest执行NtCreateFile：
    call NtCreateFile
    ↓
    触发EPT Violation（ExecuteAccess = 0）
    ↓
    VM-Exit到VMM
    ✅ 成功拦截

// ============================================
// 优势
// ============================================
✅ 完全隐藏（代码未修改）
✅ 绕过完整性检查
✅ 硬件级别的拦截
✅ 可以实现读/写/执行的任意组合

❌ 劣势：
    - 性能开销（每次触发VM-Exit）
    - 需要硬件支持（Intel VT-x + EPT）
    - 实现复杂（需要MTF配合）
```

---

## 6.2 EPT Hook类型

### 6.2.1 执行Hook（Execute Hook）

**用途**：拦截代码执行，最常用的Hook类型

```c
/**
 * @brief 设置执行Hook
 *
 * @param PhysicalAddress 目标物理地址
 * @return BOOLEAN 成功返回TRUE
 */
BOOLEAN EptHookExecuteOnly(UINT64 PhysicalAddress) {
    PEPT_HOOKED_PAGE_DETAIL HookedPage;
    EPT_PML1_ENTRY * TargetPage;

    LogInfo("Setting execute hook at PA: %llx", PhysicalAddress);

    //
    // ========================================
    // 1. 分配Hook详情结构
    // ========================================
    //
    HookedPage = PlatformMemAllocateZeroedNonPagedPool(
        sizeof(EPT_HOOKED_PAGE_DETAIL)
    );

    if (HookedPage == NULL) {
        LogError("Failed to allocate hook detail");
        return FALSE;
    }

    //
    // ========================================
    // 2. 获取目标页的EPT表项
    // ========================================
    //
    TargetPage = EptGetPml1Entry(g_EptState->EptPageTable, PhysicalAddress);

    if (TargetPage == NULL) {
        LogError("Failed to get EPT entry");
        PlatformMemFreePool(HookedPage);
        return FALSE;
    }

    //
    // ========================================
    // 3. 保存原始EPT表项
    // ========================================
    //
    HookedPage->OriginalEntry = *TargetPage;

    //
    // ========================================
    // 4. 修改EPT权限
    // ========================================
    //
    HookedPage->ChangedEntry = *TargetPage;
    HookedPage->ChangedEntry.ReadAccess = 1;      // 保持可读
    HookedPage->ChangedEntry.WriteAccess = 1;     // 保持可写
    HookedPage->ChangedEntry.ExecuteAccess = 0;   // 移除执行权限 ← 关键

    //
    // ========================================
    // 5. 应用修改
    // ========================================
    //
    *TargetPage = HookedPage->ChangedEntry;

    //
    // ========================================
    // 6. 刷新EPT TLB
    // ========================================
    //
    EptInveptSingleContext(g_EptState->EptPointer.AsUInt);

    //
    // ========================================
    // 7. 保存Hook信息
    // ========================================
    //
    HookedPage->PhysicalAddress = PhysicalAddress;
    HookedPage->VirtualAddress = PhysicalToVirtual(PhysicalAddress);
    HookedPage->IsExecuteHook = TRUE;
    HookedPage->IsReadHook = FALSE;
    HookedPage->IsWriteHook = FALSE;

    // 添加到Hook列表
    InsertHeadList(&g_EptHookList, &HookedPage->PageHookList);

    LogInfo("Execute hook set successfully");

    return TRUE;
}

/**
 * @brief 处理执行违规
 */
BOOLEAN EptHandleExecuteViolation(
    VIRTUAL_MACHINE_STATE * VCpu,
    PEPT_HOOKED_PAGE_DETAIL HookedPage
) {
    UINT64 GuestRip = VCpu->LastVmexitRip;

    LogInfo("Execute hook triggered at RIP: %llx, Target: %llx",
            GuestRip, HookedPage->VirtualAddress);

    //
    // ========================================
    // 1. 记录Hook信息
    // ========================================
    //
    // 在这里可以：
    // - 记录调用栈
    // - 修改寄存器
    // - 统计调用次数
    // - 执行自定义代码

    // 示例：记录调用
    LogInfo("Hooked function called from: %llx", GuestRip);

    //
    // ========================================
    // 2. 临时恢复执行权限
    // ========================================
    //
    EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(
        VCpu->EptPageTable,
        HookedPage->PhysicalAddress
    );

    EptEntry->ExecuteAccess = 1;   // 允许执行
    EptEntry->ReadAccess = 0;      // 移除读权限（可选，增强隐藏性）

    //
    // ========================================
    // 3. 设置MTF - 关键技术
    // ========================================
    //
    VmFuncSetMonitorTrapFlag(TRUE);

    //
    // ========================================
    // 4. 保存恢复点
    // ========================================
    //
    VCpu->MtfEptHookRestorePoint = HookedPage;

    //
    // ========================================
    // 5. 刷新EPT
    // ========================================
    //
    EptInveptSingleContext(VCpu->EptPointer.AsUInt);

    //
    // ========================================
    // 6. 不增加RIP（重新执行指令）
    // ========================================
    //
    VCpu->IncrementRip = FALSE;

    LogInfo("Execute violation handled, MTF set");

    return TRUE;
}
```

### 6.2.2 写Hook（Write Hook）

**用途**：监控内存修改，常用于数据监控

```c
/**
 * @brief 设置写Hook
 *
 * @param PhysicalAddress 目标物理地址
 * @return BOOLEAN 成功返回TRUE
 */
BOOLEAN EptHookWriteOnly(UINT64 PhysicalAddress) {
    PEPT_HOOKED_PAGE_DETAIL HookedPage;
    EPT_PML1_ENTRY * TargetPage;

    LogInfo("Setting write hook at PA: %llx", PhysicalAddress);

    //
    // ========================================
    // 分配并初始化Hook结构
    // ========================================
    //
    HookedPage = PlatformMemAllocateZeroedNonPagedPool(sizeof(EPT_HOOKED_PAGE_DETAIL));

    TargetPage = EptGetPml1Entry(g_EptState->EptPageTable, PhysicalAddress);

    // 保存原始权限
    HookedPage->OriginalEntry = *TargetPage;

    //
    // ========================================
    // 修改EPT权限
    // ========================================
    //
    HookedPage->ChangedEntry = *TargetPage;
    HookedPage->ChangedEntry.ReadAccess = 1;      // 保持可读
    HookedPage->ChangedEntry.WriteAccess = 0;     // 移除写权限 ← 关键
    HookedPage->ChangedEntry.ExecuteAccess = 1;   // 保持可执行

    // 应用修改
    *TargetPage = HookedPage->ChangedEntry;

    // 刷新EPT
    EptInveptSingleContext(g_EptState->EptPointer.AsUInt);

    // 保存信息
    HookedPage->PhysicalAddress = PhysicalAddress;
    HookedPage->IsWriteHook = TRUE;

    InsertHeadList(&g_EptHookList, &HookedPage->PageHookList);

    LogInfo("Write hook set successfully");

    return TRUE;
}

/**
 * @brief 处理写违规
 */
BOOLEAN EptHandleWriteViolation(
    VIRTUAL_MACHINE_STATE * VCpu,
    PEPT_HOOKED_PAGE_DETAIL HookedPage
) {
    UINT64 GuestRip = VCpu->LastVmexitRip;
    UINT64 GuestLinearAddress;

    // 读取被写入的线性地址
    __vmx_vmread(VMCS_GUEST_LINEAR_ADDRESS, &GuestLinearAddress);

    LogInfo("Write hook triggered at RIP: %llx, Target VA: %llx",
            GuestRip, GuestLinearAddress);

    //
    // ========================================
    // 1. 记录写入信息
    // ========================================
    //
    // 可以记录：
    // - 写入的地址
    // - 写入的值（在MTF后读取）
    // - 调用者RIP
    // - 调用栈

    LogInfo("Memory write from: %llx to: %llx",
            GuestRip, GuestLinearAddress);

    //
    // ========================================
    // 2. 临时允许写入
    // ========================================
    //
    EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(
        VCpu->EptPageTable,
        HookedPage->PhysicalAddress
    );

    EptEntry->WriteAccess = 1;   // 允许写入

    //
    // ========================================
    // 3. 设置MTF
    // ========================================
    //
    VmFuncSetMonitorTrapFlag(TRUE);
    VCpu->MtfEptHookRestorePoint = HookedPage;

    //
    // ========================================
    // 4. 刷新EPT并重新执行
    // ========================================
    //
    EptInveptSingleContext(VCpu->EptPointer.AsUInt);
    VCpu->IncrementRip = FALSE;

    return TRUE;
}
```

**应用场景**：
- 监控全局变量修改
- 检测数据结构破坏
- 追踪内存写入
- 实现Copy-on-Write机制

### 6.2.3 读Hook（Read Hook）

**用途**：监控内存读取（较少使用）

```c
/**
 * @brief 设置读Hook
 */
BOOLEAN EptHookReadOnly(UINT64 PhysicalAddress) {
    PEPT_HOOKED_PAGE_DETAIL HookedPage;
    EPT_PML1_ENTRY * TargetPage;

    HookedPage = PlatformMemAllocateZeroedNonPagedPool(sizeof(EPT_HOOKED_PAGE_DETAIL));
    TargetPage = EptGetPml1Entry(g_EptState->EptPageTable, PhysicalAddress);

    // 保存原始
    HookedPage->OriginalEntry = *TargetPage;

    //
    // ========================================
    // 修改权限
    // ========================================
    //
    HookedPage->ChangedEntry = *TargetPage;
    HookedPage->ChangedEntry.ReadAccess = 0;      // 移除读权限 ← 关键
    HookedPage->ChangedEntry.WriteAccess = 1;     // 保持可写
    HookedPage->ChangedEntry.ExecuteAccess = 1;   // 保持可执行

    *TargetPage = HookedPage->ChangedEntry;
    EptInveptSingleContext(g_EptState->EptPointer.AsUInt);

    HookedPage->IsReadHook = TRUE;
    InsertHeadList(&g_EptHookList, &HookedPage->PageHookList);

    return TRUE;
}
```

**注意**：读Hook使用较少，因为：
- 触发频率极高（读取操作非常频繁）
- 性能开销大
- 实用价值有限

### 6.2.4 读写执行组合Hook

可以自由组合不同的权限限制：

| Hook类型 | Read | Write | Execute | 用途 |
|---------|------|-------|---------|------|
| 执行Hook | 1 | 1 | 0 | 拦截函数调用 |
| 写Hook | 1 | 0 | 1 | 监控数据修改 |
| 读Hook | 0 | 1 | 1 | 监控数据读取 |
| 读写Hook | 0 | 0 | 1 | 完全隐藏数据 |
| 全Hook | 0 | 0 | 0 | 完全拦截访问 |

---

## 6.3 高级EPT Hook技术

### 6.3.1 分离视图（Split View）Hook

这是最高级的EPT Hook技术，为读写和执行提供不同的内存视图。

```c
/**
 * @brief 设置分离视图Hook
 *
 * @details
 * 读写视图：指向假页面（包含修改后的内容，如INT3）
 * 执行视图：指向真页面（原始代码）
 */
BOOLEAN EptHookSplitView(UINT64 TargetAddress) {
    PEPT_HOOKED_PAGE_DETAIL HookedPage;
    UINT64 OriginalPage, FakePage;
    EPT_PML1_ENTRY * EptEntry;

    //
    // ========================================
    // 1. 分配假页面
    // ========================================
    //
    FakePage = (UINT64)PlatformMemAllocateContiguousZeroedMemory(PAGE_SIZE);

    if (FakePage == NULL) {
        LogError("Failed to allocate fake page");
        return FALSE;
    }

    //
    // ========================================
    // 2. 复制原始页内容到假页面
    // ========================================
    //
    OriginalPage = TargetAddress & ~0xFFF;  // 页对齐

    RtlCopyMemory(
        (PVOID)FakePage,
        (PVOID)OriginalPage,
        PAGE_SIZE
    );

    //
    // ========================================
    // 3. 在假页面中设置Hook
    // ========================================
    //
    // 例如：在目标地址写入INT3断点
    UINT64 Offset = TargetAddress & 0xFFF;
    PBYTE FakePageBytes = (PBYTE)FakePage;

    // 保存原始字节
    HookedPage->OriginalByte[0] = FakePageBytes[Offset];

    // 写入INT3
    FakePageBytes[Offset] = 0xCC;

    LogInfo("Breakpoint set in fake page at offset: %llx", Offset);

    //
    // ========================================
    // 4. 获取EPT表项
    // ========================================
    //
    EptEntry = EptGetPml1Entry(g_EptState->EptPageTable, OriginalPage);

    // 保存原始EPT表项
    HookedPage->OriginalEntry = *EptEntry;

    //
    // ========================================
    // 5. 设置分离视图
    // ========================================
    //
    // 创建两个EPT表项：
    // - 读写访问时：指向假页面（有0xCC）
    // - 执行访问时：指向真页面（无0xCC）

    // 读写表项（指向假页面）
    HookedPage->ChangedEntry = *EptEntry;
    HookedPage->ChangedEntry.ReadAccess = 1;
    HookedPage->ChangedEntry.WriteAccess = 1;
    HookedPage->ChangedEntry.ExecuteAccess = 0;  // 移除执行权限
    HookedPage->ChangedEntry.PageFrameNumber = (PhysicalAddressOf(FakePage)) >> 12;

    // 执行表项（指向真页面）
    HookedPage->ExecuteOnlyEntry = *EptEntry;
    HookedPage->ExecuteOnlyEntry.ReadAccess = 0;
    HookedPage->ExecuteOnlyEntry.WriteAccess = 0;
    HookedPage->ExecuteOnlyEntry.ExecuteAccess = 1;  // 只允许执行
    HookedPage->ExecuteOnlyEntry.PageFrameNumber = OriginalPage >> 12;

    //
    // ========================================
    // 6. 默认应用读写视图
    // ========================================
    //
    *EptEntry = HookedPage->ChangedEntry;  // 指向假页面

    EptInveptSingleContext(g_EptState->EptPointer.AsUInt);

    //
    // ========================================
    // 7. 保存信息
    // ========================================
    //
    HookedPage->PhysicalAddress = OriginalPage;
    HookedPage->FakePageContents = FakePage;
    HookedPage->IsExecuteHook = TRUE;
    HookedPage->IsReadHook = TRUE;
    HookedPage->IsWriteHook = TRUE;

    InsertHeadList(&g_EptHookList, &HookedPage->PageHookList);

    return TRUE;
}

/**
 * @brief 处理分离视图的EPT Violation
 */
BOOLEAN EptHandleSplitViewViolation(
    VIRTUAL_MACHINE_STATE * VCpu,
    PEPT_HOOKED_PAGE_DETAIL HookedPage,
    EPT_VIOLATION_EXIT_QUALIFICATION ViolationQual
) {
    EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(
        VCpu->EptPageTable,
        HookedPage->PhysicalAddress
    );

    if (ViolationQual.DataExecute) {
        //
        // ========================================
        // 执行访问
        // ========================================
        //
        LogInfo("Execute access to split-view page");

        // 切换到执行视图（真页面）
        *EptEntry = HookedPage->ExecuteOnlyEntry;

        // 设置MTF，执行一条指令后切换回读写视图
        VmFuncSetMonitorTrapFlag(TRUE);
        VCpu->MtfEptHookRestorePoint = HookedPage;

        EptInveptSingleContext(VCpu->EptPointer.AsUInt);
        VCpu->IncrementRip = FALSE;

    } else if (ViolationQual.DataRead || ViolationQual.DataWrite) {
        //
        // ========================================
        // 读写访问
        // ========================================
        //
        LogInfo("Read/Write access to split-view page");

        // 切换到读写视图（假页面）
        *EptEntry = HookedPage->ChangedEntry;

        EptInveptSingleContext(VCpu->EptPointer.AsUInt);
        VCpu->IncrementRip = FALSE;
    }

    return TRUE;
}

// ============================================
// Guest行为
// ============================================
Guest读取被Hook的地址：
    mov rax, [HookedAddress]
    ↓
    访问假页面
    ↓
    读取到：0xCC（断点）
    ✅ 看到了"断点"

Guest执行被Hook的地址：
    call HookedAddress
    ↓
    EPT Violation（ExecuteAccess = 0 in 读写视图）
    ↓
    切换到执行视图（真页面）
    ↓
    重新执行
    ↓
    执行原始代码（无0xCC）
    ✅ 成功执行

Guest检测断点：
    BYTE b = *(BYTE*)HookedAddress;
    if (b == 0xCC) {
        // 检测到断点
    }
    ✅ 看到0xCC，但实际执行时没有
    ✅ 完美隐藏
```

---

## 6.4 EPT Hook数据结构

### 6.4.1 Hook详情结构

**文件位置**：`hyperdbg/hyperhv/header/common/State.h`

```c
/**
 * @brief EPT Hook页面详细信息
 */
typedef struct _EPT_HOOKED_PAGE_DETAIL {

    //
    // ========================================
    // 地址信息
    // ========================================
    //
    UINT64 PhysicalAddress;               // 被Hook的物理地址（页对齐）
    UINT64 VirtualAddress;                // 对应的虚拟地址

    //
    // ========================================
    // EPT表项
    // ========================================
    //
    EPT_PML1_ENTRY OriginalEntry;         // 原始EPT表项（用于恢复）
    EPT_PML1_ENTRY ChangedEntry;          // 修改后的EPT表项（当前应用的）
    EPT_PML1_ENTRY ExecuteOnlyEntry;      // 只执行表项（分离视图用）

    //
    // ========================================
    // 假页面
    // ========================================
    //
    UINT64 FakePageContents;              // 假页面的虚拟地址（分离视图用）

    //
    // ========================================
    // Hook类型标志
    // ========================================
    //
    BOOLEAN IsExecuteHook;                // 是否是执行Hook
    BOOLEAN IsReadHook;                   // 是否是读Hook
    BOOLEAN IsWriteHook;                  // 是否是写Hook
    BOOLEAN IsHiddenBreakpoint;           // 是否是隐藏断点

    //
    // ========================================
    // 断点相关（用于隐藏断点）
    // ========================================
    //
    BYTE OriginalByte[MaximumHiddenBreakpointsOnPage];        // 原始字节
    UINT64 BreakpointAddresses[MaximumHiddenBreakpointsOnPage];  // 断点地址列表
    UINT32 CountOfBreakpoints;            // 断点数量

    //
    // ========================================
    // 链表管理
    // ========================================
    //
    LIST_ENTRY PageHookList;              // Hook链表节点

    //
    // ========================================
    // 其他信息
    // ========================================
    //
    PVOID CallerAddress;                  // 设置Hook的调用者
    UINT64 HookTimestamp;                 // Hook设置时间

} EPT_HOOKED_PAGE_DETAIL, *PEPT_HOOKED_PAGE_DETAIL;

// 每页最大隐藏断点数
#define MaximumHiddenBreakpointsOnPage  40
```

### 6.4.2 Hook列表管理

```c
/**
 * @brief 全局Hook列表
 */
LIST_ENTRY g_EptHookList;

/**
 * @brief 初始化Hook列表
 */
VOID EptHookInitializeList() {
    InitializeListHead(&g_EptHookList);
    LogInfo("EPT hook list initialized");
}

/**
 * @brief 根据物理地址查找Hook
 */
PEPT_HOOKED_PAGE_DETAIL EptFindHookedPageByPhysicalAddress(UINT64 PhysicalAddress) {
    PLIST_ENTRY Entry;
    PEPT_HOOKED_PAGE_DETAIL HookedPage;

    // 页对齐
    PhysicalAddress = PhysicalAddress & ~0xFFF;

    //
    // ========================================
    // 遍历Hook列表
    // ========================================
    //
    Entry = g_EptHookList.Flink;

    while (Entry != &g_EptHookList) {
        HookedPage = CONTAINING_RECORD(Entry, EPT_HOOKED_PAGE_DETAIL, PageHookList);

        if (HookedPage->PhysicalAddress == PhysicalAddress) {
            // 找到
            return HookedPage;
        }

        Entry = Entry->Flink;
    }

    // 未找到
    return NULL;
}

/**
 * @brief 根据虚拟地址查找Hook
 */
PEPT_HOOKED_PAGE_DETAIL EptFindHookedPageByVirtualAddress(UINT64 VirtualAddress) {
    PLIST_ENTRY Entry;
    PEPT_HOOKED_PAGE_DETAIL HookedPage;

    // 页对齐
    VirtualAddress = VirtualAddress & ~0xFFF;

    Entry = g_EptHookList.Flink;

    while (Entry != &g_EptHookList) {
        HookedPage = CONTAINING_RECORD(Entry, EPT_HOOKED_PAGE_DETAIL, PageHookList);

        if (HookedPage->VirtualAddress == VirtualAddress) {
            return HookedPage;
        }

        Entry = Entry->Flink;
    }

    return NULL;
}

/**
 * @brief 移除Hook
 */
BOOLEAN EptUnhookPage(PEPT_HOOKED_PAGE_DETAIL HookedPage) {
    EPT_PML1_ENTRY * EptEntry;

    LogInfo("Unhooking page at PA: %llx", HookedPage->PhysicalAddress);

    //
    // ========================================
    // 1. 获取EPT表项
    // ========================================
    //
    EptEntry = EptGetPml1Entry(
        g_EptState->EptPageTable,
        HookedPage->PhysicalAddress
    );

    //
    // ========================================
    // 2. 恢复原始EPT表项
    // ========================================
    //
    *EptEntry = HookedPage->OriginalEntry;

    //
    // ========================================
    // 3. 刷新EPT
    // ========================================
    //
    EptInveptSingleContext(g_EptState->EptPointer.AsUInt);

    //
    // ========================================
    // 4. 释放假页面（如果有）
    // ========================================
    //
    if (HookedPage->FakePageContents != 0) {
        PlatformMemFreePool((PVOID)HookedPage->FakePageContents);
    }

    //
    // ========================================
    // 5. 从列表中移除
    // ========================================
    //
    RemoveEntryList(&HookedPage->PageHookList);

    //
    // ========================================
    // 6. 释放结构
    // ========================================
    //
    PlatformMemFreePool(HookedPage);

    LogInfo("Page unhooked successfully");

    return TRUE;
}

/**
 * @brief 移除所有Hook
 */
VOID EptUnhookAllPages() {
    PLIST_ENTRY Entry, NextEntry;
    PEPT_HOOKED_PAGE_DETAIL HookedPage;

    LogInfo("Unhooking all pages");

    Entry = g_EptHookList.Flink;

    while (Entry != &g_EptHookList) {
        NextEntry = Entry->Flink;  // 保存下一个，因为当前会被删除

        HookedPage = CONTAINING_RECORD(Entry, EPT_HOOKED_PAGE_DETAIL, PageHookList);

        EptUnhookPage(HookedPage);

        Entry = NextEntry;
    }

    LogInfo("All pages unhooked");
}
```

---

## 6.5 EPT Hook与MTF配合

### 6.5.1 为什么需要MTF？

EPT Hook本身只能拦截访问，要实现**透明性**需要MTF：

```
不使用MTF的问题：

设置执行Hook（ExecuteAccess = 0）
    ↓
Guest执行被Hook的函数
    ↓
EPT Violation
    ↓
恢复执行权限（ExecuteAccess = 1）
    ↓
VMRESUME
    ↓
Guest执行函数
    ↓
❌ 问题：执行权限一直是1，Hook失效了！
    ↓
Guest可以多次执行，不再触发Hook
    ↓
❌ Hook不工作

使用MTF的解决方案：

设置执行Hook（ExecuteAccess = 0）
    ↓
Guest执行被Hook的函数
    ↓
EPT Violation
    ↓
恢复执行权限（ExecuteAccess = 1）
    ↓
设置MTF ← 关键
    ↓
VMRESUME
    ↓
Guest执行一条指令
    ↓
MTF触发
    ↓
移除执行权限（ExecuteAccess = 0）← 关键
    ↓
关闭MTF
    ↓
VMRESUME
    ↓
✅ Hook恢复，下次执行会再次触发
```

### 6.5.2 MTF恢复Hook的实现

```c
/**
 * @brief MTF中恢复EPT Hook
 */
BOOLEAN MtfHandleVmexit(VIRTUAL_MACHINE_STATE * VCpu) {

    //
    // ========================================
    // 检查EPT Hook恢复点
    // ========================================
    //
    if (VCpu->MtfEptHookRestorePoint != NULL) {

        PEPT_HOOKED_PAGE_DETAIL HookedPage = VCpu->MtfEptHookRestorePoint;

        LogInfo("Core %d: Restoring EPT hook at PA: %llx",
                VCpu->CoreId, HookedPage->PhysicalAddress);

        //
        // ========================================
        // 1. 获取EPT表项
        // ========================================
        //
        EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(
            VCpu->EptPageTable,
            HookedPage->PhysicalAddress
        );

        //
        // ========================================
        // 2. 恢复权限限制
        // ========================================
        //
        if (HookedPage->IsExecuteHook) {
            // 执行Hook：移除执行权限
            EptEntry->ExecuteAccess = 0;
            EptEntry->ReadAccess = 1;
            EptEntry->WriteAccess = 1;

            LogInfo("Execute hook restored");

        } else if (HookedPage->IsWriteHook) {
            // 写Hook：移除写权限
            EptEntry->WriteAccess = 0;
            EptEntry->ReadAccess = 1;
            EptEntry->ExecuteAccess = 1;

            LogInfo("Write hook restored");

        } else if (HookedPage->IsReadHook) {
            // 读Hook：移除读权限
            EptEntry->ReadAccess = 0;
            EptEntry->WriteAccess = 1;
            EptEntry->ExecuteAccess = 1;

            LogInfo("Read hook restored");
        }

        //
        // ========================================
        // 3. 刷新EPT
        // ========================================
        //
        EptInveptSingleContext(VCpu->EptPointer.AsUInt);

        //
        // ========================================
        // 4. 清除恢复点
        // ========================================
        //
        VCpu->MtfEptHookRestorePoint = NULL;

        LogInfo("EPT hook restoration completed");
    }

    //
    // ========================================
    // 其他MTF处理...
    // ========================================
    //

    // 关闭MTF（除非需要保持）
    if (!VCpu->IgnoreMtfUnset) {
        HvSetMonitorTrapFlag(FALSE);
    }

    return TRUE;
}
```

---

## 6.6 隐藏断点实现

### 6.6.1 传统断点 vs 隐藏断点

```c
// ============================================
// 方法1：传统INT3断点（可检测）
// ============================================
BOOLEAN SetTraditionalBreakpoint(UINT64 Address) {
    BYTE OriginalByte;

    // 读取原始字节
    MemoryMapperReadMemorySafe(Address, &OriginalByte, 1, NULL);

    // 保存原始字节
    SaveOriginalByte(Address, OriginalByte);

    // 写入INT3
    BYTE Int3 = 0xCC;
    MemoryMapperWriteMemorySafe(Address, &Int3, 1, NULL);

    return TRUE;
}

// Guest检测：
BYTE CheckBreakpoint(UINT64 Address) {
    BYTE b = *(BYTE*)Address;
    if (b == 0xCC) {
        printf("Breakpoint detected!\n");  // ❌ 被检测到
        return TRUE;
    }
    return FALSE;
}

// ============================================
// 方法2：EPT隐藏断点（不可检测）
// ============================================
BOOLEAN SetHiddenBreakpoint(UINT64 Address) {
    UINT64 PhysicalAddress = VirtualToPhysical(Address);
    UINT64 PagePhysicalAddress = PhysicalAddress & ~0xFFF;
    UINT64 Offset = PhysicalAddress & 0xFFF;

    // 1. 查找或创建Hook页面
    PEPT_HOOKED_PAGE_DETAIL HookedPage =
        EptFindHookedPageByPhysicalAddress(PagePhysicalAddress);

    if (HookedPage == NULL) {
        // 创建新的Hook页面（使用分离视图）
        HookedPage = EptHookSplitView(PagePhysicalAddress);
    }

    // 2. 在假页面中设置断点
    PBYTE FakePage = (PBYTE)HookedPage->FakePageContents;

    // 保存原始字节
    UINT32 BpIndex = HookedPage->CountOfBreakpoints;
    HookedPage->OriginalByte[BpIndex] = FakePage[Offset];
    HookedPage->BreakpointAddresses[BpIndex] = Address;
    HookedPage->CountOfBreakpoints++;

    // 写入INT3到假页面
    FakePage[Offset] = 0xCC;

    LogInfo("Hidden breakpoint set at: %llx (fake page offset: %llx)",
            Address, Offset);

    return TRUE;
}

// Guest检测：
BYTE CheckBreakpoint(UINT64 Address) {
    BYTE b = *(BYTE*)Address;

    // 读取时访问假页面，看到0xCC
    if (b == 0xCC) {
        printf("Breakpoint detected!\n");  // ✅ 检测到（正常）
        return TRUE;
    }
    return FALSE;
}

// 但是执行时：
call Address
    ↓
    EPT Violation（ExecuteAccess = 0）
    ↓
    切换到执行视图（真页面）
    ↓
    执行原始代码（无0xCC）
    ↓
    ✅ 成功执行，无异常

// 结果：
// - Guest读取时看到断点（正常行为）
// - Guest执行时会触发拦截（VMM处理）
// - 但Guest执行的是原始代码，不会触发#BP异常
// - VMM可以模拟#BP异常或直接暂停
```

### 6.6.2 隐藏断点的EPT Violation处理

```c
/**
 * @brief 处理隐藏断点的执行违规
 */
BOOLEAN EptHandleHiddenBreakpoint(
    VIRTUAL_MACHINE_STATE * VCpu,
    PEPT_HOOKED_PAGE_DETAIL HookedPage
) {
    UINT64 GuestRip = VCpu->LastVmexitRip;
    UINT64 PhysicalAddress;
    UINT32 BpIndex;

    // 转换RIP到物理地址
    PhysicalAddress = VirtualToPhysical(GuestRip);

    LogInfo("Checking for hidden breakpoint at RIP: %llx (PA: %llx)",
            GuestRip, PhysicalAddress);

    //
    // ========================================
    // 1. 检查是否命中断点
    // ========================================
    //
    for (BpIndex = 0; BpIndex < HookedPage->CountOfBreakpoints; BpIndex++) {

        if (HookedPage->BreakpointAddresses[BpIndex] == GuestRip) {
            //
            // 命中隐藏断点！
            //
            LogInfo("Hidden breakpoint hit at: %llx", GuestRip);

            //
            // ========================================
            // 2. 通知调试器（可选）
            // ========================================
            //
            if (g_ReportHiddenBreakpoints) {
                // 调用断点处理回调
                VmmCallbackHandleBreakpointException(VCpu->CoreId);
            }

            //
            // ========================================
            // 3. 决定如何处理
            // ========================================
            //
            // 选项A：注入#BP异常到Guest
            EventInjectBreakpoint();
            VCpu->IncrementRip = FALSE;
            return TRUE;

            // 选项B：暂停调试（如果是调试器断点）
            // KdHandleBreakpointAndDebugBreakpoints(...);

            // 选项C：只记录，继续执行
            // LogInfo("Breakpoint logged");
            // 继续执行...
        }
    }

    //
    // ========================================
    // 4. 不是断点位置，正常处理执行违规
    // ========================================
    //
    LogInfo("Not a breakpoint, handling as normal execute hook");

    return EptHandleExecuteViolation(VCpu, HookedPage);
}
```

---

## 6.7 EPT Hook管理

### 6.7.1 动态PML1分配

为了实现4KB粒度的Hook，需要动态分配PML1表：

```c
/**
 * @brief 拆分2MB大页为4KB小页
 *
 * @param PageTable EPT页表
 * @param Pml2Entry 要拆分的PML2表项
 * @return BOOLEAN 成功返回TRUE
 */
BOOLEAN EptSplitLargePage(
    PVMM_EPT_PAGE_TABLE PageTable,
    PEPT_PML2_ENTRY Pml2Entry
) {
    PEPT_PML1_ENTRY Pml1Table;
    UINT64 BasePhysicalAddress;

    LogInfo("Splitting 2MB large page");

    //
    // ========================================
    // 1. 检查是否是大页
    // ========================================
    //
    if (Pml2Entry->LargePage != 1) {
        LogWarning("PML2 entry is not a large page");
        return FALSE;
    }

    //
    // ========================================
    // 2. 分配PML1表（512个表项 = 4KB）
    // ========================================
    //
    Pml1Table = PlatformMemAllocateZeroedNonPagedPool(PAGE_SIZE);

    if (Pml1Table == NULL) {
        LogError("Failed to allocate PML1 table");
        return FALSE;
    }

    //
    // ========================================
    // 3. 初始化PML1表项
    // ========================================
    //
    BasePhysicalAddress = Pml2Entry->PageFrameNumber << 12;

    for (UINT32 i = 0; i < 512; i++) {
        // 每个PML1表项映射一个4KB页
        Pml1Table[i].ReadAccess = 1;
        Pml1Table[i].WriteAccess = 1;
        Pml1Table[i].ExecuteAccess = 1;
        Pml1Table[i].MemoryType = Pml2Entry->MemoryType;
        Pml1Table[i].PageFrameNumber = (BasePhysicalAddress + i * PAGE_SIZE) >> 12;
    }

    //
    // ========================================
    // 4. 修改PML2表项指向PML1表
    // ========================================
    //
    Pml2Entry->LargePage = 0;  // 不再是大页
    Pml2Entry->PageFrameNumber = (VirtualToPhysical(Pml1Table)) >> 12;

    //
    // ========================================
    // 5. 刷新EPT
    // ========================================
    //
    EptInveptSingleContext(g_EptState->EptPointer.AsUInt);

    LogInfo("2MB page split into 512 4KB pages");

    return TRUE;
}

/**
 * @brief 获取PML1表项（自动拆分大页）
 */
EPT_PML1_ENTRY * EptGetPml1Entry(
    PVMM_EPT_PAGE_TABLE PageTable,
    UINT64 PhysicalAddress
) {
    UINT64 Pml4Index, Pml3Index, Pml2Index, Pml1Index;
    PEPT_PML4_POINTER Pml4Entry;
    PEPT_PML3_POINTER Pml3Entry;
    PEPT_PML2_ENTRY Pml2Entry;
    PEPT_PML1_ENTRY Pml1Table;

    //
    // ========================================
    // 1. 计算索引
    // ========================================
    //
    Pml4Index = ADDRMASK_EPT_PML4_INDEX(PhysicalAddress);
    Pml3Index = ADDRMASK_EPT_PML3_INDEX(PhysicalAddress);
    Pml2Index = ADDRMASK_EPT_PML2_INDEX(PhysicalAddress);
    Pml1Index = ADDRMASK_EPT_PML1_INDEX(PhysicalAddress);

    //
    // ========================================
    // 2. 遍历PML4 -> PML3 -> PML2
    // ========================================
    //
    Pml4Entry = &PageTable->PML4[Pml4Index];
    // ... 省略验证

    Pml3Entry = (PEPT_PML3_POINTER)PhysicalToVirtual(Pml4Entry->PageFrameNumber << 12);
    Pml3Entry = &Pml3Entry[Pml3Index];
    // ... 省略验证

    Pml2Entry = (PEPT_PML2_ENTRY)PhysicalToVirtual(Pml3Entry->PageFrameNumber << 12);
    Pml2Entry = &Pml2Entry[Pml2Index];

    //
    // ========================================
    // 3. 检查PML2是否是大页
    // ========================================
    //
    if (Pml2Entry->LargePage == 1) {
        //
        // 是2MB大页，需要拆分为512个4KB小页
        //
        LogInfo("PML2 is large page, splitting...");

        if (!EptSplitLargePage(PageTable, Pml2Entry)) {
            LogError("Failed to split large page");
            return NULL;
        }
    }

    //
    // ========================================
    // 4. 获取PML1表项
    // ========================================
    //
    Pml1Table = (PEPT_PML1_ENTRY)PhysicalToVirtual(Pml2Entry->PageFrameNumber << 12);

    return &Pml1Table[Pml1Index];
}
```

### 6.7.2 Hook页面的权限切换

```c
/**
 * @brief 在不同的EPT表项之间切换
 */

// ============================================
// 场景：分离视图Hook的权限切换
// ============================================

执行访问时：
    EPT Violation
        ↓
    EptHandleSplitViewViolation()
        ↓
    切换到执行视图：
        *EptEntry = HookedPage->ExecuteOnlyEntry;
        // ExecuteAccess = 1, ReadAccess = 0, WriteAccess = 0
        // PageFrameNumber = 原始页
        ↓
    设置MTF
        ↓
    VMRESUME
        ↓
    Guest执行一条指令（原始代码）
        ↓
    MTF触发
        ↓
    切换回读写视图：
        *EptEntry = HookedPage->ChangedEntry;
        // ExecuteAccess = 0, ReadAccess = 1, WriteAccess = 1
        // PageFrameNumber = 假页面
        ↓
    VMRESUME

读写访问时：
    可以直接访问（ReadAccess = 1, WriteAccess = 1）
    访问的是假页面（有0xCC断点）
```

---

## 6.8 EPT Hook的实际应用

### 6.8.1 API监控

```c
/**
 * @brief 监控NtCreateFile的所有调用
 */
VOID MonitorNtCreateFile() {
    UINT64 NtCreateFileAddress = GetKernelFunctionAddress("NtCreateFile");

    // 设置执行Hook
    EptHookExecuteOnly(VirtualToPhysical(NtCreateFileAddress));

    LogInfo("NtCreateFile monitoring enabled");
}

// EPT Violation处理中
BOOLEAN EptHandleExecuteViolation(...) {
    // 检查是否是NtCreateFile
    if (HookedPage->VirtualAddress == NtCreateFileAddress) {

        // 读取参数（根据调用约定）
        UINT64 FileHandle = VCpu->Regs->rcx;
        UINT64 DesiredAccess = VCpu->Regs->rdx;
        UINT64 ObjectAttributes = VCpu->Regs->r8;
        UINT64 IoStatusBlock = VCpu->Regs->r9;

        // 读取文件名
        UNICODE_STRING FileName;
        ReadObjectAttributes(ObjectAttributes, &FileName);

        // 记录调用
        LogInfo("NtCreateFile called:");
        LogInfo("  File: %wZ", &FileName);
        LogInfo("  Access: %llx", DesiredAccess);
        LogInfo("  Caller: %llx", VCpu->LastVmexitRip);

        // 可以决定是否允许
        if (IsBlacklistedFile(&FileName)) {
            // 拒绝访问：返回错误
            VCpu->Regs->rax = STATUS_ACCESS_DENIED;
            VCpu->IncrementRip = TRUE;  // 跳过call指令
            return TRUE;
        }
    }

    // 正常处理（临时恢复权限 + MTF）
    // ...
}
```

### 6.8.2 反作弊检测

```c
/**
 * @brief 检测游戏作弊（内存修改）
 */
VOID DetectGameCheating() {
    // Hook游戏的关键数据结构
    UINT64 PlayerHealthAddress = 0x12345000;

    // 设置写Hook
    EptHookWriteOnly(VirtualToPhysical(PlayerHealthAddress));

    LogInfo("Player health monitoring enabled");
}

// EPT Violation处理中
BOOLEAN EptHandleWriteViolation(...) {
    if (HookedPage->VirtualAddress == PlayerHealthAddress) {

        UINT64 CallerRip = VCpu->LastVmexitRip;

        // 检查调用者
        if (!IsLegitimateGameCode(CallerRip)) {
            //
            // 检测到作弊！
            //
            LogWarning("Cheat detected! Illegal write from: %llx", CallerRip);

            // 可以：
            // - 拒绝写入
            // - 记录作弊者
            // - 通知反作弊系统
            // - 修改写入的值

            // 拒绝写入（不增加RIP，不允许写入）
            VCpu->IncrementRip = FALSE;
            return TRUE;
        }
    }

    // 正常处理
    // ...
}
```

### 6.8.3 Rootkit隐藏

```c
/**
 * @brief 隐藏Rootkit驱动
 */
VOID HideRootkitDriver() {
    // Hook PsLoadedModuleList（系统驱动列表）
    UINT64 PsLoadedModuleList = GetKernelSymbol("PsLoadedModuleList");

    // 设置读Hook
    EptHookReadOnly(VirtualToPhysical(PsLoadedModuleList));

    LogInfo("Driver list access monitoring enabled");
}

// EPT Violation处理中
BOOLEAN EptHandleReadViolation(...) {
    if (HookedPage->VirtualAddress == PsLoadedModuleList) {

        //
        // 拦截对驱动列表的读取
        //

        // 1. 临时允许读取
        EptEntry->ReadAccess = 1;

        // 2. 设置MTF
        VmFuncSetMonitorTrapFlag(TRUE);

        // 3. 在MTF处理中修改返回的数据
        VCpu->MtfCallback = ModifyDriverList;

        // 4. 继续执行
        EptInveptSingleContext(...);
        VCpu->IncrementRip = FALSE;

        return TRUE;
    }
}

// MTF回调中修改数据
VOID ModifyDriverList(UINT32 CoreId) {
    // Guest已经读取了驱动列表到某个缓冲区
    // 现在修改缓冲区，移除我们的驱动

    // 1. 找到Guest的缓冲区地址（从寄存器或栈）
    // 2. 遍历驱动列表
    // 3. 删除RootkitDriver条目
    // 4. 恢复读Hook

    LogInfo("Driver list modified, rootkit hidden");
}
```

---

## 6.9 EPT Hook的优化技术

### 6.9.1 Hook池管理

```c
/**
 * @brief Hook池，减少内存分配
 */
#define MAX_HOOK_POOL_SIZE  100

typedef struct _EPT_HOOK_POOL {
    EPT_HOOKED_PAGE_DETAIL HookPool[MAX_HOOK_POOL_SIZE];
    UINT32 UsedCount;
    UINT32 FreeList[MAX_HOOK_POOL_SIZE];
} EPT_HOOK_POOL;

EPT_HOOK_POOL g_HookPool;

/**
 * @brief 从池中分配Hook结构
 */
PEPT_HOOKED_PAGE_DETAIL EptAllocateHookFromPool() {
    if (g_HookPool.UsedCount >= MAX_HOOK_POOL_SIZE) {
        LogError("Hook pool exhausted");
        return NULL;
    }

    UINT32 Index = g_HookPool.FreeList[g_HookPool.UsedCount];
    g_HookPool.UsedCount++;

    PEPT_HOOKED_PAGE_DETAIL Hook = &g_HookPool.HookPool[Index];
    RtlZeroMemory(Hook, sizeof(EPT_HOOKED_PAGE_DETAIL));

    LogInfo("Hook allocated from pool, index: %d", Index);

    return Hook;
}

/**
 * @brief 释放Hook回池
 */
VOID EptFreeHookToPool(PEPT_HOOKED_PAGE_DETAIL Hook) {
    UINT32 Index = Hook - g_HookPool.HookPool;

    if (Index >= MAX_HOOK_POOL_SIZE) {
        LogError("Invalid hook pointer");
        return;
    }

    g_HookPool.UsedCount--;
    g_HookPool.FreeList[g_HookPool.UsedCount] = Index;

    LogInfo("Hook freed to pool, index: %d", Index);
}
```

### 6.9.2 批量INVEPT优化

```c
/**
 * @brief 批量设置Hook，最后统一刷新EPT
 */
BOOLEAN EptHookMultiplePages(UINT64 * PhysicalAddresses, UINT32 Count) {

    LogInfo("Setting %d hooks", Count);

    //
    // ========================================
    // 1. 批量设置Hook（不刷新EPT）
    // ========================================
    //
    for (UINT32 i = 0; i < Count; i++) {
        EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(
            g_EptState->EptPageTable,
            PhysicalAddresses[i]
        );

        // 修改权限
        EptEntry->ExecuteAccess = 0;
        EptEntry->ReadAccess = 1;
        EptEntry->WriteAccess = 1;

        // 不刷新EPT（提高效率）
    }

    //
    // ========================================
    // 2. 统一刷新EPT（只刷新一次）
    // ========================================
    //
    EptInveptAllContexts();

    LogInfo("%d hooks set and EPT flushed", Count);

    return TRUE;
}
```

### 6.9.3 条件Hook

```c
/**
 * @brief 条件Hook - 只在特定条件下触发
 */
typedef struct _CONDITIONAL_HOOK {
    EPT_HOOKED_PAGE_DETAIL * Hook;
    BOOLEAN (*Condition)(VIRTUAL_MACHINE_STATE * VCpu);
    VOID (*Action)(VIRTUAL_MACHINE_STATE * VCpu);
} CONDITIONAL_HOOK;

/**
 * @brief 处理条件Hook
 */
BOOLEAN EptHandleConditionalHook(
    VIRTUAL_MACHINE_STATE * VCpu,
    CONDITIONAL_HOOK * CondHook
) {
    //
    // ========================================
    // 1. 检查条件
    // ========================================
    //
    if (!CondHook->Condition(VCpu)) {
        // 条件不满足，直接通过（不触发Hook）
        LogInfo("Condition not met, bypassing hook");

        // 临时恢复权限
        EptEntry->ExecuteAccess = 1;

        // 设置MTF（一条指令后恢复）
        VmFuncSetMonitorTrapFlag(TRUE);
        VCpu->MtfEptHookRestorePoint = CondHook->Hook;

        EptInveptSingleContext(...);
        VCpu->IncrementRip = FALSE;

        return TRUE;
    }

    //
    // ========================================
    // 2. 条件满足，执行Hook动作
    // ========================================
    //
    LogInfo("Condition met, executing hook action");

    CondHook->Action(VCpu);

    // 然后正常处理（临时恢复 + MTF）
    // ...

    return TRUE;
}

// 使用示例：只在特定进程中Hook
BOOLEAN IsTargetProcess(VIRTUAL_MACHINE_STATE * VCpu) {
    UINT64 CurrentCr3;
    __vmx_vmread(VMCS_GUEST_CR3, &CurrentCr3);

    return (CurrentCr3 == g_TargetProcessCr3);
}

VOID LogApiCall(VIRTUAL_MACHINE_STATE * VCpu) {
    LogInfo("API called in target process from: %llx", VCpu->LastVmexitRip);
}

// 设置条件Hook
CONDITIONAL_HOOK ConditionalHook = {
    .Hook = HookedPage,
    .Condition = IsTargetProcess,
    .Action = LogApiCall
};
```

---

## 6.10 EPT Hook vs 其他Hook技术

### 6.10.1 技术对比

| 特性 | EPT Hook | Inline Hook | IAT Hook | SSDT Hook |
|------|----------|-------------|----------|-----------|
| **实现层次** | 硬件（EPT） | 代码修改 | 导入表修改 | 系统表修改 |
| **隐藏性** | ⭐⭐⭐⭐⭐ | ⭐ | ⭐⭐ | ⭐⭐⭐ |
| **性能** | ⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| **实现复杂度** | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐ | ⭐⭐⭐ |
| **硬件要求** | VT-x + EPT | 无 | 无 | 无 |
| **绕过PatchGuard** | ✅ | ❌ | ❌ | ❌ |
| **可检测性** | 极难 | 容易 | 容易 | 中等 |

### 6.10.2 使用场景选择

```c
/**
 * @brief 选择Hook技术的决策树
 */

if (需要绝对隐藏) {
    使用EPT Hook
    // 适用于：反作弊、Rootkit、高级监控
}
else if (性能优先) {
    使用Inline Hook
    // 适用于：性能分析、频繁调用的函数
}
else if (只Hook用户态) {
    使用IAT Hook或Inline Hook
    // 适用于：用户态程序监控
}
else if (Hook系统调用) {
    if (需要隐藏) {
        使用EPT Hook + SYSCALL MSR
    } else {
        使用SSDT Hook  // 简单但已过时
    }
}
```

---

## 6.11 EPT Hook的限制和解决方案

### 6.11.1 性能开销

```c
/**
 * @brief EPT Hook的性能分析
 */

单次EPT Hook触发的开销：

Guest执行被Hook的指令
    ↓ (约50-100 cycles)
EPT权限检查失败
    ↓ (约100 cycles)
触发EPT Violation
    ↓ (约1000 cycles - VM-Exit开销)
VM-Exit到VMM
    ↓ (约500-1000 cycles - 保存寄存器等)
EPT Violation处理
    ↓ (约200-500 cycles)
临时恢复权限 + 设置MTF
    ↓ (约100 cycles)
VMRESUME
    ↓ (约1000 cycles)
Guest执行一条指令
    ↓ (约1-10 cycles)
MTF触发
    ↓ (约1000 cycles - VM-Exit)
MTF处理
    ↓ (约200-500 cycles)
恢复Hook
    ↓ (约100 cycles)
VMRESUME
    ↓ (约1000 cycles)

总开销：约6000-7000 cycles = 3-4微秒 @ 2GHz

对比：
- Inline Hook：约10-20 cycles = 0.005-0.01微秒
- EPT Hook比Inline Hook慢约300-400倍

结论：EPT Hook不适合频繁调用的函数
```

**优化方案**：

```c
/**
 * @brief 优化1：选择性启用
 */
// 只在需要时启用Hook
if (UserEnableMonitoring) {
    EptHookExecuteOnly(TargetFunction);
} else {
    EptUnhookPage(TargetFunction);
}

/**
 * @brief 优化2：采样监控
 */
UINT64 CallCount = 0;

BOOLEAN EptHandleExecuteViolation(...) {
    CallCount++;

    // 只记录每100次调用
    if (CallCount % 100 == 0) {
        LogInfo("Function called %lld times", CallCount);
    }

    // 快速通过
    // ...
}

/**
 * @brief 优化3：使用VMFUNC（如果硬件支持）
 */
// VMFUNC可以在Guest中切换EPT，无需VM-Exit
// 但支持该功能的硬件较少
```

### 6.11.2 TLB一致性问题

```c
/**
 * @brief EPT修改后必须刷新TLB
 */

问题场景：
    修改EPT表项
        ↓
    没有刷新TLB
        ↓
    ❌ CPU可能仍使用旧的TLB缓存
        ↓
    ❌ Hook不生效或行为不一致

解决方案：

// 方案1：单上下文INVEPT（推荐）
EptInveptSingleContext(EptPointer);
// 只刷新当前EPT上下文
// 性能好

// 方案2：全局INVEPT（彻底但慢）
EptInveptAllContexts();
// 刷新所有EPT上下文
// 性能差，但确保一致性

// 方案3：VPID选择性刷新
VpidInvvpidSingleContext(Vpid);
// 只刷新指定VPID的TLB
// 更精细的控制

/**
 * @brief 刷新时机
 */
// ✅ 必须刷新的情况：
// - 修改EPT权限后
// - 修改页帧号后
// - 从Hook列表添加/删除后

// ❌ 不需要刷新的情况：
// - 只读取EPT表项
// - 只修改内存内容（不修改EPT）
```

### 6.11.3 多核一致性

```c
/**
 * @brief 确保所有核心的EPT一致
 */

问题：
    Core 0修改了EPT
        ↓
    Core 0刷新了EPT
        ↓
    Core 1还在使用旧的TLB缓存
        ↓
    ❌ Core 0和Core 1看到不同的内存权限

解决方案1：共享EPT（HyperDbg的方案）
    所有核心共享同一个EPT页表
        ↓
    任何核心修改EPT后
        ↓
    在所有核心上执行INVEPT
        ↓
    ✅ 确保一致性

VOID EptSetHookOnAllCores(UINT64 PhysicalAddress) {
    // 1. 修改共享EPT
    EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(...);
    EptEntry->ExecuteAccess = 0;

    // 2. 在所有核心上刷新
    for (UINT32 i = 0; i < ProcessorCount; i++) {
        DpcRoutineRunTaskOnSingleCore(i, DpcInveptSingleContext, EptPointer);
    }
}

解决方案2：每核心EPT（复杂但隔离性好）
    每个核心维护独立的EPT
        ↓
    修改时需要更新所有EPT
        ↓
    复杂但允许per-core配置
```

---

## 6.12 EPT Hook调试技巧

### 6.12.1 Hook状态查询

```c
/**
 * @brief 查询Hook状态
 */
VOID EptDumpHookList() {
    PLIST_ENTRY Entry;
    PEPT_HOOKED_PAGE_DETAIL HookedPage;
    UINT32 Index = 0;

    LogInfo("========== EPT Hook List ==========");

    Entry = g_EptHookList.Flink;

    while (Entry != &g_EptHookList) {
        HookedPage = CONTAINING_RECORD(Entry, EPT_HOOKED_PAGE_DETAIL, PageHookList);

        LogInfo("[%d] Hook at PA: %llx, VA: %llx",
                Index++,
                HookedPage->PhysicalAddress,
                HookedPage->VirtualAddress);

        LogInfo("    Type: %s%s%s",
                HookedPage->IsReadHook ? "R" : "-",
                HookedPage->IsWriteHook ? "W" : "-",
                HookedPage->IsExecuteHook ? "X" : "-");

        LogInfo("    Original: R=%d W=%d X=%d",
                HookedPage->OriginalEntry.ReadAccess,
                HookedPage->OriginalEntry.WriteAccess,
                HookedPage->OriginalEntry.ExecuteAccess);

        LogInfo("    Changed:  R=%d W=%d X=%d",
                HookedPage->ChangedEntry.ReadAccess,
                HookedPage->ChangedEntry.WriteAccess,
                HookedPage->ChangedEntry.ExecuteAccess);

        if (HookedPage->IsHiddenBreakpoint) {
            LogInfo("    Breakpoints: %d", HookedPage->CountOfBreakpoints);
            for (UINT32 i = 0; i < HookedPage->CountOfBreakpoints; i++) {
                LogInfo("      [%d] Address: %llx, Original: %02x",
                        i,
                        HookedPage->BreakpointAddresses[i],
                        HookedPage->OriginalByte[i]);
            }
        }

        Entry = Entry->Flink;
    }

    LogInfo("Total hooks: %d", Index);
}

/**
 * @brief 验证EPT Hook是否正确设置
 */
BOOLEAN EptVerifyHook(PEPT_HOOKED_PAGE_DETAIL HookedPage) {
    EPT_PML1_ENTRY * EptEntry;

    EptEntry = EptGetPml1Entry(
        g_EptState->EptPageTable,
        HookedPage->PhysicalAddress
    );

    //
    // ========================================
    // 检查EPT表项是否匹配ChangedEntry
    // ========================================
    //
    if (EptEntry->AsUInt != HookedPage->ChangedEntry.AsUInt) {
        LogError("EPT entry mismatch!");
        LogError("  Expected: %llx", HookedPage->ChangedEntry.AsUInt);
        LogError("  Actual:   %llx", EptEntry->AsUInt);
        return FALSE;
    }

    //
    // ========================================
    // 检查假页面（如果有）
    // ========================================
    //
    if (HookedPage->FakePageContents != 0) {
        // 验证假页面内容
        // ...
    }

    LogInfo("Hook verified successfully");
    return TRUE;
}
```

### 6.12.2 EPT Violation统计

```c
/**
 * @brief EPT Violation统计信息
 */
typedef struct _EPT_VIOLATION_STATS {
    UINT64 TotalViolations;
    UINT64 ExecuteViolations;
    UINT64 WriteViolations;
    UINT64 ReadViolations;
    UINT64 UnhandledViolations;

    // Per-page统计
    struct {
        UINT64 PhysicalAddress;
        UINT64 ViolationCount;
    } TopPages[10];

} EPT_VIOLATION_STATS;

EPT_VIOLATION_STATS g_EptStats;

/**
 * @brief 更新统计信息
 */
VOID EptUpdateStats(
    UINT64 GuestPhysicalAddress,
    EPT_VIOLATION_EXIT_QUALIFICATION ViolationQual
) {
    // 更新总数
    g_EptStats.TotalViolations++;

    // 更新类型统计
    if (ViolationQual.DataExecute) {
        g_EptStats.ExecuteViolations++;
    }
    if (ViolationQual.DataWrite) {
        g_EptStats.WriteViolations++;
    }
    if (ViolationQual.DataRead) {
        g_EptStats.ReadViolations++;
    }

    // 更新Top Pages
    UpdateTopPages(GuestPhysicalAddress);

    // 定期输出统计
    if (g_EptStats.TotalViolations % 10000 == 0) {
        EptDumpStats();
    }
}

/**
 * @brief 输出统计信息
 */
VOID EptDumpStats() {
    LogInfo("========== EPT Violation Statistics ==========");
    LogInfo("Total:   %lld", g_EptStats.TotalViolations);
    LogInfo("Execute: %lld", g_EptStats.ExecuteViolations);
    LogInfo("Write:   %lld", g_EptStats.WriteViolations);
    LogInfo("Read:    %lld", g_EptStats.ReadViolations);
    LogInfo("Unhandled: %lld", g_EptStats.UnhandledViolations);

    LogInfo("Top 10 pages:");
    for (UINT32 i = 0; i < 10; i++) {
        if (g_EptStats.TopPages[i].ViolationCount > 0) {
            LogInfo("  [%d] PA: %llx, Count: %lld",
                    i,
                    g_EptStats.TopPages[i].PhysicalAddress,
                    g_EptStats.TopPages[i].ViolationCount);
        }
    }
}
```

---

## 6.13 高级应用示例

### 6.13.1 完整的函数Hook示例

```c
/**
 * @brief Hook NtCreateFile并记录所有文件创建操作
 */

// ============================================
// Step 1: 初始化
// ============================================
UINT64 g_NtCreateFileAddress;
PEPT_HOOKED_PAGE_DETAIL g_NtCreateFileHook;

BOOLEAN InitializeFileMonitoring() {
    // 获取NtCreateFile地址
    g_NtCreateFileAddress = GetKernelFunctionAddress("NtCreateFile");

    if (g_NtCreateFileAddress == 0) {
        LogError("Failed to find NtCreateFile");
        return FALSE;
    }

    LogInfo("NtCreateFile at: %llx", g_NtCreateFileAddress);

    // 设置执行Hook
    UINT64 PhysicalAddress = VirtualToPhysical(g_NtCreateFileAddress);

    if (!EptHookExecuteOnly(PhysicalAddress)) {
        LogError("Failed to hook NtCreateFile");
        return FALSE;
    }

    // 保存Hook指针
    g_NtCreateFileHook = EptFindHookedPageByPhysicalAddress(PhysicalAddress);

    LogInfo("NtCreateFile monitoring initialized");

    return TRUE;
}

// ============================================
// Step 2: EPT Violation处理
// ============================================
BOOLEAN EptHandleExecuteViolation(
    VIRTUAL_MACHINE_STATE * VCpu,
    PEPT_HOOKED_PAGE_DETAIL HookedPage
) {
    // 检查是否是NtCreateFile
    if (HookedPage == g_NtCreateFileHook) {

        LogInfo("NtCreateFile called from: %llx", VCpu->LastVmexitRip);

        //
        // ========================================
        // 读取函数参数
        // ========================================
        //
        // Windows x64调用约定：RCX, RDX, R8, R9, [RSP+28h]...

        UINT64 FileHandlePtr = VCpu->Regs->rcx;       // PHANDLE
        UINT64 DesiredAccess = VCpu->Regs->rdx;       // ACCESS_MASK
        UINT64 ObjectAttributesPtr = VCpu->Regs->r8;  // POBJECT_ATTRIBUTES
        UINT64 IoStatusBlockPtr = VCpu->Regs->r9;     // PIO_STATUS_BLOCK

        // 从栈读取其他参数
        UINT64 GuestRsp;
        __vmx_vmread(VMCS_GUEST_RSP, &GuestRsp);

        UINT64 AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions;
        MemoryMapperReadMemorySafe(GuestRsp + 0x28, &AllocationSize, 8, NULL);
        MemoryMapperReadMemorySafe(GuestRsp + 0x30, &FileAttributes, 8, NULL);
        MemoryMapperReadMemorySafe(GuestRsp + 0x38, &ShareAccess, 8, NULL);
        MemoryMapperReadMemorySafe(GuestRsp + 0x40, &CreateDisposition, 8, NULL);
        MemoryMapperReadMemorySafe(GuestRsp + 0x48, &CreateOptions, 8, NULL);

        //
        // ========================================
        // 读取文件名
        // ========================================
        //
        UNICODE_STRING FileName = {0};
        OBJECT_ATTRIBUTES ObjectAttributes;

        MemoryMapperReadMemorySafe(
            ObjectAttributesPtr,
            &ObjectAttributes,
            sizeof(OBJECT_ATTRIBUTES),
            NULL
        );

        if (ObjectAttributes.ObjectName != NULL) {
            MemoryMapperReadMemorySafe(
                (UINT64)ObjectAttributes.ObjectName,
                &FileName,
                sizeof(UNICODE_STRING),
                NULL
            );

            WCHAR FileNameBuffer[260];
            if (FileName.Length > 0 && FileName.Length < sizeof(FileNameBuffer)) {
                MemoryMapperReadMemorySafe(
                    (UINT64)FileName.Buffer,
                    FileNameBuffer,
                    FileName.Length,
                    NULL
                );

                FileNameBuffer[FileName.Length / sizeof(WCHAR)] = L'\0';

                //
                // ========================================
                // 记录文件访问
                // ========================================
                //
                LogInfo("File: %ws", FileNameBuffer);
                LogInfo("  DesiredAccess: %llx", DesiredAccess);
                LogInfo("  CreateDisposition: %llx", CreateDisposition);
                LogInfo("  Caller: %llx", VCpu->LastVmexitRip);

                //
                // ========================================
                // 可以拒绝访问
                // ========================================
                //
                if (wcsstr(FileNameBuffer, L"secret.txt") != NULL) {
                    LogWarning("Blocked access to secret.txt");

                    // 返回错误
                    VCpu->Regs->rax = STATUS_ACCESS_DENIED;

                    // 跳过函数调用
                    VCpu->IncrementRip = TRUE;

                    return TRUE;
                }
            }
        }
    }

    //
    // ========================================
    // 正常处理（临时恢复 + MTF）
    // ========================================
    //
    EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(...);

    EptEntry->ExecuteAccess = 1;
    VmFuncSetMonitorTrapFlag(TRUE);
    VCpu->MtfEptHookRestorePoint = HookedPage;

    EptInveptSingleContext(...);
    VCpu->IncrementRip = FALSE;

    return TRUE;
}

// ============================================
// Step 3: MTF恢复Hook
// ============================================
MtfHandleVmexit()中自动恢复Hook状态

// ============================================
// Step 4: 可选 - 读取返回值
// ============================================
// 如果想知道NtCreateFile的返回值，可以Hook返回地址

BOOLEAN HookReturnAddress(UINT64 ReturnAddress) {
    // 在调用者的返回地址设置临时Hook
    // 当函数返回时触发
    // 可以读取RAX（返回值）
}
```

### 6.13.2 系统调用监控

```c
/**
 * @brief Hook系统调用入口点
 */
BOOLEAN HookSystemCallEntry() {
    //
    // ========================================
    // 方法1：Hook KiSystemCall64
    // ========================================
    //
    UINT64 KiSystemCall64 = GetKernelSymbol("KiSystemCall64");

    EptHookExecuteOnly(VirtualToPhysical(KiSystemCall64));

    LogInfo("System call entry hooked");

    return TRUE;
}

/**
 * @brief 处理系统调用Hook
 */
BOOLEAN EptHandleSystemCallHook(VIRTUAL_MACHINE_STATE * VCpu) {
    UINT64 SyscallNumber = VCpu->Regs->rax;  // 系统调用号在RAX

    //
    // ========================================
    // 根据系统调用号过滤
    // ========================================
    //
    switch (SyscallNumber) {

        case SYSCALL_NtQuerySystemInformation:
        {
            UINT64 SystemInformationClass = VCpu->Regs->rcx;

            if (SystemInformationClass == SystemKernelDebuggerInformation) {
                //
                // 拦截调试器检测
                //
                LogInfo("SystemKernelDebuggerInformation query intercepted");

                // 设置Trap Flag，在返回后修改结果
                VmFuncSetRflagTrapFlag(TRUE);
            }

            break;
        }

        case SYSCALL_NtCreateFile:
        case SYSCALL_NtOpenFile:
        {
            // 监控文件访问
            LogInfo("File operation syscall: %lld", SyscallNumber);
            break;
        }

        // ... 其他系统调用
    }

    //
    // ========================================
    // 正常处理Hook
    // ========================================
    //
    EPT_PML1_ENTRY * EptEntry = EptGetPml1Entry(...);

    EptEntry->ExecuteAccess = 1;
    VmFuncSetMonitorTrapFlag(TRUE);
    VCpu->MtfEptHookRestorePoint = HookedPage;

    EptInveptSingleContext(...);
    VCpu->IncrementRip = FALSE;

    return TRUE;
}
```

---

## 本章小结

本章深入讲解了EPT Hook技术：

1. **EPT Hook原理**
   - 通过EPT权限控制实现拦截
   - 不修改代码，完全隐藏
   - 硬件级别的监控

2. **Hook类型**
   - 执行Hook：拦截函数调用
   - 写Hook：监控数据修改
   - 读Hook：监控数据读取
   - 分离视图：读写和执行不同内容

3. **与MTF配合**
   - MTF实现Hook透明性
   - 执行一条指令后恢复限制
   - 确保Hook持续有效

4. **数据结构**
   - EPT_HOOKED_PAGE_DETAIL
   - Hook列表管理
   - 原始/修改/执行视图

5. **高级技术**
   - 分离视图实现隐藏断点
   - 条件Hook
   - 性能优化

6. **实际应用**
   - API监控
   - 反作弊检测
   - 系统调用拦截
   - Rootkit隐藏

7. **限制和优化**
   - 性能开销分析
   - TLB一致性
   - 多核同步
   - 调试技巧

EPT Hook是现代虚拟化监控的核心技术，结合MTF机制可以实现完全透明的内存访问拦截。

---

[<< 上一章：NMI广播与MTF机制](./第五章-NMI广播与MTF机制.md) | [下一章：HyperEvade反检测模块 >>](./第七章-HyperEvade反检测模块.md)
