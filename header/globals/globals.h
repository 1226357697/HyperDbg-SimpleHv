/**
 * @file Global.h
 * @author Sina Karvandi (sina@hyperdbg.org)
 * @brief Headers for global variables
 * @version 0.1
 * @date 2023-01-13
 *
 * @copyright This project is released under the GNU Public License v3.
 *
 */
#pragma once

 /**
  * @brief Save the state and variables related to debugging on each to logical core
  *
  */
PROCESSOR_DEBUGGING_STATE* g_DbgState;

/**
 * @brief Event to show whether the user debugger is waiting for a command or not
 *
 */
KEVENT g_UserDebuggerWaitingCommandEvent;

/**
 * @brief Buffer to hold the command from user debugger
 *
 */
PVOID g_UserDebuggerWaitingCommandBuffer;

/**
 * @brief Length of the input command buffer from user debugger
 *
 */
UINT32 g_UserDebuggerWaitingCommandInputBufferLength;

/**
 * @brief Length of the output command buffer from user debugger
 *
 */
UINT32 g_UserDebuggerWaitingCommandOutputBufferLength;

/**
 * @brief Holder of script engines global variables
 *
 */
UINT64* g_ScriptGlobalVariables;

/**
 * @brief State of the trap-flag
 *
 */
DEBUGGER_TRAP_FLAG_STATE g_TrapFlagState;

/**
 * @brief Determines whether the one application gets the handle or not
 * this is used to ensure that only one application can get the handle
 *
 */
BOOLEAN g_HandleInUse;

/**
 * @brief Determines whether the clients are allowed to send IOCTL to the drive or not
 *
 */
BOOLEAN g_AllowIOCTLFromUsermode;


/**
 * @brief The value of last error
 *
 */
UINT32 g_LastError;

/**
 * @brief Determines whether the debugger events should be active or not
 *
 */
BOOLEAN g_EnableDebuggerEvents;

/**
 * @brief List header of breakpoints for debugger-mode
 *
 */
LIST_ENTRY g_BreakpointsListHead;

/**
 * @brief Seed for setting id of breakpoints
 *
 */
UINT64 g_MaximumBreakpointId;

/**
 * @brief shows whether the kernel debugger is enabled or disabled
 *
 */
BOOLEAN g_KernelDebuggerState;

/**
 * @brief shows whether the user debugger is enabled or disabled
 *
 */
BOOLEAN g_UserDebuggerState;

/**
 * @brief shows whether the debugger should intercept breakpoints (#BP) or not
 *
 */
BOOLEAN g_InterceptBreakpoints;

/**
 * @brief shows whether the debugger should intercept breakpoints (#DB) or not
 *
 */
BOOLEAN g_InterceptDebugBreaks;


/**
 * @brief Seed for tokens of unique details buffer for threads
 *
 */
UINT64 g_SeedOfUserDebuggingDetails;

/**
 * @brief Whether the thread attaching mechanism is waiting for a page-fault
 * finish or not
 *
 */
BOOLEAN g_IsWaitingForReturnAndRunFromPageFault;

/**
 * @brief List header of thread debugging details
 *
 */
LIST_ENTRY g_ProcessDebuggingDetailsListHead;

/**
 * @brief Whether the thread attaching mechanism is waiting for #DB or not
 *
 */
BOOLEAN g_IsWaitingForUserModeProcessEntryToBeCalled;

/**
 * @brief To avoid getting stuck from getting hit from the breakpoints while executing
 * the commands in the remote computer, for example, bp NtQuerySystemInformation and lm,
 * the debugger should intercept the breakponts and events.
 *
 */
BOOLEAN g_InterceptBreakpointsAndEventsForCommandsInRemoteComputer;

/**
 * @brief Global test flag (for testing purposes)
 *
 */
BOOLEAN g_TestFlag;

