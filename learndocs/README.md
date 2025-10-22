# HyperDbg深度学习文档

## 📚 文档概览

本目录包含HyperDbg项目的完整学习文档，基于深度技术对话整理而成，涵盖从Intel VT-x基础到高级反检测技术的完整知识体系。

## 📁 文档结构

```
docs/
├── HyperDbg深度学习文档.md              - 📖 总目录（从这里开始）
├── README.md                             - 📝 本文件
│
├── 第一章-Intel-VT-x虚拟化技术基础.md    - 🔰 VT-x基础概念
├── 第二章-HyperDbg项目架构概览.md        - 🏗️  项目架构
├── 第三章-VMM回调机制详解.md             - 🔄 回调系统
├── 第四章-调试器通信机制.md              - 📡 通信协议
├── 第五章-NMI广播与MTF机制.md            - ⚡ 多核调试
├── 第六章-EPT-Hook技术深入.md            - 🎣 Hook技术
├── 第七章-HyperEvade反检测模块.md        - 🛡️  反检测
└── 第八章-实践与总结.md                  - 🎯 实践指南
```

## 🚀 快速开始

### 新手入门

1. **从总目录开始**：[HyperDbg深度学习文档.md](./HyperDbg深度学习文档.md)
2. **按顺序阅读**：第一章 → 第八章
3. **重点章节**：
   - 第一章：理解VT-x基础（必读）
   - 第五章：理解NMI和MTF（重点）
   - 第六章：掌握EPT Hook（核心）

### 有经验的开发者

直接跳转到感兴趣的章节：
- **架构设计** → [第二章](./第二章-HyperDbg项目架构概览.md)、[第三章](./第三章-VMM回调机制详解.md)
- **调试技术** → [第四章](./第四章-调试器通信机制.md)、[第五章](./第五章-NMI广播与MTF机制.md)
- **Hook技术** → [第六章](./第六章-EPT-Hook技术深入.md)
- **反检测** → [第七章](./第七章-HyperEvade反检测模块.md)

### 解决特定问题

使用Ctrl+F在各章节中搜索关键词：
- `VMCS配置` → 第一章
- `回调初始化` → 第三章
- `串口发送` → 第四章
- `NMI广播` → 第五章
- `隐藏断点` → 第六章
- `CPUID伪造` → 第七章

## 📊 文档统计

```
总字数：   ~100,000字
总页数：   ~300页（A4纸估算）
代码示例： 200+个
图表：     50+个
章节：     8章
小节：     80+节
```

## 🎯 学习路径

### 路径1：理论学习（2-3周）
```
第一章（基础）
  ↓
第二章（架构）
  ↓
第三章（回调）
  ↓
第七章（反检测）
  ↓
第八章（总结）
```

### 路径2：实践导向（1-2个月）
```
第一章（基础）
  ↓
第二章（架构）
  ↓ 编译运行HyperDbg
第六章（EPT Hook）
  ↓ 实现简单Hook
第五章（NMI和MTF）
  ↓ 实现单步调试
第七章（反检测）
  ↓ 测试透明模式
第八章（实践项目）
  ↓ 完整项目开发
```

### 路径3：问题解决（按需）
```
遇到具体问题
  ↓
查看第八章的问题解决方案
  ↓
在相关章节查找详细信息
  ↓
查看代码示例
  ↓
解决问题
```

## 🔍 关键技术索引

### VMX相关
- VMXON/VMXOFF → 第一章 1.2.2
- VMLAUNCH/VMRESUME → 第一章 1.4.4
- VMREAD/VMWRITE → 第一章 1.2.2
- VMCS配置 → 第一章 1.3.4

### EPT相关
- EPT页表结构 → 第一章 1.5.2
- EPT初始化 → 第一章 1.5.5
- EPT Hook → 第六章 6.2
- EPT Violation → 第一章 1.5.6

### 调试相关
- 断点处理 → 第三章 3.6.3
- 单步执行 → 第五章 5.3.4
- NMI暂停 → 第五章 5.2
- 串口通信 → 第四章 4.3

### 反检测相关
- CPUID伪造 → 第七章 7.3.1
- 系统调用Hook → 第七章 7.4
- 透明模式 → 第七章 7.2

## 💻 代码示例导航

### 完整示例（可直接运行）
- 最小Hypervisor → 第八章 8.7.1
- EPT Hook示例 → 第八章 8.7.2
- 隐藏断点 → 第六章 6.6

### 核心代码分析
- VmxVmexitHandler → 第一章 1.4.3
- AsmVmexitHandler → 第一章 1.4.3
- EptInitialize → 第一章 1.5.5
- MtfHandleVmexit → 第五章 5.3.3

### 实用工具代码
- 调试日志 → 第八章 8.2.2
- 性能统计 → 第八章 8.2.3
- 错误处理 → 第八章 8.3

## 🛠️ 使用建议

### 阅读工具推荐

- **Markdown阅读器**：
  - VS Code（推荐，支持预览和导航）
  - Typora（所见即所得）
  - Obsidian（支持图谱）

- **代码高亮**：
  - 使用支持C/C++语法高亮的编辑器
  - 代码块已正确标记语言类型

### 配合源码阅读

```
1. 克隆HyperDbg仓库：
   git clone https://github.com/HyperDbg/HyperDbg.git

2. 在VS Code中打开：
   - 左侧：HyperDbg源码
   - 右侧：本文档

3. 对照阅读：
   - 文档引用了具体的文件路径
   - 可以直接跳转到源码位置
```

### 实践环境准备

```
硬件要求：
- 支持Intel VT-x的CPU
- 至少8GB内存
- 建议使用物理机（不要在虚拟机中嵌套）

软件要求：
- Windows 10/11 x64
- Visual Studio 2019/2022
- Windows Driver Kit (WDK)
- WinDbg（调试工具）

测试环境：
- 建议使用测试机或虚拟机
- 启用测试签名模式
- 准备好系统还原点
```

## ❓ 常见问题

### Q1: 文档太长，如何快速入门？
**A**: 阅读第一章和第八章，然后直接实践第八章的初级项目。

### Q2: 需要什么基础知识？
**A**:
- C/C++编程（必需）
- x64汇编基础（重要）
- Windows内核开发（推荐）
- 操作系统原理（推荐）

### Q3: 文档中的代码可以直接使用吗？
**A**: 大部分代码是从HyperDbg源码提取或基于源码简化的，可以作为参考，但建议查看实际源码获取最新版本。

### Q4: 如何贡献改进？
**A**:
1. 发现错误或有改进建议时，记录下来
2. 提交到HyperDbg的GitHub Issues
3. 或直接提交Pull Request

### Q5: 文档会更新吗？
**A**: 本文档基于2025年10月的HyperDbg版本，如果项目有重大更新，文档可能需要相应更新。

## 📖 术语表

快速查找常用术语：

| 术语 | 全称 | 说明 |
|------|------|------|
| VT-x | Virtualization Technology for x86 | Intel虚拟化技术 |
| VMM | Virtual Machine Monitor | 虚拟机监视器/Hypervisor |
| VMX | Virtual Machine Extensions | 虚拟机扩展 |
| VMCS | Virtual Machine Control Structure | 虚拟机控制结构 |
| EPT | Extended Page Tables | 扩展页表 |
| MTF | Monitor Trap Flag | 监控陷阱标志 |
| NMI | Non-Maskable Interrupt | 不可屏蔽中断 |
| DPC | Deferred Procedure Call | 延迟过程调用 |
| GVA | Guest Virtual Address | Guest虚拟地址 |
| GPA | Guest Physical Address | Guest物理地址 |
| HPA | Host Physical Address | Host物理地址 |

## 🎓 学习检查清单

完成学习后，检查是否达到以下目标：

**基础知识** □
- [ ] 理解VMX Root和Non-root模式
- [ ] 能够解释VM-Exit和VM-Entry
- [ ] 了解VMCS的结构和字段
- [ ] 理解EPT的工作原理

**代码理解** □
- [ ] 能够阅读Vmexit.c的处理流程
- [ ] 理解AsmVmexitHandler.asm的寄存器保存
- [ ] 掌握回调机制的设计
- [ ] 了解EPT Hook的实现

**实践能力** □
- [ ] 能够编译HyperDbg
- [ ] 能够使用WinDbg调试
- [ ] 能够实现简单的CPUID Hook
- [ ] 能够设置基本的EPT Hook

**高级技术** □
- [ ] 理解MTF与EPT Hook的配合
- [ ] 掌握隐藏断点的实现
- [ ] 了解HyperEvade的反检测技术
- [ ] 能够处理多核同步问题

## 🔗 链接索引

### 外部链接
- [HyperDbg GitHub](https://github.com/HyperDbg/HyperDbg)
- [Intel SDM下载](https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html)
- [HyperDbg官方文档](https://docs.hyperdbg.org)
- [OSDev Wiki](https://wiki.osdev.org/)

### 内部章节
- [总目录](./HyperDbg深度学习文档.md)
- [第一章](./第一章-Intel-VT-x虚拟化技术基础.md)
- [第二章](./第二章-HyperDbg项目架构概览.md)
- [第三章](./第三章-VMM回调机制详解.md)
- [第四章](./第四章-调试器通信机制.md)
- [第五章](./第五章-NMI广播与MTF机制.md)
- [第六章](./第六章-EPT-Hook技术深入.md)
- [第七章](./第七章-HyperEvade反检测模块.md)
- [第八章](./第八章-实践与总结.md)

## 💡 使用提示

1. **首次阅读**：从[总目录](./HyperDbg深度学习文档.md)开始
2. **查找特定主题**：使用文档内的搜索功能（Ctrl+F）
3. **实践学习**：每章末尾都有相关的实践建议
4. **代码对照**：文档中的代码可以在HyperDbg源码中找到对应位置

## 📞 获取帮助

- **技术问题**：查看第八章的"常见问题"部分
- **Bug报告**：提交到HyperDbg的GitHub Issues
- **讨论交流**：加入HyperDbg Discord社区

---

**祝学习愉快！** 🎉

开始阅读 → [HyperDbg深度学习文档](./HyperDbg深度学习文档.md)
