// test_my_file_operation.cpp
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

// 1. 引入你不负责的底层 Mock 模块
#include "../mock/common_mock.hpp"
#include "../mock/sync_mock.hpp"
#include "../mock/buffer_pool_mock.hpp"
#include "../mock/fat_table_file_mock.hpp"
#include "../mock/buffer_pool_mock.hpp"

// 2. 引入你自己实现的代码（关键！测试的核心对象）
#include "backend/fs_core/directory.hpp"      // 你自己写的目录操作
#include "backend/fs_core/file_interface.hpp" // 你自己写的文件操作接口

// 测试工具函数：打印结果
void print_test_result(const std::string &test_name, bool success)
{
  std::cout << "[" << (success ? "√ 成功" : "× 失败") << "] " << test_name << "\n";
}

int main()
{
  // Windows 控制台编码适配
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  std::cout << "===== 测试【自己编写】的文件操作接口 =====\n\n";

  // ========== 步骤1：初始化 Mock 底层依赖（别人的模块） ==========
  std::cout << "1. 初始化底层 Mock 模块...\n";
  Disk disk("./my_disk.bin");
  bool disk_ok = disk.init_disk();
  print_test_result("磁盘 Mock 初始化", disk_ok);

  FATTable fat_table(disk);
  bool fat_ok = fat_table.init_fat();
  print_test_result("FAT表 Mock 初始化", fat_ok);

  BufferPool buffer_pool(disk);
  print_test_result("缓冲池 Mock 初始化", true);

  // ========== 步骤2：初始化你自己实现的 Directory（核心） ==========
  std::cout << "\n2. 初始化【自己实现】的目录模块...\n";
  Directory my_dir(disk, fat_table); // 你自己写的 Directory
  bool dir_ok = my_dir.init_directory();
  print_test_result("目录模块初始化", dir_ok);

  // ========== 步骤3：初始化你自己实现的 FileInterface（测试对象） ==========
  std::cout << "\n3. 初始化【自己实现】的文件操作接口...\n";
  FileInterface my_file_if(disk, fat_table, my_dir, buffer_pool); // 你自己写的 FileInterface
  print_test_result("文件操作接口初始化", true);

  // ========== 步骤4：测试你自己的文件操作逻辑 ==========
  std::cout << "\n4. 测试核心文件操作（自己的代码）...\n";
  const std::string test_dir = "/my_test_dir";
  const std::string test_file = "/my_test_dir/my_test.txt";
  pid_t my_pid = GetCurrentThreadId();

  // 测试1：创建目录（调用你自己的 Directory）
  bool create_dir = my_file_if.create_directory(test_dir);
  print_test_result("创建测试目录 " + test_dir, create_dir);

  // 测试2：创建文件（调用你自己的 FileInterface）
  FilePermission perm = static_cast<FilePermission>(File_READ | File_WRITE | File_DEL);
  bool create_file = my_file_if.create_file(test_file, perm, "这是我测试自己写的文件操作！");
  print_test_result("创建测试文件 " + test_file, create_file);

  // 测试3：获取文件块（调用你自己的 FileInterface）
  std::vector<int> blocks = my_file_if.get_file_all_blocks(test_file);
  bool get_blocks = !blocks.empty();
  print_test_result("获取文件物理块列表", get_blocks);
  if (get_blocks)
  {
    std::cout << "   文件物理块：";
    for (int b : blocks)
      std::cout << b << " ";
    std::cout << "\n";
  }

  // 测试4：查看文件内容（调用你自己的 FileInterface）
  std::string content = my_file_if.view_file_block(test_file, 0);
  bool view_content = !content.empty();
  print_test_result("查看文件第0块内容", view_content);
  if (view_content)
  {
    std::cout << "   文件内容：" << content << "\n";
  }

  // 测试5：修改文件内容（调用你自己的 FileInterface）
  bool modify_ok = my_file_if.modify_file_block(test_file, 0, "修改后的测试内容！");
  print_test_result("修改文件内容", modify_ok);
  if (modify_ok)
  {
    std::string new_content = my_file_if.view_file_block(test_file, 0);
    std::cout << "   修改后内容：" << new_content << "\n";
  }

  // 测试6：删除文件（调用你自己的 FileInterface）
  bool delete_file = my_file_if.delete_file(test_file, my_pid);
  print_test_result("删除测试文件", delete_file);

  // 测试7：删除目录（调用你自己的 Directory）
  bool delete_dir = my_file_if.delete_directory(test_dir, true);
  print_test_result("删除测试目录", delete_dir);

  std::cout << "\n===== 自己编写的文件操作接口测试完成 =====\n";
  return 0;
}