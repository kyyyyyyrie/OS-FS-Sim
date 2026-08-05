#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <windows.h>

// 1. 引入Mock底层模块（保持测试环境一致）
#include "../mock/common_mock.hpp"
#include "../mock/sync_mock.hpp"
#include "../mock/disk_file_mock.hpp"
#include "../mock/fat_table_file_mock.hpp"
#include "../mock/buffer_pool_mock.hpp"

// 2. 引入你自己实现的核心代码
#include "../src/backend/fs_core/directory.hpp"
#include "../src/backend/fs_core/file_interface.hpp"

// 工具函数：拆分用户输入的命令（按空格拆分，保留带/的路径）
std::vector<std::string> split_command(const std::string &input)
{
  std::vector<std::string> args;
  std::stringstream ss(input);
  std::string arg;
  while (ss >> arg)
  {
    args.push_back(arg);
  }
  return args;
}

// 安全读取 UTF-8 输入（Windows 专用）
std::string safe_getline_utf8()
{
#ifdef _WIN32
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  if (hStdin == INVALID_HANDLE_VALUE)
  {
    // 回退到 std::getline
    std::string fallback;
    std::getline(std::cin, fallback);
    return fallback;
  }

  DWORD charsRead = 0;
  const DWORD bufferSize = 1024;
  std::vector<wchar_t> buffer(bufferSize);

  // 读取一行宽字符（包括换行符）
  if (!ReadConsoleW(hStdin, buffer.data(), bufferSize - 1, &charsRead, nullptr))
  {
    std::string fallback;
    std::getline(std::cin, fallback);
    return fallback;
  }

  // 去掉末尾的 \r\n 或 \n
  while (charsRead > 0 && (buffer[charsRead - 1] == L'\n' || buffer[charsRead - 1] == L'\r'))
  {
    charsRead--;
  }

  if (charsRead == 0)
    return "";

  // 转换为 UTF-8
  int utf8Size = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(charsRead), nullptr, 0, nullptr, nullptr);
  if (utf8Size <= 0)
    return "";

  std::string utf8Str(utf8Size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(charsRead), &utf8Str[0], utf8Size, nullptr, nullptr);

  return utf8Str;
#else
  std::string fallback;
  std::getline(std::cin, fallback);
  return fallback;
#endif
}

// 工具函数：打印帮助信息
void print_help()
{
  std::cout << "\n===== 文件系统交互式测试命令列表 =====" << std::endl;
  std::cout << "1. 目录操作：" << std::endl;
  std::cout << "   mkdir <目录路径>    - 创建目录（如：mkdir /test_dir）" << std::endl;
  std::cout << "   rmdir <目录路径>    - 删除目录（如：rmdir /test_dir）" << std::endl;
  std::cout << "   cd <目录路径>       - 切换目录（如：cd /test_dir 或 cd ..）" << std::endl;
  std::cout << "   pwd                 - 查看当前工作目录" << std::endl;
  std::cout << "2. 文件操作：" << std::endl;
  std::cout << "   touch <文件路径>    - 创建文件（如：touch /test_dir/test.txt）" << std::endl;
  std::cout << "   cat <文件路径> <逻辑块号> - 查看文件指定块内容（如：cat /test.txt 0）" << std::endl;
  std::cout << "   edit <文件路径> <逻辑块号> <内容> - 修改文件块（如：edit /test.txt 0 新内容）" << std::endl;
  std::cout << "   rm <文件路径>       - 删除文件（如：rm /test_dir/test.txt）" << std::endl;
  std::cout << "3. 通用操作：" << std::endl;
  std::cout << "   help                - 查看帮助" << std::endl;
  std::cout << "   exit/quit           - 退出程序" << std::endl;
  std::cout << "=======================================\n"
            << std::endl;
}

int main()
{
  // ========== 初始化环境（和之前的测试代码一致） ==========
  SetConsoleOutputCP(CP_UTF8); // 确保中文输出正常
  std::cout << "===== 文件系统交互式测试工具 =====" << std::endl;
  std::cout << "输入 'help' 查看可用命令，'exit' 退出\n"
            << std::endl;

  // 1. 初始化Mock底层模块
  Disk disk("./my_disk.bin");
  disk.init_disk();
  FATTable fat_table(disk);
  fat_table.init_fat();
  BufferPool buffer_pool(disk);

  // 2. 初始化你实现的核心模块
  Directory my_dir(disk, fat_table);
  my_dir.init_directory();
  FileInterface my_file_if(disk, fat_table, my_dir, buffer_pool);

  // ========== 交互式循环 ==========
  std::string input;
  pid_t current_pid = GetCurrentThreadId(); // 当前进程ID
  while (true)
  {
    // 显示当前工作目录提示符
    std::string cwd = my_dir.get_current_work_dir_path();
    std::cout << "[" << cwd << "] > ";
    std::getline(std::cin, input);

    // 去除输入首尾空格
    input.erase(0, input.find_first_not_of(" \t"));
    input.erase(input.find_last_not_of(" \t") + 1);
    if (input.empty())
      continue;

    // 拆分命令和参数
    std::vector<std::string> args = split_command(input);
    std::string cmd = args[0];

    // ========== 解析并执行命令 ==========
    // 1. 退出命令
    if (cmd == "exit" || cmd == "quit")
    {
      std::cout << "退出文件系统测试工具..." << std::endl;
      break;
    }
    // 2. 帮助命令
    else if (cmd == "help")
    {
      print_help();
    }
    // 3. 查看当前目录
    else if (cmd == "pwd")
    {
      std::cout << "当前工作目录：" << my_dir.get_current_work_dir_path() << std::endl;
    }
    // 4. 创建目录
    else if (cmd == "mkdir")
    {
      if (args.size() < 2)
      {
        std::cout << "[错误] 参数不足！用法：mkdir <目录路径>" << std::endl;
        continue;
      }
      std::string dir_path = args[1];
      if (my_file_if.create_directory(dir_path))
      {
        std::cout << "[成功] 目录创建成功：" << dir_path << std::endl;
      }
      else
      {
        std::cout << "[失败] 目录创建失败（路径不存在/已存在）：" << dir_path << std::endl;
      }
    }
    // 5. 删除目录
    else if (cmd == "rmdir")
    {
      if (args.size() < 2)
      {
        std::cout << "[错误] 参数不足！用法：rmdir <目录路径>" << std::endl;
        continue;
      }
      std::string dir_path = args[1];
      if (my_file_if.delete_directory(dir_path, true))
      {
        std::cout << "[成功] 目录删除成功：" << dir_path << std::endl;
      }
      else
      {
        std::cout << "[失败] 目录删除失败（路径不存在/非空）：" << dir_path << std::endl;
      }
    }
    // 6. 切换目录
    else if (cmd == "cd")
    {
      if (args.size() < 2)
      {
        std::cout << "[错误] 参数不足！用法：cd <目录路径>" << std::endl;
        continue;
      }
      std::string dir_path = args[1];
      if (my_dir.change_directory(dir_path))
      {
        std::cout << "[成功] 切换到目录：" << dir_path << std::endl;
      }
      else
      {
        std::cout << "[失败] 目录切换失败（路径不存在）：" << dir_path << std::endl;
      }
    }
    // 7. 创建文件
    else if (cmd == "touch")
    {
      if (args.size() < 2)
      {
        std::cout << "[错误] 参数不足！用法：touch <文件路径>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      FilePermission perm = static_cast<FilePermission>(File_READ | File_WRITE | File_DEL);
      if (my_file_if.create_file(file_path, perm, "默认初始化内容"))
      {
        std::cout << "[成功] 文件创建成功：" << file_path << std::endl;
      }
      else
      {
        std::cout << "[失败] 文件创建失败（路径不存在/已存在）：" << file_path << std::endl;
      }
    }
    // 8. 查看文件块内容
    else if (cmd == "cat")
    {
      if (args.size() < 3)
      {
        std::cout << "[错误] 参数不足！用法：cat <文件路径> <逻辑块号>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      int logic_block = std::stoi(args[2]);
      std::string content = my_file_if.view_file_block(file_path, logic_block);
      if (!content.empty())
      {
        std::cout << "[成功] 文件块内容（逻辑块" << logic_block << "）：\n"
                  << content << std::endl;
      }
      else
      {
        std::cout << "[失败] 读取文件块失败（文件不存在/块号无效）：" << file_path << std::endl;
      }
    }
    // 9. 修改文件块内容
    else if (cmd == "edit")
    {
      if (args.size() < 4)
      {
        std::cout << "[错误] 参数不足！用法：edit <文件路径> <逻辑块号> <修改内容>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      int logic_block = std::stoi(args[2]);
      // 拼接所有后续参数作为修改内容（支持空格）
      std::string new_content;
      for (size_t i = 3; i < args.size(); ++i)
      {
        new_content += args[i];
        if (i != args.size() - 1)
          new_content += " ";
      }
      if (my_file_if.modify_file_block(file_path, logic_block, new_content))
      {
        std::cout << "[成功] 文件块修改成功！新内容：\n"
                  << new_content << std::endl;
      }
      else
      {
        std::cout << "[失败] 文件块修改失败（文件不存在/块号无效）：" << file_path << std::endl;
      }
    }
    // 10. 删除文件
    else if (cmd == "rm")
    {
      if (args.size() < 2)
      {
        std::cout << "[错误] 参数不足！用法：rm <文件路径>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      if (my_file_if.delete_file(file_path, current_pid))
      {
        std::cout << "[成功] 文件删除成功：" << file_path << std::endl;
      }
      else
      {
        std::cout << "[失败] 文件删除失败（文件不存在/被占用）：" << file_path << std::endl;
      }
    }
    // 未知命令
    else
    {
      std::cout << "[错误] 未知命令！输入 'help' 查看可用命令" << std::endl;
    }
  }

  return 0;
}