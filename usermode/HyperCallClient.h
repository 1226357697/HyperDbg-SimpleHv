#pragma once

#include <Windows.h>
#include <stdint.h>

// 验证密钥（需要和内核端保持一致）
#define HYPERCALL_KEY  ((UINT64)0xE4B881E79C9F) // 丁真

// Hypervisor签名（用于ping验证）
#define HYPERCALL_SIGN ((UINT64)0xE79086E5A198) // 理塘

//
// ========================================
// 错误码
// ========================================
//
#define HYPERCALL_SUCCESS           0
#define HYPERCALL_FAIL              0xFFFFFFFFFFFFFFFFULL
#define HYPERCALL_ERROR_INVALID_KEY 0xFFFFFFFF00000001ULL
#define HYPERCALL_ERROR_INVALID_CODE 0xFFFFFFFF00000002ULL

// 通讯的Code枚举
typedef enum _HYPERCALL_CODE {

    // 基础功能
    HYPERCALL_PING = 0x00,                  // 测试连接

    // EPT Hook 功能
    HYPERCALL_INSTALL_EPTHOOK = 0x01,       // 安装 EPT Hook（自动检测 R0/R3）

} HYPERCALL_CODE;


//
// ========================================
// 外部声明 - 汇编函数
// ========================================
//
// UINT64 VmxVmCall(UINT64 VmcallNumber, UINT64 OptionalParam1, UINT64 OptionalParam2, UINT64 OptionalParam3)
extern "C" UINT64 VmxVmCall(UINT64 VmcallNumber, UINT64 OptionalParam1, UINT64 OptionalParam2, UINT64 OptionalParam3);


//
// ========================================
// C++ 封装函数
// ========================================
//
namespace HyperCall {

    // 检查 Hypervisor 是否运行
    inline bool IsHypervisorRunning() {
        __try {
            // RCX = VmcallNumber (HYPERCALL_PING)
            // RDX = OptionalParam1 (HYPERCALL_KEY - 密钥验证)
            // R8  = OptionalParam2 (0)
            // R9  = OptionalParam3 (0)
            UINT64 result = VmxVmCall(HYPERCALL_PING, HYPERCALL_KEY, 0, 0);
            return result == HYPERCALL_SIGN;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Ping Hypervisor
    inline UINT64 Ping() {
        return VmxVmCall(HYPERCALL_PING, HYPERCALL_KEY, 0, 0);
    }

    // 在每个逻辑处理器上执行回调
    template <typename Fn>
    inline void ForEachCpu(Fn fn) {
        SYSTEM_INFO sysInfo = { 0 };
        GetSystemInfo(&sysInfo);

        for (DWORD i = 0; i < sysInfo.dwNumberOfProcessors; ++i) {
            DWORD_PTR prevAffinity = SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << i);
            fn(i);
            SetThreadAffinityMask(GetCurrentThread(), prevAffinity);
        }
    }

    //
    // ========================================
    // 通用 Hypercall 调用封装
    // ========================================
    //

    // 执行通用的 Hypercall（自动设置密钥）
    inline UINT64 CallHypercall(UINT64 code,
                                 UINT64 param1 = 0,
                                 UINT64 param2 = 0,
                                 UINT64 param3 = 0) {
        // RCX = VmcallNumber (code)
        // RDX = OptionalParam1 (HYPERCALL_KEY - 密钥验证)
        // R8  = OptionalParam2 (param1)
        // R9  = OptionalParam3 (param2)
        // 注意：目前只支持3个参数，因为x64 fastcall只有4个寄存器参数
        return VmxVmCall(code, HYPERCALL_KEY, param1, param2);
    }

    //
    // ========================================
    // EPT Hook 功能
    // ========================================
    //

    /**
     * @brief 安装 EPT Inline Hook（自动检测 R0/R3）
     *
     * @param TargetAddress 目标函数地址（支持 R0/R3 自动检测）
     * @param HookHandler Hook 处理函数地址
     * @param ProcessId 进程 ID（R3 Hook 需要，R0 Hook 忽略）
     *                  - 0: 对于 R3 使用当前进程，对于 R0 忽略
     *                  - 非 0: 对于 R3 使用指定进程，对于 R0 忽略
     * @return UINT64 成功返回 STATUS_SUCCESS(0)，失败返回错误码
     */
    inline UINT64 InstallEptHook(PVOID TargetAddress,
                                  PVOID HookHandler,
                                  UINT32 ProcessId = 0) {
        return VmxVmCall(HYPERCALL_INSTALL_EPTHOOK,
            HYPERCALL_KEY,
                        (UINT64)HookHandler,
                        (UINT64)ProcessId);
    }

} // namespace HyperCall
