// #include "process.hpp"
// #include <iostream>
// // #include <nlohmann/json.hpp>
// #include <mutex>
// #include <chrono>
// #include <atomic>
// using namespace std;
// #include "json.hpp"

// using json = nlohmann::json;
// mutex g_mtx;

// /**
//  * @brief ProcessManager类的构造函数
//  * @param fi 文件接口引用，用于文件操作
//  * @param mq 消息队列引用，用于进程间通信
//  */

// ProcessManager::ProcessManager(FileInterface &fi, MessageQueue &mq)
//     : file_interface(fi), msg_queue(mq), schedule_sem(0), is_scheduler_running(false)
// {
// }

// /**
//  * 析构函数：ProcessManager类的析构函数
//  * 当ProcessManager对象被销毁时，此函数会被自动调用
//  * 它会确保所有正在运行的进程都被正确停止
//  */
// ProcessManager::~ProcessManager()
// {
//   stop_all_processes(); // 调用stop_all_processes函数停止所有进程
// }

// /**
//  * 进程调度函数，负责调度进程执行
//  * 该函数使用循环调度算法，按照先进先出的顺序调度进程
//  */
// void ProcessManager::schedule()
// {
//   // 当调度器运行标志为真时，持续执行调度
//   while (is_scheduler_running)
//   {
//     // 等待调度信号量，表示有新进程需要调度
//     schedule_sem.wait();
//     // 检查调度器是否仍在运行，如果被停止则退出循环
//     if (!is_scheduler_running)
//       break;

//     // 使用互斥锁保护对就绪队列的访问
//     unique_lock<mutex> lock(g_mtx);
//     // 如果就绪队列为空，则跳过当前循环
//     if (ready_queue.empty())
//     {
//       lock.unlock();
//       continue;
//     }
//     // 获取队首进程的PID，并将其从队列中移除
//     pid_t current_pid = ready_queue.front();
//     ready_queue.pop();
//     lock.unlock();

//     // 查找当前进程在运行进程映射中的迭代器
//     auto proc_iter = process_running.find(current_pid);
//     // 如果进程不存在或未在运行，则跳过当前循环
//     if (proc_iter == process_running.end() || !proc_iter->second.load())
//     {
//       continue;
//     }

//     // 模拟进程执行，让当前进程运行一个时间片
//     this_thread::sleep_for(chrono::milliseconds(time_slice));

//     // 将进程状态标记为未运行
//     proc_iter->second.store(false);
//     // 重新获取锁，以便安全地访问就绪队列
//     lock.lock();
//     // 将进程重新加入就绪队列，等待下一次调度
//     ready_queue.push(current_pid);
//     lock.unlock();
//     // 发送调度信号量，通知调度器有新的进程需要调度
//     schedule_sem.post();
//   }
// }

// void ProcessManager::run_command(CommandType type, const string &args, pid_t pid)
// {
//   json args_json;
//   try
//   {
//     args_json = json::parse(args);
//   }
//   catch (const json::parse_error &e)
//   {
//     Message err_msg;
//     err_msg.sender_pid = pid;
//     err_msg.receiver_pid = 0;
//     err_msg.type = MessageType::RES_RESULT;
//     err_msg.content = json({{"success", false}, {"msg", "参数解析失败: " + string(e.what())}}).dump();
//     msg_queue.send_message(err_msg);
//     process_running[pid].store(false);
//     return;
//   }

//   bool success = true;
//   string result = "执行成功";

//   try
//   {
//     while (process_running[pid].load())
//     {
//       switch (type)
//       {
//       case CommandType::CREATE_FILE:
//       {
//         // 修正：匹配FileInterface::create_file的参数顺序（路径→权限→内容）
//         string path = args_json["path"];
//         string content = args_json["content"];
//         int perm = File_READ | File_WRITE;
//         success = file_interface.create_file(path, static_cast<FilePermission>(perm), content);
//         if (!success)
//           result = "创建文件失败";
//         break;
//       }
//       case CommandType::DELETE_FILE:
//       {
//         // 修正：补充delete_file需要的pid参数
//         string path = args_json["path"];
//         success = file_interface.delete_file(path, pid);
//         if (!success)
//           result = "删除文件失败";
//         break;
//       }
//       case CommandType::MKDIR:
//       {
//         // 修正：函数名改为create_directory（匹配FileInterface）
//         string path = args_json["path"];
//         success = file_interface.create_directory(path);
//         if (!success)
//           result = "创建目录失败";
//         break;
//       }
//       case CommandType::QUERY_DIR:
//       {
//         // 修正：函数名改为query_directory（匹配FileInterface），返回值为string
//         string path = args_json["path"];
//         result = file_interface.query_directory(path);
//         success = !result.empty();
//         if (!success)
//           result = "查询目录失败";
//         break;
//       }
//       default:
//       {
//         success = false;
//         result = "不支持的命令类型";
//         break;
//       }
//       }

//       process_running[pid].store(false);
//       while (!process_running[pid].load() && process_running.count(pid))
//       {
//         this_thread::sleep_for(chrono::milliseconds(10));
//       }
//     }
//   }
//   catch (const exception &e)
//   {
//     success = false;
//     result = "执行异常: " + string(e.what());
//   }

//   Message res_msg;
//   res_msg.sender_pid = pid;
//   res_msg.receiver_pid = 0;
//   res_msg.type = MessageType::RES_RESULT;
//   res_msg.content = json({{"success", success}, {"result", result}}).dump();
//   msg_queue.send_message(res_msg);

//   process_running[pid].store(false);
// }

// pid_t ProcessManager::create_process(CommandType type, const string &args)
// {
//   pid_t new_pid = next_pid++;
//   process_running[new_pid].store(false);

//   process_map[new_pid] = thread(&ProcessManager::run_command, this, type, args, new_pid);

//   unique_lock<mutex> lock(g_mtx);
//   ready_queue.push(new_pid);
//   lock.unlock();

//   schedule_sem.post();
//   return new_pid;
// }

// void ProcessManager::start_scheduler()
// {
//   if (!is_scheduler_running)
//   {
//     is_scheduler_running = true;
//     thread scheduler_thread(&ProcessManager::schedule, this);
//     scheduler_thread.detach();
//   }
// }

// void ProcessManager::stop_all_processes()
// {
//   is_scheduler_running = false;
//   schedule_sem.post();

//   for (auto &pair : process_running)
//   {
//     pair.second.store(false);
//   }

//   for (auto &pair : process_map)
//   {
//     if (pair.second.joinable())
//     {
//       pair.second.join();
//     }
//   }

//   process_map.clear();
//   process_running.clear();
//   unique_lock<mutex> lock(g_mtx);
//   ready_queue = queue<pid_t>();
// }

// process.cpp 最终版（基于你的代码修改）
// #include "process.hpp"
// #include "../../common/common.hpp" // 引用你的common.hpp
// #include "ipc.hpp"
// #include <iostream>
// #include <mutex>
// #include <chrono>
// #include <atomic>
// #include "json.hpp" // 确保已引入JSON库

// using json = nlohmann::json;
// using namespace std;
// mutex g_mtx;

// /**
//  * @brief ProcessManager类的构造函数
//  * @param fi 文件接口引用，用于文件操作
//  * @param mq 消息队列引用，用于进程间通信
//  */
// ProcessManager::ProcessManager(FileInterface &fi, MessageQueue &mq)
//     : file_interface(fi), msg_queue(mq), is_scheduler_running(false)
// {
//   // 初始化Windows信号量（初始计数0）
//   schedule_sem = CreateSemaphore(NULL, 0, INT_MAX, NULL);
// }

// /**
//  * 析构函数：ProcessManager类的析构函数
//  * 当ProcessManager对象被销毁时，此函数会被自动调用
//  * 它会确保所有正在运行的进程都被正确停止
//  */
// ProcessManager::~ProcessManager()
// {
//   stop_all_processes();
//   CloseHandle(schedule_sem); // 释放信号量句柄
// }

// /**
//  * 进程调度函数，负责调度进程执行
//  * 该函数使用循环调度算法，按照先进先出的顺序调度进程
//  */
// void ProcessManager::schedule()
// {
//   // 当调度器运行标志为真时，持续执行调度
//   while (is_scheduler_running)
//   {
//     // 等待调度信号量（Windows原生API）
//     WaitForSingleObject(schedule_sem, INFINITE);
//     // 检查调度器是否仍在运行，如果被停止则退出循环
//     if (!is_scheduler_running)
//       break;

//     // 使用互斥锁保护对就绪队列的访问
//     unique_lock<mutex> lock(g_mtx);
//     // 如果就绪队列为空，则跳过当前循环
//     if (ready_queue.empty())
//     {
//       lock.unlock();
//       continue;
//     }
//     // 获取队首进程的PID，并将其从队列中移除
//     pid_t current_pid = ready_queue.front();
//     ready_queue.pop();
//     lock.unlock();

//     // 查找当前进程在运行进程映射中的迭代器
//     auto proc_iter = process_running.find(current_pid);
//     // 如果进程不存在或未在运行，则跳过当前循环
//     if (proc_iter == process_running.end() || !proc_iter->second.load())
//     {
//       continue;
//     }

//     // 模拟进程执行，让当前进程运行一个时间片
//     this_thread::sleep_for(chrono::milliseconds(time_slice));

//     // 将进程状态标记为未运行
//     proc_iter->second.store(false);
//     // 重新获取锁，以便安全地访问就绪队列
//     lock.lock();
//     // 将进程重新加入就绪队列，等待下一次调度
//     ready_queue.push(current_pid);
//     lock.unlock();
//     // 发送调度信号量，通知调度器有新的进程需要调度
//     ReleaseSemaphore(schedule_sem, 1, NULL);
//   }
// }

// void ProcessManager::run_command(CommandType type, const string &args, pid_t pid)
// {
//   json args_json;
//   try
//   {
//     args_json = json::parse(args);
//   }
//   catch (const json::parse_error &e)
//   {
//     Message err_msg;
//     err_msg.sender_pid = pid;
//     err_msg.receiver_pid = 0;
//     err_msg.type = MessageType::RES_RESULT;
//     err_msg.content = json({{"success", false}, {"msg", "参数解析失败: " + string(e.what())}}).dump();
//     msg_queue.send_message(err_msg);
//     process_running[pid].store(false);
//     return;
//   }

//   bool success = true;
//   string result = "执行成功";

//   try
//   {
//     while (process_running[pid].load())
//     {
//       switch (type)
//       {
//       case CommandType::CREATE_FILE:
//       {
//         // 匹配你的common.hpp中的FilePermission
//         string path = args_json["path"];
//         string content = args_json["content"];
//         int perm = File_READ | File_WRITE;
//         success = file_interface.create_file(path, static_cast<FilePermission>(perm), content);
//         if (!success)
//           result = "创建文件失败";
//         break;
//       }
//       case CommandType::DELETE_FILE:
//       {
//         string path = args_json["path"];
//         success = file_interface.delete_file(path, pid);
//         if (!success)
//           result = "删除文件失败";
//         break;
//       }
//       case CommandType::MKDIR:
//       {
//         string path = args_json["path"];
//         success = file_interface.create_directory(path);
//         if (!success)
//           result = "创建目录失败";
//         break;
//       }
//       case CommandType::QUERY_DIR:
//       {
//         string path = args_json["path"];
//         result = file_interface.query_directory(path);
//         success = !result.empty();
//         if (!success)
//           result = "查询目录失败";
//         break;
//       }
//       default:
//       {
//         success = false;
//         result = "不支持的命令类型";
//         break;
//       }
//       }

//       process_running[pid].store(false);
//       while (!process_running[pid].load() && process_running.count(pid))
//       {
//         this_thread::sleep_for(chrono::milliseconds(10));
//       }
//     }
//   }
//   catch (const exception &e)
//   {
//     success = false;
//     result = "执行异常: " + string(e.what());
//   }

//   Message res_msg;
//   res_msg.sender_pid = pid;
//   res_msg.receiver_pid = 0;
//   res_msg.type = MessageType::RES_RESULT;
//   res_msg.content = json({{"success", success}, {"result", result}}).dump();
//   msg_queue.send_message(res_msg);

//   process_running[pid].store(false);
// }

// pid_t ProcessManager::create_process(CommandType type, const string &args)
// {
//   pid_t new_pid = next_pid++;
//   process_running[new_pid].store(false);

//   process_map[new_pid] = thread(&ProcessManager::run_command, this, type, args, new_pid);

//   unique_lock<mutex> lock(g_mtx);
//   ready_queue.push(new_pid);
//   lock.unlock();

//   // 发送信号量（Windows原生API）
//   ReleaseSemaphore(schedule_sem, 1, NULL);
//   return new_pid;
// }

// void ProcessManager::start_scheduler()
// {
//   if (!is_scheduler_running)
//   {
//     is_scheduler_running = true;
//     thread scheduler_thread(&ProcessManager::schedule, this);
//     scheduler_thread.detach();
//   }
// }

// void ProcessManager::stop_all_processes()
// {
//   is_scheduler_running = false;
//   // 唤醒调度器，使其退出循环
//   ReleaseSemaphore(schedule_sem, 1, NULL);

//   for (auto &pair : process_running)
//   {
//     pair.second.store(false);
//   }

//   for (auto &pair : process_map)
//   {
//     if (pair.second.joinable())
//     {
//       pair.second.join();
//     }
//   }

//   process_map.clear();
//   process_running.clear();
//   unique_lock<mutex> lock(g_mtx);
//   ready_queue = queue<pid_t>();
// }

// #include "process.hpp"
// #include "../../common/common.hpp"
// #include "ipc.hpp"
// #include <iostream>
// #include <mutex>
// #include <chrono>
// #include <atomic>
// #include "json.hpp"

// using json = nlohmann::json;
// using namespace std;

// extern std::mutex g_mtx; // 全局互斥锁，保护 ready_queue

// /**
//  * @brief ProcessManager类的构造函数
//  */
// ProcessManager::ProcessManager(FileInterface &fi, MessageQueue &mq)
//     : file_interface(fi), msg_queue(mq), schedule_sem(0), is_scheduler_running(false)
// {
//   // ✅ schedule_sem(0) 调用 Semaphore(int count=0) 构造函数
//   // Windows信号量已在 sync.hpp 中创建
// }

// /**
//  * @brief 析构函数
//  */
// ProcessManager::~ProcessManager()
// {
//   stop_all_processes();
//   // ✅ 不需要 CloseHandle —— Semaphore 析构函数会自动调用
// }

// /**
//  * @brief 调度器主循环（时间片轮转）
//  */
// void ProcessManager::schedule()
// {
//   while (is_scheduler_running.load())
//   {
//     schedule_sem.wait(); // ✅ 替代 WaitForSingleObject

//     if (!is_scheduler_running.load())
//       break;

//     unique_lock<mutex> lock(g_mtx);
//     if (ready_queue.empty())
//     {
//       lock.unlock();
//       continue;
//     }

//     pid_t current_pid = ready_queue.front();
//     ready_queue.pop();
//     lock.unlock();

//     auto proc_iter = process_running.find(current_pid);
//     if (proc_iter == process_running.end() || !proc_iter->second.load())
//     {
//       continue;
//     }

//     // 模拟执行一个时间片
//     this_thread::sleep_for(chrono::milliseconds(time_slice));

//     proc_iter->second.store(false); // 标记为暂停

//     lock.lock();
//     ready_queue.push(current_pid); // 重新入队
//     lock.unlock();

//     schedule_sem.post(); // ✅ 替代 ReleaseSemaphore(..., 1, ...)
//   }
// }

// /**
//  * @brief 执行具体命令（在子线程中运行）
//  */
// void ProcessManager::run_command(CommandType type, const string &args, pid_t pid)
// {
//   json args_json;
//   try
//   {
//     args_json = json::parse(args);
//   }
//   catch (const json::parse_error &e)
//   {
//     Message err_msg;
//     err_msg.sender_pid = pid;
//     err_msg.receiver_pid = 0;
//     err_msg.type = MessageType::RES_RESULT;
//     err_msg.content = json({{"success", false}, {"msg", "参数解析失败: " + string(e.what())}}).dump();
//     msg_queue.send_message(err_msg);
//     process_running[pid].store(false);
//     return;
//   }

//   bool success = true;
//   string result = "执行成功";

//   try
//   {
//     while (process_running[pid].load())
//     {
//       switch (type)
//       {
//       case CommandType::CREATE_FILE:
//       {
//         string path = args_json["path"];
//         string content = args_json["content"];
//         int perm = File_READ | File_WRITE;
//         success = file_interface.create_file(path, static_cast<FilePermission>(perm), content);
//         if (!success)
//           result = "创建文件失败";
//         break;
//       }
//       case CommandType::DELETE_FILE:
//       {
//         string path = args_json["path"];
//         success = file_interface.delete_file(path, pid);
//         if (!success)
//           result = "删除文件失败";
//         break;
//       }
//       case CommandType::MKDIR:
//       {
//         string path = args_json["path"];
//         success = file_interface.create_directory(path);
//         if (!success)
//           result = "创建目录失败";
//         break;
//       }
//       case CommandType::QUERY_DIR:
//       {
//         string path = args_json["path"];
//         result = file_interface.query_directory(path);
//         success = !result.empty();
//         if (!success)
//           result = "查询目录失败";
//         break;
//       }
//       default:
//       {
//         success = false;
//         result = "不支持的命令类型";
//         break;
//       }
//       }

//       // 执行一次后暂停，等待下一次调度
//       process_running[pid].store(false);

//       // 等待被重新调度（由 scheduler 设置为 true）
//       while (!process_running[pid].load() && process_running.count(pid))
//       {
//         this_thread::sleep_for(chrono::milliseconds(10));
//       }
//     }
//   }
//   catch (const exception &e)
//   {
//     success = false;
//     result = "执行异常: " + string(e.what());
//   }

//   // 发送结果
//   Message res_msg;
//   res_msg.sender_pid = pid;
//   res_msg.receiver_pid = 0;
//   res_msg.type = MessageType::RES_RESULT;
//   res_msg.content = json({{"success", success}, {"result", result}}).dump();
//   msg_queue.send_message(res_msg);

//   process_running[pid].store(false);
// }

// /**
//  * @brief 创建新进程（实际是创建线程）
//  */
// pid_t ProcessManager::create_process(CommandType type, const string &args)
// {
//   pid_t new_pid = next_pid.fetch_add(1);
//   process_running[new_pid].store(false);

//   process_map[new_pid] = thread(&ProcessManager::run_command, this, type, args, new_pid);

//   {
//     unique_lock<mutex> lock(g_mtx);
//     ready_queue.push(new_pid);
//   }

//   schedule_sem.post(); // ✅ 触发调度器
//   return new_pid;
// }

// /**
//  * @brief 启动调度器线程
//  */
// void ProcessManager::start_scheduler()
// {
//   if (!is_scheduler_running.exchange(true))
//   {
//     thread scheduler_thread(&ProcessManager::schedule, this);
//     scheduler_thread.detach();
//   }
// }

// /**
//  * @brief 停止所有进程和调度器
//  */
// void ProcessManager::stop_all_processes()
// {
//   is_scheduler_running.store(false);
//   schedule_sem.post(); // ✅ 唤醒调度器线程，使其退出 wait()

//   // 停止所有工作线程
//   for (auto &pair : process_running)
//   {
//     pair.second.store(false);
//   }

//   // 等待线程结束
//   for (auto &pair : process_map)
//   {
//     if (pair.second.joinable())
//     {
//       pair.second.join();
//     }
//   }

//   process_map.clear();
//   process_running.clear();

//   // 清空就绪队列
//   unique_lock<mutex> lock(g_mtx);
//   ready_queue = queue<pid_t>();
// }

// 1. 补充必要头文件（解决线程/容器/原子操作编译报错）
// #include "process.hpp"
// #include "../../common/common.hpp"
// #include "ipc.hpp"
// #include "sync.hpp" // 补充Semaphore头文件，确保schedule_sem可调用wait/post
// #include <iostream>
// #include <mutex>
// #include <chrono>
// #include <atomic>
// #include <thread> // 补充线程头文件（thread、this_thread）
// #include <map>    // 补充map容器头文件
// #include <queue>  // 补充queue容器头文件
// #include "json.hpp"

// using json = nlohmann::json;
// using namespace std;

// // 2. 定义全局互斥锁（匹配extern声明，解决"未定义的标识符g_mtx"）
// std::mutex g_mtx;

// // 3. 初始化静态成员next_pid（关键！hpp中声明为static，cpp必须定义）
// std::atomic<pid_t> ProcessManager::next_pid{1};

// /**
//  * @brief ProcessManager类的构造函数
//  */
// ProcessManager::ProcessManager(FileInterface &fi, MessageQueue &mq)
//     : file_interface(fi), msg_queue(mq), schedule_sem(0), is_scheduler_running(false)
// {
//   // schedule_sem(0)：调用Semaphore(int count=0)构造函数，无需修改
// }

// /**
//  * @brief 析构函数
//  */
// ProcessManager::~ProcessManager()
// {
//   stop_all_processes();
//   // Semaphore析构函数自动释放Windows句柄，无需CloseHandle
// }

// /**
//  * @brief 调度器主循环（时间片轮转）
//  * 【核心修改】补充进程运行状态标记，解决进程"执行但无效果"问题
//  */
// void ProcessManager::schedule()
// {
//   while (is_scheduler_running.load())
//   {
//     schedule_sem.wait(); // 等待调度信号

//     if (!is_scheduler_running.load())
//       break;

//     unique_lock<mutex> lock(g_mtx);
//     if (ready_queue.empty())
//     {
//       lock.unlock();
//       continue;
//     }

//     pid_t current_pid = ready_queue.front();
//     ready_queue.pop();
//     lock.unlock();

//     auto proc_iter = process_running.find(current_pid);
//     if (proc_iter == process_running.end() || !proc_iter->second.load())
//     {
//       continue;
//     }

//     // ========== 关键修改1 ==========
//     // 执行时间片前，标记进程为"运行中"（否则run_command会直接退出循环）
//     proc_iter->second.store(true);

//     // 模拟执行一个时间片（100ms）
//     this_thread::sleep_for(chrono::milliseconds(time_slice));

//     proc_iter->second.store(false); // 标记为暂停

//     lock.lock();
//     ready_queue.push(current_pid); // 重新入队
//     lock.unlock();

//     schedule_sem.post(); // 触发下一次调度
//   }
// }

// /**
//  * @brief 执行具体命令（子线程中运行）
//  * 【优化】补充参数校验，避免JSON解析后直接取值导致崩溃
//  */
// void ProcessManager::run_command(CommandType type, const string &args, pid_t pid)
// {
//   json args_json;
//   try
//   {
//     args_json = json::parse(args);
//   }
//   catch (const json::parse_error &e)
//   {
//     Message err_msg;
//     err_msg.sender_pid = pid;
//     err_msg.receiver_pid = 0;
//     err_msg.type = MessageType::RES_RESULT;
//     err_msg.content = json({{"success", false}, {"msg", "参数解析失败: " + string(e.what())}}).dump();
//     msg_queue.send_message(err_msg);
//     process_running[pid].store(false);
//     return;
//   }

//   bool success = true;
//   string result = "执行成功";

//   try
//   {
//     while (process_running[pid].load())
//     {
//       switch (type)
//       {
//       case CommandType::CREATE_FILE:
//       {
//         // ========== 关键修改2 ==========
//         // 补充参数存在性校验，避免key不存在导致崩溃
//         if (!args_json.contains("path") || !args_json.contains("content"))
//         {
//           throw runtime_error("CREATE_FILE缺少必要参数：path/content");
//         }
//         string path = args_json["path"];
//         string content = args_json["content"];
//         int perm = File_READ | File_WRITE;
//         success = file_interface.create_file(path, static_cast<FilePermission>(perm), content);
//         if (!success)
//           result = "创建文件失败（路径已存在/磁盘满/路径无效）";
//         break;
//       }
//       case CommandType::DELETE_FILE:
//       {
//         if (!args_json.contains("path"))
//         {
//           throw runtime_error("DELETE_FILE缺少必要参数：path");
//         }
//         string path = args_json["path"];
//         success = file_interface.delete_file(path, pid);
//         if (!success)
//           result = "删除文件失败（文件不存在/被锁定）";
//         break;
//       }
//       case CommandType::MKDIR:
//       {
//         if (!args_json.contains("path"))
//         {
//           throw runtime_error("MKDIR缺少必要参数：path");
//         }
//         string path = args_json["path"];
//         success = file_interface.create_directory(path);
//         if (!success)
//           result = "创建目录失败（路径已存在/解析错误）";
//         break;
//       }
//       case CommandType::QUERY_DIR:
//       {
//         if (!args_json.contains("path"))
//         {
//           throw runtime_error("QUERY_DIR缺少必要参数：path");
//         }
//         string path = args_json["path"];
//         result = file_interface.query_directory(path);
//         success = !result.empty();
//         if (!success)
//           result = "查询目录失败（路径不存在）";
//         break;
//       }
//       default:
//       {
//         success = false;
//         result = "不支持的命令类型";
//         break;
//       }
//       }

//       // 执行一次后暂停，等待下一次调度
//       process_running[pid].store(false);

//       // 等待被重新调度（避免空循环占用CPU）
//       while (!process_running[pid].load() && process_running.count(pid))
//       {
//         this_thread::sleep_for(chrono::milliseconds(10));
//       }
//     }
//   }
//   catch (const exception &e)
//   {
//     success = false;
//     result = "执行异常: " + string(e.what());
//   }

//   // 发送执行结果（广播给主线程）
//   Message res_msg;
//   res_msg.sender_pid = pid;
//   res_msg.receiver_pid = 0;
//   res_msg.type = MessageType::RES_RESULT;
//   res_msg.content = json({{"success", success}, {"result", result}}).dump();
//   msg_queue.send_message(res_msg);

//   process_running[pid].store(false);
// }

// /**
//  * @brief 创建新进程（实际是创建线程）
//  */
// pid_t ProcessManager::create_process(CommandType type, const string &args)
// {
//   // 原子操作生成唯一PID（线程安全）
//   pid_t new_pid = next_pid.fetch_add(1);
//   process_running[new_pid].store(false);

//   // 创建子线程执行命令
//   process_map[new_pid] = thread(&ProcessManager::run_command, this, type, args, new_pid);

//   // 进程加入就绪队列（加锁保证线程安全）
//   {
//     unique_lock<mutex> lock(g_mtx);
//     ready_queue.push(new_pid);
//   }

//   schedule_sem.post(); // 触发调度器执行
//   return new_pid;
// }

// /**
//  * @brief 启动调度器线程
//  */
// void ProcessManager::start_scheduler()
// {
//   // 原子交换：确保调度器只启动一次（避免重复创建线程）
//   if (!is_scheduler_running.exchange(true))
//   {
//     thread scheduler_thread(&ProcessManager::schedule, this);
//     scheduler_thread.detach(); // 分离线程，后台运行
//   }
// }

// /**
//  * @brief 停止所有进程和调度器
//  */
// void ProcessManager::stop_all_processes()
// {
//   // 标记调度器停止
//   is_scheduler_running.store(false);
//   schedule_sem.post(); // 唤醒调度器，使其退出wait()

//   // 停止所有工作线程
//   for (auto &pair : process_running)
//   {
//     pair.second.store(false);
//   }

//   // 等待所有线程结束（避免资源泄漏）
//   for (auto &pair : process_map)
//   {
//     if (pair.second.joinable())
//     {
//       pair.second.join();
//     }
//   }

//   // 清空数据结构
//   process_map.clear();
//   process_running.clear();

//   // 清空就绪队列（加锁保证线程安全）
//   unique_lock<mutex> lock(g_mtx);
//   ready_queue = queue<pid_t>();
// }

// // ========== 关键修改3 ==========
// // 补充test1.cpp调用所需的方法（解决"未定义的成员函数"报错）
// bool ProcessManager::is_scheduler_running() const
// {
//   // 原子类型必须通过load()取值，不能直接返回
//   return is_scheduler_running.load();
// }

// （可选：如果hpp中get_process_running/set_time_slice不是内联，需补充实现）
// std::map<pid_t, std::atomic<bool>>& ProcessManager::get_process_running() {
//   return process_running;
// }

// void ProcessManager::set_time_slice(int ms) {
//   if (ms > 0) time_slice = ms; // 增加合法性校验
// }

// int ProcessManager::get_time_slice() const {
//   return time_slice;
// }

// 按依赖顺序包含头文件
// #include "process.hpp"
// #include "../../common/common.hpp"
// #include "ipc.hpp"                       // MessageQueue实现
// #include "sync.hpp"                      // Semaphore实现（必须包含）
// #include "../fs_core/file_interface.hpp" // FileInterface实现
// #include <iostream>
// #include <mutex>
// #include <chrono>
// #include <atomic>
// #include <thread>
// #include <map>
// #include <queue>
// #include "json.hpp" // JSON解析

// using json = nlohmann::json;
// using namespace std;

// // ========== 全局变量定义（匹配hpp声明） ==========
// std::mutex g_mtx; // 保护就绪队列的全局互斥锁

// // ========== 静态成员初始化（关键！） ==========
// std::atomic<pid_t> ProcessManager::next_pid{1}; // 静态PID生成器，初始值1

// /**
//  * @brief ProcessManager构造函数（匹配hpp声明）
//  */
// ProcessManager::ProcessManager(FileInterface &fi, MessageQueue &mq)
//     : file_interface(fi), msg_queue(mq), schedule_sem(0), // sem初始计数0（调用Semaphore(0)构造）
//       is_scheduler_running(false), time_slice(100)        // 显式初始化状态
// {
//   // sync.hpp的Semaphore(0)会创建Windows信号量，无需额外操作
// }

// /**
//  * @brief 析构函数
//  */
// ProcessManager::~ProcessManager()
// {
//   stop_all_processes();
//   // Semaphore析构函数自动释放Windows信号量句柄，无需CloseHandle
// }

// /**
//  * @brief 调度器主循环（时间片轮转核心）
//  */
// void ProcessManager::schedule()
// {
//   while (is_scheduler_running.load())
//   {
//     schedule_sem.wait(); // P操作：等待调度信号

//     // 调度器已停止，退出循环
//     if (!is_scheduler_running.load())
//       break;

//     // 加锁访问就绪队列（线程安全）
//     unique_lock<mutex> lock(g_mtx);
//     if (ready_queue.empty())
//     {
//       lock.unlock();
//       continue;
//     }

//     // 取出队首进程PID
//     pid_t current_pid = ready_queue.front();
//     ready_queue.pop();
//     lock.unlock();

//     // 检查进程是否存在且可运行
//     auto proc_iter = process_running.find(current_pid);
//     if (proc_iter == process_running.end() || !proc_iter->second.load())
//     {
//       continue;
//     }

//     // 标记进程为运行中，执行时间片
//     proc_iter->second.store(true);
//     this_thread::sleep_for(chrono::milliseconds(time_slice)); // 模拟执行
//     proc_iter->second.store(false);                           // 标记进程暂停

//     // 进程重新入队，等待下一次调度
//     lock.lock();
//     ready_queue.push(current_pid);
//     lock.unlock();

//     schedule_sem.post(); // V操作：触发下一次调度
//   }
// }

// /**
//  * @brief 执行具体命令（子线程入口）
//  */
// void ProcessManager::run_command(CommandType type, const string &args, pid_t pid)
// {
//   json args_json;
//   // 1. 解析JSON参数（捕获解析异常）
//   try
//   {
//     args_json = json::parse(args);
//   }
//   catch (const json::parse_error &e)
//   {
//     Message err_msg;
//     err_msg.sender_pid = pid;
//     err_msg.receiver_pid = 0; // 广播错误
//     err_msg.type = MessageType::RES_RESULT;
//     err_msg.content = json({{"success", false}, {"msg", "参数解析失败: " + string(e.what())}}).dump();
//     msg_queue.send_message(err_msg);
//     process_running[pid].store(false);
//     return;
//   }

//   bool success = true;
//   string result = "执行成功";

//   // 2. 执行具体命令（捕获业务异常）
//   try
//   {
//     while (process_running[pid].load())
//     {
//       switch (type)
//       {
//       case CommandType::CREATE_FILE:
//       {
//         // 参数合法性校验
//         if (!args_json.contains("path") || !args_json.contains("content"))
//           throw runtime_error("CREATE_FILE缺少参数：path/content");
//         string path = args_json["path"];
//         string content = args_json["content"];
//         int perm = File_READ | File_WRITE;
//         success = file_interface.create_file(path, static_cast<FilePermission>(perm), content);
//         if (!success)
//           result = "创建文件失败（路径已存在/磁盘满/路径无效）";
//         break;
//       }
//       case CommandType::DELETE_FILE:
//       {
//         if (!args_json.contains("path"))
//           throw runtime_error("DELETE_FILE缺少参数：path");
//         string path = args_json["path"];
//         success = file_interface.delete_file(path, pid);
//         if (!success)
//           result = "删除文件失败（文件不存在/被锁定）";
//         break;
//       }
//       case CommandType::MKDIR:
//       {
//         if (!args_json.contains("path"))
//           throw runtime_error("MKDIR缺少参数：path");
//         string path = args_json["path"];
//         success = file_interface.create_directory(path);
//         if (!success)
//           result = "创建目录失败（路径已存在/解析错误）";
//         break;
//       }
//       case CommandType::QUERY_DIR:
//       {
//         if (!args_json.contains("path"))
//           throw runtime_error("QUERY_DIR缺少参数：path");
//         string path = args_json["path"];
//         result = file_interface.query_directory(path);
//         success = !result.empty();
//         if (!success)
//           result = "查询目录失败（路径不存在）";
//         break;
//       }
//       default:
//       {
//         success = false;
//         result = "不支持的命令类型：" + to_string(static_cast<int>(type));
//         break;
//       }
//       }

//       // 执行一次后暂停，等待下一次调度
//       process_running[pid].store(false);
//       // 等待调度器重新唤醒（避免空循环占用CPU）
//       while (!process_running[pid].load() && process_running.count(pid))
//         this_thread::sleep_for(chrono::milliseconds(10));
//     }
//   }
//   catch (const exception &e)
//   {
//     success = false;
//     result = "执行异常: " + string(e.what());
//   }

//   // 3. 发送执行结果（广播）
//   Message res_msg;
//   res_msg.sender_pid = pid;
//   res_msg.receiver_pid = 0;
//   res_msg.type = MessageType::RES_RESULT;
//   res_msg.content = json({{"success", success}, {"result", result}}).dump();
//   msg_queue.send_message(res_msg);

//   // 标记进程执行完成
//   process_running[pid].store(false);
// }

// /**
//  * @brief 创建新进程（实际创建子线程）
//  */
// pid_t ProcessManager::create_process(CommandType type, const string &args)
// {
//   // 原子操作生成唯一PID（线程安全）
//   pid_t new_pid = next_pid.fetch_add(1);
//   // 初始化进程状态：未运行
//   process_running[new_pid].store(false);
//   // 创建子线程执行命令
//   process_map[new_pid] = thread(&ProcessManager::run_command, this, type, args, new_pid);
//   // 进程加入就绪队列（加锁保证线程安全）
//   {
//     unique_lock<mutex> lock(g_mtx);
//     ready_queue.push(new_pid);
//   }
//   // 唤醒调度器，执行新进程
//   schedule_sem.post();
//   return new_pid;
// }

// /**
//  * @brief 启动调度器线程
//  */
// void ProcessManager::start_scheduler()
// {
//   // 原子交换：确保调度器只启动一次
//   if (!is_scheduler_running.exchange(true))
//   {
//     thread scheduler_thread(&ProcessManager::schedule, this);
//     scheduler_thread.detach(); // 分离线程，后台运行
//   }
// }

// /**
//  * @brief 停止所有进程和调度器
//  */
// void ProcessManager::stop_all_processes()
// {
//   // 1. 标记调度器停止
//   is_scheduler_running.store(false);
//   schedule_sem.post(); // 唤醒调度器，使其退出wait()

//   // 2. 停止所有工作线程
//   for (auto &pair : process_running)
//     pair.second.store(false);

//   // 3. 等待所有线程结束（避免资源泄漏）
//   for (auto &pair : process_map)
//     if (pair.second.joinable())
//       pair.second.join();

//   // 4. 清空数据结构
//   process_map.clear();
//   process_running.clear();

//   // 5. 清空就绪队列（加锁保证线程安全）
//   unique_lock<mutex> lock(g_mtx);
//   ready_queue = queue<pid_t>();
// }

// 按依赖顺序包含头文件
// #include "process.hpp"
// #include "../../common/common.hpp"
// #include "ipc.hpp"                       // MessageQueue实现
// #include "sync.hpp"                      // Semaphore实现（必须包含）
// #include "../fs_core/file_interface.hpp" // FileInterface实现
// #include <iostream>
// #include <mutex>
// #include <chrono>
// #include <atomic>
// #include <thread>
// #include <map>
// #include <queue>
// #include "json.hpp" // JSON解析

// using json = nlohmann::json;
// using namespace std;

// // ========== 全局变量定义（匹配hpp声明） ==========
// std::mutex g_mtx; // 保护就绪队列的全局互斥锁

// // ========== 静态成员初始化（关键！） ==========
// std::atomic<pid_t> ProcessManager::next_pid{1}; // 静态PID生成器，初始值1

// /**
//  * @brief ProcessManager构造函数（匹配hpp声明）
//  */
// ProcessManager::ProcessManager(FileInterface &fi, MessageQueue &mq)
//     : file_interface(fi), msg_queue(mq), schedule_sem(0), // sem初始计数0
//       is_scheduler_running(false), time_slice(100)        // 显式初始化状态
// {
//   // sync.hpp的Semaphore(0)会创建Windows信号量，无需额外操作
// }

// /**
//  * @brief 析构函数
//  */
// ProcessManager::~ProcessManager()
// {
//   stop_all_processes();
//   // Semaphore析构函数自动释放Windows信号量句柄
// }

// /**
//  * @brief 调度器主循环（时间片轮转核心，修复原子变量访问）
//  */
// void ProcessManager::schedule()
// {
//   // 正确访问原子变量：is_scheduler_running（调用load()）
//   while (is_scheduler_running.load())
//   {
//     schedule_sem.wait(); // P操作：等待调度信号

//     // 调度器已停止，退出循环
//     if (!is_scheduler_running.load())
//       break;

//     // 加锁访问就绪队列（线程安全）
//     unique_lock<mutex> lock(g_mtx);
//     if (ready_queue.empty())
//     {
//       lock.unlock();
//       continue;
//     }

//     // 取出队首进程PID
//     pid_t current_pid = ready_queue.front();
//     ready_queue.pop();
//     lock.unlock();

//     // 检查进程是否存在且可运行
//     auto proc_iter = process_running.find(current_pid);
//     if (proc_iter == process_running.end() || !proc_iter->second.load())
//     {
//       continue;
//     }

//     // 标记进程为运行中，执行时间片
//     proc_iter->second.store(true);
//     this_thread::sleep_for(chrono::milliseconds(time_slice)); // 模拟执行
//     proc_iter->second.store(false);                           // 标记进程暂停

//     // 进程重新入队，等待下一次调度
//     lock.lock();
//     ready_queue.push(current_pid);
//     lock.unlock();

//     schedule_sem.post(); // V操作：触发下一次调度
//   }
// }

// /**
//  * @brief 执行具体命令（子线程入口）
//  */
// void ProcessManager::run_command(CommandType type, const string &args, pid_t pid)
// {
//   json args_json;
//   // 1. 解析JSON参数（捕获解析异常）
//   try
//   {
//     args_json = json::parse(args);
//   }
//   catch (const json::parse_error &e)
//   {
//     Message err_msg;
//     err_msg.sender_pid = pid;
//     err_msg.receiver_pid = 0; // 广播错误
//     err_msg.type = MessageType::RES_RESULT;
//     err_msg.content = json({{"success", false}, {"msg", "参数解析失败: " + string(e.what())}}).dump();
//     msg_queue.send_message(err_msg);
//     process_running[pid].store(false);
//     return;
//   }

//   bool success = true;
//   string result = "执行成功";

//   // 2. 执行具体命令（捕获业务异常）
//   try
//   {
//     while (process_running[pid].load())
//     {
//       switch (type)
//       {
//       case CommandType::CREATE_FILE:
//       {
//         // 参数合法性校验
//         if (!args_json.contains("path") || !args_json.contains("content"))
//           throw runtime_error("CREATE_FILE缺少参数：path/content");
//         string path = args_json["path"];
//         string content = args_json["content"];
//         int perm = File_READ | File_WRITE;
//         success = file_interface.create_file(path, static_cast<FilePermission>(perm), content);
//         if (!success)
//           result = "创建文件失败（路径已存在/磁盘满/路径无效）";
//         break;
//       }
//       case CommandType::DELETE_FILE:
//       {
//         if (!args_json.contains("path"))
//           throw runtime_error("DELETE_FILE缺少参数：path");
//         string path = args_json["path"];
//         success = file_interface.delete_file(path, pid);
//         if (!success)
//           result = "删除文件失败（文件不存在/被锁定）";
//         break;
//       }
//       case CommandType::MKDIR:
//       {
//         if (!args_json.contains("path"))
//           throw runtime_error("MKDIR缺少参数：path");
//         string path = args_json["path"];
//         success = file_interface.create_directory(path);
//         if (!success)
//           result = "创建目录失败（路径已存在/解析错误）";
//         break;
//       }
//       case CommandType::QUERY_DIR:
//       {
//         if (!args_json.contains("path"))
//           throw runtime_error("QUERY_DIR缺少参数：path");
//         string path = args_json["path"];
//         result = file_interface.query_directory(path);
//         success = !result.empty();
//         if (!success)
//           result = "查询目录失败（路径不存在）";
//         break;
//       }
//       default:
//       {
//         success = false;
//         result = "不支持的命令类型：" + to_string(static_cast<int>(type));
//         break;
//       }
//       }

//       // 执行一次后暂停，等待下一次调度
//       process_running[pid].store(false);
//       // 等待调度器重新唤醒（避免空循环占用CPU）
//       while (!process_running[pid].load() && process_running.count(pid))
//         this_thread::sleep_for(chrono::milliseconds(10));
//     }
//   }
//   catch (const exception &e)
//   {
//     success = false;
//     result = "执行异常: " + string(e.what());
//   }

//   // 3. 发送执行结果（广播）
//   Message res_msg;
//   res_msg.sender_pid = pid;
//   res_msg.receiver_pid = 0;
//   res_msg.type = MessageType::RES_RESULT;
//   res_msg.content = json({{"success", success}, {"result", result}}).dump();
//   msg_queue.send_message(res_msg);

//   // 标记进程执行完成
//   process_running[pid].store(false);
// }

// /**
//  * @brief 创建新进程（实际创建子线程）
//  */
// pid_t ProcessManager::create_process(CommandType type, const string &args)
// {
//   // 原子操作生成唯一PID（线程安全）
//   pid_t new_pid = next_pid.fetch_add(1);
//   // 初始化进程状态：未运行
//   process_running[new_pid].store(false);
//   // 创建子线程执行命令
//   process_map[new_pid] = thread(&ProcessManager::run_command, this, type, args, new_pid);
//   // 进程加入就绪队列（加锁保证线程安全）
//   {
//     unique_lock<mutex> lock(g_mtx);
//     ready_queue.push(new_pid);
//   }
//   // 唤醒调度器，执行新进程
//   schedule_sem.post();
//   return new_pid;
// }

// /**
//  * @brief 启动调度器线程（修复原子变量exchange()调用）
//  */
// void ProcessManager::start_scheduler()
// {
//   // 正确访问原子变量：is_scheduler_running（调用exchange()）
//   if (!is_scheduler_running.exchange(true))
//   {
//     thread scheduler_thread(&ProcessManager::schedule, this);
//     scheduler_thread.detach(); // 分离线程，后台运行
//   }
// }

// /**
//  * @brief 停止所有进程和调度器（修复原子变量store()调用）
//  */
// void ProcessManager::stop_all_processes()
// {
//   // 正确访问原子变量：is_scheduler_running（调用store()）
//   is_scheduler_running.store(false);
//   schedule_sem.post(); // 唤醒调度器，使其退出wait()

//   // 2. 停止所有工作线程
//   for (auto &pair : process_running)
//     pair.second.store(false);

//   // 3. 等待所有线程结束（避免资源泄漏）
//   for (auto &pair : process_map)
//     if (pair.second.joinable())
//       pair.second.join();

//   // 4. 清空数据结构
//   process_map.clear();
//   process_running.clear();

//   // 5. 清空就绪队列（加锁保证线程安全）
//   unique_lock<mutex> lock(g_mtx);
//   ready_queue = queue<pid_t>();
// }

// #include "process.hpp"
// #include "ipc.hpp"
// #include "../fs_core/file_interface.hpp"
// #include "json.hpp"
// #include <iostream>
// #include <thread>
// #include <chrono>
// #include <mutex>
// #include <stdexcept>

// using json = nlohmann::json;
// using namespace std;

// // 全局互斥锁（进程模块共享）
// mutex g_mtx;

// // ===================== ProcessManager 构造/析构 =====================
// ProcessManager::ProcessManager(FileInterface &file_if, MessageQueue &msg_q)
//     : file_interface(file_if), msg_queue(msg_q),
//       schedule_sem{1}, // 显式初始化信号量为1
//       is_scheduler_running(false), time_slice(100), next_pid(1)
// {
// }

// ProcessManager::~ProcessManager()
// {
//   stop_all_processes();
// }

// // ===================== 进程ID生成（缩小锁持有范围） =====================
// pid_t ProcessManager::generate_pid()
// {
//   pid_t pid = 0;
//   // 仅在生成PID时加锁，生成后立即释放
//   {
//     lock_guard<mutex> lock(g_mtx);
//     pid = this->next_pid++;
//   }
//   return pid;
// }

// // ===================== 创建进程（核心修复：避免锁嵌套+信号量正确使用） =====================
// pid_t ProcessManager::create_process(CommandType type, const string &args_json)
// {
//   pid_t pid = this->generate_pid(); // 先生成PID，释放g_mtx后再做后续操作

//   // 缩小锁范围：仅操作进程状态时加锁，避免持有锁执行文件操作
//   {
//     lock_guard<mutex> lock(g_mtx);
//     this->process_running[pid].store(true);
//     this->process_args[pid] = make_pair(type, args_json);
//   }

//   // 修复：先释放全局锁，再创建线程（避免线程内立即获取其他锁导致嵌套）
//   thread proc_thread(&ProcessManager::run_process, this, pid);
//   proc_thread.detach();

//   return pid;
// }

// // ===================== 进程执行核心逻辑（修复同步+避免死锁） =====================
// void ProcessManager::run_process(pid_t pid)
// {
//   // 修复：调度器启动同步（使用原子变量+短暂休眠，避免信号量阻塞）
//   int wait_count = 0;
//   while (!this->is_scheduler_running.load())
//   {
//     this_thread::sleep_for(chrono::milliseconds(10));
//     wait_count++;
//     // 防止无限等待：超过5秒则判定调度器未启动，终止进程
//     if (wait_count > 500)
//     {
//       lock_guard<mutex> lock(g_mtx);
//       this->process_running[pid].store(false);

//       Message res_msg;
//       res_msg.sender_pid = pid;
//       res_msg.receiver_pid = 0;
//       res_msg.type = MessageType::RES_RESULT;
//       res_msg.content = json({{"success", false}, {"msg", "调度器未启动，进程超时终止"}}).dump();
//       this->msg_queue.send_message(res_msg);
//       return;
//     }
//   }

//   // 信号量P操作（非阻塞模式，避免死锁）
//   if (!this->schedule_sem.try_wait())
//   { // 改用try_wait，失败则直接返回，避免阻塞
//     lock_guard<mutex> lock(g_mtx);
//     this->process_running[pid].store(false);

//     Message res_msg;
//     res_msg.sender_pid = pid;
//     res_msg.receiver_pid = 0;
//     res_msg.type = MessageType::RES_RESULT;
//     res_msg.content = json({{"success", false}, {"msg", "信号量获取失败，避免死锁"}}).dump();
//     this->msg_queue.send_message(res_msg);
//     return;
//   }

//   bool success = true;
//   string result = "执行成功";
//   auto [cmd_type, args_json] = this->process_args[pid];

//   try
//   {
//     json args = json::parse(args_json);

//     switch (cmd_type)
//     {
//     case CommandType::CREATE_FILE:
//     {
//       if (!args.contains("path") || !args.contains("content"))
//       {
//         throw runtime_error("CREATE_FILE缺少参数：path/content");
//       }
//       string path = args["path"].get<string>();
//       string content = args["content"];

//       // 修复：文件操作前释放所有进程锁，避免嵌套
//       FilePermission perm = static_cast<FilePermission>(File_READ | File_WRITE | File_DEL);
//       // 执行文件操作（此时无进程锁，仅持有文件系统自身的锁）
//       bool create_ok = this->file_interface.create_file(path, perm, "");
//       if (!create_ok)
//       {
//         throw runtime_error("文件创建失败：路径已存在/磁盘满");
//       }

//       bool write_ok = this->file_interface.write_file(path, content);
//       if (!write_ok)
//       {
//         this->file_interface.delete_file(path, pid);
//         throw runtime_error("文件创建成功，但内容写入失败");
//       }

//       result = "CREATE_FILE成功：" + path + "（内容：" + content + "）";
//       break;
//     }

//     case CommandType::DELETE_FILE:
//     {
//       if (!args.contains("path"))
//       {
//         throw runtime_error("DELETE_FILE缺少参数：path");
//       }
//       string path = args["path"].get<string>();

//       bool delete_ok = this->file_interface.delete_file(path, pid);
//       if (!delete_ok)
//       {
//         throw runtime_error("删除文件失败：文件不存在/被锁定");
//       }
//       result = "DELETE_FILE成功：" + path;
//       break;
//     }

//     case CommandType::MKDIR:
//     {
//       if (!args.contains("path"))
//       {
//         throw runtime_error("MKDIR缺少参数：path");
//       }
//       string path = args["path"].get<string>();

//       bool mkdir_ok = this->file_interface.create_directory(path);
//       if (!mkdir_ok)
//       {
//         throw runtime_error("创建目录失败：路径已存在/解析错误");
//       }
//       result = "MKDIR成功：" + path;
//       break;
//     }

//     case CommandType::QUERY_DIR:
//     {
//       if (!args.contains("path"))
//       {
//         throw runtime_error("QUERY_DIR缺少参数：path");
//       }
//       string path = args["path"].get<string>();

//       string dir_content = this->file_interface.query_directory(path);
//       if (dir_content.empty())
//       {
//         result = "QUERY_DIR：" + path + "（空目录/路径不存在）";
//       }
//       else
//       {
//         result = "QUERY_DIR成功：" + path + "\n" + dir_content;
//       }
//       break;
//     }

//     default:
//       throw runtime_error("不支持的命令类型");
//     }
//   }
//   catch (const json::parse_error &e)
//   {
//     success = false;
//     result = "JSON解析失败：" + string(e.what());
//   }
//   catch (const exception &e)
//   {
//     success = false;
//     result = "执行失败：" + string(e.what());
//   }

//   // 信号量V操作（释放，避免其他进程阻塞）
//   this->schedule_sem.post();

//   // 标记进程完成（缩小锁范围）
//   {
//     lock_guard<mutex> lock(g_mtx);
//     this->process_running[pid].store(false);
//   }

//   // 发送消息（无锁，仅操作消息队列）
//   Message res_msg;
//   res_msg.sender_pid = pid;
//   res_msg.receiver_pid = 0;
//   res_msg.type = MessageType::RES_RESULT;
//   res_msg.content = json({{"success", success}, {"msg", result}}).dump();
//   this->msg_queue.send_message(res_msg);
// }

// // ===================== 调度器启停（修复同步逻辑） =====================
// void ProcessManager::start_scheduler()
// {
//   if (this->is_scheduler_running.load())
//   {
//     throw runtime_error("调度器已启动");
//   }
//   this->is_scheduler_running.store(true);
//   // 调度器启动后，V操作唤醒所有等待的进程
//   this->schedule_sem.post();

//   thread scheduler_thread(&ProcessManager::scheduler_loop, this);
//   scheduler_thread.detach();
// }

// void ProcessManager::stop_all_processes()
// {
//   this->is_scheduler_running.store(false);
//   // 停止时释放信号量，避免进程阻塞
//   this->schedule_sem.post();

//   lock_guard<mutex> lock(g_mtx);
//   for (auto &[pid, running] : this->process_running)
//   {
//     running.store(false);
//   }
//   this->process_running.clear();
//   this->process_args.clear();
// }

// // ===================== 时间片轮转调度循环（简化，避免额外锁） =====================
// void ProcessManager::scheduler_loop()
// {
//   while (this->is_scheduler_running.load())
//   {
//     // 调度器仅做时间片控制，不持有全局锁（避免死锁）
//     this_thread::sleep_for(chrono::milliseconds(this->time_slice));
//   }
// }

// // ===================== 辅助方法（实现声明的接口） =====================
// bool ProcessManager::is_scheduler_active() const
// {
//   return this->is_scheduler_running.load();
// }

// map<pid_t, atomic<bool>> &ProcessManager::get_process_running()
// {
//   return this->process_running;
// }

// void ProcessManager::set_time_slice(int ms)
// {
//   if (ms > 0)
//   {
//     this->time_slice = ms;
//   }
// }

// int ProcessManager::get_time_slice() const
// {
//   return this->time_slice;
// }

// #include "process.hpp"
// #include "ipc.hpp"
// #include "../fs_core/file_interface.hpp"
// #include "json.hpp"
// #include <iostream>
// #include <thread>
// #include <chrono>
// #include <mutex>
// #include <stdexcept>
// #include <vector>

// using json = nlohmann::json;
// using namespace std;

// // 全局互斥锁
// mutex g_mtx;

// // ===================== 构造/析构 =====================
// ProcessManager::ProcessManager(FileInterface &file_if, MessageQueue &msg_q)
//     : file_interface(file_if), msg_queue(msg_q),
//       schedule_sem{1},
//       is_scheduler_running(false), is_scheduling(false),
//       current_running_pid(0), time_slice(100), next_pid(1)
// {
// }

// ProcessManager::~ProcessManager()
// {
//   stop_all_processes();
// }

// // ===================== PID生成 =====================
// pid_t ProcessManager::generate_pid()
// {
//   pid_t pid = 0;
//   {
//     lock_guard<mutex> lock(g_mtx);
//     pid = this->next_pid++;
//   }
//   return pid;
// }

// // ===================== 创建进程（仅入队，不立即执行） =====================
// pid_t ProcessManager::create_process(CommandType type, const string &args_json)
// {
//   pid_t pid = this->generate_pid();

//   // 初始化进程状态（未运行）
//   {
//     lock_guard<mutex> lock(g_mtx);
//     this->process_running[pid].store(false);
//     this->process_args[pid] = make_pair(type, args_json);
//   }

//   // 加入就绪队列
//   {
//     lock_guard<mutex> lock(queue_mtx);
//     ready_queue.push(pid);
//   }

//   cout << "✅ 进程创建成功，PID：" << pid << "（已加入就绪队列，等待调度）\n";
//   return pid;
// }

// // ===================== 执行单个进程的一个时间片 =====================
// void ProcessManager::execute_process_slice(pid_t pid)
// {
//   if (!is_scheduler_running.load())
//   {
//     cerr << "[ERROR] 调度器未启动，无法执行进程PID：" << pid << "\n";
//     return;
//   }

//   // 标记进程运行中
//   current_running_pid.store(pid);
//   process_running[pid].store(true);

//   // 可视化日志
//   cout << "\n⏱️  时间片开始（" << time_slice << "ms）：执行进程PID=" << pid << "\n";
//   cout << "----------------------------------------\n";

//   bool success = true;
//   string result = "执行成功";
//   auto [cmd_type, args_json] = this->process_args[pid];

//   try
//   {
//     json args = json::parse(args_json);

//     switch (cmd_type)
//     {
//     case CommandType::CREATE_FILE:
//     {
//       if (!args.contains("path") || !args.contains("content"))
//         throw runtime_error("CREATE_FILE缺少参数：path/content");

//       string path = args["path"].get<string>();
//       string content = args["content"];
//       FilePermission perm = static_cast<FilePermission>(File_READ | File_WRITE | File_DEL);

//       bool create_ok = this->file_interface.create_file(path, perm, "");
//       if (!create_ok)
//         throw runtime_error("文件创建失败：路径已存在/磁盘满");

//       bool write_ok = this->file_interface.write_file(path, content);
//       if (!write_ok)
//       {
//         this->file_interface.delete_file(path, pid);
//         throw runtime_error("文件创建成功，但内容写入失败");
//       }

//       result = "CREATE_FILE成功：" + path + "（内容：" + content + "）";
//       break;
//     }

//     case CommandType::DELETE_FILE:
//     {
//       if (!args.contains("path"))
//         throw runtime_error("DELETE_FILE缺少参数：path");

//       string path = args["path"].get<string>();
//       bool delete_ok = this->file_interface.delete_file(path, pid);
//       if (!delete_ok)
//         throw runtime_error("删除文件失败：文件不存在/被锁定");

//       result = "DELETE_FILE成功：" + path;
//       break;
//     }

//     case CommandType::MKDIR:
//     {
//       if (!args.contains("path"))
//         throw runtime_error("MKDIR缺少参数：path");

//       string path = args["path"].get<string>();
//       bool mkdir_ok = this->file_interface.create_directory(path);
//       if (!mkdir_ok)
//         throw runtime_error("创建目录失败：路径已存在/解析错误");

//       result = "MKDIR成功：" + path;
//       break;
//     }

//     case CommandType::QUERY_DIR:
//     {
//       if (!args.contains("path"))
//         throw runtime_error("QUERY_DIR缺少参数：path");

//       string path = args["path"].get<string>();
//       string dir_content = this->file_interface.query_directory(path);
//       result = dir_content.empty()
//                    ? "QUERY_DIR：" + path + "（空目录/路径不存在）"
//                    : "QUERY_DIR成功：" + path + "\n" + dir_content;
//       break;
//     }

//     default:
//       throw runtime_error("不支持的命令类型");
//     }
//   }
//   catch (const json::parse_error &e)
//   {
//     success = false;
//     result = "JSON解析失败：" + string(e.what());
//   }
//   catch (const exception &e)
//   {
//     success = false;
//     result = "执行失败：" + string(e.what());
//   }

//   // 输出执行结果
//   cout << "[PID:" << pid << "] 执行结果：" << (success ? "✅ " : "❌ ") << result << "\n";

//   // 模拟时间片执行
//   this_thread::sleep_for(chrono::milliseconds(time_slice));

//   // 时间片结束
//   cout << "----------------------------------------\n";
//   cout << "⏹️  时间片结束（PID=" << pid << "）：已执行" << time_slice << "ms\n";

//   // 标记进程就绪
//   process_running[pid].store(false);
//   current_running_pid.store(0);

//   // 发送执行结果消息
//   Message res_msg;
//   res_msg.sender_pid = pid;
//   res_msg.receiver_pid = 0;
//   res_msg.type = MessageType::RES_RESULT;
//   res_msg.content = json({{"success", success}, {"msg", result}}).dump();
//   this->msg_queue.send_message(res_msg);
// }

// // ===================== 进程执行入口（兼容原有逻辑） =====================
// void ProcessManager::run_process(pid_t pid)
// {
//   // 已整合到 execute_process_slice，此处仅留兼容接口
//   execute_process_slice(pid);
// }

// // ===================== 启动调度器（仅初始化，不执行） =====================
// void ProcessManager::start_scheduler()
// {
//   if (this->is_scheduler_running.load())
//     throw runtime_error("调度器已启动");

//   this->is_scheduler_running.store(true);
//   cout << "🚀 时间片轮转调度器已初始化（时间片：" << time_slice << "ms）\n";
//   cout << "💡 提示：创建进程后执行 run_scheduler 开始调度执行\n";
// }

// // ===================== 触发时间片轮转调度 =====================
// void ProcessManager::run_scheduler()
// {
//   if (!is_scheduler_running.load())
//   {
//     cerr << "❌ 请先执行 start_scheduler 启动调度器！\n";
//     return;
//   }
//   if (is_scheduling.load())
//   {
//     cout << "⚠️  调度器已在运行中，当前就绪队列进程数：" << get_ready_queue().size() << "\n";
//     return;
//   }

//   is_scheduling.store(true);
//   cout << "\n🚦 开始时间片轮转调度（时间片：" << time_slice << "ms）\n";
//   cout << "========================================\n";

//   // 调度线程（后台运行）
//   thread scheduler_thread([this]()
//                           {
//     while (is_scheduling.load() && is_scheduler_running.load())
//     {
//       pid_t current_pid = -1;

//       // 从就绪队列取进程（加锁）
//       {
//         lock_guard<mutex> lock(queue_mtx);
//         if (!ready_queue.empty())
//         {
//           current_pid = ready_queue.front();
//           ready_queue.pop();
//         }
//       }

//       // 无就绪进程则休眠
//       if (current_pid == -1)
//       {
//         this_thread::sleep_for(chrono::milliseconds(100));
//         continue;
//       }

//       // 执行一个时间片
//       execute_process_slice(current_pid);

//       // 放回就绪队列尾部（轮转）
//       {
//         lock_guard<mutex> lock(queue_mtx);
//         ready_queue.push(current_pid);
//       }

//       // 调度切换日志
//       {
//         lock_guard<mutex> lock(queue_mtx);
//         cout << "\n🔄 调度切换：PID=" << current_pid << " 放回就绪队列，剩余就绪进程数：" << ready_queue.size() << "\n\n";
//       }
//     }

//     is_scheduling.store(false);
//     cout << "========================================\n";
//     cout << "🛑 调度器停止运行\n"; });

//   scheduler_thread.detach();
// }

// // ===================== 停止调度（保留就绪队列） =====================
// void ProcessManager::stop_scheduling()
// {
//   if (is_scheduling.load())
//   {
//     is_scheduling.store(false);
//     cout << "🛑 调度执行已停止，就绪队列进程保留\n";
//   }
//   else
//   {
//     cout << "⚠️  调度器未在运行\n";
//   }
// }

// // ===================== 停止所有进程 =====================
// void ProcessManager::stop_all_processes()
// {
//   // 停止调度
//   is_scheduling.store(false);
//   is_scheduler_running.store(false);

//   // 释放信号量
//   this->schedule_sem.post();

//   // 清空进程状态
//   {
//     lock_guard<mutex> lock(g_mtx);
//     for (auto &[pid, running] : this->process_running)
//       running.store(false);
//     this->process_running.clear();
//     this->process_args.clear();
//   }

//   // 清空就绪队列
//   {
//     lock_guard<mutex> lock(queue_mtx);
//     while (!ready_queue.empty())
//       ready_queue.pop();
//   }

//   cout << "🔌 所有进程已停止，就绪队列已清空\n";
// }

// // ===================== 调度器循环（兼容原有接口） =====================
// void ProcessManager::scheduler_loop()
// {
//   // 已整合到 run_scheduler 的调度线程，此处仅留兼容接口
//   run_scheduler();
// }

// // ===================== 获取就绪队列 =====================
// vector<pid_t> ProcessManager::get_ready_queue()
// {
//   vector<pid_t> queue_copy;
//   lock_guard<mutex> lock(queue_mtx);
//   queue<pid_t> temp_queue = ready_queue;

//   while (!temp_queue.empty())
//   {
//     queue_copy.push_back(temp_queue.front());
//     temp_queue.pop();
//   }
//   return queue_copy;
// }

// // ===================== 辅助方法实现 =====================
// bool ProcessManager::is_scheduler_active() const
// {
//   return this->is_scheduler_running.load();
// }

// map<pid_t, atomic<bool>> &ProcessManager::get_process_running()
// {
//   return this->process_running;
// }

// void ProcessManager::set_time_slice(int ms)
// {
//   if (ms > 0)
//     this->time_slice = ms;
// }

// int ProcessManager::get_time_slice() const
// {
//   return this->time_slice;
// }

// #include "process.hpp"
// #include "ipc.hpp"
// #include "../fs_core/file_interface.hpp"
// #include "json.hpp"
// #include <iostream>
// #include <thread>
// #include <chrono>
// #include <mutex>
// #include <stdexcept>
// #include <vector>

// using json = nlohmann::json;
// using namespace std;

// // 全局互斥锁
// mutex g_mtx;

// // ===================== 构造/析构 =====================
// ProcessManager::ProcessManager(FileInterface &file_if, MessageQueue &msg_q)
//     : file_interface(file_if), msg_queue(msg_q),
//       schedule_sem{1},
//       is_scheduler_running(false), is_scheduling(false),
//       current_running_pid(0), time_slice(100), next_pid(1)
// {
// }

// ProcessManager::~ProcessManager()
// {
//   stop_all_processes();
// }

// // ===================== PID生成 =====================
// pid_t ProcessManager::generate_pid()
// {
//   pid_t pid = 0;
//   {
//     lock_guard<mutex> lock(g_mtx);
//     pid = this->next_pid++;
//   }
//   return pid;
// }

// // ===================== 创建进程（仅入队，不立即执行） =====================
// pid_t ProcessManager::create_process(CommandType type, const string &args_json)
// {
//   pid_t pid = this->generate_pid();

//   // 初始化进程状态（未运行、未完成）
//   {
//     lock_guard<mutex> lock(g_mtx);
//     this->process_running[pid].store(false);
//     this->process_completed[pid].store(false); // 初始化未完成
//     this->process_args[pid] = make_pair(type, args_json);
//   }

//   // 加入就绪队列
//   {
//     lock_guard<mutex> lock(queue_mtx);
//     ready_queue.push(pid);
//   }

//   cout << "✅ 进程创建成功，PID：" << pid << "（已加入就绪队列，等待调度）\n";
//   return pid;
// }

// // 新增：标记进程为已完成
// void ProcessManager::mark_process_completed(pid_t pid)
// {
//   lock_guard<mutex> lock(g_mtx);
//   if (process_completed.find(pid) != process_completed.end())
//   {
//     process_completed[pid].store(true);
//   }
// }

// // ===================== 执行单个进程的一个时间片 =====================
// void ProcessManager::execute_process_slice(pid_t pid)
// {
//   if (!is_scheduler_running.load())
//   {
//     cerr << "[ERROR] 调度器未启动，无法执行进程PID：" << pid << "\n";
//     return;
//   }

//   // 检查进程是否已完成，已完成则直接返回
//   {
//     lock_guard<mutex> lock(g_mtx);
//     if (process_completed[pid].load())
//     {
//       cout << "[PID:" << pid << "] 已完成，跳过执行\n";
//       return;
//     }
//   }

//   // 标记进程运行中
//   current_running_pid.store(pid);
//   process_running[pid].store(true);

//   // 可视化日志
//   cout << "\n⏱️  时间片开始（" << time_slice << "ms）：执行进程PID=" << pid << "\n";
//   cout << "----------------------------------------\n";

//   bool success = true;
//   string result = "执行成功";
//   auto [cmd_type, args_json] = this->process_args[pid];

//   try
//   {
//     json args = json::parse(args_json);

//     switch (cmd_type)
//     {
//     case CommandType::CREATE_FILE:
//     {
//       if (!args.contains("path") || !args.contains("content"))
//         throw runtime_error("CREATE_FILE缺少参数：path/content");

//       string path = args["path"].get<string>();
//       string content = args["content"];
//       FilePermission perm = static_cast<FilePermission>(File_READ | File_WRITE | File_DEL);

//       bool create_ok = this->file_interface.create_file(path, perm, "");
//       if (!create_ok)
//         throw runtime_error("文件创建失败：路径已存在/磁盘满");

//       bool write_ok = this->file_interface.write_file(path, content);
//       if (!write_ok)
//       {
//         this->file_interface.delete_file(path, pid);
//         throw runtime_error("文件创建成功，但内容写入失败");
//       }

//       result = "CREATE_FILE成功：" + path + "（内容：" + content + "）";
//       break;
//     }

//     case CommandType::DELETE_FILE:
//     {
//       if (!args.contains("path"))
//         throw runtime_error("DELETE_FILE缺少参数：path");

//       string path = args["path"].get<string>();
//       bool delete_ok = this->file_interface.delete_file(path, pid);
//       if (!delete_ok)
//         throw runtime_error("删除文件失败：文件不存在/被锁定");

//       result = "DELETE_FILE成功：" + path;
//       break;
//     }

//     case CommandType::MKDIR:
//     {
//       if (!args.contains("path"))
//         throw runtime_error("MKDIR缺少参数：path");

//       string path = args["path"].get<string>();
//       bool mkdir_ok = this->file_interface.create_directory(path);
//       if (!mkdir_ok)
//         throw runtime_error("创建目录失败：路径已存在/解析错误");

//       result = "MKDIR成功：" + path;
//       break;
//     }

//     case CommandType::QUERY_DIR:
//     {
//       if (!args.contains("path"))
//         throw runtime_error("QUERY_DIR缺少参数：path");

//       string path = args["path"].get<string>();
//       string dir_content = this->file_interface.query_directory(path);
//       result = dir_content.empty()
//                    ? "QUERY_DIR：" + path + "（空目录/路径不存在）"
//                    : "QUERY_DIR成功：" + path + "\n" + dir_content;
//       break;
//     }

//     default:
//       throw runtime_error("不支持的命令类型");
//     }
//   }
//   catch (const json::parse_error &e)
//   {
//     success = false;
//     result = "JSON解析失败：" + string(e.what());
//   }
//   catch (const exception &e)
//   {
//     success = false;
//     result = "执行失败：" + string(e.what());
//   }

//   // 输出执行结果
//   cout << "[PID:" << pid << "] 执行结果：" << (success ? "✅ " : "❌ ") << result << "\n";

//   // 模拟时间片执行
//   this_thread::sleep_for(chrono::milliseconds(time_slice));

//   // 时间片结束
//   cout << "----------------------------------------\n";
//   cout << "⏹️  时间片结束（PID=" << pid << "）：已执行" << time_slice << "ms\n";

//   // 标记进程为已完成（无论成功/失败，仅执行一次）
//   mark_process_completed(pid);

//   // 标记进程就绪（未运行）
//   process_running[pid].store(false);
//   current_running_pid.store(0);

//   // 发送执行结果消息
//   Message res_msg;
//   res_msg.sender_pid = pid;
//   res_msg.receiver_pid = 0;
//   res_msg.type = MessageType::RES_RESULT;
//   res_msg.content = json({{"success", success}, {"msg", result}}).dump();
//   this->msg_queue.send_message(res_msg);
// }

// // ===================== 进程执行入口（兼容原有逻辑） =====================
// void ProcessManager::run_process(pid_t pid)
// {
//   execute_process_slice(pid);
// }

// // ===================== 启动调度器（仅初始化，不执行） =====================
// void ProcessManager::start_scheduler()
// {
//   if (this->is_scheduler_running.load())
//     throw runtime_error("调度器已启动");

//   this->is_scheduler_running.store(true);
//   cout << "🚀 时间片轮转调度器已初始化（时间片：" << time_slice << "ms）\n";
//   cout << "💡 提示：创建进程后执行 run_scheduler 开始调度执行\n";
// }

// // ===================== 触发时间片轮转调度（核心修复：执行后不放回队列） =====================
// void ProcessManager::run_scheduler()
// {
//   if (!is_scheduler_running.load())
//   {
//     cerr << "❌ 请先执行 start_scheduler 启动调度器！\n";
//     return;
//   }
//   if (is_scheduling.load())
//   {
//     cout << "⚠️  调度器已在运行中，当前就绪队列进程数：" << get_ready_queue().size() << "\n";
//     return;
//   }

//   is_scheduling.store(true);
//   cout << "\n🚦 开始时间片轮转调度（时间片：" << time_slice << "ms）\n";
//   cout << "========================================\n";

//   // 调度线程（后台运行）
//   thread scheduler_thread([this]()
//                           {
//     while (is_scheduling.load() && is_scheduler_running.load())
//     {
//       pid_t current_pid = -1;

//       // 从就绪队列取进程（加锁）
//       {
//         lock_guard<mutex> lock(queue_mtx);
//         if (!ready_queue.empty())
//         {
//           current_pid = ready_queue.front();
//           ready_queue.pop(); // 取出后直接移除，不再放回
//         }
//       }

//       // 无就绪进程则休眠并检查是否退出
//       if (current_pid == -1)
//       {
//         this_thread::sleep_for(chrono::milliseconds(100));
//         // 就绪队列为空时自动停止调度
//         {
//           lock_guard<mutex> lock(queue_mtx);
//           if (ready_queue.empty())
//           {
//             cout << "\n📋 就绪队列为空，调度器自动停止\n";
//             is_scheduling.store(false);
//             break;
//           }
//         }
//         continue;
//       }

//       // 执行一个时间片（执行后标记为已完成，不再放回）
//       execute_process_slice(current_pid);

//       // 移除：不再将进程放回就绪队列，实现「仅执行一次」
//       cout << "\n🔄 调度切换：PID=" << current_pid << " 已完成，从就绪队列移除\n\n";
//     }

//     is_scheduling.store(false);
//     cout << "========================================\n";
//     cout << "🛑 调度器停止运行\n"; });

//   scheduler_thread.detach();
// }

// // ===================== 停止调度（保留就绪队列） =====================
// void ProcessManager::stop_scheduling()
// {
//   if (is_scheduling.load())
//   {
//     is_scheduling.store(false);
//     cout << "🛑 调度执行已停止，就绪队列进程保留\n";
//   }
//   else
//   {
//     cout << "⚠️  调度器未在运行\n";
//   }
// }

// // ===================== 停止所有进程 =====================
// void ProcessManager::stop_all_processes()
// {
//   // 停止调度
//   is_scheduling.store(false);
//   is_scheduler_running.store(false);

//   // 释放信号量
//   this->schedule_sem.post();

//   // 清空进程状态
//   {
//     lock_guard<mutex> lock(g_mtx);
//     for (auto &[pid, running] : this->process_running)
//       running.store(false);
//     this->process_running.clear();
//     this->process_completed.clear(); // 清空完成状态
//     this->process_args.clear();
//   }

//   // 清空就绪队列
//   {
//     lock_guard<mutex> lock(queue_mtx);
//     while (!ready_queue.empty())
//       ready_queue.pop();
//   }

//   cout << "🔌 所有进程已停止，就绪队列已清空\n";
// }

// // ===================== 调度器循环（兼容原有接口） =====================
// void ProcessManager::scheduler_loop()
// {
//   run_scheduler();
// }

// // ===================== 获取就绪队列 =====================
// vector<pid_t> ProcessManager::get_ready_queue()
// {
//   vector<pid_t> queue_copy;
//   lock_guard<mutex> lock(queue_mtx);
//   queue<pid_t> temp_queue = ready_queue;

//   while (!temp_queue.empty())
//   {
//     queue_copy.push_back(temp_queue.front());
//     temp_queue.pop();
//   }
//   return queue_copy;
// }

// // ===================== 辅助方法实现 =====================
// bool ProcessManager::is_scheduler_active() const
// {
//   return this->is_scheduler_running.load();
// }

// map<pid_t, atomic<bool>> &ProcessManager::get_process_running()
// {
//   return this->process_running;
// }

// void ProcessManager::set_time_slice(int ms)
// {
//   if (ms > 0)
//     this->time_slice = ms;
// }

// int ProcessManager::get_time_slice() const
// {
//   return this->time_slice;
// }

// #include "process.hpp"
// #include "ipc.hpp"
// #include "../fs_core/file_interface.hpp"
// #include "json.hpp"
// #include <iostream>
// #include <thread>
// #include <chrono>
// #include <mutex>
// #include <stdexcept>
// #include <vector>

// using json = nlohmann::json;
// using namespace std;

// // 全局互斥锁
// mutex g_mtx;

// // ===================== 构造/析构 =====================
// ProcessManager::ProcessManager(FileInterface &file_if, MessageQueue &msg_q)
//     : file_interface(file_if), msg_queue(msg_q),
//       schedule_sem{1},
//       is_scheduler_running(false), is_scheduling(false),
//       current_running_pid(0), time_slice(100), next_pid(1)
// {
// }

// ProcessManager::~ProcessManager()
// {
//   stop_all_processes();
// }

// // ===================== PID生成 =====================
// pid_t ProcessManager::generate_pid()
// {
//   pid_t pid = 0;
//   {
//     lock_guard<mutex> lock(g_mtx);
//     pid = this->next_pid++;
//   }
//   return pid;
// }

// // ===================== 新增：实现mark_process_completed方法 =====================
// void ProcessManager::mark_process_completed(pid_t pid)
// {
//   lock_guard<mutex> lock(g_mtx);
//   // 必须加this->访问类成员
//   if (this->process_completed.find(pid) != this->process_completed.end())
//   {
//     this->process_completed[pid].store(true);
//   }
// }

// // ===================== 创建进程（仅入队，不立即执行） =====================
// pid_t ProcessManager::create_process(CommandType type, const string &args_json)
// {
//   pid_t pid = this->generate_pid();

//   // 初始化进程状态（未运行、未完成）
//   {
//     lock_guard<mutex> lock(g_mtx);
//     this->process_running[pid].store(false);
//     // ========== 修复：加this-> ==========
//     this->process_completed[pid].store(false); // 初始化未完成
//     // ===================================
//     this->process_args[pid] = make_pair(type, args_json);
//   }

//   // 加入就绪队列
//   {
//     lock_guard<mutex> lock(queue_mtx);
//     ready_queue.push(pid);
//   }

//   cout << "✅ 进程创建成功，PID：" << pid << "（已加入就绪队列，等待调度）\n";
//   return pid;
// }

// // ===================== 执行单个进程的一个时间片（核心修复） =====================
// void ProcessManager::execute_process_slice(pid_t pid)
// {
//   if (!is_scheduler_running.load())
//   {
//     cerr << "[ERROR] 调度器未启动，无法执行进程PID：" << pid << "\n";
//     return;
//   }

//   // 检查进程是否已完成，已完成则直接返回
//   {
//     lock_guard<mutex> lock(g_mtx);
//     // ========== 修复：加this-> ==========
//     if (this->process_completed.find(pid) != this->process_completed.end() &&
//         this->process_completed[pid].load())
//     // ===================================
//     {
//       cout << "[PID:" << pid << "] 已完成，跳过执行\n";
//       return;
//     }
//   }

//   // 标记进程运行中
//   current_running_pid.store(pid);
//   this->process_running[pid].store(true);

//   // 可视化日志
//   cout << "\n⏱️  时间片开始（" << time_slice << "ms）：执行进程PID=" << pid << "\n";
//   cout << "----------------------------------------\n";

//   bool success = true;
//   string result = "执行成功";
//   auto [cmd_type, args_json] = this->process_args[pid];

//   try
//   {
//     json args = json::parse(args_json);

//     switch (cmd_type)
//     {
//     case CommandType::CREATE_FILE:
//     {
//       if (!args.contains("path") || !args.contains("content"))
//         throw runtime_error("CREATE_FILE缺少参数：path/content");

//       string path = args["path"].get<string>();
//       string content = args["content"];
//       FilePermission perm = static_cast<FilePermission>(File_READ | File_WRITE | File_DEL);

//       // ========== 处理warning C4834：接收nodiscard函数的返回值 ==========
//       bool create_ok = this->file_interface.create_file(path, perm, "");
//       if (!create_ok)
//         throw runtime_error("文件创建失败：路径已存在/磁盘满");

//       bool write_ok = this->file_interface.write_file(path, content);
//       if (!write_ok)
//       {
//         // 显式接收返回值，避免warning
//         bool del_ok = this->file_interface.delete_file(path, pid);
//         (void)del_ok; // 显式忽略（如果不需要使用）
//         throw runtime_error("文件创建成功，但内容写入失败");
//       }

//       result = "CREATE_FILE成功：" + path + "（内容：" + content + "）";
//       break;
//     }

//     case CommandType::DELETE_FILE:
//     {
//       if (!args.contains("path"))
//         throw runtime_error("DELETE_FILE缺少参数：path");

//       string path = args["path"].get<string>();
//       bool delete_ok = this->file_interface.delete_file(path, pid);
//       if (!delete_ok)
//         throw runtime_error("删除文件失败：文件不存在/被锁定");

//       result = "DELETE_FILE成功：" + path;
//       break;
//     }

//     case CommandType::MKDIR:
//     {
//       if (!args.contains("path"))
//         throw runtime_error("MKDIR缺少参数：path");

//       string path = args["path"].get<string>();
//       bool mkdir_ok = this->file_interface.create_directory(path);
//       if (!mkdir_ok)
//         throw runtime_error("创建目录失败：路径已存在/解析错误");

//       result = "MKDIR成功：" + path;
//       break;
//     }

//     case CommandType::QUERY_DIR:
//     {
//       if (!args.contains("path"))
//         throw runtime_error("QUERY_DIR缺少参数：path");

//       string path = args["path"].get<string>();
//       string dir_content = this->file_interface.query_directory(path);
//       result = dir_content.empty()
//                    ? "QUERY_DIR：" + path + "（空目录/路径不存在）"
//                    : "QUERY_DIR成功：" + path + "\n" + dir_content;
//       break;
//     }

//     default:
//       throw runtime_error("不支持的命令类型");
//     }
//   }
//   catch (const json::parse_error &e)
//   {
//     success = false;
//     result = "JSON解析失败：" + string(e.what());
//   }
//   catch (const exception &e)
//   {
//     success = false;
//     result = "执行失败：" + string(e.what());
//   }

//   // 输出执行结果
//   cout << "[PID:" << pid << "] 执行结果：" << (success ? "✅ " : "❌ ") << result << "\n";

//   // 模拟时间片执行
//   this_thread::sleep_for(chrono::milliseconds(time_slice));

//   // 时间片结束
//   cout << "----------------------------------------\n";
//   cout << "⏹️  时间片结束（PID=" << pid << "）：已执行" << time_slice << "ms\n";

//   // 标记进程为已完成（无论成功/失败，仅执行一次）
//   this->mark_process_completed(pid); // ========== 修复：加this-> ==========

//   // 标记进程就绪（未运行）
//   this->process_running[pid].store(false);
//   current_running_pid.store(0);

//   // 发送执行结果消息
//   Message res_msg;
//   res_msg.sender_pid = pid;
//   res_msg.receiver_pid = 0;
//   res_msg.type = MessageType::RES_RESULT;
//   res_msg.content = json({{"success", success}, {"msg", result}}).dump();
//   // ========== 处理warning C4834：接收返回值 ==========
//   bool send_ok = this->msg_queue.send_message(res_msg);
//   (void)send_ok; // 显式忽略返回值，消除warning
// }

// // ===================== 进程执行入口（兼容原有逻辑） =====================
// void ProcessManager::run_process(pid_t pid)
// {
//   execute_process_slice(pid);
// }

// // ===================== 启动调度器（仅初始化，不执行） =====================
// void ProcessManager::start_scheduler()
// {
//   if (this->is_scheduler_running.load())
//     throw runtime_error("调度器已启动");

//   this->is_scheduler_running.store(true);
//   cout << "🚀 时间片轮转调度器已初始化（时间片：" << time_slice << "ms）\n";
//   cout << "💡 提示：创建进程后执行 run_scheduler 开始调度执行\n";
// }

// // ===================== 触发时间片轮转调度（核心修复） =====================
// void ProcessManager::run_scheduler()
// {
//   if (!is_scheduler_running.load())
//   {
//     cerr << "❌ 请先执行 start_scheduler 启动调度器！\n";
//     return;
//   }
//   if (is_scheduling.load())
//   {
//     cout << "⚠️  调度器已在运行中，当前就绪队列进程数：" << get_ready_queue().size() << "\n";
//     return;
//   }

//   is_scheduling.store(true);
//   cout << "\n🚦 开始时间片轮转调度（时间片：" << time_slice << "ms）\n";
//   cout << "========================================\n";

//   // 调度线程（后台运行）
//   thread scheduler_thread([this]()
//                           {
//     while (is_scheduling.load() && is_scheduler_running.load())
//     {
//       pid_t current_pid = -1;

//       // 从就绪队列取进程（加锁）
//       {
//         lock_guard<mutex> lock(queue_mtx);
//         if (!ready_queue.empty())
//         {
//           current_pid = ready_queue.front();
//           ready_queue.pop(); // 取出后直接移除，不再放回
//         }
//       }

//       // 无就绪进程则休眠并检查是否退出
//       if (current_pid == -1)
//       {
//         this_thread::sleep_for(chrono::milliseconds(100));
//         // 就绪队列为空时自动停止调度
//         {
//           lock_guard<mutex> lock(queue_mtx);
//           if (ready_queue.empty())
//           {
//             cout << "\n📋 就绪队列为空，调度器自动停止\n";
//             is_scheduling.store(false);
//             break;
//           }
//         }
//         continue;
//       }

//       // 执行一个时间片（执行后标记为已完成，不再放回）
//       execute_process_slice(current_pid);

//       // 移除：不再将进程放回就绪队列
//       cout << "\n🔄 调度切换：PID=" << current_pid << " 已完成，从就绪队列移除\n\n";
//     }

//     is_scheduling.store(false);
//     cout << "========================================\n";
//     cout << "🛑 调度器停止运行\n"; });

//   scheduler_thread.detach();
// }

// // ===================== 停止调度（保留就绪队列） =====================
// void ProcessManager::stop_scheduling()
// {
//   if (is_scheduling.load())
//   {
//     is_scheduling.store(false);
//     cout << "🛑 调度执行已停止，就绪队列进程保留\n";
//   }
//   else
//   {
//     cout << "⚠️  调度器未在运行\n";
//   }
// }

// // ===================== 停止所有进程 =====================
// void ProcessManager::stop_all_processes()
// {
//   // 停止调度
//   is_scheduling.store(false);
//   is_scheduler_running.store(false);

//   // 释放信号量
//   this->schedule_sem.post();

//   // 清空进程状态
//   {
//     lock_guard<mutex> lock(g_mtx);
//     for (auto &[pid, running] : this->process_running)
//       running.store(false);
//     this->process_running.clear();
//     // ========== 修复：清空process_completed ==========
//     this->process_completed.clear();
//     // ===================================
//     this->process_args.clear();
//   }

//   // 清空就绪队列
//   {
//     lock_guard<mutex> lock(queue_mtx);
//     while (!ready_queue.empty())
//       ready_queue.pop();
//   }

//   cout << "🔌 所有进程已停止，就绪队列已清空\n";
// }

// // ===================== 获取就绪队列 =====================
// vector<pid_t> ProcessManager::get_ready_queue()
// {
//   vector<pid_t> queue_copy;
//   lock_guard<mutex> lock(queue_mtx);
//   queue<pid_t> temp_queue = ready_queue;

//   while (!temp_queue.empty())
//   {
//     queue_copy.push_back(temp_queue.front());
//     temp_queue.pop();
//   }
//   return queue_copy;
// }

// // ===================== 辅助方法实现 =====================
// bool ProcessManager::is_scheduler_active() const
// {
//   return this->is_scheduler_running.load();
// }

// map<pid_t, atomic<bool>> &ProcessManager::get_process_running()
// {
//   return this->process_running;
// }

// void ProcessManager::set_time_slice(int ms)
// {
//   if (ms > 0)
//     this->time_slice = ms;
// }

// int ProcessManager::get_time_slice() const
// {
//   return this->time_slice;
// }

#include "process.hpp"
#include "ipc.hpp"
#include "../fs_core/file_interface.hpp"
#include "../../common/common.hpp"
#include "json.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <algorithm>
#include <sstream>

using json = nlohmann::json;
using namespace std;

// 全局互斥锁定义（与交互式终端共享）
mutex g_mtx;

// ========== ProcessManager 构造/析构 ==========
ProcessManager::ProcessManager(FileInterface &file_if, MessageQueue &msg_queue)
    : file_interface(file_if), msg_queue(msg_queue),
      scheduler_running(false), time_slice_ms(1) {}

ProcessManager::~ProcessManager()
{
  stop_all_processes();
}

// ========== 核心配置 ==========
void ProcessManager::set_time_slice(int ms)
{
  lock_guard<mutex> lock(g_mtx);
  if (ms > 0)
  {
    time_slice_ms = ms;
  }
}

// ========== 进程创建（仅入队，不立即执行） ==========
pid_t ProcessManager::create_process(CommandType cmd_type, const string &args_json)
{
  lock_guard<mutex> lock(g_mtx);

  // 生成唯一PID（简单自增策略，可根据需要优化）
  static pid_t next_pid = 1;
  pid_t new_pid = next_pid++;

  // 初始化进程状态
  process_running.emplace(new_pid, false);
  process_completed.emplace(new_pid, false);
  // process_completed[new_pid] = atomic<bool>(false);
  process_args[new_pid] = make_pair(cmd_type, args_json);

  // 加入就绪队列
  ready_queue.push(new_pid);

  // 打印提示（与交互式终端交互）
  cout << "✅ 进程创建成功，PID：" << new_pid
       << "（已加入就绪队列，等待调度）" << endl;

  return new_pid;
}

// ========== 启动调度器（时间片轮转核心逻辑） ==========
void ProcessManager::start_scheduler()
{
  lock_guard<mutex> lock(g_mtx);
  if (scheduler_running)
  {
    cout << "⚠️  调度器已在运行中！" << endl;
    return;
  }

  scheduler_running = true;
  cout << "🚀 时间片轮转调度器已初始化（时间片：" << time_slice_ms << "ms）" << endl;
  cout << "💡 提示：创建进程后执行 run_scheduler 开始调度执行" << endl;
}

void ProcessManager::run_scheduler()
{
  lock_guard<mutex> lock(g_mtx);
  if (!scheduler_running)
  {
    cout << "❌ 调度器未初始化！请先执行 start_scheduler" << endl;
    return;
  }
  if (ready_queue.empty())
  {
    cout << "⚠️  就绪队列为空，无进程可执行！" << endl;
    return;
  }

  cout << "▶️  启动时间片轮转调度执行..." << endl;

  // 调度线程（避免阻塞主线程）
  thread scheduler_thread([this]()
                          {
        while (true) {
            pid_t current_pid = -1;
            {
                lock_guard<mutex> lock(g_mtx);
                // 退出条件：调度器停止 或 就绪队列为空
                if (!scheduler_running || ready_queue.empty()) {
                    break;
                }
                // 取出队首进程
                current_pid = ready_queue.front();
                ready_queue.pop();
                // 标记为运行中
                process_running[current_pid].store(true);
            }

            if (current_pid != -1) {
                try {
                    // 执行进程（时间片内）
                    execute_process_slice(current_pid);
                } catch (const exception& e) {
                    // 捕获执行异常，发送错误消息
                    json result_json;
                    result_json["success"] = false;
                    result_json["msg"] = "进程执行异常：" + string(e.what());
                    send_process_result(current_pid, result_json.dump());
                }

                // 标记为已完成
                {
                    lock_guard<mutex> lock(g_mtx);
                    process_running[current_pid].store(false);
                    process_completed[current_pid].store(true);
                }
            }

            // 时间片间隔（模拟CPU调度延迟）
            this_thread::sleep_for(chrono::milliseconds(10));
        }

        cout << "⏹️  调度执行完成（就绪队列为空）" << endl; });

  // 分离线程（避免主线程阻塞）
  scheduler_thread.detach();
}

// ========== 停止调度 ==========
void ProcessManager::stop_scheduling()
{
  lock_guard<mutex> lock(g_mtx);
  scheduler_running = false;
  cout << "🛑 调度执行已停止（就绪队列保留）" << endl;
}

void ProcessManager::stop_all_processes()
{
  lock_guard<mutex> lock(g_mtx);
  scheduler_running = false;

  // 清空就绪队列
  while (!ready_queue.empty())
  {
    ready_queue.pop();
  }

  // 重置进程状态
  process_running.clear();
  process_completed.clear();
  process_args.clear();

  cout << "🔌 所有进程已终止，就绪队列已清空" << endl;
}

// ========== 进程状态查询 ==========
unordered_map<pid_t, atomic<bool>> &ProcessManager::get_process_running()
{
  return process_running;
}

unordered_map<pid_t, atomic<bool>> &ProcessManager::get_process_completed()
{
  return process_completed;
}

vector<pid_t> ProcessManager::get_ready_queue()
{
  lock_guard<mutex> lock(g_mtx);
  vector<pid_t> res;
  queue<pid_t> temp = ready_queue; // 复制队列避免修改原数据
  while (!temp.empty())
  {
    res.push_back(temp.front());
    temp.pop();
  }
  return res;
}

// ========== 核心：执行单个进程（时间片内） ==========
void ProcessManager::execute_process_slice(pid_t pid)
{
  cout << "\n📌 开始执行进程 PID：" << pid << "（时间片：" << time_slice_ms << "ms）" << endl;

  // 获取进程参数
  auto [cmd_type, args_json] = process_args[pid];
  json args;
  bool args_valid = true;
  try
  {
    if (!args_json.empty() && args_json != "{}")
    {
      args = json::parse(args_json);
    }
  }
  catch (const json::parse_error &e)
  {
    args_valid = false;
    json result_json;
    result_json["success"] = false;
    result_json["msg"] = "JSON参数解析失败：" + string(e.what());
    send_process_result(pid, result_json.dump());
    return;
  }

  if (!args_valid)
  {
    return;
  }

  // 执行具体命令（核心逻辑：覆盖所有目录/文件操作）
  bool success = false;
  string result_msg;
  try
  {
    switch (cmd_type)
    {
    // ========== 原有命令类型（兼容） ==========
    case CommandType::CREATE_FILE:
    {
      if (!args.contains("path") || !args.contains("content"))
      {
        throw runtime_error("CREATE_FILE缺少参数：path/content");
      }
      string path = args["path"].get<string>();
      string content = args["content"];
      FilePermission perm = static_cast<FilePermission>(File_READ | File_WRITE | File_DEL);
      if (file_interface.create_file(path, perm, content))
      {
        result_msg = "CREATE_FILE成功：" + path;
        success = true;
      }
      else
      {
        result_msg = "CREATE_FILE失败：路径不存在/已存在/磁盘满 - " + path;
        success = false;
      }
      break;
    }
    case CommandType::DELETE_FILE:
    {
      if (!args.contains("path"))
      {
        throw runtime_error("DELETE_FILE缺少参数：path");
      }
      string path = args["path"].get<string>();
      if (file_interface.delete_file(path, pid))
      {
        result_msg = "DELETE_FILE成功：" + path;
        success = true;
      }
      else
      {
        result_msg = "DELETE_FILE失败：文件不存在/被锁定 - " + path;
        success = false;
      }
      break;
    }
    case CommandType::MKDIR:
    { // 兼容原有MKDIR，映射到DIR_MKDIR
      if (!args.contains("path"))
      {
        throw runtime_error("MKDIR缺少参数：path");
      }
      string path = args["path"].get<string>();
      if (file_interface.create_directory(path))
      {
        result_msg = "MKDIR成功：" + path;
        success = true;
      }
      else
      {
        result_msg = "MKDIR失败：路径已存在/解析错误 - " + path;
        success = false;
      }
      break;
    }
    case CommandType::QUERY_DIR:
    { // 兼容原有QUERY_DIR，映射到DIR_LS
      string path = args.contains("path") ? args["path"].get<string>() : file_interface.get_current_work_dir();
      string content = file_interface.query_directory(path);
      if (!content.empty())
      {
        result_msg = "QUERY_DIR成功（" + path + "）：\n" + content;
        success = true;
      }
      else
      {
        result_msg = "QUERY_DIR失败：目录为空或路径不存在 - " + path;
        success = false;
      }
      break;
    }

    // ========== 新增：目录操作 ==========
    case CommandType::DIR_PWD:
    {
      string cwd = file_interface.get_current_work_dir();
      result_msg = "DIR_PWD成功：当前工作目录 = " + cwd;
      success = true;
      break;
    }
    case CommandType::DIR_LS:
    {
      string path = args.contains("path") ? args["path"].get<string>() : file_interface.get_current_work_dir();
      string content = file_interface.query_directory(path);
      if (!content.empty())
      {
        result_msg = "DIR_LS成功（" + path + "）：\n" + content;
        success = true;
      }
      else
      {
        result_msg = "DIR_LS失败：目录为空或路径不存在 - " + path;
        success = false;
      }
      break;
    }
    // case CommandType::DIR_TREE:
    // {
    //   string tree = Directory.list_all_dirs();
    //   result_msg = "DIR_TREE成功：\n" + tree;
    //   success = true;
    //   break;
    // }
    case CommandType::DIR_MKDIR:
    {
      if (!args.contains("path"))
      {
        throw runtime_error("DIR_MKDIR缺少参数：path");
      }
      string path = args["path"].get<string>();
      if (file_interface.create_directory(path))
      {
        result_msg = "DIR_MKDIR成功：" + path;
        success = true;
      }
      else
      {
        result_msg = "DIR_MKDIR失败：路径已存在/解析错误 - " + path;
        success = false;
      }
      break;
    }
    case CommandType::DIR_CD:
    {
      if (!args.contains("path"))
      {
        throw runtime_error("DIR_CD缺少参数：path");
      }
      string path = args["path"].get<string>();
      if (file_interface.change_directory(path))
      {
        result_msg = "DIR_CD成功：切换到 " + file_interface.get_current_work_dir();
        success = true;
      }
      else
      {
        result_msg = "DIR_CD失败：路径不存在 - " + path;
        success = false;
      }
      break;
    }
    case CommandType::DIR_RMDIR:
    {
      if (!args.contains("path"))
      {
        throw runtime_error("DIR_RMDIR缺少参数：path");
      }
      string path = args["path"].get<string>();
      bool force = args.contains("force") ? args["force"].get<bool>() : false;
      if (file_interface.delete_directory(path, force))
      {
        result_msg = "DIR_RMDIR成功：" + path;
        success = true;
      }
      else
      {
        using CmdUnderlyingType = std::underlying_type_t<CommandType>;
        result_msg = "未知命令类型：" + to_string(static_cast<CmdUnderlyingType>(cmd_type));
        success = false;
      }
      break;
    }

    // ========== 新增：文件操作 ==========
    case CommandType::FILE_TOUCH:
    {
      if (!args.contains("path"))
      {
        throw runtime_error("FILE_TOUCH缺少参数：path");
      }
      string path = args["path"].get<string>();
      FilePermission perm = static_cast<FilePermission>(File_READ | File_WRITE | File_DEL);
      if (file_interface.create_file(path, perm, ""))
      {
        result_msg = "FILE_TOUCH成功：" + path;
        success = true;
      }
      else
      {
        result_msg = "FILE_TOUCH失败：路径不存在/已存在/磁盘满 - " + path;
        success = false;
      }
      break;
    }
    case CommandType::FILE_WRITE:
    {
      if (!args.contains("path") || !args.contains("content"))
      {
        throw runtime_error("FILE_WRITE缺少参数：path/content");
      }
      string path = args["path"].get<string>();
      string content = args["content"];
      if (file_interface.write_file(path, content))
      {
        result_msg = "FILE_WRITE成功：" + path;
        success = true;
      }
      else
      {
        result_msg = "FILE_WRITE失败：" + path;
        success = false;
      }
      break;
    }
    case CommandType::FILE_READ:
    {
      if (!args.contains("path"))
      {
        throw runtime_error("FILE_READ缺少参数：path");
      }
      string path = args["path"].get<string>();
      string content = file_interface.read_file(path);
      if (!content.empty())
      {
        result_msg = "FILE_READ成功（" + path + "）：\n" + content;
        success = true;
      }
      else
      {
        result_msg = "FILE_READ失败：文件不存在/无读权限 - " + path;
        success = false;
      }
      break;
    }
    case CommandType::FILE_CAT:
    {
      if (!args.contains("path") || !args.contains("block"))
      {
        throw runtime_error("FILE_CAT缺少参数：path/block");
      }
      string path = args["path"].get<string>();
      int block = args["block"];
      string content = file_interface.view_file_block(path, block);
      if (!content.empty())
      {
        result_msg = "FILE_CAT成功（" + path + " - 块" + to_string(block) + "）：\n" + content;
        success = true;
      }
      else
      {
        result_msg = "FILE_CAT失败：文件不存在/块号无效 - " + path;
        success = false;
      }
      break;
    }
    case CommandType::FILE_EDIT:
    {
      if (!args.contains("path") || !args.contains("block") || !args.contains("content"))
      {
        throw runtime_error("FILE_EDIT缺少参数：path/block/content");
      }
      string path = args["path"].get<string>();
      int block = args["block"];
      string content = args["content"];
      if (file_interface.modify_file_block(path, block, content))
      {
        result_msg = "FILE_EDIT成功：" + path + "（块" + to_string(block) + "）";
        success = true;
      }
      else
      {
        result_msg = "FILE_EDIT失败：文件不存在/块号无效 - " + path;
        success = false;
      }
      break;
    }
    case CommandType::FILE_RM:
    {
      if (!args.contains("path"))
      {
        throw runtime_error("FILE_RM缺少参数：path");
      }
      string path = args["path"].get<string>();
      if (file_interface.delete_file(path, pid))
      {
        result_msg = "FILE_RM成功：" + path;
        success = true;
      }
      else
      {
        result_msg = "FILE_RM失败：文件不存在/被锁定 - " + path;
        success = false;
      }
      break;
    }

    // ========== 默认：未知命令 ==========
    default:
    {
      result_msg = "未知命令类型：" + to_string(static_cast<int>(cmd_type));
      success = false;
      break;
    }
    }
  }
  catch (const exception &e)
  {
    result_msg = "进程执行异常：" + string(e.what());
    success = false;
  }

  // 构造执行结果JSON
  json result_json;
  result_json["success"] = success;
  result_json["msg"] = result_msg;
  result_json["pid"] = pid;
  result_json["cmd_type"] = static_cast<int>(cmd_type);

  // 发送执行结果到消息队列
  send_process_result(pid, result_json.dump());

  // 模拟时间片消耗（核心：时间片轮转的延迟控制）
  this_thread::sleep_for(chrono::milliseconds(time_slice_ms));

  cout << "✅ 进程 PID：" << pid << " 执行完成" << endl;
}

// ========== 辅助：发送进程执行结果到IPC消息队列 ==========
void ProcessManager::send_process_result(pid_t pid, const string &result)
{
  Message msg;
  msg.sender_pid = pid;
  msg.receiver_pid = 0; // 0表示广播（所有进程可接收）
  msg.type = MessageType::RES_RESULT;
  msg.content = result;

  try
  {
    msg_queue.send_message(msg);
  }
  catch (const exception &e)
  {
    cerr << "❌ 进程 " << pid << " 结果发送失败：" << e.what() << endl;
  }
}
