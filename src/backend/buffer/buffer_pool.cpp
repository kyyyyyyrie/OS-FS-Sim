// #include "buffer_pool.hpp"
// #include <algorithm>
// #include <cstring>

// // BufferPage 实现
// BufferPage::BufferPage()
// {
//   this->block_num = -1;
//   this->filename = "";
//   this->owner_pid = 0;
//   this->last_access_time = 0;
//   this->status = BufferPageStatus::CLEAN;
//   this->is_locked = false;
//   this->pin_count = 0;

//   // 初始化数据区
//   std::memset(this->data, 0, BLOCK_SIZE);
// }

// // BufferPool 实现
// BufferPool::BufferPool(Disk &disk)
//     : disk(disk), mutex(1), empty_pages(BUFFER_PAGE_NUM)
// {
//   // 1) 初始化缓冲页数组
//   pages.resize(BUFFER_PAGE_NUM);

//   // 2) 初始化 LRU 链表：所有页初始为空闲，按顺序加入
//   lru_list.clear();
//   for (int i = 0; i < BUFFER_PAGE_NUM; i++)
//   {
//     lru_list.push_back(i);
//   }

//   // 3) 初始化映射表
//   block_to_page.clear();
// }

// int BufferPool::lru_replace()
// {
//   // 从 LRU 链表尾部开始找（最久未使用）
//   std::list<int>::iterator it;

//   // it 指向 end()，先 -- 才能拿到最后一个元素
//   it = this->lru_list.end();

//   while (it != this->lru_list.begin())
//   {
//     --it;

//     int page_idx = *it;
//     BufferPage &page = this->pages[page_idx];

//     // 跳过：被锁定或被 pin 的页不允许置换
//     if (page.is_locked == true)
//     {
//       continue;
//     }
//     if (page.pin_count > 0)
//     {
//       continue;
//     }

//     // 如果是脏页，先写回磁盘
//     if (page.status == BufferPageStatus::DIRTY)
//     {
//       bool write_ok = this->disk.write_block(page.block_num, page.data);
//       if (write_ok == false)
//       {
//         // 写回失败：尝试更旧的页
//         continue;
//       }
//     }

//     // 从映射表删除旧映射（仅当映射确实指向这个 page_idx）
//     if (page.block_num >= 0)
//     {
//       std::map<int, int>::iterator map_it = this->block_to_page.find(page.block_num);
//       if (map_it != this->block_to_page.end())
//       {
//         if (map_it->second == page_idx)
//         {
//           this->block_to_page.erase(map_it);
//         }
//       }
//     }

//     // 将该页从 LRU 链表中移除（可选，但更干净）
//     // 注意：这里 it 仍然有效，erase 后 it 失效，所以必须在 return 前结束
//     this->lru_list.erase(it);

//     // 重置页面状态，变成真正的 FREE
//     page.block_num = -1;
//     page.filename = "";
//     page.owner_pid = 0;
//     page.last_access_time = 0;
//     page.status = BufferPageStatus::CLEAN;
//     page.is_locked = false;
//     page.pin_count = 0;
//     std::memset(page.data, 0, BLOCK_SIZE);

//     return page_idx;
//   }

//   // 如果 while 退出，说明所有页都被锁定或 pin，无法置换
//   return -1;
// }

// void BufferPool::update_lru(int page_idx)
// {
//   // 1) 先在 LRU 链表中找到该页索引，并删除它的旧位置（只删一次）
//   std::list<int>::iterator it;
//   for (it = this->lru_list.begin(); it != this->lru_list.end(); ++it)
//   {
//     if (*it == page_idx)
//     {
//       this->lru_list.erase(it);
//       break;
//     }
//   }

//   // 2) 插入到链表头部，表示最近使用
//   this->lru_list.push_front(page_idx);

//   // 3) 更新访问时间戳
//   this->pages[page_idx].last_access_time = std::time(NULL);
// }

// BufferPage *BufferPool::get_buffer_page(int block_num, const std::string &filename, pid_t pid)
// {
//   // （可选）如果你们要严格限制并发占用页数，就启用这一行
//   // this->empty_pages.wait();

//   while (true)
//   {
//     this->mutex.wait();

//     // 1) 先查映射（命中）
//     std::map<int, int>::iterator map_it = this->block_to_page.find(block_num);
//     if (map_it != this->block_to_page.end())
//     {
//       int page_idx = map_it->second;
//       BufferPage &page = this->pages[page_idx];

//       // 若被占用（锁定），则释放 mutex，让出时间片重试
//       if (page.is_locked == true)
//       {
//         this->mutex.post();
//         Sleep(0);
//         continue;
//       }

//       // 占用该页并更新访问信息
//       page.is_locked = true;
//       page.owner_pid = pid;

//       this->update_lru(page_idx);

//       this->mutex.post();
//       return &page;
//     }

//     // 2) 未命中：选择 victim（此时仍在临界区内，避免竞态）
//     int page_idx = this->lru_replace();
//     if (page_idx == -1)
//     {
//       // 无可置换页：释放锁后重试或返回 NULL
//       this->mutex.post();
//       Sleep(0);
//       continue;
//     }

//     BufferPage &page = this->pages[page_idx];

//     // 3) 读盘装入（如果读失败，直接认为失败更稳）
//     bool read_ok = this->disk.read_block(block_num, page.data);
//     if (read_ok == false)
//     {
//       // 读失败：回滚为 FREE，并释放 mutex
//       page.block_num = -1;
//       page.filename = "";
//       page.owner_pid = 0;
//       page.status = BufferPageStatus::CLEAN;
//       page.is_locked = false;
//       page.pin_count = 0;
//       std::memset(page.data, 0, BLOCK_SIZE);

//       this->mutex.post();

//       // （可选）如果你用了 empty_pages.wait()，这里要 post 回去
//       // this->empty_pages.post();

//       return NULL;
//     }

//     // 4) 填充元信息并建表（返回前必须锁定）
//     page.block_num = block_num;
//     page.filename = filename;
//     page.owner_pid = pid;
//     page.status = BufferPageStatus::CLEAN;

//     page.is_locked = true;
//     page.pin_count = 0;

//     this->block_to_page[block_num] = page_idx;

//     this->update_lru(page_idx);

//     this->mutex.post();
//     return &page;
//   }
// }

// bool BufferPool::release_buffer_page(int block_num, bool is_modified)
// {
//   this->mutex.wait();

//   // 1) 查找映射
//   std::map<int, int>::iterator map_it = this->block_to_page.find(block_num);
//   if (map_it == this->block_to_page.end())
//   {
//     this->mutex.post();
//     return false;
//   }

//   int page_idx = map_it->second;
//   BufferPage &page = this->pages[page_idx];

//   // 2) 如果本次发生写入，标记为 DIRTY
//   if (is_modified == true)
//   {
//     page.status = BufferPageStatus::DIRTY;
//   }
//   else
//   {
//     // 若不是修改，不把 DIRTY 改回 CLEAN（保持原状态）
//     // 为了可读性可以显式写出来：
//     if (page.status != BufferPageStatus::DIRTY)
//     {
//       page.status = BufferPageStatus::CLEAN;
//     }
//   }

//   // 3) 解锁页面（表示上层不再占用）
//   page.is_locked = false;

//   // 4) 更新 LRU（表示刚刚使用过）
//   this->update_lru(page_idx);

//   this->mutex.post();

//   // 5) 如果你在 get_buffer_page() 里使用了 empty_pages.wait()
//   //    那这里必须对应归还一个名额
//   // this->empty_pages.post();

//   return true;
// }

// bool BufferPool::write_back_all_dirty_pages()
// {
//   bool all_success;
//   all_success = true;

//   this->mutex.wait();

//   int i;
//   for (i = 0; i < (int)this->pages.size(); i++)
//   {
//     BufferPage &page = this->pages[i];

//     // 只写回有效块号的脏页
//     if (page.status == BufferPageStatus::DIRTY)
//     {
//       if (page.block_num < 0)
//       {
//         // 异常状态：DIRTY 但没有有效块号
//         all_success = false;
//         continue;
//       }

//       // （可选更严谨）如果你不希望写回正在使用的页，可加：
//       // if (page.is_locked == true) { continue; }

//       bool write_ok;
//       write_ok = this->disk.write_block(page.block_num, page.data);

//       if (write_ok == true)
//       {
//         page.status = BufferPageStatus::CLEAN;
//       }
//       else
//       {
//         all_success = false;
//       }
//     }
//   }

//   this->mutex.post(); // 按你们 Semaphore 的实际释放函数名改
//   return all_success;
// }

// const std::vector<BufferPage> &BufferPool::get_pages() const
// {
//   return pages;
// }

// // ==================== 冲高分接口实现 ====================

// bool BufferPool::pin_block(int block_num, const std::string &filename, pid_t pid)
// {
//   // 1) 先在缓冲池中查找（命中直接 pin）
//   this->mutex.wait();

//   std::map<int, int>::iterator map_it = this->block_to_page.find(block_num);
//   if (map_it != this->block_to_page.end())
//   {
//     int page_idx = map_it->second;
//     BufferPage &page = this->pages[page_idx];

//     page.pin_count = page.pin_count + 1;
//     this->update_lru(page_idx);

//     this->mutex.post();
//     return true;
//   }

//   // 2) 未命中：先退出临界区，再调用 get_buffer_page 装入
//   this->mutex.post();

//   BufferPage *loaded_page = this->get_buffer_page(block_num, filename, pid);
//   if (loaded_page == NULL)
//   {
//     return false;
//   }

//   // 3) get_buffer_page 通常会把页设为 is_locked=true
//   //    我们只是为了装入并 pin，所以 pin 后应立即 release（不修改）
//   this->mutex.wait();

//   std::map<int, int>::iterator map_it2 = this->block_to_page.find(block_num);
//   if (map_it2 == this->block_to_page.end())
//   {
//     this->mutex.post();
//     // 回滚：把 get 占用释放掉（如果你们 get 会锁页）
//     this->release_buffer_page(block_num, false);
//     return false;
//   }

//   int page_idx2 = map_it2->second;
//   BufferPage &page2 = this->pages[page_idx2];

//   page2.pin_count = page2.pin_count + 1;
//   this->update_lru(page_idx2);

//   this->mutex.post();

//   // 释放 get_buffer_page 的短期占用（不改脏标记）
//   this->release_buffer_page(block_num, false);

//   return true;
// }

// bool BufferPool::unpin_block(int block_num, const std::string &filename, pid_t pid)
// {
//   (void)filename;
//   (void)pid;

//   this->mutex.wait();

//   std::map<int, int>::iterator map_it = this->block_to_page.find(block_num);
//   if (map_it == this->block_to_page.end())
//   {
//     this->mutex.post();
//     return false;
//   }

//   int page_idx = map_it->second;
//   BufferPage &page = this->pages[page_idx];

//   if (page.pin_count > 0)
//   {
//     page.pin_count = page.pin_count - 1;
//   }

//   // （可选）为了让刚解除 pin 的页不立刻被淘汰，可以更新 LRU
//   this->update_lru(page_idx);

//   this->mutex.post();
//   return true;
// }

// bool BufferPool::flush_file(const std::string &filename)
// {
//   bool all_success;
//   all_success = true;

//   this->mutex.wait();

//   int i;
//   for (i = 0; i < (int)this->pages.size(); i++)
//   {
//     BufferPage &page = this->pages[i];

//     // 只处理属于指定文件的脏页
//     if (page.filename == filename && page.status == BufferPageStatus::DIRTY)
//     {
//       // 块号必须有效
//       if (page.block_num < 0)
//       {
//         all_success = false;
//         continue;
//       }

//       // （可选更严谨）如果你不希望写回正在使用的页，可加：
//       // if (page.is_locked == true) { continue; }

//       bool write_ok;
//       write_ok = this->disk.write_block(page.block_num, page.data);

//       if (write_ok == true)
//       {
//         page.status = BufferPageStatus::CLEAN;
//       }
//       else
//       {
//         all_success = false;
//       }
//     }
//   }

//   this->mutex.post();
//   return all_success;
// }

// bool BufferPool::flush_block(int block_num, const std::string &filename)
// {
//   this->mutex.wait();

//   std::map<int, int>::iterator map_it = this->block_to_page.find(block_num);
//   if (map_it == this->block_to_page.end())
//   {
//     // 不在缓冲池中：视为无需刷新
//     this->mutex.post();
//     return true;
//   }

//   int page_idx = map_it->second;
//   BufferPage &page = this->pages[page_idx];

//   // 可选：文件名一致性检查（用于调试/保护）
//   if (filename.empty() == false)
//   {
//     if (page.filename != filename)
//     {
//       this->mutex.post();
//       return false;
//     }
//   }

//   // 块号必须有效
//   if (page.block_num < 0)
//   {
//     this->mutex.post();
//     return false;
//   }

//   // 脏页则写回
//   if (page.status == BufferPageStatus::DIRTY)
//   {
//     bool write_ok = this->disk.write_block(page.block_num, page.data);
//     if (write_ok == true)
//     {
//       page.status = BufferPageStatus::CLEAN;
//     }
//     else
//     {
//       this->mutex.post();
//       return false;
//     }
//   }

//   // 可选：更新 LRU（表示刚处理过）
//   // this->update_lru(page_idx);

//   this->mutex.post();
//   return true;
// }

#include "buffer_pool.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
// ========== 新增：延时所需头文件 ==========
#include <thread>                  // std::this_thread::sleep_for
#include <chrono>                  // 时间单位（milliseconds）
#include "../../common/common.hpp" // 引入延时常量BUFFER_SWAP_DELAY_MS
// =========================================

// BufferPage 实现
BufferPage::BufferPage()
{
  this->block_num = -1;
  this->filename = "";
  this->owner_pid = 0;
  this->last_access_time = 0;
  this->status = BufferPageStatus::CLEAN;
  this->is_locked = false;
  this->pin_count = 0;

  // 初始化数据区
  std::memset(this->data, 0, BLOCK_SIZE);
}

// BufferPool 实现
BufferPool::BufferPool(Disk &disk)
    : disk(disk), mutex(1), empty_pages(BUFFER_PAGE_NUM)
{
  // 1) 初始化缓冲页数组
  pages.resize(BUFFER_PAGE_NUM);

  // 2) 初始化 LRU 链表：所有页初始为空闲，按顺序加入
  lru_list.clear();
  for (int i = 0; i < BUFFER_PAGE_NUM; i++)
  {
    lru_list.push_back(i);
  }

  // 3) 初始化映射表
  block_to_page.clear();
}

int BufferPool::lru_replace()
{
  // 从 LRU 链表尾部开始找（最久未使用）
  std::list<int>::iterator it;

  // it 指向 end()，先 -- 才能拿到最后一个元素
  it = this->lru_list.end();

  while (it != this->lru_list.begin())
  {
    --it;

    int page_idx = *it;
    BufferPage &page = this->pages[page_idx];

    // 跳过：被锁定或被 pin 的页不允许置换
    if (page.is_locked == true)
    {
      continue;
    }
    if (page.pin_count > 0)
    {
      continue;
    }

    // 如果是脏页，先写回磁盘
    if (page.status == BufferPageStatus::DIRTY)
    {
      bool write_ok = this->disk.write_block(page.block_num, page.data);
      if (write_ok == false)
      {
        // 写回失败：尝试更旧的页
        continue;
      }

      // ========== 新增：脏页写回后添加换页延时 ==========
      std::cout << "[延时] 缓冲页" << page_idx << "（块" << page.block_num << "）为脏页，写回磁盘完成，等待" << BUFFER_SWAP_DELAY_MS << "ms...\n";
      std::this_thread::sleep_for(std::chrono::milliseconds(BUFFER_SWAP_DELAY_MS));
      // =================================================
    }

    // 从映射表删除旧映射（仅当映射确实指向这个 page_idx）
    if (page.block_num >= 0)
    {
      std::map<int, int>::iterator map_it = this->block_to_page.find(page.block_num);
      if (map_it != this->block_to_page.end())
      {
        if (map_it->second == page_idx)
        {
          this->block_to_page.erase(map_it);
        }
      }
    }

    // 将该页从 LRU 链表中移除（可选，但更干净）
    // 注意：这里 it 仍然有效，erase 后 it 失效，所以必须在 return 前结束
    this->lru_list.erase(it);

    // ========== 新增：重置页面前添加换页延时 ==========
    std::cout << "[延时] 开始置换缓冲页" << page_idx << "（原块" << page.block_num << "），等待" << BUFFER_SWAP_DELAY_MS << "ms...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(BUFFER_SWAP_DELAY_MS));
    // =================================================

    // 重置页面状态，变成真正的 FREE
    page.block_num = -1;
    page.filename = "";
    page.owner_pid = 0;
    page.last_access_time = 0;
    page.status = BufferPageStatus::CLEAN;
    page.is_locked = false;
    page.pin_count = 0;
    std::memset(page.data, 0, BLOCK_SIZE);

    return page_idx;
  }

  // 如果 while 退出，说明所有页都被锁定或 pin，无法置换
  return -1;
}

void BufferPool::update_lru(int page_idx)
{
  // 1) 先在 LRU 链表中找到该页索引，并删除它的旧位置（只删一次）
  std::list<int>::iterator it;
  for (it = this->lru_list.begin(); it != this->lru_list.end(); ++it)
  {
    if (*it == page_idx)
    {
      this->lru_list.erase(it);
      break;
    }
  }

  // 2) 插入到链表头部，表示最近使用
  this->lru_list.push_front(page_idx);

  // 3) 更新访问时间戳
  this->pages[page_idx].last_access_time = std::time(NULL);
}

BufferPage *BufferPool::get_buffer_page(int block_num, const std::string &filename, pid_t pid)
{
  // （可选）如果你们要严格限制并发占用页数，就启用这一行
  // this->empty_pages.wait();

  while (true)
  {
    this->mutex.wait();

    // 1) 先查映射（命中）
    std::map<int, int>::iterator map_it = this->block_to_page.find(block_num);
    if (map_it != this->block_to_page.end())
    {
      int page_idx = map_it->second;
      BufferPage &page = this->pages[page_idx];

      // 若被占用（锁定），则释放 mutex，让出时间片重试
      if (page.is_locked == true)
      {
        this->mutex.post();
        Sleep(0);
        continue;
      }

      // 占用该页并更新访问信息
      page.is_locked = true;
      page.owner_pid = pid;

      this->update_lru(page_idx);

      this->mutex.post();
      return &page;
    }

    // 2) 未命中：选择 victim（此时仍在临界区内，避免竞态）
    int page_idx = this->lru_replace();
    if (page_idx == -1)
    {
      // 无可置换页：释放锁后重试或返回 NULL
      this->mutex.post();
      Sleep(0);
      continue;
    }

    BufferPage &page = this->pages[page_idx];

    // ========== 新增：读盘装入前添加换页延时 ==========
    std::cout << "[延时] 缓冲页" << page_idx << "置换完成，开始读入块" << block_num << "到内存，等待" << BUFFER_SWAP_DELAY_MS << "ms...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(BUFFER_SWAP_DELAY_MS));
    // =================================================

    // 3) 读盘装入（如果读失败，直接认为失败更稳）
    bool read_ok = this->disk.read_block(block_num, page.data);
    if (read_ok == false)
    {
      // 读失败：回滚为 FREE，并释放 mutex
      page.block_num = -1;
      page.filename = "";
      page.owner_pid = 0;
      page.status = BufferPageStatus::CLEAN;
      page.is_locked = false;
      page.pin_count = 0;
      std::memset(page.data, 0, BLOCK_SIZE);

      this->mutex.post();

      // （可选）如果你用了 empty_pages.wait()，这里要 post 回去
      // this->empty_pages.post();

      return NULL;
    }

    // ========== 新增：读盘装入后添加换页延时 ==========
    std::cout << "[延时] 块" << block_num << "已读入缓冲页" << page_idx << "，等待" << BUFFER_SWAP_DELAY_MS << "ms...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(BUFFER_SWAP_DELAY_MS));
    // =================================================

    // 4) 填充元信息并建表（返回前必须锁定）
    page.block_num = block_num;
    page.filename = filename;
    page.owner_pid = pid;
    page.status = BufferPageStatus::CLEAN;

    page.is_locked = true;
    page.pin_count = 0;

    this->block_to_page[block_num] = page_idx;

    this->update_lru(page_idx);

    this->mutex.post();
    return &page;
  }
}

bool BufferPool::release_buffer_page(int block_num, bool is_modified)
{
  this->mutex.wait();

  // 1) 查找映射
  std::map<int, int>::iterator map_it = this->block_to_page.find(block_num);
  if (map_it == this->block_to_page.end())
  {
    this->mutex.post();
    return false;
  }

  int page_idx = map_it->second;
  BufferPage &page = this->pages[page_idx];

  // 2) 如果本次发生写入，标记为 DIRTY
  if (is_modified == true)
  {
    page.status = BufferPageStatus::DIRTY;
  }
  else
  {
    // 若不是修改，不把 DIRTY 改回 CLEAN（保持原状态）
    // 为了可读性可以显式写出来：
    if (page.status != BufferPageStatus::DIRTY)
    {
      page.status = BufferPageStatus::CLEAN;
    }
  }

  // 3) 解锁页面（表示上层不再占用）
  page.is_locked = false;

  // 4) 更新 LRU（表示刚刚使用过）
  this->update_lru(page_idx);

  this->mutex.post();

  // 5) 如果你在 get_buffer_page() 里使用了 empty_pages.wait()
  //    那这里必须对应归还一个名额
  // this->empty_pages.post();

  return true;
}

bool BufferPool::write_back_all_dirty_pages()
{
  bool all_success;
  all_success = true;

  this->mutex.wait();

  int i;
  for (i = 0; i < (int)this->pages.size(); i++)
  {
    BufferPage &page = this->pages[i];

    // 只写回有效块号的脏页
    if (page.status == BufferPageStatus::DIRTY)
    {
      if (page.block_num < 0)
      {
        // 异常状态：DIRTY 但没有有效块号
        all_success = false;
        continue;
      }

      // （可选更严谨）如果你不希望写回正在使用的页，可加：
      // if (page.is_locked == true) { continue; }

      bool write_ok;
      write_ok = this->disk.write_block(page.block_num, page.data);

      if (write_ok == true)
      {
        page.status = BufferPageStatus::CLEAN;
      }
      else
      {
        all_success = false;
      }
    }
  }

  this->mutex.post(); // 按你们 Semaphore 的实际释放函数名改
  return all_success;
}

const std::vector<BufferPage> &BufferPool::get_pages() const
{
  return pages;
}

// ==================== 冲高分接口实现 ====================

bool BufferPool::pin_block(int block_num, const std::string &filename, pid_t pid)
{
  // 1) 先在缓冲池中查找（命中直接 pin）
  this->mutex.wait();

  std::map<int, int>::iterator map_it = this->block_to_page.find(block_num);
  if (map_it != this->block_to_page.end())
  {
    int page_idx = map_it->second;
    BufferPage &page = this->pages[page_idx];

    page.pin_count = page.pin_count + 1;
    this->update_lru(page_idx);

    this->mutex.post();
    return true;
  }

  // 2) 未命中：先退出临界区，再调用 get_buffer_page 装入
  this->mutex.post();

  BufferPage *loaded_page = this->get_buffer_page(block_num, filename, pid);
  if (loaded_page == NULL)
  {
    return false;
  }

  // 3) get_buffer_page 通常会把页设为 is_locked=true
  //    我们只是为了装入并 pin，所以 pin 后应立即 release（不修改）
  this->mutex.wait();

  std::map<int, int>::iterator map_it2 = this->block_to_page.find(block_num);
  if (map_it2 == this->block_to_page.end())
  {
    this->mutex.post();
    // 回滚：把 get 占用释放掉（如果你们 get 会锁页）
    this->release_buffer_page(block_num, false);
    return false;
  }

  int page_idx2 = map_it2->second;
  BufferPage &page2 = this->pages[page_idx2];

  page2.pin_count = page2.pin_count + 1;
  this->update_lru(page_idx2);

  this->mutex.post();

  // 释放 get_buffer_page 的短期占用（不改脏标记）
  this->release_buffer_page(block_num, false);

  return true;
}

bool BufferPool::unpin_block(int block_num, const std::string &filename, pid_t pid)
{
  (void)filename;
  (void)pid;

  this->mutex.wait();

  std::map<int, int>::iterator map_it = this->block_to_page.find(block_num);
  if (map_it == this->block_to_page.end())
  {
    this->mutex.post();
    return false;
  }

  int page_idx = map_it->second;
  BufferPage &page = this->pages[page_idx];

  if (page.pin_count > 0)
  {
    page.pin_count = page.pin_count - 1;
  }

  // （可选）为了让刚解除 pin 的页不立刻被淘汰，可以更新 LRU
  this->update_lru(page_idx);

  this->mutex.post();
  return true;
}

bool BufferPool::flush_file(const std::string &filename)
{
  bool all_success;
  all_success = true;

  this->mutex.wait();

  int i;
  for (i = 0; i < (int)this->pages.size(); i++)
  {
    BufferPage &page = this->pages[i];

    // 只处理属于指定文件的脏页
    if (page.filename == filename && page.status == BufferPageStatus::DIRTY)
    {
      // 块号必须有效
      if (page.block_num < 0)
      {
        all_success = false;
        continue;
      }

      // （可选更严谨）如果你不希望写回正在使用的页，可加：
      // if (page.is_locked == true) { continue; }

      bool write_ok;
      write_ok = this->disk.write_block(page.block_num, page.data);

      if (write_ok == true)
      {
        page.status = BufferPageStatus::CLEAN;
      }
      else
      {
        all_success = false;
      }
    }
  }

  this->mutex.post();
  return all_success;
}

bool BufferPool::flush_block(int block_num, const std::string &filename)
{
  this->mutex.wait();

  std::map<int, int>::iterator map_it = this->block_to_page.find(block_num);
  if (map_it == this->block_to_page.end())
  {
    // 不在缓冲池中：视为无需刷新
    this->mutex.post();
    return true;
  }

  int page_idx = map_it->second;
  BufferPage &page = this->pages[page_idx];

  // 可选：文件名一致性检查（用于调试/保护）
  if (filename.empty() == false)
  {
    if (page.filename != filename)
    {
      this->mutex.post();
      return false;
    }
  }

  // 块号必须有效
  if (page.block_num < 0)
  {
    this->mutex.post();
    return false;
  }

  // 脏页则写回
  if (page.status == BufferPageStatus::DIRTY)
  {
    bool write_ok = this->disk.write_block(page.block_num, page.data);
    if (write_ok == true)
    {
      page.status = BufferPageStatus::CLEAN;
    }
    else
    {
      this->mutex.post();
      return false;
    }
  }

  // 可选：更新 LRU（表示刚处理过）
  // this->update_lru(page_idx);

  this->mutex.post();
  return true;
}
