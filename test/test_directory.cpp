#include "../src/backend/fs_core/directory.hpp"
#include "../mock/disk_mock.hpp"
#include "../mock/fat_table_mock.hpp"
#include "../src/common/common.hpp"
#include <iostream>
// 新增：解决中文乱码（可选）
#include <windows.h>

int main()
{
  // 新增：设置控制台UTF-8编码，避免中文乱码
  SetConsoleOutputCP(65001);
  SetConsoleCP(65001);

  std::cout << "===== 开始测试Directory模块 =====\n"
            << std::flush; // 强制刷新

  // 1. 初始化依赖模块
  std::cout << "[调试] 开始初始化模拟磁盘..." << std::flush;
  Disk mock_disk("./mock_disk.bin");
  if (!mock_disk.init_disk())
  {
    std::cout << "[错误] 模拟磁盘初始化失败！\n"
              << std::flush;
    return -1;
  }
  std::cout << "[调试] 模拟磁盘初始化成功\n"
            << std::flush;

  std::cout << "[调试] 开始初始化模拟FAT表..." << std::flush;
  FATTable mock_fat(mock_disk);
  if (!mock_fat.init_fat())
  {
    std::cout << "[错误] 模拟FAT表初始化失败！\n"
              << std::flush;
    return -1;
  }
  std::cout << "[调试] 模拟FAT表初始化成功\n"
            << std::flush;

  // 2. 初始化Directory模块
  std::cout << "[调试] 开始初始化目录管理器..." << std::flush;
  Directory dir(mock_disk, mock_fat);
  if (!dir.init_directory())
  {
    std::cout << "[错误] 目录管理器初始化失败！\n"
              << std::flush;
    return -1;
  }
  std::cout << "[成功] 目录管理器初始化完成，当前目录：" << dir.get_current_work_dir_path() << "\n"
            << std::flush;

  // 3. 测试1：创建多级目录
  std::cout << "\n===== 测试1：创建多级目录 =====\n"
            << std::flush;
  bool create_ok1 = dir.create_directory("/user");
  bool create_ok2 = dir.create_directory("/user/doc");
  bool create_ok3 = dir.create_directory("/user/doc/test");
  if (create_ok1 && create_ok2 && create_ok3)
  {
    std::cout << "[成功] 创建多级目录 /user/doc/test 成功！\n"
              << std::flush;
    std::cout << "当前目录树：\n"
              << dir.list_all_dirs() << "\n"
              << std::flush;
  }
  else
  {
    std::cout << "[失败] 创建目录失败！\n"
              << std::flush;
  }

  // 4. 测试2：切换目录
  std::cout << "\n===== 测试2：切换目录 =====\n"
            << std::flush;
  bool cd_ok1 = dir.change_directory("/user/doc");
  if (cd_ok1)
  {
    std::cout << "[成功] 切换到 /user/doc，当前目录：" << dir.get_current_work_dir_path() << "\n"
              << std::flush;
  }
  else
  {
    std::cout << "[失败] 切换目录失败！\n"
              << std::flush;
  }

  bool cd_ok2 = dir.change_directory(".."); // 回退到/user
  if (cd_ok2)
  {
    std::cout << "[成功] 回退到父目录，当前目录：" << dir.get_current_work_dir_path() << "\n"
              << std::flush;
  }
  else
  {
    std::cout << "[失败] 回退到父目录失败！\n"
              << std::flush; // 补充失败日志
  }

  bool cd_ok3 = dir.change_directory("/"); // 回到根目录
  if (cd_ok3)
  {
    std::cout << "[成功] 回到根目录，当前目录：" << dir.get_current_work_dir_path() << "\n"
              << std::flush;
  }
  else
  {
    std::cout << "[失败] 回到根目录失败！\n"
              << std::flush; // 补充失败日志
  }

  // 5. 测试3：添加/查询文件FCB（核心修改：加全量调试日志，删除is_dir_exist）
  std::cout << "\n===== 测试3：添加/查询文件 =====\n"
            << std::flush;

  // 调试步骤2：创建FCB对象（每一步都打印）
  std::cout << "[调试] 开始创建FCB对象，参数：文件名=test.txt，权限=File_READ|File_WRITE，起始块=64，总块数=1\n"
            << std::flush;
  FCB test_fcb(
      "test.txt",
      static_cast<FilePermission>(File_READ | File_WRITE), // 显式转换匹配构造函数
      64,
      1);
  std::cout << "[调试] FCB对象创建成功！文件名：" << test_fcb.filename << "\n"
            << std::flush; // 验证FCB成员

  // 调试步骤3：调用add_file添加文件
  std::cout << "[调试] 开始调用add_file添加文件：/user/doc/test.txt\n"
            << std::flush;
  bool add_file_ok = dir.add_file("/user/doc/test.txt", test_fcb);
  if (add_file_ok)
  {
    std::cout << "[成功] 添加文件 /user/doc/test.txt 成功！\n"
              << std::flush;
  }
  else
  {
    std::cout << "[失败] 添加文件失败！（原因：路径不存在/文件已存在/add_file函数内部错误）\n"
              << std::flush;
  }

  // 查询文件
  std::cout << "[调试] 开始查询文件：/user/doc/test.txt\n"
            << std::flush;
  FCB query_fcb;
  bool query_ok = dir.query_file("/user/doc/test.txt", query_fcb);
  if (query_ok)
  {
    std::cout << "[成功] 查询文件信息：\n"
              << std::flush;
    std::cout << "  文件名：" << query_fcb.filename << "\n"
              << std::flush;
    std::cout << "  起始盘块：" << query_fcb.start_block << "\n"
              << std::flush;
    std::cout << "  权限值：" << static_cast<int>(query_fcb.permission) << "\n"
              << std::flush;
  }
  else
  {
    std::cout << "[失败] 查询文件失败！\n"
              << std::flush;
  }

  // 查看目录内容
  std::cout << "[调试] 开始查询 /user/doc 目录内容\n"
            << std::flush;
  std::string dir_content = dir.query_directory("/user/doc");
  std::cout << "\n/user/doc 目录内容：\n"
            << dir_content << "\n"
            << std::flush;

  // 6. 测试4：锁定/解锁文件
  std::cout << "\n===== 测试4：锁定/解锁文件 =====\n"
            << std::flush;
  pid_t mock_pid = GetCurrentThreadId(); // Windows获取当前线程ID
  std::cout << "[调试] 当前线程PID：" << mock_pid << "\n"
            << std::flush;
  bool lock_ok = dir.lock_file("/user/doc/test.txt", mock_pid);
  if (lock_ok)
  {
    std::cout << "[成功] 锁定文件 test.txt（PID：" << mock_pid << "）\n"
              << std::flush;
  }
  else
  {
    std::cout << "[失败] 锁定文件失败！\n"
              << std::flush;
  }

  bool unlock_ok = dir.unlock_file("/user/doc/test.txt");
  if (unlock_ok)
  {
    std::cout << "[成功] 解锁文件 test.txt\n"
              << std::flush;
  }
  else
  {
    std::cout << "[失败] 解锁文件失败！\n"
              << std::flush;
  }

  // 7. 测试5：删除文件/目录
  std::cout << "\n===== 测试5：删除文件/目录 =====\n"
            << std::flush;
  // 删除文件
  std::cout << "[调试] 开始删除文件：/user/doc/test.txt\n"
            << std::flush;
  bool remove_file_ok = dir.remove_file("/user/doc/test.txt");
  if (remove_file_ok)
  {
    std::cout << "[成功] 删除文件 test.txt 成功！\n"
              << std::flush;
  }
  else
  {
    std::cout << "[失败] 删除文件失败！\n"
              << std::flush;
  }

  // 删除非空目录（非强制）
  std::cout << "[调试] 尝试非强制删除目录：/user\n"
            << std::flush;
  bool del_dir_ok1 = dir.delete_directory("/user");
  if (!del_dir_ok1)
  {
    std::cout << "[预期] 非强制删除非空目录 /user 失败（正确）\n"
              << std::flush;
  }
  else
  {
    std::cout << "[异常] 非强制删除非空目录 /user 成功！逻辑错误\n"
              << std::flush;
  }

  // 强制删除目录
  std::cout << "[调试] 尝试强制删除目录：/user\n"
            << std::flush;
  bool del_dir_ok2 = dir.delete_directory("/user", true);
  if (del_dir_ok2)
  {
    std::cout << "[成功] 强制删除目录 /user 成功！\n"
              << std::flush;
    std::cout << "删除后目录树：\n"
              << dir.list_all_dirs() << "\n"
              << std::flush;
  }
  else
  {
    std::cout << "[失败] 强制删除目录失败！\n"
              << std::flush;
  }

  std::cout << "\n===== 所有测试完成 =====\n"
            << std::flush;
  return 0;
}