#include "fat_table.hpp"
#include <iostream>
#include <cmath>
#include <queue> // 新增：包含优先队列头文件

// ===================== 私有方法实现 =====================
/**
 * @brief 从磁盘元数据区加载 FAT 表到内存缓存
 * @note FAT 表存储在元数据区起始位置，按块读取后解析为 int 数组
 */
void FATTable::load_fat_from_disk()
{
  // 1. 计算 FAT 表占用的盘块数（每个 FAT 项是 int，4 字节）
  int fat_total_bytes = TOTAL_BLOCKS * sizeof(int);
  int fat_block_count = ceil((double)fat_total_bytes / BLOCK_SIZE);

  // 2. 清空内存缓存
  fat_data.clear();
  fat_data.resize(TOTAL_BLOCKS, (int)FATEntryType::FREE); // 默认初始化为空闲

  // 3. 从磁盘读取 FAT 表数据（元数据区起始块 0 开始）
  char read_buf[BLOCK_SIZE] = {0};
  int read_bytes = 0; // 已读取的字节数
  for (int block = 0; block < fat_block_count; block++)
  {
    if (!disk.read_block(block, read_buf))
    {
      std::cerr << "[FAT ERROR] 读取 FAT 表盘块 " << block << " 失败" << std::endl;
      return;
    }

    // 4. 将读取的字节解析为 int 型 FAT 项（按字节拷贝）
    int remain_bytes = fat_total_bytes - read_bytes; // 剩余未读取的字节
    int copy_bytes = BLOCK_SIZE < remain_bytes ? BLOCK_SIZE : remain_bytes;
    for (int i = 0; i < copy_bytes; i += sizeof(int))
    {
      if (read_bytes + i >= fat_total_bytes)
        break;                                        // 防止越界
      int fat_index = (read_bytes + i) / sizeof(int); // FAT 表项索引（对应盘块号）
      int fat_value = *(int *)(read_buf + i);         // 解析为 int
      fat_data[fat_index] = fat_value;
    }
    read_bytes += copy_bytes;
    memset(read_buf, 0, BLOCK_SIZE); // 清空缓冲区
  }

  // 5. 初始化空闲块优先队列（遍历 FAT 表，收集所有 FREE 块）
  free_blocks = std::priority_queue<int, std::vector<int>, std::greater<int>>(); // 【修改】初始化最小优先队列
  for (int block = disk.get_data_start_block(); block < TOTAL_BLOCKS; block++)
  {
    if (fat_data[block] == (int)FATEntryType::FREE)
    {
      free_blocks.push(block); // 优先队列自动按从小到大排序
    }
  }

  std::cout << "[FAT INFO] FAT 表加载完成，空闲块数：" << free_blocks.size() << std::endl;
}

/**
 * @brief 将内存中的 FAT 表刷入磁盘元数据区
 * @note 与 load_fat_from_disk 逻辑对称，将 int 数组转为字节流写入磁盘
 */
void FATTable::write_fat_to_disk()
{
  // 1. 计算 FAT 表占用的盘块数
  int fat_total_bytes = TOTAL_BLOCKS * sizeof(int);
  int fat_block_count = ceil((double)fat_total_bytes / BLOCK_SIZE);

  // 2. 将 FAT 表转为字节流
  char write_buf[BLOCK_SIZE] = {0};
  int write_bytes = 0; // 已写入的字节数
  for (int block = 0; block < fat_block_count; block++)
  {
    memset(write_buf, 0, BLOCK_SIZE); // 清空缓冲区

    // 3. 填充当前盘块的字节数据
    int remain_bytes = fat_total_bytes - write_bytes;
    int copy_bytes = BLOCK_SIZE < remain_bytes ? BLOCK_SIZE : remain_bytes;

    for (int i = 0; i < copy_bytes; i += sizeof(int))
    {
      if (write_bytes + i >= fat_total_bytes)
        break;
      int fat_index = (write_bytes + i) / sizeof(int);
      *(int *)(write_buf + i) = fat_data[fat_index]; // 拷贝 int 到字节流
    }

    // 4. 写入磁盘元数据区对应块
    if (!disk.write_block(block, write_buf))
    {
      std::cerr << "[FAT ERROR] 写入 FAT 表盘块 " << block << " 失败" << std::endl;
      return;
    }
    write_bytes += copy_bytes;
  }

  std::cout << "[FAT INFO] FAT 表已刷入磁盘" << std::endl;
}

// ===================== 公有方法实现 =====================
/**
 * @brief 构造函数：关联磁盘并初始化 FAT 表缓存
 * @param disk 模拟磁盘实例（引用）
 */
FATTable::FATTable(Disk &disk) : disk(disk)
{
  // 初始化 FAT 数据缓存（默认全为空闲）
  fat_data.resize(TOTAL_BLOCKS, (int)FATEntryType::FREE);
}

/**
 * @brief 初始化 FAT 表（首次创建磁盘时调用）
 * @return true=初始化成功，false=失败
 */
bool FATTable::init_fat()
{
  // 1. 清空 FAT 表：所有块标记为 FREE（元数据区块除外，标记为 END）
  for (int i = 0; i < TOTAL_BLOCKS; i++)
  {
    if (i < disk.get_data_start_block())
    {
      fat_data[i] = (int)FATEntryType::END; // 元数据区块不可分配
    }
    else
    {
      fat_data[i] = (int)FATEntryType::FREE; // 数据区块初始为空闲
    }
  }

  // 2. 初始化空闲块优先队列（仅包含数据区块）
  free_blocks = std::priority_queue<int, std::vector<int>, std::greater<int>>(); // 【修改】初始化最小优先队列
  for (int block = disk.get_data_start_block(); block < TOTAL_BLOCKS; block++)
  {
    free_blocks.push(block); // 优先队列自动排序
  }

  // 3. 将初始化后的 FAT 表写入磁盘
  write_fat_to_disk();

  std::cout << "[FAT INFO] FAT 表初始化成功，总盘块数：" << TOTAL_BLOCKS
            << "，数据区起始块：" << disk.get_data_start_block() << std::endl;
  return true;
}

/**
 * @brief 更新 FAT 表项（设置指定块的下一个块号）
 * @param block_num 待更新的盘块号
 * @param next_block_num 下一个块号（END 表示链结束）
 * @return true=更新成功，false=失败（块号非法）
 */
bool FATTable::update_fat_entry(int block_num, int next_block_num)
{
  // 1. 校验块号合法性
  if (!disk.is_block_valid(block_num))
  {
    std::cerr << "[FAT ERROR] 盘块 " << block_num << " 非法，更新 FAT 项失败" << std::endl;
    return false;
  }

  // 2. 校验下一个块号（要么是合法块号，要么是 END）
  if (next_block_num != (int)FATEntryType::END && !disk.is_block_valid(next_block_num))
  {
    std::cerr << "[FAT ERROR] 下一个盘块 " << next_block_num << " 非法，更新 FAT 项失败" << std::endl;
    return false;
  }

  // 3. 更新内存中的 FAT 表项
  fat_data[block_num] = next_block_num;

  // 4. 同步到磁盘（可选：批量更新可减少 IO，课程设计中直接同步）
  write_fat_to_disk();

  std::cout << "[FAT INFO] FAT 表项更新：块 " << block_num << " → 下一块 " << next_block_num << std::endl;
  return true;
}

/**
 * @brief 获取指定块的下一个块号
 * @param block_num 盘块号
 * @return 下一个块号（FREE/END 表示无后续）
 */
int FATTable::get_next_block(int block_num)
{
  if (!disk.is_block_valid(block_num))
  {
    std::cerr << "[FAT ERROR] 盘块 " << block_num << " 非法，获取下一块失败" << std::endl;
    return (int)FATEntryType::FREE;
  }
  return fat_data[block_num];
}

/**
 * @brief 分配一个空闲盘块（优先数据区，优先最小块号）
 * @return 分配的盘块号（-1 表示无空闲块）
 */
int FATTable::allocate_free_block()
{
  // 1. 检查空闲块优先队列是否为空
  if (free_blocks.empty())
  {
    std::cerr << "[FAT ERROR] 无空闲盘块，分配失败" << std::endl;
    return -1;
  }

  // 2. 取出优先队列顶部的最小空闲块（【修改】从 front() 改为 top()）
  int free_block = free_blocks.top();
  free_blocks.pop();

  // 3. 标记该块为 "已占用"（先设为 END，后续由文件设置下一块）
  fat_data[free_block] = (int)FATEntryType::END;
  write_fat_to_disk(); // 同步到磁盘

  std::cout << "[FAT INFO] 分配空闲块：" << free_block << "，剩余空闲块：" << free_blocks.size() << std::endl;
  return free_block;
}

/**
 * @brief 释放文件占用的所有盘块（遍历 FAT 链）
 * @param start_block_num 文件起始盘块号
 * @return true=释放成功，false=失败（起始块非法/无关联）
 */
bool FATTable::release_file_blocks(int start_block_num)
{
  // 1. 校验起始块合法性
  if (!disk.is_block_valid(start_block_num))
  {
    std::cerr << "[FAT ERROR] 起始块 " << start_block_num << " 非法，释放失败" << std::endl;
    return false;
  }

  // 2. 遍历 FAT 链，释放所有关联块
  int current_block = start_block_num;
  while (current_block != (int)FATEntryType::END && current_block != (int)FATEntryType::FREE)
  {
    int next_block = fat_data[current_block]; // 保存下一块号

    // 3. 标记当前块为 FREE，并加入最小优先队列（自动排序）
    fat_data[current_block] = (int)FATEntryType::FREE;
    free_blocks.push(current_block); // 【修改】优先队列自动将低地址块排到顶部

    std::cout << "[FAT INFO] 释放盘块：" << current_block << std::endl;
    current_block = next_block; // 处理下一块
  }

  // 4. 同步到磁盘
  write_fat_to_disk();

  std::cout << "[FAT INFO] 文件盘块释放完成，剩余空闲块：" << free_blocks.size() << std::endl;
  return true;
}

/**
 * @brief 统计当前剩余空闲块数
 * @return 空闲块数量
 */
int FATTable::count_free_blocks()
{
  return free_blocks.size();
}

/**
 * @brief 获取 FAT 表原始数据（供可视化/测试）
 * @return FAT 表内存缓存的常量引用
 */
const std::vector<int> &FATTable::get_fat_data() const
{
  return fat_data;
}