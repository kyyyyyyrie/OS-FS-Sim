#pragma once
#include "../../common/common.hpp"
#include <queue>
#include <mutex>
#include <condition_variable>
using namespace std;

/**
 * @brief 消息队列类（Windows适配版，纯C++实现，替代Linux System V消息队列）
 * @note 用queue+互斥锁+条件变量实现，支持按PID过滤消息
 */
class MessageQueue
{
private:
  queue<Message> msg_queue; // 消息队列容器
  mutex mtx;                // 互斥锁（保护队列访问）
  condition_variable cv;    // 条件变量（同步生产/消费）

public:
  /**
   * @brief 构造函数：初始化消息队列
   */
  MessageQueue() = default;

  /**
   * @brief 发送消息到队列
   * @param msg 自定义Message结构体
   * @return true=发送成功，false=失败
   */
  bool send_message(const Message &msg)
  {
    lock_guard<mutex> lock(mtx);
    msg_queue.push(msg);
    // 广播消息（receiver_pid=0）唤醒所有接收者；普通消息仅唤醒一个
    if (msg.receiver_pid == 0)
    {
      cv.notify_all();
    }
    else
    {
      cv.notify_one();
    }
    return true;
  }

  /**
   * @brief 从队列接收消息（按接收者PID过滤，无匹配则等待新消息）
   * @param receiver_pid 接收者进程ID（0表示接收所有）
   * @param msg 输出参数：接收到的消息
   * @return true=接收成功（始终返回true，除非程序异常）
   */
  bool receive_message(pid_t receiver_pid, Message &msg)
  {
    unique_lock<mutex> lock(mtx);
    while (true)
    {
      cv.wait(lock, [this]()
              { return !msg_queue.empty(); });

      queue<Message> temp_queue;
      bool found = false;
      while (!msg_queue.empty())
      {
        Message current = msg_queue.front();
        msg_queue.pop();

        // 匹配条件：接收者PID一致，或消息是广播
        if (receiver_pid == 0 || current.receiver_pid == receiver_pid)
        {
          msg = current;
          found = true;
          // 广播消息（receiver_pid=0）保留在队列中，供其他接收者读取
          if (current.receiver_pid == 0)
          {
            temp_queue.push(current); // 放回广播消息
          }
        }
        else
        {
          temp_queue.push(current); // 放回非目标消息
        }
      }

      // 把非目标消息和广播消息放回原队列
      while (!temp_queue.empty())
      {
        msg_queue.push(temp_queue.front());
        temp_queue.pop();
      }

      if (found)
      {
        return true;
      }
    }
  }
  /**
   * @brief 析构函数：空实现（无系统资源需释放）
   */
  ~MessageQueue() = default;
};

// ipc.hpp 完整修复版
// #ifndef IPC_HPP_
// #define IPC_HPP_

// #include "../../common/common.hpp"
// #include <queue>
// #include <mutex>
// #include <condition_variable>
// using namespace std;

// /**
//  * @brief 消息队列类（Windows适配版，纯C++实现，替代Linux System V消息队列）
//  * @note 用queue+互斥锁+条件变量实现，支持按PID过滤消息
//  */
// class MessageQueue
// {
// private:
//   queue<Message> msg_queue; // 消息队列容器
//   mutex mtx;                // 互斥锁（保护队列访问）
//   condition_variable cv;    // 条件变量（同步生产/消费）

// public:
//   /**
//    * @brief 构造函数：初始化消息队列
//    */
//   MessageQueue() = default;

//   /**
//    * @brief 发送消息到队列
//    * @param msg 自定义Message结构体
//    * @return true=发送成功，false=失败
//    */
//   bool send_message(const Message &msg)
//   {
//     lock_guard<mutex> lock(mtx);
//     msg_queue.push(msg);
//     // 广播消息（receiver_pid=0）唤醒所有接收者；普通消息仅唤醒一个
//     if (msg.receiver_pid == 0)
//     {
//       cv.notify_all();
//     }
//     else
//     {
//       cv.notify_one();
//     }
//     return true;
//   }

//   /**
//    * @brief 从队列接收消息（按接收者PID过滤，无匹配则等待新消息）
//    * @param receiver_pid 接收者进程ID（0表示接收所有）
//    * @param msg 输出参数：接收到的消息
//    * @return true=接收成功（始终返回true，除非程序异常）
//    */
//   bool receive_message(pid_t receiver_pid, Message &msg)
//   {
//     unique_lock<mutex> lock(mtx);
//     while (true)
//     {
//       cv.wait(lock, [this]()
//               { return !msg_queue.empty(); });

//       queue<Message> temp_queue;
//       bool found = false;
//       while (!msg_queue.empty())
//       {
//         Message current = msg_queue.front();
//         msg_queue.pop();

//         // 匹配条件：接收者PID一致，或消息是广播
//         if (receiver_pid == 0 || current.receiver_pid == receiver_pid)
//         {
//           msg = current;
//           found = true;
//           // 广播消息（receiver_pid=0）保留在队列中，供其他接收者读取
//           if (current.receiver_pid == 0)
//           {
//             temp_queue.push(current); // 放回广播消息
//           }
//         }
//         else
//         {
//           temp_queue.push(current); // 放回非目标消息
//         }
//       }

//       // 把非目标消息和广播消息放回原队列
//       while (!temp_queue.empty())
//       {
//         msg_queue.push(temp_queue.front());
//         temp_queue.pop();
//       }

//       if (found)
//       {
//         return true;
//       }
//     }
//   }
//   /**
//    * @brief 析构函数：空实现（无系统资源需释放）
//    */
//   ~MessageQueue() = default;
// };

// #endif // IPC_HPP_

// process.hpp 最终版
// #pragma once
// #include "../../common/common.hpp"       // 引用你的common.hpp
// #include "../fs_core/file_interface.hpp" // 引用FileInterface
// // #include "ipc.hpp"                       // 引用MessageQueue
// #include <map>
// #include <queue>
// #include <thread>
// #include <atomic>
// #include <mutex>

// // 前置声明（避免循环包含）
// class MessageQueue; // 前置声明即可
// // class FileInterface;
// class FileInterface;

// class ProcessManager
// {
// private:
//   // 核心成员（与你的process.cpp实现匹配）
//   FileInterface &file_interface;                      // 文件接口引用
//   MessageQueue &msg_queue;                            // 消息队列引用
//   HANDLE schedule_sem;                                // Windows信号量（替代自定义Semaphore）
//   std::atomic<bool> is_scheduler_running;             // 调度器运行标志
//   int time_slice = 100;                               // 时间片（ms）
//   std::queue<pid_t> ready_queue;                      // 就绪队列
//   std::map<pid_t, std::thread> process_map;           // 进程ID -> 线程
//   std::map<pid_t, std::atomic<bool>> process_running; // 进程运行状态
//   std::atomic<pid_t> next_pid = 1;                    // 下一个PID

// public:
//   // 构造/析构
//   ProcessManager(FileInterface &fi, MessageQueue &mq);
//   ~ProcessManager();

//   // 核心方法
//   void schedule();
//   void run_command(CommandType type, const std::string &args, pid_t pid);
//   pid_t create_process(CommandType type, const std::string &args);
//   void start_scheduler();
//   void stop_all_processes();

//   // 测试工具访问方法
//   std::map<pid_t, std::atomic<bool>> &get_process_running()
//   {
//     return process_running;
//   }
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
// #include "../../common/common.hpp"
// #include "../fs_core/file_interface.hpp"
// #include "../concurrency/sync.hpp" // ✅ 包含你的 Semaphore 类定义！
// #include <map>
// #include <queue>
// #include <thread>
// #include <atomic>
// #include <mutex>

// // 前置声明（避免循环包含）
// class MessageQueue;
// class FileInterface;

// class ProcessManager
// {
// private:
//   FileInterface &file_interface;
//   MessageQueue &msg_queue;
//   Semaphore schedule_sem;                        // ✅ 改为自定义 Semaphore（不再是 HANDLE！）
//   std::atomic<bool> is_scheduler_running{false}; // 初始化为 false
//   int time_slice = 100;
//   std::queue<pid_t> ready_queue;
//   std::map<pid_t, std::thread> process_map;
//   std::map<pid_t, std::atomic<bool>> process_running;
//   std::atomic<pid_t> next_pid{1};

// public:
//   ProcessManager(FileInterface &fi, MessageQueue &mq);
//   ~ProcessManager();

//   void schedule();
//   void run_command(CommandType type, const std::string &args, pid_t pid);
//   pid_t create_process(CommandType type, const std::string &args);
//   void start_scheduler();
//   void stop_all_processes();

//   // 测试工具访问方法
//   std::map<pid_t, std::atomic<bool>> &get_process_running()
//   {
//     return process_running;
//   }
//   void set_time_slice(int ms)
//   {
//     time_slice = ms;
//   }
//   int get_time_slice() const
//   {
//     return time_slice;
//   }
// };