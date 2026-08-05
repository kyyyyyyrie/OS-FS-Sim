// fat_table_mock.hpp
#pragma once
#include "../src/common/common.hpp"
#include "../src/backend/storage/disk.hpp"
#include <vector>

class FATTable
{
private:
  Disk &disk;
  std::vector<int> fat_data;

public:
  FATTable(Disk &disk) : disk(disk) {}

  bool init_fat() { return true; }
  bool update_fat_entry(int block_num, int next_block_num) { return true; }
  int get_next_block(int block_num) { return -1; }
  int allocate_free_block() { return 64; }
  bool release_file_blocks(int start_block_num) { return true; }
  int count_free_blocks() { return 1000; }
  const std::vector<int> &get_fat_data() const { return fat_data; }
};