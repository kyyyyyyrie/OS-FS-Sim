#include "file_interface.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <windows.h>
#include <ctime>
#include <cmath>
// 必须添加的头文件（放在该.cpp文件的顶部）
#include <thread>                  // std::this_thread::sleep_for
#include <chrono>                  // 时间单位
#include "../../common/common.hpp" // 引入延时常量BLOCK_WRITE_DELAY_MS

// ========== 新增：UTF-8转GBK函数 ==========
std::string utf8_to_gbk(const std::string &utf8_str)
{
  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, NULL, 0);
  if (wlen == 0)
    return "";
  wchar_t *wstr = new wchar_t[wlen];
  MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, wstr, wlen);

  int glen = WideCharToMultiByte(CP_ACP, 0, wstr, -1, NULL, 0, NULL, NULL);
  if (glen == 0)
  {
    delete[] wstr;
    return "";
  }
  char *gbk_str = new char[glen];
  WideCharToMultiByte(CP_ACP, 0, wstr, -1, gbk_str, glen, NULL, NULL);

  std::string result(gbk_str);
  delete[] wstr;
  delete[] gbk_str;
  return result;
}

#define PRINT_GBK(utf8_str) std::cout << utf8_to_gbk(utf8_str)

// 权限检查辅助函数（适配common.hpp的FilePermission枚举）
bool FileInterface::check_permission(FilePermission fcb_perm, FilePermission required_perm)
{
  return (fcb_perm & required_perm) == required_perm;
}

// ===================== 构造函数 =====================
FileInterface::FileInterface(Disk &d, FATTable &ft, Directory &dir, BufferPool &bp)
    : disk(d), fat_table(ft), directory(dir), buffer_pool(bp) {}

// ===================== 核心文件操作 =====================
// 1. 创建文件
bool FileInterface::create_file(const std::string &file_path, FilePermission perm, const std::string &content)
{
  // 步骤1：校验文件是否已存在（调用用户实现的query_file）
  FCB exist_fcb;
  if (directory.query_file(file_path, exist_fcb))
  {
    std::cerr << "[ERROR] 创建文件失败：" << file_path << " 已存在\n";
    return false;
  }

  // 步骤2：从FAT表分配空闲盘块
  int first_block = fat_table.allocate_free_block();
  if (first_block == -1)
  {
    std::cerr << "[ERROR] 创建文件失败：磁盘无空闲块\n";
    return false;
  }

  // 步骤3：构造FCB（严格对齐用户实现的FCB结构）
  FCB new_fcb;
  std::vector<std::string> path_parts = split_path(file_path); // common.hpp的工具函数
  new_fcb.filename = path_parts.empty() ? "" : path_parts.back();
  new_fcb.permission = perm;
  new_fcb.create_time = get_current_time(); // common.hpp的工具函数
  new_fcb.start_block = first_block;
  new_fcb.total_blocks = 1;
  new_fcb.is_in_use = false;
  new_fcb.holder_pid = 0;

  // 步骤4：添加FCB到目录（调用用户实现的add_file）
  if (!directory.add_file(file_path, new_fcb))
  {
    fat_table.release_file_blocks(first_block); // 回滚：释放已分配块
    std::cerr << "[ERROR] 创建文件失败：目录添加FCB失败\n";
    return false;
  }

  // 步骤5：处理初始内容（对齐BLOCK_SIZE）
  char write_buf[BLOCK_SIZE] = {0};
  // int write_len = std::min(static_cast<int>(content.size()), static_cast<int>(BLOCK_SIZE));
  int content_size = static_cast<int>(content.size());
  int block_size_int = static_cast<int>(BLOCK_SIZE);
  int write_len = (content_size < block_size_int) ? content_size : block_size_int;
  memcpy(write_buf, content.c_str(), write_len);

  // 步骤6：写入缓冲池/磁盘
  pid_t current_pid = GetCurrentProcessId();
  BufferPage *page = buffer_pool.get_buffer_page(first_block, file_path, current_pid);
  if (page != nullptr)
  {
    memcpy(page->data, write_buf, BLOCK_SIZE);
    page->status = BufferPageStatus::DIRTY;
    buffer_pool.release_buffer_page(first_block, true);
  }
  else
  {
    // 缓冲池置换失败时直接写磁盘
    if (!disk.write_block(first_block, write_buf))
    {
      directory.remove_file(file_path);
      fat_table.release_file_blocks(first_block);
      std::cerr << "[ERROR] 创建文件失败：磁盘写入失败\n";
      return false;
    }
  }

  // 步骤7：更新FAT表+持久化目录
  fat_table.update_fat_entry(first_block, (int)FATEntryType::END);
  directory.write_dir_to_disk();

  std::cout << "[INFO] 文件创建成功：" << file_path << "，分配块号：" << first_block << "\n";
  return true;
}

// 2. 查看文件指定逻辑块内容
std::string FileInterface::view_file_block(const std::string &file_path, int block_num)
{
  // 步骤1：获取FCB（调用用户实现的query_file）
  FCB target_fcb;
  if (!directory.query_file(file_path, target_fcb))
  {
    std::cerr << "[ERROR] 查看文件块失败：" << file_path << " 不存在\n";
    return "";
  }

  // 步骤2：权限检查
  if (!check_permission(target_fcb.permission, FilePermission::File_READ))
  {
    std::cerr << "[ERROR] 查看文件块失败：无读权限\n";
    return "";
  }

  // 步骤3：通过FAT表查找物理块
  int physical_block = target_fcb.start_block;
  for (int i = 0; i < block_num; ++i)
  {
    physical_block = fat_table.get_next_block(physical_block);
    if (physical_block == (int)FATEntryType::END || physical_block == (int)FATEntryType::FREE)
    {
      std::cerr << "[ERROR] 查看文件块失败：块号" << block_num << "超出范围\n";
      return "";
    }
  }

  // 步骤4：优先从缓冲池读取
  pid_t current_pid = GetCurrentProcessId();
  BufferPage *page = buffer_pool.get_buffer_page(physical_block, file_path, current_pid);
  char read_buf[BLOCK_SIZE] = {0};

  if (page != nullptr)
  {
    memcpy(read_buf, page->data, BLOCK_SIZE);
    page->last_access_time = time(nullptr);
    buffer_pool.release_buffer_page(physical_block, false);
    std::cout << "[INFO] 从缓冲池读取块：" << physical_block << "\n";
  }
  else
  {
    if (!disk.read_block(physical_block, read_buf))
    {
      std::cerr << "[ERROR] 查看文件块失败：磁盘读取失败\n";
      return "";
    }
    // 缓存到缓冲池
    page = buffer_pool.get_buffer_page(physical_block, file_path, current_pid);
    if (page != nullptr)
    {
      memcpy(page->data, read_buf, BLOCK_SIZE);
      page->status = BufferPageStatus::CLEAN;
      buffer_pool.release_buffer_page(physical_block, false);
    }
    std::cout << "[INFO] 从磁盘读取块：" << physical_block << "\n";
  }

  // 转换为有效字符串
  std::string content(read_buf);
  content.erase(content.find_last_not_of('\0') + 1);
  return content;
}

// 3. 修改文件指定逻辑块内容
bool FileInterface::modify_file_block(const std::string &file_path, int block_num, const std::string &new_content)
{
  // 步骤1：获取FCB
  FCB target_fcb;
  if (!directory.query_file(file_path, target_fcb))
  {
    std::cerr << "[ERROR] 修改文件块失败：文件不存在\n";
    return false;
  }

  // 步骤2：权限检查
  if (!check_permission(target_fcb.permission, FilePermission::File_WRITE))
  {
    std::cerr << "[ERROR] 修改文件块失败：无写权限\n";
    return false;
  }

  // 步骤3：锁定文件（调用用户实现的lock_file）
  pid_t current_pid = GetCurrentProcessId();
  if (!directory.lock_file(file_path, current_pid))
  {
    std::cerr << "[ERROR] 修改文件块失败：文件被其他进程锁定\n";
    return false;
  }

  // 步骤4：查找物理块
  int physical_block = target_fcb.start_block;
  for (int i = 0; i < block_num; ++i)
  {
    physical_block = fat_table.get_next_block(physical_block);
    // 若块号超出范围，先解锁再返回
    if (physical_block == (int)FATEntryType::END || physical_block == (int)FATEntryType::FREE)
    {
      directory.unlock_file(file_path);
      std::cerr << "[ERROR] 修改文件块失败：块号" << block_num << "超出范围\n";
      return false;
    }
  }

  // 步骤5：处理新内容
  char modify_buf[BLOCK_SIZE] = {0};
  // int modify_len = std::min((int)new_content.size(), BLOCK_SIZE);
  int new_content_size = static_cast<int>(new_content.size()); // 显式转换为int
  int block_size_int = static_cast<int>(BLOCK_SIZE);           // BLOCK_SIZE转为int
  int modify_len = (new_content_size < block_size_int) ? new_content_size : block_size_int;
  memcpy(modify_buf, new_content.c_str(), modify_len);

  // 步骤6：写入缓冲池/磁盘
  BufferPage *page = buffer_pool.get_buffer_page(physical_block, file_path, current_pid);
  bool write_success = false;
  if (page != nullptr)
  {
    memcpy(page->data, modify_buf, BLOCK_SIZE);
    page->status = BufferPageStatus::DIRTY;
    page->last_access_time = time(nullptr);
    write_success = buffer_pool.release_buffer_page(physical_block, true);
  }
  else
  {
    write_success = disk.write_block(physical_block, modify_buf);
  }

  if (!write_success)
  {
    directory.unlock_file(file_path);
    std::cerr << "[ERROR] 修改文件块失败：写入失败\n";
    return false;
  }

  // 步骤7：解锁文件+更新FCB
  directory.unlock_file(file_path);
  // target_fcb.total_blocks = std::max(target_fcb.total_blocks, block_num + 1);
  int compare_value = block_num + 1;
  target_fcb.total_blocks = (target_fcb.total_blocks > compare_value) ? target_fcb.total_blocks : compare_value;
  directory.add_file(file_path, target_fcb); // 更新FCB
  directory.write_dir_to_disk();

  std::cout << "[INFO] 文件块修改成功：" << file_path << "（逻辑块：" << block_num << "）\n";
  return true;
}

// file_interface.cpp 中新增实现

bool FileInterface::write_file(const std::string &file_path, const std::string &content)
{
  // 1. 检查文件是否存在
  FCB target_fcb;
  if (!directory.query_file(file_path, target_fcb))
  {
    std::cerr << "[ERROR] 写入文件失败：" << file_path << " 不存在（请先创建文件）\n";
    return false;
  }

  // 2. 权限检查（需要写权限）
  if (!check_permission(target_fcb.permission, FilePermission::File_WRITE))
  {
    std::cerr << "[ERROR] 写入文件失败：无写权限\n";
    return false;
  }

  // 3. 锁定文件
  pid_t current_pid = GetCurrentProcessId();
  if (!directory.lock_file(file_path, current_pid))
  {
    std::cerr << "[ERROR] 写入文件失败：文件被其他进程锁定\n";
    return false;
  }

  // 4. 计算需要的块数（精准按字节数计算）
  size_t content_len = content.size();
  int BLOCK_SIZE_INT = static_cast<int>(BLOCK_SIZE); // 64
  int required_blocks = content_len == 0 ? 1 : (static_cast<int>(content_len) + BLOCK_SIZE_INT - 1) / BLOCK_SIZE_INT;
  std::vector<int> existing_blocks = get_file_all_blocks(file_path);
  int existing_block_count = static_cast<int>(existing_blocks.size());

  // 5. 分配新块（仅当现有块不足时分配）
  if (required_blocks > existing_block_count)
  {
    int need_more = required_blocks - existing_block_count;
    int last_block = existing_block_count > 0 ? existing_blocks.back() : -1;

    for (int i = 0; i < need_more; ++i)
    {
      int new_block = fat_table.allocate_free_block();
      if (new_block == -1)
      {
        directory.unlock_file(file_path);
        std::cerr << "[ERROR] 写入文件失败：磁盘无空闲块\n";
        return false;
      }

      // 链接到FAT链末尾
      if (last_block != -1)
      {
        fat_table.update_fat_entry(last_block, new_block);
      }
      else
      {
        target_fcb.start_block = new_block;
      }
      existing_blocks.push_back(new_block);
      last_block = new_block;
    }

    // 标记最后一块为链结束
    if (!existing_blocks.empty())
    {
      fat_table.update_fat_entry(existing_blocks.back(), static_cast<int>(FATEntryType::END));
    }
  }

  // 6. 分块写入内容（核心：精准按字节拆分，避免多字节字符截断）
  size_t block_size_size_t = static_cast<size_t>(BLOCK_SIZE_INT);
  for (int i = 0; i < required_blocks; ++i)
  {
    char write_buf[BLOCK_SIZE] = {0}; // 每次清空缓冲区
    size_t start = static_cast<size_t>(i) * block_size_size_t;
    size_t end = (start + block_size_size_t < content_len) ? (start + block_size_size_t) : content_len;
    size_t block_content_len = end - start;

    // 精准拷贝当前块的有效内容（按字节数）
    if (block_content_len > 0)
    {
      memcpy(write_buf, content.c_str() + start, block_content_len);
    }

    // 写入缓冲池/磁盘
    int physical_block = existing_blocks[i];
    BufferPage *page = buffer_pool.get_buffer_page(physical_block, file_path, current_pid);
    if (page != nullptr)
    {
      memcpy(page->data, write_buf, BLOCK_SIZE);
      page->status = BufferPageStatus::DIRTY;
      buffer_pool.release_buffer_page(physical_block, true);
    }
    else
    {
      disk.write_block(physical_block, write_buf);
    }

    // ================ 新增：块与块之间添加延时（核心修改） ================
    // 1. 打印可视化提示，方便观察延时触发
    std::cout << "[延时] 已写入文件 " << file_path << " 的第 " << (i + 1) << "/" << required_blocks
              << " 块（物理块ID：" << physical_block << "），等待 " << BLOCK_WRITE_DELAY_MS << "ms...\n";
    // 2. 执行延时（跨平台兼容，Windows/Linux都可用）
    std::this_thread::sleep_for(std::chrono::milliseconds(BLOCK_WRITE_DELAY_MS));
    // ================================================================
  }

  // 7. 清理多余块
  if (existing_block_count > required_blocks)
  {
    for (int i = required_blocks; i < existing_block_count; ++i)
    {
      fat_table.release_file_blocks(existing_blocks[i]);
    }
    fat_table.update_fat_entry(existing_blocks[required_blocks - 1], static_cast<int>(FATEntryType::END));
  }

  // 8. 更新FCB（记录真实内容长度）
  target_fcb.total_blocks = required_blocks;
  target_fcb.file_size = static_cast<int>(content_len); // 新增：记录文件真实字节数
  directory.update_file_fcb(file_path, target_fcb);

  // 9. 解锁文件+持久化
  directory.unlock_file(file_path);
  buffer_pool.write_back_all_dirty_pages();

  std::cout << "[INFO] 文件写入成功：" << file_path
            << "（块大小：" << BLOCK_SIZE_INT << "B，占用块数：" << required_blocks
            << "，内容大小：" << content_len << "字节）\n";
  return true;
}

// bool FileInterface::write_file(const std::string &file_path, const std::string &content)
// {
//   // 1. 检查文件是否存在
//   FCB target_fcb;
//   if (!directory.query_file(file_path, target_fcb))
//   {
//     std::cerr << "[ERROR] 写入文件失败：" << file_path << " 不存在（请先创建文件）\n";
//     return false;
//   }

//   // 2. 权限检查（需要写权限）
//   if (!check_permission(target_fcb.permission, FilePermission::File_WRITE))
//   {
//     std::cerr << "[ERROR] 写入文件失败：无写权限\n";
//     return false;
//   }

//   // 3. 锁定文件
//   pid_t current_pid = GetCurrentProcessId();
//   if (!directory.lock_file(file_path, current_pid))
//   {
//     std::cerr << "[ERROR] 写入文件失败：文件被其他进程锁定\n";
//     return false;
//   }

//   // ========== 新增：将UTF-8内容转为GBK编码 ==========
//   std::string content_gbk = utf8_to_gbk(content); // 调用之前的转换函数
//   size_t content_len = content_gbk.size();        // 基于GBK内容计算长度
//   // ================================================

//   // 4. 计算需要的块数（精准按字节数计算）
//   int BLOCK_SIZE_INT = static_cast<int>(BLOCK_SIZE); // 64
//   int required_blocks = content_len == 0 ? 1 : (static_cast<int>(content_len) + BLOCK_SIZE_INT - 1) / BLOCK_SIZE_INT;
//   std::vector<int> existing_blocks = get_file_all_blocks(file_path);
//   int existing_block_count = static_cast<int>(existing_blocks.size());

//   // 5. 分配新块（仅当现有块不足时分配）
//   if (required_blocks > existing_block_count)
//   {
//     int need_more = required_blocks - existing_block_count;
//     int last_block = existing_block_count > 0 ? existing_blocks.back() : -1;

//     for (int i = 0; i < need_more; ++i)
//     {
//       int new_block = fat_table.allocate_free_block();
//       if (new_block == -1)
//       {
//         directory.unlock_file(file_path);
//         std::cerr << "[ERROR] 写入文件失败：磁盘无空闲块\n";
//         return false;
//       }

//       // 链接到FAT链末尾
//       if (last_block != -1)
//       {
//         fat_table.update_fat_entry(last_block, new_block);
//       }
//       else
//       {
//         target_fcb.start_block = new_block;
//       }
//       existing_blocks.push_back(new_block);
//       last_block = new_block;
//     }

//     // 标记最后一块为链结束
//     if (!existing_blocks.empty())
//     {
//       fat_table.update_fat_entry(existing_blocks.back(), static_cast<int>(FATEntryType::END));
//     }
//   }

//   // 6. 分块写入内容（核心：使用GBK编码的content_gbk）
//   size_t block_size_size_t = static_cast<size_t>(BLOCK_SIZE_INT);
//   for (int i = 0; i < required_blocks; ++i)
//   {
//     char write_buf[BLOCK_SIZE] = {0}; // 每次清空缓冲区
//     size_t start = static_cast<size_t>(i) * block_size_size_t;
//     size_t end = (start + block_size_size_t < content_len) ? (start + block_size_size_t) : content_len;
//     size_t block_content_len = end - start;

//     // 精准拷贝当前块的GBK内容
//     if (block_content_len > 0)
//     {
//       memcpy(write_buf, content_gbk.c_str() + start, block_content_len); // 改为content_gbk
//     }

//     // 写入缓冲池/磁盘
//     int physical_block = existing_blocks[i];
//     BufferPage *page = buffer_pool.get_buffer_page(physical_block, file_path, current_pid);
//     if (page != nullptr)
//     {
//       memcpy(page->data, write_buf, BLOCK_SIZE);
//       page->status = BufferPageStatus::DIRTY;
//       buffer_pool.release_buffer_page(physical_block, true);
//     }
//     else
//     {
//       disk.write_block(physical_block, write_buf);
//     }
//   }

//   // 7. 清理多余块
//   if (existing_block_count > required_blocks)
//   {
//     for (int i = required_blocks; i < existing_block_count; ++i)
//     {
//       fat_table.release_file_blocks(existing_blocks[i]);
//     }
//     fat_table.update_fat_entry(existing_blocks[required_blocks - 1], static_cast<int>(FATEntryType::END));
//   }

//   // 8. 更新FCB（记录真实内容长度）
//   target_fcb.total_blocks = required_blocks;
//   target_fcb.file_size = static_cast<int>(content_len); // 记录GBK内容的真实字节数
//   directory.update_file_fcb(file_path, target_fcb);

//   // 9. 解锁文件+持久化
//   directory.unlock_file(file_path);
//   buffer_pool.write_back_all_dirty_pages();

//   std::cout << "[INFO] 文件写入成功：" << file_path
//             << "（块大小：" << BLOCK_SIZE_INT << "B，占用块数：" << required_blocks
//             << "，内容大小：" << content_len << "字节）\n";
//   return true;
// }

// file_interface.cpp 中新增实现
std::string FileInterface::read_file(const std::string &file_path)
{
  // 1. 检查文件是否存在
  FCB target_fcb;
  if (!directory.query_file(file_path, target_fcb))
  {
    std::cerr << "[ERROR] 读取文件失败：" << file_path << " 不存在\n";
    return "";
  }

  // 2. 权限检查
  if (!check_permission(target_fcb.permission, FilePermission::File_READ))
  {
    std::cerr << "[ERROR] 读取文件失败：无读权限\n";
    return "";
  }

  // 3. 获取文件所有物理块
  std::vector<int> physical_blocks = get_file_all_blocks(file_path);
  if (physical_blocks.empty())
  {
    std::cerr << "[WARNING] 文件" << file_path << "无内容\n";
    return "";
  }

  // 4. 逐块读取（按真实内容长度拼接）
  std::string full_content;
  size_t total_valid_len = static_cast<size_t>(target_fcb.file_size); // 文件真实字节数
  size_t read_len = 0;

  for (size_t i = 0; i < physical_blocks.size() && read_len < total_valid_len; ++i)
  {
    char block_buf[BLOCK_SIZE] = {0};
    int physical_block = physical_blocks[i];

    // 从缓冲池/磁盘读取块
    BufferPage *page = buffer_pool.get_buffer_page(physical_block, file_path, GetCurrentProcessId());
    if (page != nullptr)
    {
      memcpy(block_buf, page->data, BLOCK_SIZE);
      buffer_pool.release_buffer_page(physical_block, false);
    }
    else
    {
      disk.read_block(physical_block, block_buf);
    }

    // 计算当前块的有效内容长度（避免超出文件真实长度）
    // 先将BLOCK_SIZE转为size_t，保证类型统一
    size_t block_size_st = static_cast<size_t>(BLOCK_SIZE);
    // 三元表达式实现“取较小值”（等价于std::min）
    size_t block_valid_len = (block_size_st < (total_valid_len - read_len)) ? block_size_st : (total_valid_len - read_len);
    if (block_valid_len > 0)
    {
      full_content.append(block_buf, block_valid_len);
      read_len += block_valid_len;
    }
  }

  // 5. 过滤UTF-8乱码（修复截断的多字节字符）
  // 检查UTF-8编码有效性，移除末尾不完整的字符
  size_t len = full_content.size();
  while (len > 0)
  {
    unsigned char c = static_cast<unsigned char>(full_content[len - 1]);
    // UTF-8编码规则：
    // 0xxxxxxx (单字节) | 110xxxxx 10xxxxxx (双字节) | 1110xxxx 10xxxxxx 10xxxxxx (三字节)
    if (c < 0x80)
      break; // 单字节，有效
    else if ((c & 0xE0) == 0xC0)
    { // 双字节开头，需要前1个字符
      if (len >= 2)
        break;
      else
        len--;
    }
    else if ((c & 0xF0) == 0xE0)
    { // 三字节开头（中文），需要前2个字符
      if (len >= 3)
        break;
      else
        len--;
    }
    else
    { // 无效字节，移除
      len--;
    }
  }
  full_content.resize(len);

  return full_content;
}

// std::string FileInterface::read_file(const std::string &file_path)
// {
//   // 1. 检查文件是否存在
//   FCB target_fcb;
//   if (!directory.query_file(file_path, target_fcb))
//   {
//     std::cerr << "[ERROR] 读取文件失败：" << file_path << " 不存在\n";
//     return "";
//   }

//   // 2. 权限检查
//   if (!check_permission(target_fcb.permission, FilePermission::File_READ))
//   {
//     std::cerr << "[ERROR] 读取文件失败：无读权限\n";
//     return "";
//   }

//   // 3. 获取文件所有物理块
//   std::vector<int> physical_blocks = get_file_all_blocks(file_path);
//   if (physical_blocks.empty())
//   {
//     std::cerr << "[WARNING] 文件" << file_path << "无内容\n";
//     return "";
//   }

//   // 4. 逐块读取（按真实内容长度拼接）
//   std::string full_content;
//   size_t total_valid_len = static_cast<size_t>(target_fcb.file_size); // 文件真实字节数（GBK）
//   size_t read_len = 0;

//   for (size_t i = 0; i < physical_blocks.size() && read_len < total_valid_len; ++i)
//   {
//     char block_buf[BLOCK_SIZE] = {0};
//     int physical_block = physical_blocks[i];

//     // 从缓冲池/磁盘读取块
//     BufferPage *page = buffer_pool.get_buffer_page(physical_block, file_path, GetCurrentProcessId());
//     if (page != nullptr)
//     {
//       memcpy(block_buf, page->data, BLOCK_SIZE);
//       buffer_pool.release_buffer_page(physical_block, false);
//     }
//     else
//     {
//       disk.read_block(physical_block, block_buf);
//     }

//     // 计算当前块的有效内容长度（避免超出文件真实长度）
//     size_t block_size_st = static_cast<size_t>(BLOCK_SIZE);
//     size_t block_valid_len = (block_size_st < (total_valid_len - read_len)) ? block_size_st : (total_valid_len - read_len);
//     if (block_valid_len > 0)
//     {
//       full_content.append(block_buf, block_valid_len);
//       read_len += block_valid_len;
//     }
//   }

//   // ========== 移除原有的UTF-8过滤逻辑 ==========
//   // 直接返回GBK内容（终端是GBK编码，可正常显示）
//   return full_content;
// }

// 4. 删除整个文件
bool FileInterface::delete_file(const std::string &file_path, pid_t pid)
{
  FCB target_fcb;
  if (!directory.query_file(file_path, target_fcb))
  {
    std::cerr << "[ERROR] 删除文件失败：" << file_path << " 不存在\n";
    return false;
  }

  // 文件保护：检查是否被其他进程占用
  if (target_fcb.is_in_use && target_fcb.holder_pid != pid)
  {
    std::cerr << "[ERROR] 删除文件失败：文件被进程" << target_fcb.holder_pid << "锁定\n";
    return false;
  }

  // 解锁当前进程锁定的文件
  if (target_fcb.is_in_use && target_fcb.holder_pid == pid)
  {
    directory.unlock_file(file_path);
  }

  // 移除FCB（调用用户实现的remove_file）
  if (!directory.remove_file(file_path))
  {
    std::cerr << "[ERROR] 删除文件失败：目录移除FCB失败\n";
    return false;
  }

  // 释放FAT块
  if (!fat_table.release_file_blocks(target_fcb.start_block))
  {
    directory.add_file(file_path, target_fcb); // 回滚
    std::cerr << "[ERROR] 删除文件失败：FAT表释放块失败\n";
    return false;
  }

  directory.write_dir_to_disk();
  std::cout << "[INFO] 文件删除成功：" << file_path << "，释放块数：" << target_fcb.total_blocks << "\n";
  return true;
}

// ===================== 新增块管理方法 =====================
// 5. 获取文件所有物理块列表（操作前查看文件块分布）
std::vector<int> FileInterface::get_file_all_blocks(const std::string &file_path)
{
  std::vector<int> block_list;

  // 步骤1：获取文件FCB（调用用户实现的query_file）
  FCB target_fcb;
  if (!directory.query_file(file_path, target_fcb))
  {
    std::cerr << "[ERROR] 获取文件块失败：" << file_path << " 不存在\n";
    return block_list;
  }

  // 步骤2：校验文件是否分配了块
  int start_block = target_fcb.start_block;
  if (start_block == -1 || start_block == (int)FATEntryType::FREE)
  {
    std::cout << "[INFO] 文件" << file_path << "尚未分配任何磁盘块\n";
    return block_list;
  }

  // 步骤3：遍历FAT表链，收集所有物理块
  int current_block = start_block;
  while (true)
  {
    block_list.push_back(current_block);
    int next_block = fat_table.get_next_block(current_block);
    // 终止条件：块链结束或块空闲
    if (next_block == (int)FATEntryType::END || next_block == (int)FATEntryType::FREE)
    {
      break;
    }
    current_block = next_block;
  }

  // 输出块信息（方便用户查看）
  std::cout << "[INFO] 文件" << file_path << "的块信息：\n";
  std::cout << "  - 总块数：" << block_list.size() << "\n";
  std::cout << "  - 物理块列表（逻辑块映射）：";
  for (int i = 0; i < block_list.size(); ++i)
  {
    std::cout << block_list[i] << "（逻辑块" << i << "）";
    if (i != block_list.size() - 1)
      std::cout << " → ";
  }
  std::cout << "\n";

  return block_list;
}

// 6. 截断文件到指定逻辑块（删除后续所有块）
bool FileInterface::truncate_file(const std::string &file_path, int max_logical_block)
{
  // 步骤1：获取文件FCB
  FCB target_fcb;
  if (!directory.query_file(file_path, target_fcb))
  {
    std::cerr << "[ERROR] 截断文件失败：" << file_path << " 不存在\n";
    return false;
  }

  // 步骤2：获取文件所有物理块
  std::vector<int> all_blocks = get_file_all_blocks(file_path);
  if (max_logical_block >= all_blocks.size())
  {
    std::cerr << "[ERROR] 截断文件失败：逻辑块号" << max_logical_block << "超出范围（文件仅" << all_blocks.size() << "块）\n";
    return false;
  }

  // 步骤3：锁定文件（防止并发修改）
  pid_t current_pid = GetCurrentProcessId();
  if (!directory.lock_file(file_path, current_pid))
  {
    std::cerr << "[ERROR] 截断文件失败：文件被其他进程锁定\n";
    return false;
  }

  // 步骤4：释放指定块之后的所有物理块
  bool release_success = true;
  for (int i = max_logical_block + 1; i < all_blocks.size(); ++i)
  {
    if (!fat_table.release_file_blocks(all_blocks[i]))
    {
      release_success = false;
      break;
    }
  }

  if (!release_success)
  {
    directory.unlock_file(file_path);
    std::cerr << "[ERROR] 截断文件失败：释放后续块失败\n";
    return false;
  }

  // 步骤5：更新FAT表，标记截断后的最后一块为结束
  int last_block = all_blocks[max_logical_block];
  fat_table.update_fat_entry(last_block, (int)FATEntryType::END);

  // 步骤6：更新FCB+解锁文件
  target_fcb.total_blocks = max_logical_block + 1;
  directory.add_file(file_path, target_fcb); // 更新FCB
  directory.unlock_file(file_path);
  directory.write_dir_to_disk();

  std::cout << "[INFO] 文件截断成功：" << file_path << " 已截断到逻辑块" << max_logical_block << "，释放块数：" << (all_blocks.size() - max_logical_block - 1) << "\n";
  return true;
}

// ===================== 目录操作封装 =====================
// 7. 创建目录
bool FileInterface::create_directory(const std::string &dir_path)
{
  bool res = directory.create_directory(dir_path);
  if (res)
    std::cout << "[INFO] 目录创建成功：" << dir_path << "\n";
  else
    std::cerr << "[ERROR] 目录创建失败：路径已存在或解析错误\n";
  return res;
}

// 8. 删除目录
bool FileInterface::delete_directory(const std::string &dir_path, bool force)
{
  bool res = directory.delete_directory(dir_path, force);
  if (res)
    std::cout << "[INFO] 目录删除成功：" << dir_path << "\n";
  else
    std::cerr << "[ERROR] 目录删除失败：非空/不存在/被占用\n";
  return res;
}

// 9. 切换工作目录
bool FileInterface::change_directory(const std::string &dir_path)
{
  bool res = directory.change_directory(dir_path);
  if (res)
    std::cout << "[INFO] 切换目录成功：" << directory.get_current_work_dir_path() << "\n";
  else
    std::cerr << "[ERROR] 切换目录失败：路径不存在\n";
  return res;
}

// 10. 回退到父目录
bool FileInterface::cd_back()
{
  return change_directory("..");
}

// 11. 查询目录内容
std::string FileInterface::query_directory(const std::string &dir_path)
{
  std::string content = directory.query_directory(dir_path);
  if (content.empty())
    std::cerr << "[WARNING] 目录内容为空：路径不存在/无内容\n";
  return content;
}

// 12. 获取当前工作目录路径
std::string FileInterface::get_current_work_dir()
{
  return directory.get_current_work_dir_path();
}

// 13. 获取父目录路径
std::string FileInterface::get_parent_dir_path()
{
  return directory.get_parent_dir_path();
}

// 14. 判断是否在根目录
bool FileInterface::is_in_root_dir()
{
  return directory.is_current_dir_root();
}

// #include "file_interface.hpp"
// #include <iostream>
// #include <sstream>
// #include <algorithm>
// #include <cstring>
// #include <windows.h>
// #include <ctime>
// #include <cmath>

// // ========== 新增：UTF-8转GBK函数（仅用于终端显示） ==========
// std::string utf8_to_gbk(const std::string &utf8_str)
// {
//   int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), -1, NULL, 0);
//   if (wlen == 0)
//     return "";
//   wchar_t *wstr = new wchar_t[wlen];
//   MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), -1, wstr, wlen);

//   int glen = WideCharToMultiByte(CP_ACP, 0, wstr, -1, NULL, 0, NULL, NULL);
//   if (glen == 0)
//   {
//     delete[] wstr;
//     return "";
//   }
//   char *gbk_str = new char[glen];
//   WideCharToMultiByte(CP_ACP, 0, wstr, -1, gbk_str, glen, NULL, NULL);

//   std::string result(gbk_str);
//   delete[] wstr;
//   delete[] gbk_str;
//   return result;
// }

// #define PRINT_GBK(utf8_str) std::cout << utf8_to_gbk(utf8_str)

// // 权限检查辅助函数（适配common.hpp的FilePermission枚举）
// bool FileInterface::check_permission(FilePermission fcb_perm, FilePermission required_perm)
// {
//   return (fcb_perm & required_perm) == required_perm;
// }

// // ===================== 构造函数 =====================
// FileInterface::FileInterface(Disk &d, FATTable &ft, Directory &dir, BufferPool &bp)
//     : disk(d), fat_table(ft), directory(dir), buffer_pool(bp) {}

// // ===================== 核心文件操作 =====================
// // 1. 创建文件（修复：正确记录file_size）
// bool FileInterface::create_file(const std::string &file_path, FilePermission perm, const std::string &content)
// {
//   // 步骤1：校验文件是否已存在（调用用户实现的query_file）
//   FCB exist_fcb;
//   if (directory.query_file(file_path, exist_fcb))
//   {
//     std::cerr << "[ERROR] 创建文件失败：" << file_path << " 已存在\n";
//     return false;
//   }

//   // 步骤2：从FAT表分配空闲盘块
//   int first_block = fat_table.allocate_free_block();
//   if (first_block == -1)
//   {
//     std::cerr << "[ERROR] 创建文件失败：磁盘无空闲块\n";
//     return false;
//   }

//   // 步骤3：构造FCB（严格对齐用户实现的FCB结构）
//   FCB new_fcb;
//   std::vector<std::string> path_parts = split_path(file_path); // common.hpp的工具函数
//   new_fcb.filename = path_parts.empty() ? "" : path_parts.back();
//   new_fcb.permission = perm;
//   new_fcb.create_time = get_current_time(); // common.hpp的工具函数
//   new_fcb.start_block = first_block;
//   new_fcb.total_blocks = 1;
//   new_fcb.is_in_use = false;
//   new_fcb.holder_pid = 0;
//   // 关键修复：记录初始内容的字节数
//   new_fcb.file_size = static_cast<int>(content.size());

//   // 步骤4：添加FCB到目录（调用用户实现的add_file）
//   if (!directory.add_file(file_path, new_fcb))
//   {
//     fat_table.release_file_blocks(first_block); // 回滚：释放已分配块
//     std::cerr << "[ERROR] 创建文件失败：目录添加FCB失败\n";
//     return false;
//   }

//   // 步骤5：处理初始内容（对齐BLOCK_SIZE，改用data()避免截断）
//   char write_buf[BLOCK_SIZE] = {0};
//   size_t content_size = content.size();
//   // size_t write_len = std::min(content_size, static_cast<size_t>(BLOCK_SIZE));
//   size_t write_len = content_size < static_cast<size_t>(BLOCK_SIZE) ? content_size : static_cast<size_t>(BLOCK_SIZE);
//   // 修复：用data()替代c_str()，避免\0截断
//   memcpy(write_buf, content.data(), write_len);

//   // 步骤6：写入缓冲池/磁盘
//   pid_t current_pid = GetCurrentProcessId();
//   BufferPage *page = buffer_pool.get_buffer_page(first_block, file_path, current_pid);
//   if (page != nullptr)
//   {
//     memcpy(page->data, write_buf, BLOCK_SIZE);
//     page->status = BufferPageStatus::DIRTY;
//     buffer_pool.release_buffer_page(first_block, true);
//   }
//   else
//   {
//     // 缓冲池置换失败时直接写磁盘
//     if (!disk.write_block(first_block, write_buf))
//     {
//       directory.remove_file(file_path);
//       fat_table.release_file_blocks(first_block);
//       std::cerr << "[ERROR] 创建文件失败：磁盘写入失败\n";
//       return false;
//     }
//   }

//   // 步骤7：更新FAT表+持久化目录
//   fat_table.update_fat_entry(first_block, (int)FATEntryType::END);
//   directory.write_dir_to_disk();

//   std::cout << "[INFO] 文件创建成功：" << file_path << "，分配块号：" << first_block
//             << "，初始内容大小：" << content.size() << "字节\n";
//   return true;
// }

// // 2. 查看文件指定逻辑块内容（修复：避免空字符截断）
// std::string FileInterface::view_file_block(const std::string &file_path, int block_num)
// {
//   // 步骤1：获取FCB（调用用户实现的query_file）
//   FCB target_fcb;
//   if (!directory.query_file(file_path, target_fcb))
//   {
//     std::cerr << "[ERROR] 查看文件块失败：" << file_path << " 不存在\n";
//     return "";
//   }

//   // 步骤2：权限检查
//   if (!check_permission(target_fcb.permission, FilePermission::File_READ))
//   {
//     std::cerr << "[ERROR] 查看文件块失败：无读权限\n";
//     return "";
//   }

//   // 步骤3：通过FAT表查找物理块
//   int physical_block = target_fcb.start_block;
//   for (int i = 0; i < block_num; ++i)
//   {
//     physical_block = fat_table.get_next_block(physical_block);
//     if (physical_block == (int)FATEntryType::END || physical_block == (int)FATEntryType::FREE)
//     {
//       std::cerr << "[ERROR] 查看文件块失败：块号" << block_num << "超出范围\n";
//       return "";
//     }
//   }

//   // 步骤4：优先从缓冲池读取
//   pid_t current_pid = GetCurrentProcessId();
//   BufferPage *page = buffer_pool.get_buffer_page(physical_block, file_path, current_pid);
//   char read_buf[BLOCK_SIZE] = {0};

//   if (page != nullptr)
//   {
//     memcpy(read_buf, page->data, BLOCK_SIZE);
//     page->last_access_time = time(nullptr);
//     buffer_pool.release_buffer_page(physical_block, false);
//     std::cout << "[INFO] 从缓冲池读取块：" << physical_block << "\n";
//   }
//   else
//   {
//     if (!disk.read_block(physical_block, read_buf))
//     {
//       std::cerr << "[ERROR] 查看文件块失败：磁盘读取失败\n";
//       return "";
//     }
//     // 缓存到缓冲池
//     page = buffer_pool.get_buffer_page(physical_block, file_path, current_pid);
//     if (page != nullptr)
//     {
//       memcpy(page->data, read_buf, BLOCK_SIZE);
//       page->status = BufferPageStatus::CLEAN;
//       buffer_pool.release_buffer_page(physical_block, false);
//     }
//     std::cout << "[INFO] 从磁盘读取块：" << physical_block << "\n";
//   }

//   // 修复：按文件真实大小截取，避免空字符截断
//   size_t valid_len = 0;
//   // 计算当前块的有效长度
//   size_t block_start = static_cast<size_t>(block_num) * BLOCK_SIZE;
//   if (block_start < static_cast<size_t>(target_fcb.file_size))
//   {
//     // valid_len = std::min(static_cast<size_t>(BLOCK_SIZE),
//     // static_cast<size_t>(target_fcb.file_size) - block_start);
//     valid_len = static_cast<size_t>(BLOCK_SIZE) < (static_cast<size_t>(target_fcb.file_size) - block_start) ? static_cast<size_t>(BLOCK_SIZE) : (static_cast<size_t>(target_fcb.file_size) - block_start);
//   }
//   // 按有效长度构造字符串，而非直接用read_buf（避免\0截断）
//   return std::string(read_buf, valid_len);
// }

// // 3. 修改文件指定逻辑块内容（修复：改用data()，统一size_t）
// bool FileInterface::modify_file_block(const std::string &file_path, int block_num, const std::string &new_content)
// {
//   // 步骤1：获取FCB
//   FCB target_fcb;
//   if (!directory.query_file(file_path, target_fcb))
//   {
//     std::cerr << "[ERROR] 修改文件块失败：文件不存在\n";
//     return false;
//   }

//   // 步骤2：权限检查
//   if (!check_permission(target_fcb.permission, FilePermission::File_WRITE))
//   {
//     std::cerr << "[ERROR] 修改文件块失败：无写权限\n";
//     return false;
//   }

//   // 步骤3：锁定文件（调用用户实现的lock_file）
//   pid_t current_pid = GetCurrentProcessId();
//   if (!directory.lock_file(file_path, current_pid))
//   {
//     std::cerr << "[ERROR] 修改文件块失败：文件被其他进程锁定\n";
//     return false;
//   }

//   // 步骤4：查找物理块
//   int physical_block = target_fcb.start_block;
//   for (int i = 0; i < block_num; ++i)
//   {
//     physical_block = fat_table.get_next_block(physical_block);
//     // 若块号超出范围，先解锁再返回
//     if (physical_block == (int)FATEntryType::END || physical_block == (int)FATEntryType::FREE)
//     {
//       directory.unlock_file(file_path);
//       std::cerr << "[ERROR] 修改文件块失败：块号" << block_num << "超出范围\n";
//       return false;
//     }
//   }

//   // 步骤5：处理新内容（统一size_t，改用data()）
//   char modify_buf[BLOCK_SIZE] = {0};
//   size_t new_content_size = new_content.size();
//   // size_t modify_len = std::min(new_content_size, static_cast<size_t>(BLOCK_SIZE));
//   size_t modify_len = new_content_size < static_cast<size_t>(BLOCK_SIZE) ? new_content_size : static_cast<size_t>(BLOCK_SIZE);
//   memcpy(modify_buf, new_content.data(), modify_len); // 修复：用data()

//   // 步骤6：写入缓冲池/磁盘
//   BufferPage *page = buffer_pool.get_buffer_page(physical_block, file_path, current_pid);
//   bool write_success = false;
//   if (page != nullptr)
//   {
//     memcpy(page->data, modify_buf, BLOCK_SIZE);
//     page->status = BufferPageStatus::DIRTY;
//     page->last_access_time = time(nullptr);
//     write_success = buffer_pool.release_buffer_page(physical_block, true);
//   }
//   else
//   {
//     write_success = disk.write_block(physical_block, modify_buf);
//   }

//   if (!write_success)
//   {
//     directory.unlock_file(file_path);
//     std::cerr << "[ERROR] 修改文件块失败：写入失败\n";
//     return false;
//   }

//   // 步骤7：解锁文件+更新FCB
//   directory.unlock_file(file_path);
//   // 修复：统一size_t计算
//   size_t new_total_blocks = static_cast<size_t>(block_num) + 1;
//   if (new_total_blocks > static_cast<size_t>(target_fcb.total_blocks))
//   {
//     target_fcb.total_blocks = static_cast<int>(new_total_blocks);
//   }
//   // 修复：更新文件总大小
//   size_t file_total_size = block_num * BLOCK_SIZE + new_content_size;
//   target_fcb.file_size = static_cast<int>(file_total_size);

//   directory.add_file(file_path, target_fcb); // 更新FCB
//   directory.write_dir_to_disk();

//   std::cout << "[INFO] 文件块修改成功：" << file_path << "（逻辑块：" << block_num << "）\n";
//   return true;
// }

// // 4. 写入文件（统一UTF-8，修复编码混乱，改用data()）
// bool FileInterface::write_file(const std::string &file_path, const std::string &content)
// {
//   // 1. 检查文件是否存在
//   FCB target_fcb;
//   if (!directory.query_file(file_path, target_fcb))
//   {
//     std::cerr << "[ERROR] 写入文件失败：" << file_path << " 不存在（请先创建文件）\n";
//     return false;
//   }

//   // 2. 权限检查（需要写权限）
//   if (!check_permission(target_fcb.permission, FilePermission::File_WRITE))
//   {
//     std::cerr << "[ERROR] 写入文件失败：无写权限\n";
//     return false;
//   }

//   // 3. 锁定文件
//   pid_t current_pid = GetCurrentProcessId();
//   if (!directory.lock_file(file_path, current_pid))
//   {
//     std::cerr << "[ERROR] 写入文件失败：文件被其他进程锁定\n";
//     return false;
//   }

//   // 4. 计算需要的块数（精准按字节数计算，统一size_t）
//   size_t content_len = content.size();
//   size_t block_size = static_cast<size_t>(BLOCK_SIZE);
//   size_t required_blocks = content_len == 0 ? 1 : (content_len + block_size - 1) / block_size;
//   std::vector<int> existing_blocks = get_file_all_blocks(file_path);
//   size_t existing_block_count = existing_blocks.size();

//   // 5. 分配新块（仅当现有块不足时分配）
//   if (required_blocks > existing_block_count)
//   {
//     size_t need_more = required_blocks - existing_block_count;
//     int last_block = existing_block_count > 0 ? existing_blocks.back() : -1;

//     for (size_t i = 0; i < need_more; ++i)
//     {
//       int new_block = fat_table.allocate_free_block();
//       if (new_block == -1)
//       {
//         directory.unlock_file(file_path);
//         std::cerr << "[ERROR] 写入文件失败：磁盘无空闲块\n";
//         return false;
//       }

//       // 链接到FAT链末尾
//       if (last_block != -1)
//       {
//         fat_table.update_fat_entry(last_block, new_block);
//       }
//       else
//       {
//         target_fcb.start_block = new_block;
//       }
//       existing_blocks.push_back(new_block);
//       last_block = new_block;
//     }

//     // 标记最后一块为链结束
//     if (!existing_blocks.empty())
//     {
//       fat_table.update_fat_entry(existing_blocks.back(), static_cast<int>(FATEntryType::END));
//     }
//   }

//   // 6. 分块写入内容（核心：精准按字节拆分，改用data()）
//   for (size_t i = 0; i < required_blocks; ++i)
//   {
//     char write_buf[BLOCK_SIZE] = {0}; // 每次清空缓冲区
//     size_t start = i * block_size;
//     size_t end = (start + block_size < content_len) ? (start + block_size) : content_len;
//     size_t block_content_len = end - start;

//     // 精准拷贝当前块的有效内容（按字节数，改用data()）
//     if (block_content_len > 0)
//     {
//       memcpy(write_buf, content.data() + start, block_content_len);
//     }

//     // 写入缓冲池/磁盘
//     int physical_block = existing_blocks[i];
//     BufferPage *page = buffer_pool.get_buffer_page(physical_block, file_path, current_pid);
//     if (page != nullptr)
//     {
//       memcpy(page->data, write_buf, BLOCK_SIZE);
//       page->status = BufferPageStatus::DIRTY;
//       buffer_pool.release_buffer_page(physical_block, true);
//     }
//     else
//     {
//       disk.write_block(physical_block, write_buf);
//     }
//   }

//   // 7. 清理多余块
//   if (existing_block_count > required_blocks)
//   {
//     for (size_t i = required_blocks; i < existing_block_count; ++i)
//     {
//       fat_table.release_file_blocks(existing_blocks[i]);
//     }
//     fat_table.update_fat_entry(existing_blocks[required_blocks - 1], static_cast<int>(FATEntryType::END));
//   }

//   // 8. 更新FCB（记录真实内容长度）
//   target_fcb.total_blocks = static_cast<int>(required_blocks);
//   target_fcb.file_size = static_cast<int>(content_len); // 记录UTF-8字节数
//   directory.update_file_fcb(file_path, target_fcb);

//   // 9. 解锁文件+持久化
//   directory.unlock_file(file_path);
//   buffer_pool.write_back_all_dirty_pages();

//   std::cout << "[INFO] 文件写入成功：" << file_path
//             << "（块大小：" << block_size << "B，占用块数：" << required_blocks
//             << "，内容大小：" << content_len << "字节）\n";
//   return true;
// }

// // 5. 读取文件（修复UTF-8校验逻辑，统一UTF-8，终端显示时转GBK）
// std::string FileInterface::read_file(const std::string &file_path)
// {
//   // 1. 检查文件是否存在
//   FCB target_fcb;
//   if (!directory.query_file(file_path, target_fcb))
//   {
//     std::cerr << "[ERROR] 读取文件失败：" << file_path << " 不存在\n";
//     return "";
//   }

//   // 2. 权限检查
//   if (!check_permission(target_fcb.permission, FilePermission::File_READ))
//   {
//     std::cerr << "[ERROR] 读取文件失败：无读权限\n";
//     return "";
//   }

//   // 3. 获取文件所有物理块
//   std::vector<int> physical_blocks = get_file_all_blocks(file_path);
//   if (physical_blocks.empty())
//   {
//     std::cerr << "[WARNING] 文件" << file_path << "无内容\n";
//     return "";
//   }

//   // 4. 逐块读取（按真实内容长度拼接，改用data()）
//   std::string full_content;
//   size_t total_valid_len = static_cast<size_t>(target_fcb.file_size); // 文件真实字节数
//   size_t read_len = 0;
//   size_t block_size = static_cast<size_t>(BLOCK_SIZE);

//   for (size_t i = 0; i < physical_blocks.size() && read_len < total_valid_len; ++i)
//   {
//     char block_buf[BLOCK_SIZE] = {0};
//     int physical_block = physical_blocks[i];

//     // 从缓冲池/磁盘读取块
//     BufferPage *page = buffer_pool.get_buffer_page(physical_block, file_path, GetCurrentProcessId());
//     if (page != nullptr)
//     {
//       memcpy(block_buf, page->data, BLOCK_SIZE);
//       buffer_pool.release_buffer_page(physical_block, false);
//     }
//     else
//     {
//       disk.read_block(physical_block, block_buf);
//     }

//     // 计算当前块的有效内容长度（避免超出文件真实长度）
//     // size_t block_valid_len = std::min(block_size, total_valid_len - read_len);
//     size_t block_valid_len = block_size < (total_valid_len - read_len) ? block_size : (total_valid_len - read_len);
//     if (block_valid_len > 0)
//     {
//       // 修复：按字节append，避免string构造时的\0截断
//       full_content.append(block_buf, block_valid_len);
//       read_len += block_valid_len;
//     }
//   }

//   // 5. 修复UTF-8校验逻辑（正确处理3字节中文）
//   // 仅移除末尾不完整的UTF-8字符，不错误截断有效字符
//   size_t len = full_content.size();
//   while (len > 0)
//   {
//     unsigned char c = static_cast<unsigned char>(full_content[len - 1]);
//     // UTF-8编码规则：
//     if (c < 0x80)
//     {
//       // 单字节（ASCII），有效
//       break;
//     }
//     else if ((c & 0xC0) == 0x80)
//     {
//       // 续字节（10xxxxxx），需要向前找首字节
//       len--;
//     }
//     else if ((c & 0xE0) == 0xC0)
//     {
//       // 双字节首字符（110xxxxx），需要至少2字节
//       if (len >= 2)
//         break;
//       else
//         len--;
//     }
//     else if ((c & 0xF0) == 0xE0)
//     {
//       // 三字节首字符（1110xxxx，中文），需要至少3字节
//       if (len >= 3)
//         break;
//       else
//         len--;
//     }
//     else if ((c & 0xF8) == 0xF0)
//     {
//       // 四字节首字符，需要至少4字节
//       if (len >= 4)
//         break;
//       else
//         len--;
//     }
//     else
//     {
//       // 无效字节
//       len--;
//     }
//   }
//   full_content.resize(len);

//   // 读取的是UTF-8内容，终端显示时再转GBK，这里返回原始UTF-8
//   return full_content;
// }

// // 6. 删除整个文件
// bool FileInterface::delete_file(const std::string &file_path, pid_t pid)
// {
//   FCB target_fcb;
//   if (!directory.query_file(file_path, target_fcb))
//   {
//     std::cerr << "[ERROR] 删除文件失败：" << file_path << " 不存在\n";
//     return false;
//   }

//   // 文件保护：检查是否被其他进程占用
//   if (target_fcb.is_in_use && target_fcb.holder_pid != pid)
//   {
//     std::cerr << "[ERROR] 删除文件失败：文件被进程" << target_fcb.holder_pid << "锁定\n";
//     return false;
//   }

//   // 解锁当前进程锁定的文件
//   if (target_fcb.is_in_use && target_fcb.holder_pid == pid)
//   {
//     directory.unlock_file(file_path);
//   }

//   // 移除FCB（调用用户实现的remove_file）
//   if (!directory.remove_file(file_path))
//   {
//     std::cerr << "[ERROR] 删除文件失败：目录移除FCB失败\n";
//     return false;
//   }

//   // 释放FAT块
//   if (!fat_table.release_file_blocks(target_fcb.start_block))
//   {
//     directory.add_file(file_path, target_fcb); // 回滚
//     std::cerr << "[ERROR] 删除文件失败：FAT表释放块失败\n";
//     return false;
//   }

//   directory.write_dir_to_disk();
//   std::cout << "[INFO] 文件删除成功：" << file_path << "，释放块数：" << target_fcb.total_blocks << "\n";
//   return true;
// }

// // ===================== 新增块管理方法 =====================
// // 7. 获取文件所有物理块列表（操作前查看文件块分布）
// std::vector<int> FileInterface::get_file_all_blocks(const std::string &file_path)
// {
//   std::vector<int> block_list;

//   // 步骤1：获取文件FCB（调用用户实现的query_file）
//   FCB target_fcb;
//   if (!directory.query_file(file_path, target_fcb))
//   {
//     std::cerr << "[ERROR] 获取文件块失败：" << file_path << " 不存在\n";
//     return block_list;
//   }

//   // 步骤2：校验文件是否分配了块
//   int start_block = target_fcb.start_block;
//   if (start_block == -1 || start_block == (int)FATEntryType::FREE)
//   {
//     std::cout << "[INFO] 文件" << file_path << "尚未分配任何磁盘块\n";
//     return block_list;
//   }

//   // 步骤3：遍历FAT表链，收集所有物理块
//   int current_block = start_block;
//   while (true)
//   {
//     block_list.push_back(current_block);
//     int next_block = fat_table.get_next_block(current_block);
//     // 终止条件：块链结束或块空闲
//     if (next_block == (int)FATEntryType::END || next_block == (int)FATEntryType::FREE)
//     {
//       break;
//     }
//     current_block = next_block;
//   }

//   // 输出块信息（方便用户查看）
//   std::cout << "[INFO] 文件" << file_path << "的块信息：\n";
//   std::cout << "  - 总块数：" << block_list.size() << "\n";
//   std::cout << "  - 物理块列表（逻辑块映射）：";
//   for (size_t i = 0; i < block_list.size(); ++i)
//   {
//     std::cout << block_list[i] << "（逻辑块" << i << "）";
//     if (i != block_list.size() - 1)
//       std::cout << " → ";
//   }
//   std::cout << "\n";

//   return block_list;
// }

// // 8. 截断文件到指定逻辑块（删除后续所有块）
// bool FileInterface::truncate_file(const std::string &file_path, int max_logical_block)
// {
//   // 步骤1：获取文件FCB
//   FCB target_fcb;
//   if (!directory.query_file(file_path, target_fcb))
//   {
//     std::cerr << "[ERROR] 截断文件失败：" << file_path << " 不存在\n";
//     return false;
//   }

//   // 步骤2：获取文件所有物理块
//   std::vector<int> all_blocks = get_file_all_blocks(file_path);
//   if (max_logical_block >= static_cast<int>(all_blocks.size()))
//   {
//     std::cerr << "[ERROR] 截断文件失败：逻辑块号" << max_logical_block << "超出范围（文件仅" << all_blocks.size() << "块）\n";
//     return false;
//   }

//   // 步骤3：锁定文件（防止并发修改）
//   pid_t current_pid = GetCurrentProcessId();
//   if (!directory.lock_file(file_path, current_pid))
//   {
//     std::cerr << "[ERROR] 截断文件失败：文件被其他进程锁定\n";
//     return false;
//   }

//   // 步骤4：释放指定块之后的所有物理块
//   bool release_success = true;
//   for (int i = max_logical_block + 1; i < static_cast<int>(all_blocks.size()); ++i)
//   {
//     if (!fat_table.release_file_blocks(all_blocks[i]))
//     {
//       release_success = false;
//       break;
//     }
//   }

//   if (!release_success)
//   {
//     directory.unlock_file(file_path);
//     std::cerr << "[ERROR] 截断文件失败：释放后续块失败\n";
//     return false;
//   }

//   // 步骤5：更新FAT表，标记截断后的最后一块为结束
//   int last_block = all_blocks[max_logical_block];
//   fat_table.update_fat_entry(last_block, (int)FATEntryType::END);

//   // 步骤6：更新FCB+解锁文件
//   target_fcb.total_blocks = max_logical_block + 1;
//   // 修复：更新截断后的文件大小
//   target_fcb.file_size = static_cast<int>((max_logical_block + 1) * BLOCK_SIZE);
//   directory.add_file(file_path, target_fcb); // 更新FCB
//   directory.unlock_file(file_path);
//   directory.write_dir_to_disk();

//   std::cout << "[INFO] 文件截断成功：" << file_path << " 已截断到逻辑块" << max_logical_block << "，释放块数：" << (all_blocks.size() - max_logical_block - 1) << "\n";
//   return true;
// }

// // ===================== 目录操作封装 =====================
// // 9. 创建目录
// bool FileInterface::create_directory(const std::string &dir_path)
// {
//   bool res = directory.create_directory(dir_path);
//   if (res)
//     std::cout << "[INFO] 目录创建成功：" << dir_path << "\n";
//   else
//     std::cerr << "[ERROR] 目录创建失败：路径已存在或解析错误\n";
//   return res;
// }

// // 10. 删除目录
// bool FileInterface::delete_directory(const std::string &dir_path, bool force)
// {
//   bool res = directory.delete_directory(dir_path, force);
//   if (res)
//     std::cout << "[INFO] 目录删除成功：" << dir_path << "\n";
//   else
//     std::cerr << "[ERROR] 目录删除失败：非空/不存在/被占用\n";
//   return res;
// }

// // 11. 切换工作目录
// bool FileInterface::change_directory(const std::string &dir_path)
// {
//   bool res = directory.change_directory(dir_path);
//   if (res)
//     std::cout << "[INFO] 切换目录成功：" << directory.get_current_work_dir_path() << "\n";
//   else
//     std::cerr << "[ERROR] 切换目录失败：路径不存在\n";
//   return res;
// }

// // 12. 回退到父目录
// bool FileInterface::cd_back()
// {
//   return change_directory("..");
// }

// // 13. 查询目录内容
// std::string FileInterface::query_directory(const std::string &dir_path)
// {
//   std::string content = directory.query_directory(dir_path);
//   if (content.empty())
//     std::cerr << "[WARNING] 目录内容为空：路径不存在/无内容\n";
//   return content;
// }

// // 14. 获取当前工作目录路径
// std::string FileInterface::get_current_work_dir()
// {
//   return directory.get_current_work_dir_path();
// }

// // 15. 获取父目录路径
// std::string FileInterface::get_parent_dir_path()
// {
//   return directory.get_parent_dir_path();
// }

// // 16. 判断是否在根目录
// bool FileInterface::is_in_root_dir()
// {
//   return directory.is_current_dir_root();
// }