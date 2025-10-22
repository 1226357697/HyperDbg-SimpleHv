#pragma once

// 验证密钥（防止其他程序调用）
#define HYPERCALL_KEY  ((UINT64)0xE4B881E79C9F) // 丁真

// Hypervisor签名（用于ping验证）
#define HYPERCALL_SIGN ((UINT64)0xE79086E5A198)     // 理塘

// 通信魔数
#define HYPERCALL_MAGIC_NUMBER1 ((UINT64)0xE79086) // 理
#define HYPERCALL_MAGIC_NUMBER2 ((UINT64)0xE5A198) // 塘
#define HYPERCALL_MAGIC_NUMBER3 ((UINT64)0xE78E8B) // 王

//
// ========================================
// 返回码
// ========================================
//
#define HYPERCALL_SUCCESS           0
#define HYPERCALL_FAIL      0xFFFFFFFFFFFFFFFFULL
#define HYPERCALL_ERROR_INVALID_KEY 0xFFFFFFFF00000001ULL
#define HYPERCALL_ERROR_INVALID_CODE 0xFFFFFFFF00000002ULL


// 通讯的Code枚举
typedef enum _HYPERCALL_CODE {

    // 基础操作
    HYPERCALL_PING = 0x00,                  // 测试连接
    HYPERCALL_INSTALL_EPTHOOK = 0x01,                  // 安装EPTHook

 
} HYPERCALL_CODE;


// Hypercall处理函数
NTSTATUS
HyperVmcallHandler(UINT32 CoreId,
    UINT64 VmcallNumber,
    UINT64 OptionalParam1,
    UINT64 OptionalParam2,
    UINT64 OptionalParam3);