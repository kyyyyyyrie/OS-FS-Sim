#pragma once
#include "disk.hpp"
#include "../../common/common.hpp"
#include <vector>
#include <iostream>
#include <queue>

/**
 * @brief FAT表类（文件分配表，管理盘块的链式分配/空闲块）
 * @note 核心逻辑：内存缓存FAT表，基于FAT实现文件盘块链和空闲块管理，依赖Disk类持久化
 */
class FATTable
{
private:
  Disk &disk;                // 关联的模拟磁盘（引用，不拷贝）
  std::vector<int> fat_data; // FAT表内存缓存（索引=盘块号，值=下一个盘块号/FATEntryType）
  // std::queue<int> free_blocks; // 空闲块缓存队列（优化分配效率，避免每次遍历FAT表）
  std::priority_queue<int, std::vector<int>, std::greater<int>> free_blocks;

  /**
   * @brief 私有方法：从磁盘元数据区加载FAT表到内存
   * @note 初始化/重启时调用，FAT表存储在元数据区前N个盘块
   */
  void load_fat_from_disk();

  /**
   * @brief 私有方法：将内存FAT表刷入磁盘元数据区
   * @note FAT表修改后调用，保证持久化
   */
  void write_fat_to_disk();

public:
  /**
   * @brief 构造函数：关联磁盘并初始化FAT表缓存
   * @param disk 模拟磁盘实例（引用）
   */
  FATTable(Disk &disk);

  /**
   * @brief 初始化FAT表
   * @return true=初始化成功，false=失败
   * @note 所有盘块标记为空闲（FATEntryType::FREE），初始化空闲块队列
   */
  bool init_fat();

  /**
   * @brief 更新FAT表项（设置盘块的下一个盘块号）
   * @param block_num 待更新的盘块号
   * @param next_block_num 下一个盘块号（FATEntryType::END表示结束）
   * @return true=更新成功，false=失败（盘块号非法）
   */
  bool update_fat_entry(int block_num, int next_block_num);

  /**
   * @brief 获取指定盘块的下一个盘块号
   * @param block_num 盘块号
   * @return 下一个盘块号（FATEntryType::FREE/END表示无后续）
   */
  int get_next_block(int block_num);

  /**
   * @brief 分配一个空闲盘块（优先数据区）
   * @return 分配的盘块号（-1表示无空闲块）
   */
  int allocate_free_block();

  /**
   * @brief 释放文件占用的所有盘块（遍历链式结构）
   * @param start_block_num 文件起始盘块号
   * @return true=释放成功，false=失败（起始盘块号非法）
   * @note 遍历FAT表链，将所有盘块标记为空闲，更新空闲块队列
   */
  bool release_file_blocks(int start_block_num);

  /**
   * @brief 统计当前剩余空闲块数
   * @return 空闲块数量
   */
  int count_free_blocks();

  /**
   * @brief 获取FAT表原始数据（供可视化/测试）
   * @return FAT表内存缓存的常量引用
   */
  const std::vector<int> &get_fat_data() const;
};