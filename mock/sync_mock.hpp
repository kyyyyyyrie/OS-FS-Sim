// sync_mock.hpp
#pragma once
#include <windows.h>
#include <stdexcept>
#include <string>

class Semaphore
{
private:
  HANDLE sem_handle;
  LONG initial_count;
  LONG max_count;

public:
  Semaphore(int count = 0) : initial_count(count), max_count(1000)
  {
    sem_handle = (HANDLE)1; // 模拟有效句柄，无真实系统调用
  }

  ~Semaphore()
  {
    sem_handle = NULL;
  }

  void wait() {} // 模拟等待，无真实阻塞
  void post() {} // 模拟释放，无真实计数修改

  Semaphore(const Semaphore &) = delete;
  Semaphore &operator=(const Semaphore &) = delete;
};