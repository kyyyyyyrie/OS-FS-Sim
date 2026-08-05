#pragma once
#include "../src/common/common.hpp"
#include "../src/backend/storage/disk.hpp"
#include "../src/backend/concurrency/sync.hpp"
#include <vector>
#include <list>
#include <map>
#include <ctime>
#include <algorithm> // 用于std::find

class BufferPage
{
public:
  int block_num = -1;
  std::string filename = "";
  pid_t owner_pid = 0;
  time_t last_access_time = std::time(nullptr);
  BufferPageStatus status = BufferPageStatus::CLEAN;
  char data[BLOCK_SIZE] = {0};
  bool is_locked = false;

  BufferPage() = default;
  ~BufferPage() = default;
};

class BufferPool
{
private:
  Disk &disk;
  Semaphore mutex;
  Semaphore empty_pages;
  std::vector<BufferPage> pages;
  std::list<int> lru_list;          // 记录页的LRU顺序（前：最近使用，后：最久未用）
  std::map<int, int> block_to_page; // 块号 → 缓冲页索引的映射
  bool is_initialized = true;

  // 置换策略：选择最久未使用的页（LRU）
  int lru_replace()
  {
    return lru_list.empty() ? -1 : lru_list.back(); // 取最后一个（最久未用）
  }

  // 更新LRU顺序：将访问的页移到最前面
  void update_lru(int page_idx)
  {
    auto it = std::find(lru_list.begin(), lru_list.end(), page_idx);
    if (it != lru_list.end())
      lru_list.erase(it);
    lru_list.push_front(page_idx); // 移到队首（标记为最近使用）
  }

public:
  BufferPool(Disk &d) : disk(d), mutex(1), empty_pages(10)
  {
    pages.resize(10); // 初始化10个缓冲页
    for (int i = 0; i < 10; ++i)
    {
      lru_list.push_back(i); // 初始LRU顺序：0→1→...→9
    }
  }

  ~BufferPool() = default;

  // 获取指定块号的缓冲页（核心修复：块号-页映射+LRU更新+脏页处理）
  BufferPage *get_buffer_page(int block_num, const std::string &filename, pid_t pid)
  {
    if (!is_initialized || !disk.is_block_valid(block_num))
    {
      return nullptr;
    }

    mutex.wait(); // 加锁（Mock中保留原同步逻辑）

    BufferPage *target_page = nullptr;
    int page_idx = -1;

    // 1. 检查该块号是否已在缓冲池中
    auto map_it = block_to_page.find(block_num);
    if (map_it != block_to_page.end())
    {
      page_idx = map_it->second;
      target_page = &pages[page_idx];
    }
    // 2. 缓冲池中无该块，需置换页
    else
    {
      page_idx = lru_replace();
      if (page_idx == -1)
      {
        mutex.post();
        return nullptr;
      }

      // 处理被置换页的旧数据
      BufferPage &old_page = pages[page_idx];
      if (old_page.block_num != -1)
      {
        // 移除旧块号的映射
        block_to_page.erase(old_page.block_num);
        // 若旧页是脏的，写回磁盘（Mock中执行实际写盘）
        if (old_page.status == BufferPageStatus::DIRTY)
        {
          disk.write_block(old_page.block_num, old_page.data);
        }
      }

      // 重置页信息，关联新块号
      old_page.block_num = block_num;
      old_page.filename = filename;
      old_page.owner_pid = pid;
      old_page.status = BufferPageStatus::CLEAN;
      old_page.is_locked = true;
      old_page.last_access_time = std::time(nullptr);
      memset(old_page.data, 0, BLOCK_SIZE); // 清空旧数据

      // 建立新块号与页的映射
      block_to_page[block_num] = page_idx;
      target_page = &old_page;
    }

    // 3. 更新LRU顺序（标记当前页为最近使用）
    target_page->last_access_time = std::time(nullptr);
    target_page->is_locked = true;
    update_lru(page_idx);

    mutex.post(); // 解锁
    return target_page;
  }

  // 释放缓冲页（核心修复：状态更新）
  bool release_buffer_page(int block_num, bool is_modified)
  {
    if (!is_initialized || !disk.is_block_valid(block_num))
    {
      return false;
    }

    mutex.wait();

    auto map_it = block_to_page.find(block_num);
    if (map_it == block_to_page.end())
    {
      mutex.post();
      return false;
    }

    BufferPage &page = pages[map_it->second];
    page.is_locked = false;
    // 标记为脏页（后续需写回）
    if (is_modified)
    {
      page.status = BufferPageStatus::DIRTY;
    }

    mutex.post();
    return true;
  }

  // 写回所有脏页（保持原逻辑）
  bool write_back_all_dirty_pages()
  {
    mutex.wait();
    for (auto &page : pages)
    {
      if (page.status == BufferPageStatus::DIRTY && page.block_num != -1)
      {
        disk.write_block(page.block_num, page.data);
        page.status = BufferPageStatus::CLEAN;
      }
    }
    mutex.post();
    return true;
  }

  const std::vector<BufferPage> &get_pages() const
  {
    return pages;
  }
};