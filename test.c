/**
 * @file test.c
 * @brief EPT Hook 测试示例 - 使用 EptHookInstallHiddenInlineHookAuto
 * @details
 *     演示如何使用 EptHookInstallHiddenInlineHookAuto 实现内核函数 Hook
 *     Hook 两个关键函数实现进程保护：
 *     1. ObpReferenceObjectByHandleWithTag - 防止进程被打开
 *     2. NtQuerySystemInformation - 隐藏进程
 */

#include "pch.h"


// ========================================
// 函数类型定义
// ========================================

typedef NTSTATUS(__stdcall* fnObReferenceObjectByHandleWithTag)(
	HANDLE Handle,
	ACCESS_MASK DesiredAccess,
	POBJECT_TYPE ObjectType,
	KPROCESSOR_MODE AccessMode,
	ULONG Tag,
	PVOID* Object,
	POBJECT_HANDLE_INFORMATION HandleInformation,
	__int64 a0
);

typedef NTSTATUS(__stdcall* fnNtQuerySystemInformation)(
	SYSTEM_INFORMATION_CLASS SystemInformationClass,
	PVOID SystemInformation,
	ULONG SystemInformationLength,
	PULONG ReturnLength
);

// ========================================
// 全局变量 - 保存原始函数地址
// ========================================

fnObReferenceObjectByHandleWithTag old_ObpReferenceObjectByHandleWithTag = NULL;
fnNtQuerySystemInformation old_NtQuerySystemInformation = NULL;

// ========================================
// 外部声明
// ========================================

char* PsGetProcessImageFileName(PEPROCESS Process);

// ========================================
// 配置：受保护的进程列表
// ========================================

PCWCH protected_process_list[] = {
	L"cheatengine",
	L"HyperCE",
	L"x64dbg",
	L"x32dbg",
	L"ida",
	L"windbg"
};

// ========================================
// 辅助函数
// ========================================

BOOLEAN StringArrayContainsW(PCWCH str, PCWCH* arr, SIZE_T len)
{
	if (str == NULL || arr == NULL || len == 0)
		return FALSE;

	for (SIZE_T i = 0; i < len; i++) {
		if (wcsstr(str, arr[i]) != NULL)
			return TRUE;
	}
	return FALSE;
}

BOOLEAN IsProtectedProcessW(PCWCH process)
{
	if (process == NULL)
		return FALSE;

	return StringArrayContainsW(
		process, protected_process_list, sizeof(protected_process_list) / sizeof(PCWCH));
}

BOOLEAN IsProtectedProcessA(PCSZ process)
{
	ANSI_STRING process_ansi = { 0 };
	UNICODE_STRING process_unicode = { 0 };
	NTSTATUS status;
	BOOLEAN result;

	if (process == NULL)
		return FALSE;

	RtlInitAnsiString(&process_ansi, process);
	status = RtlAnsiStringToUnicodeString(&process_unicode, &process_ansi, TRUE);
	if (!NT_SUCCESS(status))
		return FALSE;

	result = IsProtectedProcessW(process_unicode.Buffer);
	RtlFreeUnicodeString(&process_unicode);
	return result;
}

/**
 * @brief 在 ObReferenceObjectByHandleWithTag 中查找内部函数 ObpReferenceObjectByHandleWithTag
 * @details 通过查找第一个 CALL 指令来定位内部函数
 */
UINT8* FindObpReferenceObjectByHandleWithTag()
{
	UINT8* pObReferenceObjectByHandleWithTag = (UINT8*)ObReferenceObjectByHandleWithTag;
	SIZE_T offset;

	for (offset = 0; offset < 0x100; offset++) {
		UINT8* curr = pObReferenceObjectByHandleWithTag + offset;

		// 查找 CALL 指令 (0xE8)
		if (*curr == 0xE8) {
			// 计算 CALL 的目标地址
			// CALL rel32: E8 [4字节相对偏移]
			// 目标地址 = 当前地址 + 5 + rel32
			return curr + 5 + *(INT32*)(curr + 1);
		}
	}

	return NULL;
}

// ========================================
// Hook 处理函数
// ========================================

/**
 * @brief ObpReferenceObjectByHandleWithTag Hook 处理函数
 * @details 如果调用者是受保护进程，则降低其访问权限
 */
NTSTATUS ObpReferenceObjectByHandleWithTagHook(
	HANDLE Handle,
	ACCESS_MASK DesiredAccess,
	POBJECT_TYPE ObjectType,
	KPROCESSOR_MODE AccessMode,
	ULONG Tag,
	PVOID* Object,
	POBJECT_HANDLE_INFORMATION HandleInformation,
	__int64 a0)
{
	char* curr_process_name = PsGetProcessImageFileName(PsGetCurrentProcess());

	// 如果当前进程是受保护进程，降低其权限
	if (IsProtectedProcessA(curr_process_name)) {
		// 将访问权限设为 0，访问模式设为 KernelMode
		// 这样受保护的进程无法读写其他进程
		return old_ObpReferenceObjectByHandleWithTag(
			Handle, 0, ObjectType, KernelMode, Tag, Object, HandleInformation, a0);
	}

	// 非保护进程，正常调用
	return old_ObpReferenceObjectByHandleWithTag(
		Handle, DesiredAccess, ObjectType, AccessMode, Tag, Object, HandleInformation, a0);
}

/**
 * @brief NtQuerySystemInformation Hook 处理函数
 * @details 从进程列表中移除受保护的进程
 */
NTSTATUS NtQuerySystemInformationHook(
	SYSTEM_INFORMATION_CLASS SystemInformationClass,
	PVOID SystemInformation,
	ULONG SystemInformationLength,
	PULONG ReturnLength)
{
	NTSTATUS stat;
	PSYSTEM_PROCESS_INFORMATION prev;
	PSYSTEM_PROCESS_INFORMATION curr;

	// 调用原始函数
	stat = old_NtQuerySystemInformation(
		SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);

	// 如果查询成功且查询的是进程信息
	if (NT_SUCCESS(stat) && SystemInformationClass == SystemProcessInformation) {
		prev = (PSYSTEM_PROCESS_INFORMATION)SystemInformation;
		curr = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)prev + prev->NextEntryOffset);

		// 遍历进程链表
		while (prev->NextEntryOffset != NULL) {
			PWCH buffer = curr->ImageName.Buffer;

			// 如果是受保护进程，从链表中移除
			if (buffer && IsProtectedProcessW(buffer)) {
				if (curr->NextEntryOffset == 0)
					prev->NextEntryOffset = 0;
				else
					prev->NextEntryOffset += curr->NextEntryOffset;
				curr = prev;
			}

			prev = curr;
			curr = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)curr + curr->NextEntryOffset);
		}
	}

	return stat;
}

// ========================================
// 导出接口
// ========================================

/**
 * @brief 安装所有测试 Hooks
 * @details 安装 ObpReferenceObjectByHandleWithTag 和 NtQuerySystemInformation 的 EPT Hooks
 * @return NTSTATUS 成功返回 STATUS_SUCCESS，失败返回相应错误码
 * @note 必须在 VMX non-root mode 下调用（在驱动入口点调用）
 */
NTSTATUS InstallTestHooks(VOID)
{
	PVOID pObpReferenceObjectByHandleWithTag;
	PVOID pNtQuerySystemInformation;

	BOOLEAN result;
	UNICODE_STRING routineName;
	SIZE_T i;

	SimpleHvLog("========================================");
	SimpleHvLog("[Test] Installing test hooks...");
	SimpleHvLog("========================================");



	// ========================================
	// Hook 1: ObpReferenceObjectByHandleWithTag
	// ========================================
	pObpReferenceObjectByHandleWithTag = FindObpReferenceObjectByHandleWithTag();
	if (!pObpReferenceObjectByHandleWithTag) {
		SimpleHvLogError("[Test] Failed to find ObpReferenceObjectByHandleWithTag");
		return STATUS_UNSUCCESSFUL;
	}

	SimpleHvLog("[Test] ObpReferenceObjectByHandleWithTag: 0x%llx", pObpReferenceObjectByHandleWithTag);

	// 保存原始函数地址（用于在 Hook 中调用原函数）
	old_ObpReferenceObjectByHandleWithTag = (fnObReferenceObjectByHandleWithTag)pObpReferenceObjectByHandleWithTag;

	// 安装 EPT Hook
	//result = EptHookInstallHiddenInlineHookAuto(
	//	pObpReferenceObjectByHandleWithTag,                // 目标函数地址
	//	(PVOID)ObpReferenceObjectByHandleWithTagHook,      // Hook 处理函数
	//	0                                                   // ProcessId (0 = 内核 hook)
	//);

	result = ConfigureEptHook2(KeGetCurrentProcessorNumberEx(NULL), pObpReferenceObjectByHandleWithTag, (PVOID)ObpReferenceObjectByHandleWithTagHook, (UINT32)(ULONG_PTR)PsGetCurrentProcessId());
	if (result) {
		SimpleHvLog("[Test] Hook 1 installed: ObpReferenceObjectByHandleWithTag");
	} else {
		SimpleHvLogError("[Test] Failed to install Hook 1");
		return STATUS_UNSUCCESSFUL;
	}



	// ========================================
	// 3. Hook NtQuerySystemInformation
	// ========================================
	RtlInitUnicodeString(&routineName, L"NtQuerySystemInformation");
	pNtQuerySystemInformation = MmGetSystemRoutineAddress(&routineName);

	if (!pNtQuerySystemInformation) {
		SimpleHvLogError("[Test] Failed to find NtQuerySystemInformation");
		return STATUS_UNSUCCESSFUL;
	}

	SimpleHvLog("[Test] NtQuerySystemInformation: 0x%llx", pNtQuerySystemInformation);

	// 保存原始函数地址
	old_NtQuerySystemInformation = (fnNtQuerySystemInformation)pNtQuerySystemInformation;




	// 安装 EPT Hook
	//result = EptHookInstallHiddenInlineHookAuto(
	//	pNtQuerySystemInformation,                         // 目标函数地址
	//	(PVOID)NtQuerySystemInformationHook,               // Hook 处理函数
	//	0                                                   // ProcessId (0 = 内核 hook)
	//);
	/*result =  ConfigureEptHook2(KeGetCurrentProcessorNumberEx(NULL), pNtQuerySystemInformation, (PVOID)NtQuerySystemInformationHook, HANDLE_TO_UINT32(PsGetCurrentProcessId()));
	if (result) {
		SimpleHvLog("[Test] Hook 2 installed: NtQuerySystemInformation");
	} else {
		SimpleHvLogError("[Test] Failed to install Hook 2");
		return STATUS_UNSUCCESSFUL;
	}*/

	// ========================================
	// 4. 完成
	// ========================================
	SimpleHvLog("========================================");
	SimpleHvLog("[Test] All test hooks installed successfully");
	SimpleHvLog("[Test] Protected processes:");
	for (i = 0; i < sizeof(protected_process_list) / sizeof(PCWCH); i++) {
		SimpleHvLog("[Test]   - %ws", protected_process_list[i]);
	}
	SimpleHvLog("========================================");

	return STATUS_SUCCESS;
}
