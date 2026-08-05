// disk_mock.hpp
#pragma once
#include "../src/common/common.hpp"
#include <string>

class Disk
{
private:
  std::string disk_file_path;
  bool is_initialized = false;

public:
  Disk(const std::string &path) : disk_file_path(path) {}
  ~Disk() = default;

  bool init_disk()
  {
    is_initialized = true;
    return true;
  }

  bool read_block(int block_num, char *buf)
  {
    if (!is_initialized || block_num < 0 || block_num >= TOTAL_BLOCKS || buf == nullptr)
    {
      return false;
    }
    memset(buf, 0, BLOCK_SIZE);
    return true;
  }

  bool write_block(int block_num, const char *buf)
  {
    if (!is_initialized || block_num < 0 || block_num >= TOTAL_BLOCKS || buf == nullptr)
    {
      return false;
    }
    return true;
  }

  bool is_block_valid(int block_num)
  {
    return block_num >= 0 && block_num < TOTAL_BLOCKS;
  }

  int get_data_start_block() const { return METADATA_BLOCKS; }
  int get_total_blocks() const { return TOTAL_BLOCKS; }
};
