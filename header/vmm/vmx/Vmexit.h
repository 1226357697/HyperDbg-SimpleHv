/**
 * @file Vmexit.h
 * @author Your Name
 * @brief VM-Exit Handler Definitions
 * @version 0.1
 * @date 2025-10-20
 *
 * @copyright This project is released under the GNU Public License v3.
 *
 */
#pragma once

//
// ========================================
// Function Prototypes
// ========================================
//

// Main VM-Exit handler (called from assembly)
BOOLEAN VmxVmexitHandler(PGUEST_REGS GuestRegs);

// Specific exit handlers
VOID VmxHandleCpuid(PVIRTUAL_MACHINE_STATE VmState);
BOOLEAN VmxHandleVmcall(PVIRTUAL_MACHINE_STATE VmState);

// TODO: Add more handlers as needed

