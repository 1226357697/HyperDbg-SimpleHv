# SimpleHv - Educational Windows Hypervisor Driver

**[中文文档](README.md) | English**

![License](https://img.shields.io/badge/license-GPL--3.0-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-lightgrey.svg)
![VT-x](https://img.shields.io/badge/Intel-VT--x-0071c5.svg)

A sophisticated educational kernel-mode hypervisor driver implementing Intel VT-x virtualization technology with advanced features including EPT-based memory hooks, multi-core synchronization, syscall interception, and anti-detection capabilities.

## Overview

SimpleHv is designed as a learning-oriented hypervisor project that demonstrates core virtualization concepts while implementing production-grade features. Built on the proven HyperDbg architecture, it provides a comprehensive reference implementation for Windows kernel developers and security researchers studying hardware-assisted virtualization.

## Key Features

### Core Hypervisor Capabilities
- **Intel VT-x Implementation**: Complete VMCS configuration, VM-Entry/Exit handling, and guest state management
- **Extended Page Tables (EPT)**: 4-level EPT paging with support for 4KB/2MB/1GB pages
- **Multi-Core Support**: NMI broadcast mechanism for synchronized operations across all CPU cores
- **VM-Exit Handlers**: Comprehensive handling for MSR, I/O, CPUID, and other intercept events

### Advanced Features
- **EPT-Based Memory Hooks**: Hidden memory interception without modifying guest page tables
- **Syscall Interception**: Transparent SYSCALL/SYSRET hooking via EFER register manipulation
- **Execution Monitoring**: Mode-based execution hooks, MTF single-stepping, and hidden breakpoints
- **Dirty Logging**: Page-level write access tracking for memory monitoring
- **MMIO Shadowing**: Memory-Mapped I/O interception and virtualization

### HyperEvade - Anti-Detection Module
- CPUID spoofing to hide hypervisor presence
- RDTSC timing attack mitigation
- VMX instruction footprint removal
- Anti-VM tool detection bypass
- Transparent process mode for stealth operation

## Project Structure

```
SimpleHv/
├── code/                    # Implementation files
│   ├── assembly/           # Low-level VMX/EPT operations (MASM)
│   ├── vmm/                # Core VMX and EPT implementation
│   ├── hooks/              # EPT hooks and syscall hooks
│   ├── memory/             # Memory management and EPT handling
│   ├── broadcast/          # Multi-core synchronization
│   ├── transparency/       # Anti-detection implementation
│   └── interface/          # Driver communication and hypercalls
├── header/                 # Header files (mirrors code/ structure)
├── include/                # SDK headers and platform abstractions
├── dependencies/           # Third-party libraries
│   ├── zydis/             # Instruction disassembly engine
│   ├── keystone/          # Instruction assembly engine
│   └── ia32-doc/          # Intel instruction/CPUID definitions
├── usermode/              # User-mode test client
├── learndocs/             # Comprehensive learning documentation (Chinese)
└── SimpleHv.sln           # Visual Studio solution
```

## Prerequisites

### Hardware Requirements
- Intel processor with VT-x (Virtualization Technology) support
- x64 architecture
- 8GB+ RAM recommended

### Software Requirements
- Windows 10/11 x64
- Visual Studio 2019/2022
- Windows Driver Kit (WDK) 10.0 or later
- Test Mode enabled for unsigned driver testing

### Dependencies
- **Zydis** (v4.1+): Instruction disassembly engine (included)
- **Keystone Engine**: Instruction assembly (headers included)
- **Windows WDK**: Kernel development headers and libraries

## Building

### Build Steps
1. Open `SimpleHv.sln` in Visual Studio 2019/2022
2. Ensure Windows Driver Kit (WDK) is properly installed
3. Select build configuration (Debug/Release) and platform (x64)
4. Build solution (Ctrl+Shift+B)

### Build Output
- **Driver**: `build/bin/[Debug|Release]/SimpleHv.sys`
- **User-mode test client**: `build/bin/[Debug|Release]/UsermodeTest.exe`

### Cleanup
Run `clean.bat` to remove build artifacts and intermediate files.

## Installation & Testing

### Enable Test Mode
```batch
bcdedit /set testsigning on
bcdedit /set debug on
```
Reboot after enabling test mode.

### Load the Driver
```batch
sc create SimpleHv type= kernel binPath= "C:\path\to\SimpleHv.sys"
sc start SimpleHv
```

### Run User-Mode Test Client
```batch
UsermodeTest.exe
```

The test application will:
- Verify hypervisor presence via PING hypercall
- Test multi-core communication
- Validate HyperEvade anti-detection features

### Unload the Driver
```batch
sc stop SimpleHv
sc delete SimpleHv
```

## Configuration

Compile-time configuration is available in `include/config/Configuration.h`:

```c
#define ShowSystemTimeOnDebugMessages   TRUE
#define UseWPPTracing                   FALSE
#define DebugMode                       FALSE
#define EnableInstantEventMechanism     TRUE
#define ActivateUserModeDebugger        TRUE
#define ActivateHyperEvadeProject       TRUE
```

## Architecture Overview

### VMX Operations
- **VMXON Region**: Per-core VMX activation and VMCS management
- **Guest State**: Complete CPU register save/restore on VM-Exit/Entry
- **VM-Exit Handling**: Dispatcher routing exits to specialized handlers

### EPT Implementation
- **4-Level Paging**: PML4 → PML3 → PML2 → PML1 page table hierarchy
- **Access Control**: Read/Write/Execute permission enforcement
- **Hook Mechanism**: Split permissions for hidden code injection
- **VPID Support**: Virtual Processor ID for TLB optimization

### Hypercall Interface
- **VMCALL Protocol**: Secure hypercall mechanism with signature validation
- **Per-Core Execution**: Targeted hypercalls to specific processors
- **Result Collection**: Aggregated results from multi-core operations

## Learning Resources

The project includes comprehensive documentation in the `learndocs/` directory:

1. Intel VT-x Virtualization Technology Fundamentals
2. HyperDbg Project Architecture Overview
3. VMM Callback Mechanism Deep Dive
4. Debugger Communication Protocol
5. NMI Broadcasting & MTF Mechanism
6. EPT Hook Technology Deep Dive
7. HyperEvade Anti-Detection Module
8. Practice & Implementation Guide

## Use Cases

### Educational
- Learning Intel VT-x virtualization concepts
- Understanding EPT-based memory virtualization
- Studying hypervisor implementation techniques

### Security Research
- Kernel debugging and inspection
- Memory access monitoring and analysis
- System call tracing and filtering
- Anti-VM/anti-debugging evasion testing
- Performance profiling and optimization

### Development Reference
- Hypervisor design patterns
- Multi-core synchronization techniques
- Hardware-assisted virtualization APIs

## Technical Details

### Key Components

| Component | Description |
|-----------|-------------|
| **Vmx.c** | VMX instruction wrappers (VMREAD, VMWRITE, VMLAUNCH) |
| **Ept.c** | EPT initialization and 4-level page table management |
| **Vmexit.c** | VM-Exit dispatcher and handler routing |
| **EptHook.c** | EPT-based memory hook implementation |
| **SyscallCallback.c** | System call interception via EFER hooks |
| **Broadcast.c** | NMI-based multi-core synchronization |
| **Transparency.c** | Anti-detection and stealth mechanisms |
| **HyperCall.c** | Hypercall handler and VMCALL dispatcher |

### Assembly Modules
- `AsmVmxOperation.asm`: Low-level VMX operations
- `AsmVmexitHandler.asm`: VM-Exit entry point
- `AsmVmxContextState.asm`: Guest register save/restore
- `AsmEpt.asm`: EPT-specific operations
- `AsmInterruptHandlers.asm`: Interrupt handling

## Code Statistics

- **Total Files**: 317+
- **C Source Files**: 62
- **Header Files**: 58
- **Assembly Files**: 8
- **Documentation**: 11 comprehensive markdown guides
- **Estimated LOC**: 100,000+ lines

## License

This project is licensed under the GNU General Public License v3.0 - see the LICENSE file for details.

## Acknowledgments

- Based on/inspired by the **HyperDbg** architecture
- Uses **Zydis** disassembly engine for instruction analysis
- Intel VT-x documentation and specifications
- Windows Driver Development Kit (WDK) community

## Disclaimer

This software is provided for **educational and research purposes only**. The authors are not responsible for any misuse or damage caused by this software. Only use this hypervisor in authorized testing environments such as:
- Personal learning and experimentation
- CTF competitions
- Authorized penetration testing engagements
- Security research with proper authorization
- Defensive security implementations

**DO NOT** use this software for:
- Malicious purposes or unauthorized access
- Production environments without thorough testing
- Evading detection for illegal activities
- Any activity that violates applicable laws

## Support & Contributing

This is an educational project. For issues, questions, or contributions:
1. Review the documentation in `learndocs/`
2. Check existing issues before creating new ones
3. Provide detailed information when reporting bugs
4. Follow Windows kernel coding conventions for contributions

## Safety Notes

- Always test in a virtual machine or isolated environment first
- Enable kernel debugging during development
- Use checked (debug) builds for initial testing
- Monitor system stability and performance
- Keep backups before loading kernel drivers
- Disable driver signature enforcement only in test environments

---

**Star this project if you find it useful for learning hypervisor development!**
