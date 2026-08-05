// #pragma once
// #include <windows.h> // Windows信号量API
// #include <stdexcept>
// #include <string>
// using namespace std;

// /**
//  * @brief 信号量类（Windows适配版，封装CreateSemaphore/WaitForSingleObject）
//  * @note 替代Linux sem_t，接口保持wait()/post()不变
//  */
// class Semaphore
// {
// private:
//   HANDLE sem_handle;  // Windows信号量句柄（替代Linux sem_t）
//   LONG initial_count; // 初始计数
//   LONG max_count;     // 最大计数

// public:
//   /**
//    * @brief 构造函数：初始化Windows信号量
//    * @param count 信号量初始值（0=同步，1=互斥）
//    * @throw runtime_error 初始化失败时抛出异常
//    */
//   Semaphore(int count = 0) : initial_count(count), max_count(1000)
//   {
//     sem_handle = CreateSemaphore(
//         NULL,          // 安全属性（默认）
//         initial_count, // 初始计数
//         max_count,     // 最大计数
//         NULL           // 匿名信号量（无需命名）
//     );
//     if (sem_handle == NULL)
//     {
//       throw runtime_error("信号量创建失败：" + to_string(GetLastError()));
//     }
//   }

//   /**
//    * @brief 析构函数：销毁信号量
//    */
//   ~Semaphore()
//   {
//     if (sem_handle != NULL)
//     {
//       CloseHandle(sem_handle);
//     }
//   }

//   /**
//    * @brief P操作（等待/申请资源）
//    * @note Windows WaitForSingleObject替代Linux sem_wait
//    */
//   void wait()
//   {
//     WaitForSingleObject(sem_handle, INFINITE); // 无限等待
//   }

//   /**
//    * @brief V操作（释放/发布资源）
//    * @note Windows ReleaseSemaphore替代Linux sem_post
//    */
//   void post()
//   {
//     ReleaseSemaphore(sem_handle, 1, NULL); // 计数+1
//   }

//   /**
//    * @brief 禁止拷贝构造和赋值
//    */
//   Semaphore(const Semaphore &) = delete;
//   Semaphore &operator=(const Semaphore &) = delete;
// };

#pragma once
#include <windows.h> // Windows信号量API
#include <stdexcept>
#include <string>
using namespace std;

/**
 * @brief 信号量类（Windows适配版，封装CreateSemaphore/WaitForSingleObject）
 * @note 替代Linux sem_t，接口保持wait()/post()不变，新增try_wait()非阻塞方法
 */
class Semaphore
{
private:
  HANDLE sem_handle;  // Windows信号量句柄（替代Linux sem_t）
  LONG initial_count; // 初始计数
  LONG max_count;     // 最大计数

public:
  /**
   * @brief 构造函数：初始化Windows信号量
   * @param count 信号量初始值（0=同步，1=互斥）
   * @throw runtime_error 初始化失败时抛出异常
   */
  Semaphore(int count = 0) : initial_count(count), max_count(1000)
  {
    sem_handle = CreateSemaphore(
        NULL,          // 安全属性（默认）
        initial_count, // 初始计数
        max_count,     // 最大计数
        NULL           // 匿名信号量（无需命名）
    );
    if (sem_handle == NULL)
    {
      throw runtime_error("信号量创建失败：" + to_string(GetLastError()));
    }
  }

  /**
   * @brief 析构函数：销毁信号量
   */
  ~Semaphore()
  {
    if (sem_handle != NULL)
    {
      CloseHandle(sem_handle);
    }
  }

  /**
   * @brief P操作（阻塞式等待/申请资源）
   * @note Windows WaitForSingleObject替代Linux sem_wait
   */
  void wait()
  {
    WaitForSingleObject(sem_handle, INFINITE); // 无限等待
  }

  /**
   * @brief 非阻塞式P操作（新增）
   * @return bool：true=获取信号量成功，false=信号量不足（非阻塞失败）
   * @note 超时设为0，立即返回结果，不阻塞
   */
  bool try_wait()
  {
    DWORD result = WaitForSingleObject(sem_handle, 0); // 0毫秒超时=非阻塞
    if (result == WAIT_OBJECT_0)
    {
      return true; // 成功获取信号量
    }
    else
    {
      return false; // 信号量不足/超时，获取失败
    }
  }

  /**
   * @brief V操作（释放/发布资源）
   * @note Windows ReleaseSemaphore替代Linux sem_post
   */
  void post()
  {
    ReleaseSemaphore(sem_handle, 1, NULL); // 计数+1
  }

  /**
   * @brief 禁止拷贝构造和赋值
   */
  Semaphore(const Semaphore &) = delete;
  Semaphore &operator=(const Semaphore &) = delete;
};