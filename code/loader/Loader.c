/**
 * @file Loader.c
 * @author Sina Karvandi (sina@hyperdbg.org)
 * @brief The functions used in loading the debugger and VMM
 * @version 0.2
 * @date 2023-01-15
 *
 * @copyright This project is released under the GNU Public License v3.
 *
 */
#include "pch.h"

 /**
  * @brief Initialize the VMM and Debugger
  *
  * @return BOOLEAN
  */
BOOLEAN
LoaderInitVmm()
{
	VMM_CALLBACKS             VmmCallbacks = { 0 };

	// 初始化外部VMCall处理的回调函数
	//VmmCallbacks.VmmCallbackVmcallHandler = HyperVmcallHandler;


	if (!VmFuncInitVmm(&VmmCallbacks))
	{
		SimpleHvLogError("HvInitVmm failed!");
		return FALSE;
	}
 
    return TRUE;
}


/**
初始化Driver环境
**/
BOOLEAN
LoaderInitDriver()
{

    //
    // Initialize NMI broadcasting mechanism
    //
    VmFuncVmxBroadcastInitialize();

    //
    // Pre-allocate pools for possible EPT hooks
    //
    ConfigureEptHookReservePreallocatedPoolsForEptHooks(MAXIMUM_NUMBER_OF_INITIAL_PREALLOCATED_EPT_HOOKS);
    ConfigureEptHookAllocateExtraHookingPagesForMemoryMonitorsAndExecEptHooks(MAXIMUM_NUMBER_OF_INITIAL_PREALLOCATED_EPT_HOOKS_MEM_MONITOR);

    //
    // 真正执行池分配（从请求队列中）
    //
    if (!PoolManagerCheckAndPerformAllocationAndDeallocation())
    {
        SimpleHvLogWarning("Warning, cannot allocate the pre-allocated pools for EPT hooks");

        //
        // 这会导致HyperEvade失败，但不终止驱动加载
        //
        return FALSE;  // 或者返回FALSE让Driver.c知道失败
    }

    SimpleHvLog("[PoolManager] EPT hook pools allocated successfully");

    return TRUE;
}
