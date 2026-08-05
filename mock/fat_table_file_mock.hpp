// fat_table_mock.hpp
#pragma once
#include "../src/common/common.hpp"
#include "../src/backend/storage/disk.hpp"
#include <vector>
#include <queue>

class FATTable
{
private:
  Disk &disk;
  std::vector<int> fat_data;
  std::queue<int> free_blocks;
  bool is_initialized = false;

public:
  FATTable(Disk &d) : disk(d)
  {
    fat_data.resize(TOTAL_BLOCKS, static_cast<int>(FATEntryType::FREE));
    // 初始化空闲块（仅数据区）
    for (int i = METADATA_BLOCKS; i < TOTAL_BLOCKS; ++i)
    {
      free_blocks.push(i);
    }
  }

  ~FATTable() = default;

  bool init_fat()
  {
    is_initialized = true;
    // 标记元数据区为已用
    for (int i = 0; i < METADATA_BLOCKS; ++i)
    {
      fat_data[i] = 1;
    }
    return true;
  }

  bool update_fat_entry(int block_num, int next_block_num)
  {
    if (!is_initialized || !disk.is_block_valid(block_num))
    {
      return false;
    }
    fat_data[block_num] = next_block_num;
    return true;
  }

  int get_next_block(int block_num)
  {
    if (!is_initialized || !disk.is_block_valid(block_num))
    {
      return static_cast<int>(FATEntryType::FREE);
    }
    return fat_data[block_num];
  }

  int allocate_free_block()
  {
    if (!is_initialized || free_blocks.empty())
    {
      return -1;
    }
    int block = free_blocks.front();
    free_blocks.pop();
    fat_data[block] = static_cast<int>(FATEntryType::END);
    return block;
  }

  bool release_file_blocks(int start_block_num)
  {
    if (!is_initialized || !disk.is_block_valid(start_block_num))
    {
      return false;
    }
    int current = start_block_num;
    while (current != static_cast<int>(FATEntryType::END) && current != static_cast<int>(FATEntryType::FREE))
    {
      int next = fat_data[current];
      fat_data[current] = static_cast<int>(FATEntryType::FREE);
      free_blocks.push(current);
      current = next;
    }
    return true;
  }

  int count_free_blocks()
  {
    return free_blocks.size();
  }

  const std::vector<int> &get_fat_data() const
  {
    return fat_data;
  }
};