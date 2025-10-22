# SimpleHv - 教育型 Windows 虚拟机监控器驱动

**简体中文 | [English](README_EN.md)**

![License](https://img.shields.io/badge/license-GPL--3.0-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-lightgrey.svg)
![VT-x](https://img.shields.io/badge/Intel-VT--x-0071c5.svg)

一个基于 Intel VT-x 虚拟化技术的教育型内核模式虚拟机监控器（Hypervisor）驱动，实现了包括 EPT 内存钩子、多核同步、系统调用拦截和反检测等高级功能。

## 项目概述

SimpleHv 是一个面向学习的虚拟机监控器项目，在展示核心虚拟化概念的同时实现了生产级别的功能特性。项目基于成熟的 HyperDbg 架构构建，为 Windows 内核开发者和安全研究人员提供了学习硬件辅助虚拟化技术的完整参考实现。

## 核心特性

### 基础虚拟机监控器能力
- **Intel VT-x 实现**：完整的 VMCS 配置、VM-Entry/Exit 处理和客户机状态管理
- **扩展页表（EPT）**：支持 4KB/2MB/1GB 页面的四级 EPT 分页
- **多核支持**：基于 NMI 广播机制的跨 CPU 核心同步操作
- **VM-Exit 处理器**：全面处理 MSR、I/O、CPUID 等拦截事件

### 高级功能
- **基于 EPT 的内存钩子**：无需修改客户机页表的隐藏式内存拦截
- **系统调用拦截**：通过 EFER 寄存器操作实现的透明 SYSCALL/SYSRET 挂钩
- **执行监控**：基于模式的执行钩子、MTF 单步执行和隐藏断点
- **脏页日志**：用于内存监控的页级写访问跟踪
- **MMIO 影子化**：内存映射 I/O 拦截和虚拟化

### HyperEvade - 反检测模块
- CPUID 欺骗以隐藏虚拟机监控器存在
- RDTSC 时序攻击缓解
- VMX 指令足迹移除
- 反虚拟机工具检测绕过
- 用于隐蔽操作的透明进程模式

## 项目结构

```
SimpleHv/
├── code/                    # 实现文件
│   ├── assembly/           # 底层 VMX/EPT 操作（MASM 汇编）
│   ├── vmm/                # 核心 VMX 和 EPT 实现
│   ├── hooks/              # EPT 钩子和系统调用钩子
│   ├── memory/             # 内存管理和 EPT 处理
│   ├── broadcast/          # 多核心同步
│   ├── transparency/       # 反检测实现
│   └── interface/          # 驱动通信和超级调用
├── header/                 # 头文件（镜像 code/ 结构）
├── include/                # SDK 头文件和平台抽象
├── dependencies/           # 第三方库
│   ├── zydis/             # 指令反汇编引擎
│   ├── keystone/          # 指令汇编引擎
│   └── ia32-doc/          # Intel 指令/CPUID 定义
├── usermode/              # 用户模式测试客户端
├── learndocs/             # 详尽的学习文档（中文）
└── SimpleHv.sln           # Visual Studio 解决方案
```

## 环境要求

### 硬件要求
- 支持 VT-x（虚拟化技术）的 Intel 处理器
- x64 架构
- 建议 8GB 及以上内存

### 软件要求
- Windows 10/11 x64
- Visual Studio 2019/2022
- Windows Driver Kit (WDK) 10.0 或更高版本
- 启用测试模式以加载未签名驱动

### 依赖项
- **Zydis**（v4.1+）：指令反汇编引擎（已包含）
- **Keystone Engine**：指令汇编（已包含头文件）
- **Windows WDK**：内核开发头文件和库

## 编译构建

### 构建步骤
1. 在 Visual Studio 2019/2022 中打开 `SimpleHv.sln`
2. 确保已正确安装 Windows Driver Kit (WDK)
3. 选择构建配置（Debug/Release）和平台（x64）
4. 构建解决方案（Ctrl+Shift+B）

### 构建输出
- **驱动程序**：`build/bin/[Debug|Release]/SimpleHv.sys`
- **用户模式测试客户端**：`build/bin/[Debug|Release]/UsermodeTest.exe`

### 清理
运行 `clean.bat` 以删除构建产物和中间文件。

## 安装与测试

### 启用测试模式
```batch
bcdedit /set testsigning on
bcdedit /set debug on
```
启用测试模式后需要重启系统。

### 加载驱动
```batch
sc create SimpleHv type= kernel binPath= "C:\path\to\SimpleHv.sys"
sc start SimpleHv
```

### 运行用户模式测试客户端
```batch
UsermodeTest.exe
```

测试应用程序将：
- 通过 PING 超级调用验证虚拟机监控器是否存在
- 测试多核通信
- 验证 HyperEvade 反检测功能

### 卸载驱动
```batch
sc stop SimpleHv
sc delete SimpleHv
```

## 配置选项

编译时配置位于 `include/config/Configuration.h`：

```c
#define ShowSystemTimeOnDebugMessages   TRUE    // 调试消息显示系统时间
#define UseWPPTracing                   FALSE   // 使用 WPP 跟踪
#define DebugMode                       FALSE   // 调试模式
#define EnableInstantEventMechanism     TRUE    // 启用即时事件机制
#define ActivateUserModeDebugger        TRUE    // 激活用户模式调试器
#define ActivateHyperEvadeProject       TRUE    // 激活 HyperEvade 项目
```

## 架构概览

### VMX 操作
- **VMXON 区域**：每核心 VMX 激活和 VMCS 管理
- **客户机状态**：VM-Exit/Entry 时完整的 CPU 寄存器保存/恢复
- **VM-Exit 处理**：将退出事件分发到专门的处理器

### EPT 实现
- **四级分页**：PML4 → PML3 → PML2 → PML1 页表层次结构
- **访问控制**：读/写/执行权限强制执行
- **钩子机制**：用于隐藏代码注入的分离权限
- **VPID 支持**：用于 TLB 优化的虚拟处理器 ID

### 超级调用接口
- **VMCALL 协议**：带签名验证的安全超级调用机制
- **每核心执行**：针对特定处理器的定向超级调用
- **结果收集**：来自多核操作的聚合结果

## 学习资源

项目在 `learndocs/` 目录下包含详尽的文档：

1. Intel VT-x 虚拟化技术基础
2. HyperDbg 项目架构概览
3. VMM 回调机制深入剖析
4. 调试器通信协议
5. NMI 广播与 MTF 机制
6. EPT Hook 技术深入剖析
7. HyperEvade 反检测模块
8. 实践与实现指南

## 应用场景

### 教育用途
- 学习 Intel VT-x 虚拟化概念
- 理解基于 EPT 的内存虚拟化
- 研究虚拟机监控器实现技术

### 安全研究
- 内核调试和检查
- 内存访问监控和分析
- 系统调用跟踪和过滤
- 反虚拟机/反调试规避测试
- 性能分析和优化

### 开发参考
- 虚拟机监控器设计模式
- 多核同步技术
- 硬件辅助虚拟化 API

## 技术细节

### 核心组件

| 组件 | 描述 |
|------|------|
| **Vmx.c** | VMX 指令包装器（VMREAD、VMWRITE、VMLAUNCH） |
| **Ept.c** | EPT 初始化和四级页表管理 |
| **Vmexit.c** | VM-Exit 分发器和处理器路由 |
| **EptHook.c** | 基于 EPT 的内存钩子实现 |
| **SyscallCallback.c** | 通过 EFER 钩子实现的系统调用拦截 |
| **Broadcast.c** | 基于 NMI 的多核同步 |
| **Transparency.c** | 反检测和隐蔽机制 |
| **HyperCall.c** | 超级调用处理器和 VMCALL 分发器 |

### 汇编模块
- `AsmVmxOperation.asm`：底层 VMX 操作
- `AsmVmexitHandler.asm`：VM-Exit 入口点
- `AsmVmxContextState.asm`：客户机寄存器保存/恢复
- `AsmEpt.asm`：EPT 特定操作
- `AsmInterruptHandlers.asm`：中断处理

## 代码统计

- **总文件数**：317+
- **C 源文件**：62 个
- **头文件**：58 个
- **汇编文件**：8 个
- **文档**：11 篇详尽的 Markdown 指南
- **估计代码行数**：100,000+ 行

## 许可证

本项目采用 GNU 通用公共许可证 v3.0 - 详见 LICENSE 文件。

## 致谢

- 基于/受启发于 **HyperDbg** 架构
- 使用 **Zydis** 反汇编引擎进行指令分析
- Intel VT-x 文档和规范
- Windows 驱动开发工具包（WDK）社区

## 免责声明

本软件**仅供教育和研究目的**。作者对本软件造成的任何滥用或损害概不负责。请仅在授权的测试环境中使用此虚拟机监控器，例如：
- 个人学习和实验
- CTF 竞赛
- 授权的渗透测试任务
- 经过适当授权的安全研究
- 防御性安全实施

**请勿**将本软件用于：
- 恶意目的或未经授权的访问
- 未经充分测试的生产环境
- 为非法活动规避检测
- 任何违反适用法律的活动

## 支持与贡献

这是一个教育项目。对于问题、疑问或贡献：
1. 查看 `learndocs/` 中的文档
2. 在创建新问题之前检查现有问题
3. 报告错误时提供详细信息
4. 贡献时遵循 Windows 内核编码规范

## 安全注意事项

- 始终先在虚拟机或隔离环境中测试
- 开发期间启用内核调试
- 初始测试使用检查版（debug）构建
- 监控系统稳定性和性能
- 加载内核驱动前保持备份
- 仅在测试环境中禁用驱动程序签名强制

## 快速开始

### 1. 检查硬件支持
```batch
# 检查 CPU 是否支持 VT-x
wmic cpu get VirtualizationFirmwareEnabled
```

### 2. 准备开发环境
- 安装 Visual Studio 2019/2022
- 安装 Windows SDK 和 WDK
- 配置内核调试环境（推荐使用双机调试）

### 3. 编译项目
```batch
# 使用 MSBuild 命令行编译
msbuild SimpleHv.sln /p:Configuration=Release /p:Platform=x64
```

### 4. 部署测试
```batch
# 启用测试签名
bcdedit /set testsigning on

# 重启系统
shutdown /r /t 0

# 加载驱动
sc create SimpleHv type= kernel binPath= "完整路径\SimpleHv.sys"
sc start SimpleHv

# 运行测试程序
UsermodeTest.exe
```

## 常见问题

### Q: 驱动加载失败怎么办？
A: 确保已启用测试模式，并且以管理员权限运行命令。检查 `dmesg` 或事件查看器中的错误日志。

### Q: 系统蓝屏怎么办？
A: 这是内核开发的正常现象。配置内核调试器（WinDbg）来分析崩溃转储文件，定位问题代码。

### Q: 如何在虚拟机中测试？
A: 需要宿主机支持嵌套虚拟化（Nested Virtualization）。VMware Workstation 和 Hyper-V 都支持此功能。

### Q: 性能影响有多大？
A: 作为教育型虚拟机监控器，性能不是主要考虑因素。生产环境请谨慎使用。

### Q: 支持 AMD 处理器吗？
A: 不支持。本项目专门针对 Intel VT-x 技术。AMD-V 需要不同的实现。

## 进阶主题

### 自定义 EPT Hook
参考 `code/hooks/EptHook.c` 实现自己的内存钩子：
```c
// 示例：钩子特定内存地址
BOOLEAN CustomEptHook(PVOID TargetAddress) {
    // 1. 获取 EPT 页表项
    // 2. 修改页表权限
    // 3. 设置钩子处理器
    // 4. 刷新 EPT TLB
}
```

### 系统调用过滤
参考 `code/hooks/SyscallCallback.c` 过滤特定系统调用：
```c
// 示例：拦截 NtCreateFile
NTSTATUS SyscallFilter(UINT32 SyscallNumber, PVOID Parameters) {
    if (SyscallNumber == SYSCALL_NTCREATEFILE) {
        // 自定义处理逻辑
    }
}
```

### 添加自定义 VM-Exit 处理器
参考 `code/vmm/vmx/Vmexit.c` 添加新的退出原因处理：
```c
// 在 VmExitDispatcher 中添加新的 case
case EXIT_REASON_YOUR_REASON:
    YourCustomHandler(GuestRegs);
    break;
```

## 调试技巧

### 1. 启用内核调试输出
在 `Configuration.h` 中设置：
```c
#define DebugMode TRUE
#define ShowSystemTimeOnDebugMessages TRUE
```

### 2. 使用 WinDbg 调试
```batch
# 宿主机运行 WinDbg
windbg -k net:port=50000,key=1.2.3.4

# 目标机配置
bcdedit /debug on
bcdedit /dbgsettings net hostip:宿主机IP port:50000 key:1.2.3.4
```

### 3. 分析崩溃转储
```batch
# 使用 WinDbg 打开 MEMORY.DMP
!analyze -v
```

## 性能优化建议

1. **减少 VM-Exit 频率**：仅拦截必要的事件
2. **使用 VPID**：避免频繁的 TLB 刷新
3. **优化 EPT 结构**：使用大页（2MB/1GB）减少页表遍历
4. **缓存 VMCS 字段**：减少 VMREAD/VMWRITE 次数
5. **批处理操作**：将多个操作合并到一次 VM-Exit

## 相关资源

### 官方文档
- [Intel VT-x 规范](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Windows Driver Kit 文档](https://docs.microsoft.com/en-us/windows-hardware/drivers/)

### 推荐阅读
- 《Intel 64 and IA-32 Architectures Software Developer's Manual Volume 3》
- 《Windows Internals》
- 《Rootkits and Bootkits》

### 相关项目
- [HyperDbg](https://github.com/HyperDbg/HyperDbg) - 高级内核调试器
- [Bochs](https://bochs.sourceforge.io/) - x86 模拟器（学习参考）
- [KVM](https://www.linux-kvm.org/) - Linux 内核虚拟化

## 更新日志

查看 Git 提交历史了解详细的更新记录。

## 联系方式

- 问题反馈：通过 GitHub Issues
- 技术讨论：参考 `learndocs/` 文档

---

**如果本项目对你学习虚拟机监控器开发有帮助，请给个 Star！**

## 附录：术语对照表

| 英文术语 | 中文译名 | 说明 |
|---------|---------|------|
| Hypervisor | 虚拟机监控器 | 运行在最高权限级别的软件层 |
| VMX | 虚拟机扩展 | Intel 的硬件虚拟化技术 |
| EPT | 扩展页表 | 用于客户机物理地址转换的硬件机制 |
| VMCS | 虚拟机控制结构 | 存储 VM 状态的数据结构 |
| VM-Exit | 虚拟机退出 | 从客户机模式切换到 VMM |
| VM-Entry | 虚拟机进入 | 从 VMM 切换到客户机模式 |
| Guest | 客户机 | 运行在虚拟机中的操作系统 |
| Host | 宿主机 | 运行虚拟机监控器的物理机 |
| VPID | 虚拟处理器标识符 | 用于优化 TLB 的硬件特性 |
| MTF | 监控陷阱标志 | 用于单步执行的硬件机制 |
| NMI | 不可屏蔽中断 | 最高优先级的硬件中断 |
| DPC | 延迟过程调用 | Windows 内核的异步执行机制 |
| MMIO | 内存映射 I/O | 通过内存地址访问外设 |
| Hypercall | 超级调用 | 客户机与 VMM 通信的机制 |
