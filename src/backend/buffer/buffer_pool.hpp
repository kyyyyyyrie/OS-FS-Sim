#pragma once
#include "../../common/common.hpp"
#include "E:\os\src\backend\storage\disk.hpp"
#include "E:\os\src\backend\concurrency\sync.hpp"
#include <vector>
#include <list>
#include <map>
#include <ctime>

/**
 * @brief 缓冲页类（内存缓冲的基本单元）
 * @note Windows适配：纯C++逻辑，无系统依赖
 */
class BufferPage
{
public:
  int block_num;           // 对应的磁盘盘块号
  std::string filename;    // 所属文件的完整路径
  pid_t owner_pid;         // 占用缓冲页的进程ID（Windows DWORD）
  time_t last_access_time; // 最后访问时间（用于LRU置换）
  BufferPageStatus status; // 缓冲页状态
  char data[BLOCK_SIZE];   // 盘块数据
  bool is_locked;          // 是否被锁定
  // 【新增】pin 计数：>0 表示该页正在被“映射/长期占用”，LRU 置换不能换出
  // 不要求其他同学立刻配合调用；即使没人用，默认 0 不影响旧逻辑
  int pin_count;
  /**
   * @brief 构造函数：初始化缓冲页
   */
  BufferPage();
};

/**
 * @brief 缓冲池类（管理多个缓冲页，实现LRU置换）
 * @note Windows适配：同步依赖Windows信号量（Semaphore类）
 */
class BufferPool
{
private:
  Disk &disk;                       // 关联的模拟磁盘
  Semaphore mutex;                  // 互斥信号量（Windows适配版）
  Semaphore empty_pages;            // 同步信号量（Windows适配版）
  std::vector<BufferPage> pages;    // 缓冲页数组
  std::list<int> lru_list;          // LRU链表
  std::map<int, int> block_to_page; // 盘块号→缓冲页索引映射

  // 私有方法：LRU置换算法
  int lru_replace();

  // 私有方法：更新LRU链表
  void update_lru(int page_idx);

public:
  /**
   * @brief 构造函数：初始化缓冲池
   * @param disk 模拟磁盘实例
   */
  BufferPool(Disk &disk);

  /**
   * @brief 获取指定盘块的缓冲页
   * @param block_num 磁盘盘块号
   * @param filename 所属文件路径
   * @param pid 申请缓冲页的进程ID
   * @return 缓冲页指针
   */
  BufferPage *get_buffer_page(int block_num, const std::string &filename, pid_t pid);

  /**
   * @brief 释放缓冲页
   * @param block_num 磁盘盘块号
   * @param is_modified 是否修改过
   * @return true=释放成功，false=失败
   */
  bool release_buffer_page(int block_num, bool is_modified);

  /**
   * @brief 批量写回所有脏页到磁盘
   * @return true=写回成功，false=部分失败
   */
  bool write_back_all_dirty_pages();

  /**
   * @brief 获取缓冲池所有页的状态
   * @return 缓冲页数组的常量引用
   */
  const std::vector<BufferPage> &get_pages() const;

  // ===========================
  // 【新增：冲高分接口】（可选使用）
  // ===========================

  /**
   * @brief pin 指定块：表示该缓冲页被映射/长期占用，不允许被置换
   * @note 不影响 get/release 的基本功能；只是在置换时保护该页
   */
  bool pin_block(int block_num, const std::string &filename, pid_t pid);

  /**
   * @brief unpin 指定块：解除长期占用
   */
  bool unpin_block(int block_num, const std::string &filename, pid_t pid);

  /**
   * @brief flush 指定文件的脏页：模拟 fsync/msync(file)
   */
  bool flush_file(const std::string &filename);

  /**
   * @brief flush 指定块：模拟 msync(page)
   */
  bool flush_block(int block_num, const std::string &filename);
};