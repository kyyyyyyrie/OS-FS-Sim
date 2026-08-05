# 操作系统文件系统模拟器

一个基于 C++ 实现的操作系统课程项目，模拟了完整的文件系统核心模块，包括存储管理、缓冲池、目录/文件操作、进程调度与进程间通信，并提供了 PyQt6 图形界面与命令行交互两种使用方式。

---

## 项目结构

```
E:\OS-FS-Sim
├── src/
│   ├── main.cpp                 # 主入口（交互式命令行测试工具）
│   ├── common/
│   │   ├── common.hpp           # 全局常量、枚举、结构体定义
│   │   └── common.cpp           # 通用工具函数实现
│   ├── backend/
│   │   ├── storage/
│   │   │   ├── disk.hpp / .cpp           # 模拟磁盘管理
│   │   │   └── fat_table.hpp / .cpp      # FAT 文件分配表
│   │   ├── buffer/
│   │   │   └── buffer_pool.hpp / .cpp    # 缓冲池管理
│   │   ├── fs_core/
│   │   │   ├── directory.hpp / .cpp      # 目录管理
│   │   │   └── file_interface.hpp / .cpp  # 文件操作统一接口
│   │   └── concurrency/
│   │       ├── sync.hpp / .cpp           # 同步原语（互斥锁、信号量）
│   │       ├── process.hpp / .cpp        # 进程管理与时间片轮转调度
│   │       ├── ipc.hpp / .cpp            # 进程间消息队列通信
│   │       └── json.hpp                  # nlohmann/json 单头库
│   └── frontend/
│       └── gui.py               # PyQt6 图形前端界面
├── mock/                        # 测试用 Mock 模块
├── test/                        # 各模块单元测试
├── build/
│   ├── bin/                     # 编译输出可执行文件
│   └── obj/                     # 编译中间文件
└── mock_disk.bin                # 模拟磁盘镜像文件
```

---

## 核心模块

### 1. 存储管理（Storage）
- **Disk**：模拟磁盘设备，提供块级读写接口，支持初始化、读写盘块。
- **FATTable**：基于 FAT（文件分配表）的磁盘块管理，负责空闲块分配、文件块链维护与回收。

### 2. 缓冲池管理（Buffer Pool）
- **BufferPool**：实现固定大小的缓冲页池，支持按块号置换、脏页写回、互斥访问控制，提升磁盘 I/O 效率。

### 3. 文件系统核心（FS Core）
- **Directory**：目录树管理，支持多级目录创建、删除、切换、递归列举、路径解析、FCB 查询、文件加锁/解锁。
- **FileInterface**：对外统一接口，封装底层存储细节，提供文件创建、写入、读取、查看块、修改块、删除、截断、查询物理块分布等能力。

### 4. 并发控制（Concurrency）
- **Sync**：提供互斥锁（Mutex）与信号量（Semaphore）同步原语，保障多线程/多进程环境下的资源安全。
- **ProcessManager**：进程管理核心，支持创建进程、时间片轮转调度、就绪队列管理、进程终止。
- **MessageQueue**：基于消息队列的进程间通信（IPC），支持点对点发送与广播接收，消息类型包括执行结果与自定义消息。

### 5. 前端界面（Frontend）
- **gui.py**：基于 PyQt6 的图形化前端，通过子进程启动后端可执行文件，实时读取输出并解析提示符，提供类似终端的交互体验。

---

## 快速开始

### 环境要求
- Windows 操作系统
- MinGW-w64（g++ 15.2.0 或兼容版本）
- Python 3.10+
- PyQt6

### 编译项目

使用 g++ 编译（需 UTF-8 编码支持）：

```powershell
cd E:\OS-FS-Sim
g++ -std=c++17 -I"src" -I"mock" `
  src\main.cpp `
  src\backend\fs_core\directory.cpp `
  src\backend\fs_core\file_interface.cpp `
  src\backend\storage\disk.cpp `
  src\common\common.cpp `
  src\backend\concurrency\sync.cpp `
  src\backend\storage\fat_table.cpp `
  src\backend\buffer\buffer_pool.cpp `
  src\backend\concurrency\process.cpp `
  src\backend\concurrency\ipc.cpp `
  -o build\bin\os_main.exe
```

> 注：源码中已使用 nlohmann/json 单头库（`src/backend/concurrency/json.hpp`），无需额外安装。

### 运行方式

#### 1. 命令行交互模式

直接运行编译后的可执行文件：

```powershell
E:\OS-FS-Sim\build\bin\os_main.exe
```

支持的命令包括：

| 分类 | 命令 | 说明 |
|------|------|------|
| 目录操作 | `pwd` | 查看当前工作目录 |
| | `ls [路径]` | 查看目录内容 |
| | `tree` | 递归列出目录树 |
| | `mkdir <路径>` | 创建多级目录 |
| | `cd <路径>` | 切换目录 |
| | `rmdir [-f] <路径>` | 删除目录 |
| 文件操作 | `touch <路径>` | 创建空文件 |
| | `write <路径> <内容>` | 写入文件内容 |
| | `read <路径>` | 读取完整文件内容 |
| | `cat <路径> <块号>` | 查看指定逻辑块内容 |
| | `edit <路径> <块号> <内容>` | 修改文件块 |
| | `rm <路径>` | 删除文件 |
| 高级操作 | `blocks <路径>` | 查看文件物理块分布 |
| | `stat <路径>` | 查询文件 FCB 详细信息 |
| | `lock <路径>` | 锁定文件 |
| | `unlock <路径>` | 解锁文件 |
| | `truncate <路径> <块号>` | 截断文件到指定逻辑块 |
| 进程调度 | `start_scheduler` | 启动时间片轮转调度器 |
| | `stop_scheduler` | 停止调度器并终止所有进程 |
| | `create_proc <类型> <JSON>` | 创建进程执行文件操作 |
| | `list_procs` | 查看所有进程状态 |
| | `recv_msg [PID]` | 接收进程执行结果 |
| | `send_msg <PID> <内容>` | 发送消息到指定进程 |
| 通用 | `help` | 查看帮助信息 |
| | `clear` | 清空控制台 |
| | `exit` | 退出程序 |

#### 2. 图形界面模式

```powershell
cd E:\OS-FS-Sim
python src/frontend/gui.py
```

图形界面自动连接后端 `build/bin/os_main.exe`，提供带命令历史、目录树可视化、文件信息表格的交互式终端。

---

## 参数配置

核心常量定义在 `src/common/common.hpp` 中，可根据需要调整：

| 常量 | 默认值 | 说明 |
|------|--------|------|
| `BLOCK_SIZE` | 128 字节 | 单个盘块大小 |
| `TOTAL_BLOCKS` | 1024 | 磁盘总块数 |
| `BUFFER_PAGE_NUM` | 8 | 缓冲池页数 |
| `METADATA_BLOCKS` | 64 | 元数据区占用块数 |
| `BLOCK_WRITE_DELAY_MS` | 1000 ms | 块间写入延时 |
| `BUFFER_SWAP_DELAY_MS` | 1000 ms | 缓冲页换页延时 |

---

## 测试

项目包含多个测试程序，位于 `test/` 目录：

- `test_directory.cpp`：目录模块单元测试
- `test_file.cpp`：文件操作接口单元测试
- `test_interactive.cpp`：交互式测试（Mock 模式）
- `test_interactive_full.cpp`：完整交互式测试（真实磁盘模式）

---

## 技术栈

- **语言**：C++17
- **GUI**：Python 3 + PyQt6
- **依赖库**：nlohmann/json（单头 JSON 库）
- **平台**：Windows（含 Windows API 控制台适配）

---

## 注意事项

- 项目默认使用 `mock_disk.bin` 作为磁盘镜像，首次运行会自动初始化。
- 进程调度模块使用 Windows 线程与信号量实现时间片轮转。
- 图形界面与后端通过标准输入输出管道通信，需保持后端可执行文件路径正确。
