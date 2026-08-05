// #pragma once
// #include "../../common/common.hpp"
// #include "../fs_core/file_interface.hpp"
// #include "../concurrency/ipc.hpp"
// #include <thread>
// #include <queue>
// #include <map>
// #include <windows.h>
// using namespace std;

// /**
//  * @brief 进程/线程管理器类（Windows适配版，用thread替代pthread）
//  * @note 时间片轮转调度逻辑不变，仅适配Windows线程ID
//  */

// /**
//  * @brief 构造函数：初始化进程管理器
//  * @param fi 文件/目录接口实例
//  * @param mq 消息队列实例
//  */

// /**
//  * @brief 析构函数：停止调度器，终止所有进程
//  */

// /**
//  * @brief 创建命令进程（线程）
//  * @param type 命令类型
//  * @param args 命令参数（JSON格式）
//  * @return 新建线程的ID（Windows DWORD）
//  */

// /**
//  * @brief 启动调度器
//  */

// /**
//  * @brief 停止所有进程和调度器
//  */

// class ProcessManager
// {
// private:
//   FileInterface &file_interface;  // 关联的文件/目录接口
//   MessageQueue &msg_queue;        // 关联的消息队列
//   queue<pid_t> ready_queue;       // 就绪进程队列
//   Semaphore schedule_sem;         // 调度器同步信号量
//   map<pid_t, thread> process_map; // 线程ID→线程映射
//   int time_slice = 100;           // 时间片大小（毫秒）
//   bool is_scheduler_running;      // 调度器是否运行

//   // 在 ProcessManager 类的 private 区域添加：
//   pid_t next_pid = 1001;                    // 全局唯一PID生成器（从1001开始）
//   map<pid_t, atomic<bool>> process_running; // 每个进程的运行标志（原子类型，线程安全）

//   // 私有方法：时间片轮转调度核心逻辑
//   void schedule();

//   // 私有方法：执行具体命令
//   void run_command(CommandType type, const string &args, pid_t pid);

// public:
//   ProcessManager(FileInterface &fi, MessageQueue &mq);

//   ~ProcessManager();

//   pid_t create_process(CommandType type, const string &args);

//   void start_scheduler();

//   void stop_all_processes();

//   // 新增：供测试工具访问进程状态
//   std::map<pid_t, std::atomic<bool>> &get_process_running()
//   {
//     return process_running;
//   }

//   // 新增：设置/获取时间片
//   void set_time_slice(int ms)
//   {
//     time_slice = ms;
//   }
//   int get_time_slice() const
//   {
//     return time_slice;
//   }
// };

// #pragma once
// // 1. 先包含自定义Semaphore所在的sync.hpp（关键！路径需匹配你的项目结构）
// #include "../concurrency/sync.hpp"
// // 2. 补充原子类型头文件（必须）
// #include <atomic>
// #include <mutex>
// // 3. 原有头文件（保持不变）
// #include "../../common/common.hpp"
// #include "../fs_core/file_interface.hpp"
// #include "ipc.hpp"
// #include <thread>
// #include <queue>
// #include <map>
// #include <windows.h>
// using namespace std;

// /**
//  * @brief 进程/线程管理器类（Windows适配版，用thread替代pthread）
//  * @note 时间片轮转调度逻辑不变，仅适配Windows线程ID
//  */
// class ProcessManager
// {
// private:
//   FileInterface &file_interface; // 关联的文件/目录接口
//   MessageQueue &msg_queue;       // 关联的消息队列
//   queue<pid_t> ready_queue;      // 就绪进程队列
//   // 2. 恢复为自定义Semaphore类（来自sync.hpp），无需再用HANDLE
//   Semaphore schedule_sem;         // 调度器同步信号量（适配sync.hpp的Semaphore）
//   map<pid_t, thread> process_map; // 线程ID→线程映射
//   int time_slice = 100;           // 时间片大小（毫秒）
//   bool is_scheduler_running;      // 调度器是否运行

//   // 原子类型PID生成器（线程安全，必须保留）
//   atomic<pid_t> next_pid = 1001;
//   map<pid_t, atomic<bool>> process_running; // 每个进程的运行标志（原子类型）

//   // 私有方法：时间片轮转调度核心逻辑
//   void schedule();

//   // 私有方法：执行具体命令
//   void run_command(CommandType type, const string &args, pid_t pid);

// public:
//   /**
//    * @brief 构造函数：初始化进程管理器
//    * @param fi 文件/目录接口实例
//    * @param mq 消息队列实例
//    */
//   ProcessManager(FileInterface &fi, MessageQueue &mq);

//   /**
//    * @brief 析构函数：停止调度器，终止所有进程
//    */
//   ~ProcessManager();

//   /**
//    * @brief 创建命令进程（线程）
//    * @param type 命令类型
//    * @param args 命令参数（JSON格式）
//    * @return 新建线程的ID（Windows DWORD）
//    */
//   pid_t create_process(CommandType type, const string &args);

//   /**
//    * @brief 启动调度器
//    */
//   void start_scheduler();

//   /**
//    * @brief 停止所有进程和调度器
//    */
//   void stop_all_processes();

//   // 供测试工具访问进程状态
//   std::map<pid_t, std::atomic<bool>> &get_process_running()
//   {
//     return process_running;
//   }

//   // 设置/获取时间片
//   void set_time_slice(int ms)
//   {
//     time_slice = ms;
//   }
//   int get_time_slice() const
//   {
//     return time_slice;
//   }
// };

// process.hpp
// #pragma once
// #include "../concurrency/sync.hpp" // ✅ 包含你的 Semaphore
// #include <atomic>
// #include <mutex>
// #include "../../common/common.hpp"
// #include "../fs_core/file_interface.hpp"
// #include "ipc.hpp" // 注意循环包含！
// #include <thread>
// #include <queue>
// #include <map>

// // extern std::mutex g_mtx;
// // extern std::mutex g_mtx;

// class MessageQueue; // 前置声明

// class ProcessManager
// {
// private:
//   FileInterface &file_interface;
//   MessageQueue &msg_queue;
//   Semaphore schedule_sem; // ✅ 类型正确
//   std::atomic<bool> is_scheduler_running{false};
//   int time_slice = 100;
//   std::queue<pid_t> ready_queue;
//   std::map<pid_t, std::thread> process_map;
//   std::map<pid_t, std::atomic<bool>> process_running;
//   std::atomic<pid_t> next_pid{1};

//   void schedule();
//   void run_command(CommandType type, const std::string &args, pid_t pid);

// public:
//   ProcessManager(FileInterface &fi, MessageQueue &mq);
//   ~ProcessManager();

//   pid_t create_process(CommandType type, const std::string &args);
//   void start_scheduler();
//   void stop_all_processes();

//   // 测试接口
//   std::map<pid_t, std::atomic<bool>> &get_process_running() { return process_running; }
//   void set_time_slice(int ms) { time_slice = ms; }
//   int get_time_slice() const { return time_slice; }
// };

// #pragma once
// // 1. 先包含基础头文件（避免循环依赖）
// #include <atomic>
// #include <mutex>
// #include <thread>
// #include <queue>
// #include <map>
// #include <string>

// // 2. 前置声明（解决循环包含问题）
// class FileInterface;
// class MessageQueue;
// class Semaphore;

// // 3. 引入枚举/类型定义（确保CommandType、pid_t等可识别）
// #include "../../common/common.hpp"

// // 4. 类定义（规范访问权限+补充缺失方法）
// class ProcessManager
// {
// private:
//   // ========== 私有成员（核心状态） ==========
//   FileInterface &file_interface;                      // 文件系统接口（引用不可赋值，必须初始化）
//   MessageQueue &msg_queue;                            // 消息队列（引用）
//   Semaphore &schedule_sem;                            // 调度器信号量（改为引用，避免拷贝Semaphore）
//   std::atomic<bool> is_scheduler_running;             // 调度器运行状态（原子类型）
//   int time_slice;                                     // 时间片大小（ms）
//   std::queue<pid_t> ready_queue;                      // 进程就绪队列
//   std::map<pid_t, std::thread> process_map;           // 进程ID -> 线程映射
//   std::map<pid_t, std::atomic<bool>> process_running; // 进程运行状态
//   static std::atomic<pid_t> next_pid;                 // 静态PID生成器（全局唯一）

//   // ========== 私有方法（内部逻辑） ==========
//   void schedule();                                                        // 调度器主循环
//   void run_command(CommandType type, const std::string &args, pid_t pid); // 进程执行逻辑

// public:
//   // ========== 构造/析构 ==========
//   ProcessManager(FileInterface &fi, MessageQueue &mq, Semaphore &sem); // 修正：传入Semaphore引用
//   ~ProcessManager();

//   // ========== 核心功能 ==========
//   pid_t create_process(CommandType type, const std::string &args); // 创建进程
//   void start_scheduler();                                          // 启动调度器
//   void stop_all_processes();                                       // 停止所有进程

//   // ========== 测试/访问接口（供test1.cpp调用） ==========
//   bool is_scheduler_running() const { return is_scheduler_running.load(); } // 补充：检查调度器状态
//   std::map<pid_t, std::atomic<bool>> &get_process_running() { return process_running; }
//   void set_time_slice(int ms)
//   {
//     if (ms > 0)
//       time_slice = ms;
//   } // 增加合法性校验
//   int get_time_slice() const { return time_slice; }
// };

// // ========== 全局变量声明（供process.cpp使用） ==========
// extern std::mutex g_mtx; // 保护就绪队列的全局互斥锁

// #pragma once
// // 基础头文件（避免循环依赖，按依赖顺序包含）
// #include <atomic>
// #include <mutex>
// #include <thread>
// #include <queue>
// #include <map>
// #include <string>

// // ========== 关键修复：包含Semaphore的完整定义 ==========
// #include "sync.hpp" // 替换仅有的前置声明，确保Semaphore可被实例化

// // 前置声明（解决循环包含问题）
// class FileInterface;
// class MessageQueue;
// // 移除：class Semaphore; // 不再需要前置声明，已包含完整定义

// // 引入枚举/类型定义（确保CommandType、pid_t、Message等可识别）
// #include "../../common/common.hpp"

// // 类定义（其他内容不变）
// class ProcessManager
// {
// private:
//   // ========== 私有成员（核心状态） ==========
//   FileInterface &file_interface;                      // 文件系统接口（引用）
//   MessageQueue &msg_queue;                            // 消息队列（引用）
//   Semaphore schedule_sem;                             // 现在Semaphore有完整定义，可正常实例化
//   std::atomic<bool> is_scheduler_running{false};      // 原子变量（保存状态）
//   int time_slice{100};                                // 时间片大小（ms，默认100）
//   std::queue<pid_t> ready_queue;                      // 进程就绪队列
//   std::map<pid_t, std::thread> process_map;           // 进程ID -> 线程映射
//   std::map<pid_t, std::atomic<bool>> process_running; // 进程运行状态
//   static std::atomic<pid_t> next_pid;                 // 静态PID生成器

//   // ========== 私有方法（内部逻辑） ==========
//   void schedule();                                                        // 调度器主循环
//   void run_command(CommandType type, const std::string &args, pid_t pid); // 进程执行逻辑

// public:
//   // ========== 构造/析构（参数匹配cpp定义） ==========
//   ProcessManager(FileInterface &fi, MessageQueue &mq);
//   ~ProcessManager();

//   // ========== 核心功能 ==========
//   pid_t create_process(CommandType type, const std::string &args); // 创建进程
//   void start_scheduler();                                          // 启动调度器
//   void stop_all_processes();                                       // 停止所有进程

//   // ========== 测试/访问接口（函数改名，避免与变量重名） ==========
//   bool is_scheduler_active() const { return is_scheduler_running.load(); } // 改名核心
//   std::map<pid_t, std::atomic<bool>> &get_process_running() { return process_running; }
//   void set_time_slice(int ms)
//   {
//     if (ms > 0)
//       time_slice = ms;
//   }
//   int get_time_slice() const { return time_slice; }
// };

// // ========== 全局变量声明（供process.cpp定义） ==========
// extern std::mutex g_mtx; // 保护就绪队列的全局互斥锁

// #pragma once
// // 基础头文件（避免循环依赖，按依赖顺序包含）
// #include <atomic>
// #include <mutex>
// #include <thread>
// #include <queue>
// #include <map>
// #include <string>
// #include <utility> // 新增：用于pair类型

// // ========== 关键修复：包含Semaphore的完整定义 ==========
// #include "sync.hpp" // 替换仅有的前置声明，确保Semaphore可被实例化

// // 前置声明（解决循环包含问题）
// class FileInterface;
// class MessageQueue;

// // 引入枚举/类型定义（确保CommandType、pid_t、Message等可识别）
// #include "../../common/common.hpp"

// // 类定义
// class ProcessManager
// {
// private:
//   // ========== 私有成员（核心状态：对齐cpp实现） ==========
//   FileInterface &file_interface;                                     // 文件系统接口（引用）
//   MessageQueue &msg_queue;                                           // 消息队列（引用）
//   Semaphore schedule_sem;                                            // Semaphore完整定义，可正常实例化
//   std::atomic<bool> is_scheduler_running{false};                     // 原子变量（保存状态）
//   int time_slice{100};                                               // 时间片大小（ms，默认100）
//   std::queue<pid_t> ready_queue;                                     // 进程就绪队列（保留，若后续调度用）
//   std::map<pid_t, std::thread> process_map;                          // 进程ID -> 线程映射（保留）
//   std::map<pid_t, std::atomic<bool>> process_running;                // 进程运行状态
//   std::map<pid_t, std::pair<CommandType, std::string>> process_args; // 新增：cpp中用到的进程参数存储
//   std::atomic<pid_t> next_pid{1};                                    // 修正：移除static，改为成员变量（对齐cpp）

//   // ========== 私有方法（内部逻辑：方法名对齐cpp实现） ==========
//   pid_t generate_pid();        // 新增：cpp中用到的PID生成方法
//   void scheduler_loop();       // 修正：原schedule()改为scheduler_loop（对齐cpp）
//   void run_process(pid_t pid); // 修正：原run_command()改为run_process（对齐cpp）

// public:
//   // ========== 构造/析构（参数匹配cpp定义） ==========
//   ProcessManager(FileInterface &fi, MessageQueue &mq);
//   ~ProcessManager();

//   // ========== 核心功能 ==========
//   pid_t create_process(CommandType type, const std::string &args); // 创建进程
//   void start_scheduler();                                          // 启动调度器
//   void stop_all_processes();                                       // 停止所有进程

//   // ========== 测试/访问接口（函数改名，避免与变量重名） ==========
//   bool is_scheduler_active() const { return is_scheduler_running.load(); }
//   std::map<pid_t, std::atomic<bool>> &get_process_running() { return process_running; }
//   void set_time_slice(int ms)
//   {
//     if (ms > 0)
//       time_slice = ms;
//   }
//   int get_time_slice() const { return time_slice; }
// };

// // ========== 全局变量声明（供process.cpp定义） ==========
// extern std::mutex g_mtx; // 保护就绪队列的全局互斥锁

// #pragma once
// // 基础头文件（避免循环依赖，按依赖顺序包含）
// #include <atomic>
// #include <mutex>
// #include <thread>
// #include <queue>
// #include <map>
// #include <string>
// #include <utility> // 新增：用于pair类型

// // ========== 关键修复：包含Semaphore的完整定义 ==========
// #include "sync.hpp" // 替换仅有的前置声明，确保Semaphore可被实例化

// // 前置声明（解决循环包含问题）
// class FileInterface;
// class MessageQueue;

// // 引入枚举/类型定义（确保CommandType、pid_t、Message等可识别）
// #include "../../common/common.hpp"

// // 类定义
// class ProcessManager
// {
// private:
//   // ========== 私有成员（核心状态：对齐cpp实现） ==========
//   FileInterface &file_interface;                                     // 文件系统接口（引用）
//   MessageQueue &msg_queue;                                           // 消息队列（引用）
//   Semaphore schedule_sem;                                            // Semaphore完整定义，可正常实例化
//   std::atomic<bool> is_scheduler_running{false};                     // 原子变量（保存状态）
//   int time_slice{100};                                               // 时间片大小（ms，默认100）
//   std::queue<pid_t> ready_queue;                                     // 进程就绪队列（保留，若后续调度用）
//   std::map<pid_t, std::thread> process_map;                          // 进程ID -> 线程映射（保留）
//   std::map<pid_t, std::atomic<bool>> process_running;                // 进程运行状态
//   std::map<pid_t, std::pair<CommandType, std::string>> process_args; // 新增：cpp中用到的进程参数存储
//   std::atomic<pid_t> next_pid{1};                                    // 修正：移除static，改为成员变量（对齐cpp）

//   // ========== 私有方法（内部逻辑：方法名对齐cpp实现） ==========
//   pid_t generate_pid();        // 新增：cpp中用到的PID生成方法
//   void scheduler_loop();       // 修正：原schedule()改为scheduler_loop（对齐cpp）
//   void run_process(pid_t pid); // 修正：原run_command()改为run_process（对齐cpp）

// public:
//   // ========== 构造/析构（参数匹配cpp定义） ==========
//   ProcessManager(FileInterface &fi, MessageQueue &mq);
//   ~ProcessManager();

//   // ========== 核心功能 ==========
//   pid_t create_process(CommandType type, const std::string &args); // 创建进程
//   void start_scheduler();                                          // 启动调度器
//   void stop_all_processes();                                       // 停止所有进程

//   // ========== 测试/访问接口（关键修复：仅保留声明，删除内联函数体） ==========
//   bool is_scheduler_active() const;                          // 仅声明，移除原内联实现
//   std::map<pid_t, std::atomic<bool>> &get_process_running(); // 仅声明，移除原内联实现
//   void set_time_slice(int ms);                               // 仅声明，移除原内联实现
//   int get_time_slice() const;                                // 仅声明，移除原内联实现
// };

// // ========== 全局变量声明（供process.cpp定义） ==========
// extern std::mutex g_mtx; // 保护就绪队列的全局互斥锁

// #pragma once
// // 基础头文件（避免循环依赖，按依赖顺序包含）
// #include <atomic>
// #include <mutex>
// #include <thread>
// #include <queue>
// #include <map>
// #include <string>
// #include <utility>
// #include <vector> // 新增：用于就绪队列返回

// // 包含Semaphore完整定义
// #include "sync.hpp"

// // 前置声明
// class FileInterface;
// class MessageQueue;

// // 引入枚举/类型定义
// #include "../../common/common.hpp"

// // 类定义
// class ProcessManager
// {
// private:
//   // 核心成员
//   FileInterface &file_interface;
//   MessageQueue &msg_queue;
//   Semaphore schedule_sem;
//   std::atomic<bool> is_scheduler_running{false};
//   std::atomic<bool> is_scheduling{false};    // 新增：调度执行状态
//   std::atomic<pid_t> current_running_pid{0}; // 新增：当前运行进程PID
//   int time_slice{100};
//   std::queue<pid_t> ready_queue; // 就绪队列（核心）
//   std::mutex queue_mtx;          // 新增：就绪队列锁
//   std::map<pid_t, std::thread> process_map;
//   std::map<pid_t, std::atomic<bool>> process_running;
//   std::map<pid_t, std::pair<CommandType, std::string>> process_args;
//   std::atomic<pid_t> next_pid{1};

//   // 私有方法
//   pid_t generate_pid();
//   void scheduler_loop();                 // 原调度循环（改造为时间片轮转）
//   void run_process(pid_t pid);           // 进程执行逻辑（改造为单时间片）
//   void execute_process_slice(pid_t pid); // 新增：执行单个时间片

// public:
//   // 构造/析构
//   ProcessManager(FileInterface &fi, MessageQueue &mq);
//   ~ProcessManager();

//   // 核心功能
//   pid_t create_process(CommandType type, const std::string &args);
//   void start_scheduler(); // 初始化调度器（不立即执行）
//   void run_scheduler();   // 新增：触发时间片轮转执行
//   void stop_all_processes();
//   void stop_scheduling(); // 新增：停止调度（保留就绪队列）

//   // 辅助接口
//   bool is_scheduler_active() const;
//   std::map<pid_t, std::atomic<bool>> &get_process_running();
//   void set_time_slice(int ms);
//   int get_time_slice() const;
//   std::vector<pid_t> get_ready_queue(); // 新增：获取就绪队列
// };

// // 全局互斥锁声明
// extern std::mutex g_mtx;

// #pragma once
// // 基础头文件（避免循环依赖，按依赖顺序包含）
// #include <atomic>
// #include <mutex>
// #include <thread>
// #include <queue>
// #include <map>
// #include <string>
// #include <utility>
// #include <vector>

// // 包含Semaphore完整定义
// #include "sync.hpp"

// // 前置声明
// class FileInterface;
// class MessageQueue;

// // 引入枚举/类型定义
// #include "../../common/common.hpp"

// // 类定义
// class ProcessManager
// {
// private:
//   // 核心成员
//   FileInterface &file_interface;
//   MessageQueue &msg_queue;
//   Semaphore schedule_sem;
//   std::atomic<bool> is_scheduler_running{false};
//   std::atomic<bool> is_scheduling{false};
//   std::atomic<pid_t> current_running_pid{0};
//   int time_slice{100};
//   std::queue<pid_t> ready_queue;
//   std::mutex queue_mtx;
//   std::map<pid_t, std::thread> process_map;
//   std::map<pid_t, std::atomic<bool>> process_running;
//   std::map<pid_t, std::atomic<bool>> process_completed; // 新增：标记进程是否已完成
//   std::map<pid_t, std::pair<CommandType, std::string>> process_args;
//   std::atomic<pid_t> next_pid{1};

//   // 私有方法
//   pid_t generate_pid();
//   void scheduler_loop();
//   void run_process(pid_t pid);
//   void execute_process_slice(pid_t pid);

// public:
//   // 构造/析构
//   ProcessManager(FileInterface &fi, MessageQueue &mq);
//   ~ProcessManager();

//   // 核心功能
//   pid_t create_process(CommandType type, const std::string &args);
//   void start_scheduler();
//   void run_scheduler();
//   void stop_all_processes();
//   void stop_scheduling();

//   // 辅助接口
//   bool is_scheduler_active() const;
//   std::map<pid_t, std::atomic<bool>> &get_process_running();
//   void set_time_slice(int ms);
//   int get_time_slice() const;
//   std::vector<pid_t> get_ready_queue();
//   void mark_process_completed(pid_t pid); // 新增：标记进程为已完成
// };

// // 全局互斥锁声明
// extern std::mutex g_mtx;

// #pragma once
// // 基础头文件
// #include <atomic>
// #include <mutex>
// #include <thread>
// #include <queue>
// #include <map>
// #include <string>
// #include <utility>
// #include <vector>

// // 包含Semaphore定义
// #include "sync.hpp"

// // 前置声明
// class FileInterface;
// class MessageQueue;

// // 引入枚举/类型定义
// #include "../../common/common.hpp"

// // 类定义
// class ProcessManager
// {
// private:
//   // 核心成员（必须全部声明）
//   FileInterface &file_interface;
//   MessageQueue &msg_queue;
//   Semaphore schedule_sem;
//   std::atomic<bool> is_scheduler_running{false};
//   std::atomic<bool> is_scheduling{false};
//   std::atomic<pid_t> current_running_pid{0};
//   int time_slice{100};
//   std::queue<pid_t> ready_queue;
//   std::mutex queue_mtx;
//   std::map<pid_t, std::thread> process_map;
//   std::map<pid_t, std::atomic<bool>> process_running;
//   // ========== 新增：必须声明 ==========
//   std::map<pid_t, std::atomic<bool>> process_completed; // 进程完成状态标记
//   // ===================================
//   std::map<pid_t, std::pair<CommandType, std::string>> process_args;
//   std::atomic<pid_t> next_pid{1};

//   // 私有方法
//   pid_t generate_pid();
//   void scheduler_loop();
//   void run_process(pid_t pid);
//   void execute_process_slice(pid_t pid);

// public:
//   // 构造/析构
//   ProcessManager(FileInterface &fi, MessageQueue &mq);
//   ~ProcessManager();

//   // 核心功能
//   pid_t create_process(CommandType type, const std::string &args);
//   void start_scheduler();
//   void run_scheduler();
//   void stop_all_processes();
//   void stop_scheduling();

//   // ========== 新增：必须声明 ==========
//   void mark_process_completed(pid_t pid); // 标记进程完成的方法
//   // ===================================

//   // 辅助接口
//   bool is_scheduler_active() const;
//   std::map<pid_t, std::atomic<bool>> &get_process_running();
//   void set_time_slice(int ms);
//   int get_time_slice() const;
//   std::vector<pid_t> get_ready_queue();
// };

// // 全局互斥锁声明
// extern std::mutex g_mtx;

#ifndef PROCESS_HPP
#define PROCESS_HPP

// 1. 先包含common.hpp（获取pid_t和CommandType，路径必须正确）
#include "../../common/common.hpp"

// 2. 其他必要头文件
#include <unordered_map>
#include <atomic>
#include <queue>
#include <mutex>
#include <string>

// 前置声明
class FileInterface;
class MessageQueue;

// 3. 删除原有的CommandType枚举（已在common.hpp中定义，避免重定义）

// 进程管理器类（核心调度器）
class ProcessManager
{
public:
  ProcessManager(FileInterface &file_if, MessageQueue &msg_queue);
  ~ProcessManager();

  // 调度器控制
  void start_scheduler();    // 初始化调度器
  void run_scheduler();      // 启动时间片轮转执行
  void stop_scheduling();    // 停止调度（保留就绪队列）
  void stop_all_processes(); // 停止所有进程+清空队列

  // 进程创建
  pid_t create_process(CommandType cmd_type, const std::string &args_json);

  // 配置
  void set_time_slice(int ms); // 设置时间片（毫秒）

  // 状态查询
  std::unordered_map<pid_t, std::atomic<bool>> &get_process_running();
  std::unordered_map<pid_t, std::atomic<bool>> &get_process_completed();
  std::vector<pid_t> get_ready_queue();

private:
  // 核心依赖
  FileInterface &file_interface; // 文件系统接口
  MessageQueue &msg_queue;       // IPC消息队列

  // 调度器状态
  bool scheduler_running; // 调度器是否运行
  int time_slice_ms;      // 时间片大小（毫秒）

  // 进程状态管理
  std::unordered_map<pid_t, std::atomic<bool>> process_running;                // 运行中状态
  std::unordered_map<pid_t, std::atomic<bool>> process_completed;              // 完成状态
  std::unordered_map<pid_t, std::pair<CommandType, std::string>> process_args; // 进程参数
  std::queue<pid_t> ready_queue;                                               // 就绪队列

  // 核心函数
  void execute_process_slice(pid_t pid);                          // 执行单个进程（时间片内）
  void send_process_result(pid_t pid, const std::string &result); // 发送执行结果
};

// 全局互斥锁声明（与交互式终端共享）
extern std::mutex g_mtx;

#endif // PROCESS_HPP