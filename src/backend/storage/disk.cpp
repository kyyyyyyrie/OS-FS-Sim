#include "disk.hpp"
#include <iostream>
#include <cstring>

// 构造函数实现（与声明匹配）
Disk::Disk(const std::string &path, int total_blocks, int block_size, int metadata_blocks)
    : disk_path(path),
      total_blocks(total_blocks),
      block_size(block_size),
      metadata_blocks(metadata_blocks)
{
  // 初始化文件流（空实现，在init_disk中打开）
}

// 析构函数实现
Disk::~Disk()
{
  if (disk_file.is_open())
  {
    disk_file.close();
  }
}

// 初始化磁盘
bool Disk::init_disk()
{
  // 打开/创建磁盘文件
  disk_file.open(disk_path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
  if (!disk_file.is_open())
  {
    std::cerr << "[Disk错误] 无法创建/打开磁盘文件：" << disk_path << std::endl;
    return false;
  }

  // 扩展文件到指定大小
  long long total_size = (long long)total_blocks * block_size;
  disk_file.seekp(total_size - 1, std::ios::beg);
  disk_file.put('\0');
  disk_file.flush();

  std::cout << "[Disk信息] 磁盘初始化成功：" << std::endl;
  std::cout << "  - 磁盘文件：" << disk_path << std::endl;
  std::cout << "  - 总盘块数：" << total_blocks << "（总大小：" << total_size << "字节）" << std::endl;
  std::cout << "  - 盘块大小：" << block_size << "字节" << std::endl;
  std::cout << "  - 元数据区：" << metadata_blocks << "块，数据区：" << (total_blocks - metadata_blocks) << "块" << std::endl;

  return true;
}

// 写入盘块
bool Disk::write_block(int block_num, const char *data)
{
  if (!is_block_valid(block_num))
  {
    std::cerr << "[Disk错误] 盘块号" << block_num << "非法，写入失败" << std::endl;
    return false;
  }
  if (!disk_file.is_open())
  {
    std::cerr << "[Disk错误] 磁盘文件未打开，写入失败" << std::endl;
    return false;
  }

  long long offset = (long long)block_num * block_size;
  disk_file.seekp(offset, std::ios::beg);
  if (!disk_file.good())
  {
    std::cerr << "[Disk错误] 定位到盘块" << block_num << "失败，写入失败" << std::endl;
    return false;
  }

  disk_file.write(data, block_size);
  disk_file.flush();
  if (disk_file.fail())
  {
    std::cerr << "[Disk错误] 写入盘块" << block_num << "失败" << std::endl;
    return false;
  }
  return true;
}

// 读取盘块
bool Disk::read_block(int block_num, char *buffer)
{
  if (!is_block_valid(block_num))
  {
    std::cerr << "[Disk错误] 盘块号" << block_num << "非法，读取失败" << std::endl;
    return false;
  }
  if (buffer == nullptr)
  {
    std::cerr << "[Disk错误] 读取缓冲区为空，读取失败" << std::endl;
    return false;
  }
  if (!disk_file.is_open())
  {
    std::cerr << "[Disk错误] 磁盘文件未打开，读取失败" << std::endl;
    return false;
  }

  long long offset = (long long)block_num * block_size;
  disk_file.seekg(offset, std::ios::beg);
  if (!disk_file.good())
  {
    std::cerr << "[Disk错误] 定位到盘块" << block_num << "失败，读取失败" << std::endl;
    return false;
  }

  memset(buffer, 0, block_size);
  disk_file.read(buffer, block_size);
  if (disk_file.fail())
  {
    std::cerr << "[Disk错误] 读取盘块" << block_num << "失败" << std::endl;
    return false;
  }
  return true;
}

// 校验盘块号合法性（加const修饰符，与声明匹配）
bool Disk::is_block_valid(int block_num) const
{
  return (block_num >= 0 && block_num < total_blocks);
}

// 获取数据区起始盘块号
int Disk::get_data_start_block() const
{
  return metadata_blocks;
}

// 补充get_block_size()实现
int Disk::get_block_size() const
{
  return block_size;
}

// 补充get_metadata_blocks()实现
int Disk::get_metadata_blocks() const
{
  return metadata_blocks;
}

// 补充get_total_blocks()实现
int Disk::get_total_blocks() const
{
  return total_blocks;
}