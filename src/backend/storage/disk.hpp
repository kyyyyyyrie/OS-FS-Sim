#pragma once
#include "../../common/common.hpp"
#include <fstream>
#include <string>
#include <vector>

class Disk
{
private:
  // 补充所有私有成员变量声明
  std::string disk_path;  // 磁盘文件路径
  std::fstream disk_file; // 磁盘文件流
  int total_blocks;       // 总盘块数
  int block_size;         // 单盘块大小（字节）
  int metadata_blocks;    // 元数据区盘块数

public:
  // 构造函数声明（与disk.cpp实现匹配）
  Disk(const std::string &path,
       int total_blocks = TOTAL_BLOCKS,
       int block_size = BLOCK_SIZE,
       int metadata_blocks = METADATA_BLOCKS);

  // 析构函数声明
  ~Disk();

  // 补充所有公共方法声明
  bool init_disk();
  bool write_block(int block_num, const char *data);
  bool read_block(int block_num, char *buffer);
  bool is_block_valid(int block_num) const; // 注意：加const修饰符
  int get_data_start_block() const;
  int get_total_blocks() const;
  int get_block_size() const;      // 补充get_block_size()
  int get_metadata_blocks() const; // 补充get_metadata_blocks()
};