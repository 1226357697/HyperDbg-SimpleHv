#include "pch.h"
//
//
///**
// * @brief Termination function for external-interrupts
// *
// * @param CoreId
// * @param VmcallNumber
// * @param OptionalParam1
// * @param OptionalParam2
// * @param OptionalParam3
// *
// * @return NTSTATUS
// */
//NTSTATUS
//HyperVmcallHandler(UINT32 CoreId,
//    UINT64 VmcallNumber,
//    UINT64 OptionalParam1,
//    UINT64 OptionalParam2,
//    UINT64 OptionalParam3)
//{
//    UNREFERENCED_PARAMETER(OptionalParam3);
//    UNREFERENCED_PARAMETER(OptionalParam2);
//
//    NTSTATUS                     Result = STATUS_UNSUCCESSFUL;
//    VIRTUAL_MACHINE_STATE* VCpu = &g_GuestState[CoreId];
//
//    SimpleHvLog("[HyperVmcallHandler] Called: VmcallNumber=0x%llx, OptionalParam1=0x%llx",
//                VmcallNumber, OptionalParam1);
//
//    // 处理外部Call
//    switch (VmcallNumber)
//    {
//         case HYPERCALL_PING:
//            VCpu->Regs->rax = HYPERCALL_SIGN;
//            Result = STATUS_RAX_ALREADY_SET;
//            break;
//
//         case HYPERCALL_INSTALL_EPTHOOK:
//            // InstallTestHooks 应该从驱动入口点调用，不应该从 VMCALL handler 调用
//            // 因为它需要在 VMX non-root mode 下执行（需要调用 MmGetSystemRoutineAddress 等 API）
//            SimpleHvLogWarning("[HyperVmcallHandler] HYPERCALL_INSTALL_EPTHOOK is not implemented");
//            SimpleHvLogWarning("  Please call InstallTestHooks() from DriverEntry instead");
//            Result = STATUS_NOT_IMPLEMENTED;
//            break;
//
//         default:
//            SimpleHvLogWarning("[HyperVmcallHandler] Unknown VmcallNumber: 0x%llx", VmcallNumber);
//            break;
//    }
//
//    //SimpleHvLog("[HyperVmcallHandler] Returning: 0x%llx", Result);
//    return Result;
//}