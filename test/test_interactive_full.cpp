#include<bits/stdc++.h>
#include <string>
#include <vector>
#include <sstream>
#include <windows.h>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <ctime>

// 1. 引入项目头文件（请根据实际路径调整）
#include "../src/backend/fs_core/directory.hpp"
#include "../src/backend/fs_core/file_interface.hpp"

// 2. 引入Mock模块头文件（请根据实际路径调整）
#include "../src/common/common.hpp"
#include "../src/backend/storage/disk.hpp"
#include "../src/backend/concurrency/sync.hpp"
#include "../src/backend/storage/fat_table.hpp"
#include "../src/backend/buffer/buffer_pool.hpp"
// #include "../mock/buffer_pool_mock.hpp"

// ========== 工具函数 ==========
// 拆分命令（支持引号包裹空格内容）
std::vector<std::string> split_command(const std::string &input)
{
  std::vector<std::string> args;
  std::string current_arg;
  bool in_quote = false;

  for (char c : input)
  {
    if (c == '"')
    {
      in_quote = !in_quote;
    }
    else if (c == ' ' && !in_quote)
    {
      if (!current_arg.empty())
      {
        args.push_back(current_arg);
        current_arg.clear();
      }
    }
    else
    {
      current_arg += c;
    }
  }
  if (!current_arg.empty())
    args.push_back(current_arg);
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

// 打印帮助信息（包含read命令）
void print_help()
{
  std::cout << "\n===== 完整文件系统交互式测试命令列表 =====" << std::endl;
  std::cout << "==========================================" << std::endl;
  std::cout << "【基础目录操作】" << std::endl;
  std::cout << "  pwd                 - 查看当前工作目录" << std::endl;
  std::cout << "  ls [目录路径]       - 查看目录内容（默认当前目录）" << std::endl;
  std::cout << "  tree                - 递归列出所有目录树结构" << std::endl;
  std::cout << "  mkdir <目录路径>    - 创建多级目录（如：mkdir /a/b/c）" << std::endl;
  std::cout << "  cd <目录路径>       - 切换目录（如：cd /a/b 或 cd ..）" << std::endl;
  std::cout << "  rmdir [-f] <路径>   - 删除目录（-f强制删除非空目录）" << std::endl;
  std::cout << "==========================================" << std::endl;
  std::cout << "【基础文件操作】" << std::endl;
  std::cout << "  touch <文件路径>    - 创建文件（如：touch /a/test.txt）" << std::endl;
  std::cout << "  write <文件> <内容> - 写入整个文件内容（自动分配多块）" << std::endl;
  std::cout << "  read <文件路径>     - 读取整个文件的完整内容（自动拼接所有块）" << std::endl;
  std::cout << "  cat <文件> <块号>   - 查看文件指定逻辑块内容" << std::endl;
  std::cout << "  edit <文件> <块号> <内容> - 修改文件块（内容支持空格）" << std::endl;
  std::cout << "  rm <文件路径>       - 删除文件" << std::endl;
  std::cout << "==========================================" << std::endl;
  std::cout << "【高级文件操作】" << std::endl;
  std::cout << "  blocks <文件路径>   - 查看文件所有物理块分布" << std::endl;
  std::cout << "  stat <文件路径>     - 查询文件FCB详细信息" << std::endl;
  std::cout << "  lock <文件路径>     - 锁定文件（防止其他进程修改）" << std::endl;
  std::cout << "  unlock <文件路径>   - 解锁文件" << std::endl;
  std::cout << "  truncate <文件> <块号> - 截断文件到指定逻辑块" << std::endl;
  std::cout << "==========================================" << std::endl;
  std::cout << "【通用操作】" << std::endl;
  std::cout << "  help                - 查看此帮助信息" << std::endl;
  std::cout << "  clear               - 清空控制台" << std::endl;
  std::cout << "  exit/quit           - 退出程序" << std::endl;
  std::cout << "==========================================\n"
            << std::endl;
}

// 清空控制台
void clear_console()
{
  system("cls");
}

// 打印FCB详细信息
void print_fcb_info(const FCB &fcb)
{
  std::cout << "\n===== 文件FCB详细信息 =====" << std::endl;
  std::cout << "文件名：" << fcb.filename << std::endl;
  std::cout << "创建时间：" << fcb.create_time << std::endl;
  std::cout << "权限：";
  if (fcb.permission & FilePermission::File_READ)
    std::cout << "读 ";
  if (fcb.permission & FilePermission::File_WRITE)
    std::cout << "写 ";
  if (fcb.permission & FilePermission::File_DEL)
    std::cout << "删除 ";
  std::cout << std::endl;
  std::cout << "起始物理块：" << fcb.start_block << std::endl;
  std::cout << "总块数：" << fcb.total_blocks << std::endl;
  std::cout << "文件大小：" << fcb.total_blocks * BLOCK_SIZE << " 字节" << std::endl;
  std::cout << "占用状态：" << (fcb.is_in_use ? "已锁定" : "未锁定") << std::endl;
  if (fcb.is_in_use)
  {
    std::cout << "锁定进程ID：" << fcb.holder_pid << std::endl;
  }
  std::cout << "===========================\n"
            << std::endl;
}

// ========== 主函数 ==========
int main()
{
  // // 1. 强制设置控制台编码（输出+输入）
  // SetConsoleOutputCP(CP_UTF8);
  // SetConsoleCP(CP_UTF8);

#ifdef _WIN32
  // 启用 UTF-8 全局支持（Windows 10 1903+）
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  setlocale(LC_ALL, ".utf8");

// 额外：告诉 Windows 这是一个 UTF-8 应用（Vista+）
// 需要链接 AdvAPI32.lib
#pragma comment(lib, "AdvAPI32.lib")
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  // 初始化Mock底层模块
  Disk disk("./mock_disk.bin");
  if (!disk.init_disk())
  {
    std::cerr << "[ERROR] 磁盘初始化失败！" << std::endl;
    return -1;
  }
  cout << 2 << endl
       << std::flush;
  FATTable fat_table(disk);
  cout << 3 << endl
       << std::flush;
  fat_table.init_fat();
  cout << 4 << endl
       << std::flush;

  BufferPool buffer_pool(disk);
  cout << 5 << endl
       << std::flush;

  // 初始化核心模块
  Directory my_dir(disk, fat_table);
  cout << 1 << endl
       << std::flush;
  if (!my_dir.init_directory())
  {
    cout << 6 << endl
         << std::flush;
    std::cerr << "[ERROR] 目录模块初始化失败！" << std::endl;
    return -1;
  }
  cout << 7 << endl
       << std::flush;
  std::cout << "[DEBUG] Directory初始化完成，准备创建FileInterface..." << std::endl
            << std::flush; // 新增
  FileInterface my_file_if(disk, fat_table, my_dir, buffer_pool);
  std::cout << "[DEBUG] FileInterface创建成功，准备输出欢迎信息..." << std::endl
            << std::flush; // 新增

  // 欢迎信息
  std::cout << "===== 完整文件系统交互式测试工具 =====" << std::endl
            << std::flush;
  std::cout << "输入 'help' 查看所有命令，'exit' 退出\n"
            << std::endl;

  // 交互式循环
  std::string input;
  pid_t current_pid = GetCurrentThreadId();
  while (true)
  {
    // 显示当前工作目录提示符
    std::string cwd = my_dir.get_current_work_dir_path();
    std::cout << "[" << cwd << "] > ";
    input = safe_getline_utf8();

    // 去除首尾空格
    input.erase(0, input.find_first_not_of(" \t"));
    input.erase(input.find_last_not_of(" \t") + 1);
    if (input.empty())
      continue;

    // 拆分命令和参数
    std::vector<std::string> args = split_command(input);
    std::string cmd = args[0];

    // ========== 命令解析 ==========
    // 1. 退出
    if (cmd == "exit" || cmd == "quit")
    {
      std::cout << "退出文件系统测试工具..." << std::endl;
      break;
    }
    // 2. 帮助
    else if (cmd == "help")
    {
      print_help();
    }
    // 3. 清空控制台
    else if (cmd == "clear")
    {
      clear_console();
    }
    // 4. 查看当前目录
    else if (cmd == "pwd")
    {
      std::cout << "当前工作目录：" << my_dir.get_current_work_dir_path() << std::endl;
    }
    // 5. 查看目录内容
    else if (cmd == "ls")
    {
      std::string dir_path = (args.size() >= 2) ? args[1] : my_dir.get_current_work_dir_path();
      std::string dir_content = my_file_if.query_directory(dir_path);
      if (!dir_content.empty())
      {
        std::cout << dir_content << std::endl;
      }
      else
      {
        std::cout << "[WARNING] 目录为空或路径不存在：" << dir_path << std::endl;
      }
    }
    // 6. 列出目录树
    else if (cmd == "tree")
    {
      std::cout << "\n===== 完整目录树结构 =====" << std::endl;
      std::cout << my_dir.list_all_dirs(nullptr, 0) << std::endl;
    }
    // 7. 创建目录
    else if (cmd == "mkdir")
    {
      if (args.size() < 2)
      {
        std::cout << "[ERROR] 参数不足！用法：mkdir <目录路径>" << std::endl;
        continue;
      }
      std::string dir_path = args[1];
      if (my_file_if.create_directory(dir_path))
      {
        std::cout << "[SUCCESS] 目录创建成功：" << dir_path << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 目录创建失败（路径已存在/解析错误）：" << dir_path << std::endl;
      }
    }
    // 8. 切换目录
    else if (cmd == "cd")
    {
      if (args.size() < 2)
      {
        std::cout << "[ERROR] 参数不足！用法：cd <目录路径>" << std::endl;
        continue;
      }
      std::string dir_path = args[1];
      if (my_file_if.change_directory(dir_path))
      {
        std::cout << "[SUCCESS] 切换到目录：" << my_dir.get_current_work_dir_path() << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 目录切换失败（路径不存在）：" << dir_path << std::endl;
      }
    }
    // 9. 删除目录
    else if (cmd == "rmdir")
    {
      if (args.size() < 2)
      {
        std::cout << "[ERROR] 参数不足！用法：rmdir [-f] <目录路径>" << std::endl;
        continue;
      }
      bool force = false;
      std::string dir_path;
      if (args[1] == "-f")
      {
        if (args.size() < 3)
        {
          std::cout << "[ERROR] 参数不足！用法：rmdir -f <目录路径>" << std::endl;
          continue;
        }
        force = true;
        dir_path = args[2];
      }
      else
      {
        dir_path = args[1];
      }
      if (my_file_if.delete_directory(dir_path, force))
      {
        std::cout << "[SUCCESS] 目录删除成功：" << dir_path << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 目录删除失败（" << (force ? "路径不存在/被占用" : "非空/路径不存在/被占用") << "）：" << dir_path << std::endl;
      }
    }
    // 10. 创建文件
    else if (cmd == "touch")
    {
      if (args.size() < 2)
      {
        std::cout << "[ERROR] 参数不足！用法：touch <文件路径>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      FilePermission perm = static_cast<FilePermission>(File_READ | File_WRITE | File_DEL);
      if (my_file_if.create_file(file_path, perm, "默认初始化内容"))
      {
        std::cout << "[SUCCESS] 文件创建成功：" << file_path << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 文件创建失败（路径不存在/已存在/磁盘满）：" << file_path << std::endl;
      }
    }
    // 11. 写入文件内容
    // 11. 写入文件内容
    // 11. 写入文件内容
    else if (cmd == "write")
    {
      if (args.size() < 3)
      {
        std::cout << "[ERROR] 参数不足！用法：write <文件路径> <内容>" << std::endl;
        continue;
      }

      // 第一步：提取文件路径（args[1] 是可靠的，因为它不含空格）
      std::string file_path = args[1];

      // 第二步：从原始 input 中提取 content 部分（保留所有空格！）
      std::string content;
      size_t pos1 = input.find(' '); // 第一个空格（在 "write" 后）
      if (pos1 == std::string::npos)
      { /* 不可能 */
      }
      size_t pos2 = input.find(' ', pos1 + 1); // 第二个空格（在文件名后）
      if (pos2 == std::string::npos)
      {
        // 理论上不会发生，因为 args.size() >= 3
        content = "";
      }
      else
      {
        // 从第二个空格之后开始截取（跳过空格本身）
        content = input.substr(pos2 + 1);
      }

      // 调试输出（可选）
      // std::cout << "[DEBUG] Content: [" << content << "]\n";
      // std::cout << "[DEBUG] Bytes: ";
      // for (unsigned char c : content) printf("%02X ", c); std::cout << "\n";

      if (my_file_if.write_file(file_path, content))
      {
        std::cout << "[SUCCESS] 文件内容写入完成！\n";
      }
      else
      {
        std::cout << "[ERROR] 文件内容写入失败！\n";
      }
    }
    // 12. 读取整个文件
    else if (cmd == "read")
    {
      if (args.size() < 2)
      {
        std::cout << "[ERROR] 参数不足！用法：read <文件路径>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      std::string full_content = my_file_if.read_file(file_path);
      if (!full_content.empty())
      {
        std::cout << "\n[SUCCESS] 文件完整内容：\n"
                  << "------------------------\n"
                  << full_content
                  << "\n------------------------\n"
                  << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 读取整个文件失败（文件不存在/无读权限）：" << file_path << std::endl;
      }
    }
    // 13. 查看指定块
    else if (cmd == "cat")
    {
      if (args.size() < 3)
      {
        std::cout << "[ERROR] 参数不足！用法：cat <文件路径> <逻辑块号>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      int logic_block = std::stoi(args[2]);
      std::string content = my_file_if.view_file_block(file_path, logic_block);
      if (!content.empty())
      {
        std::cout << "\n[SUCCESS] 文件块内容（逻辑块" << logic_block << "）：\n"
                  << "------------------------\n"
                  << content
                  << "\n------------------------\n"
                  << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 读取文件块失败（文件不存在/块号无效）：" << file_path << std::endl;
      }
    }
    // 14. 修改文件块
    // 14. 修改文件块
    else if (cmd == "edit")
    {
      if (args.size() < 4)
      {
        std::cout << "[ERROR] 参数不足！用法：edit <文件路径> <逻辑块号> <修改内容>" << std::endl;
        continue;
      }

      std::string file_path = args[1];
      int logic_block = std::stoi(args[2]);

      // 从原始 input 提取 content（保留空格）
      std::string content;
      size_t pos1 = input.find(' ');           // after "edit"
      size_t pos2 = input.find(' ', pos1 + 1); // after file_path
      size_t pos3 = input.find(' ', pos2 + 1); // after block number
      if (pos3 != std::string::npos)
      {
        content = input.substr(pos3 + 1);
      }

      if (my_file_if.modify_file_block(file_path, logic_block, content))
      {
        std::cout << "[SUCCESS] 文件块修改成功！新内容：\n"
                  << content << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 文件块修改失败（文件不存在/块号无效）：" << file_path << std::endl;
      }
    }
    // 15. 删除文件
    else if (cmd == "rm")
    {
      if (args.size() < 2)
      {
        std::cout << "[ERROR] 参数不足！用法：rm <文件路径>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      if (my_file_if.delete_file(file_path, current_pid))
      {
        std::cout << "[SUCCESS] 文件删除成功：" << file_path << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 文件删除失败（文件不存在/被锁定）：" << file_path << std::endl;
      }
    }
    // 16. 查看文件块分布
    else if (cmd == "blocks")
    {
      if (args.size() < 2)
      {
        std::cout << "[ERROR] 参数不足！用法：blocks <文件路径>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      std::vector<int> block_list = my_file_if.get_file_all_blocks(file_path);
      if (!block_list.empty())
      {
        std::cout << "[SUCCESS] 文件物理块列表：";
        for (size_t i = 0; i < block_list.size(); ++i)
        {
          std::cout << block_list[i];
          if (i != block_list.size() - 1)
            std::cout << " → ";
        }
        std::cout << std::endl;
      }
      else
      {
        std::cout << "[WARNING] 文件无物理块或路径不存在：" << file_path << std::endl;
      }
    }
    // 17. 查询FCB
    else if (cmd == "stat")
    {
      if (args.size() < 2)
      {
        std::cout << "[ERROR] 参数不足！用法：stat <文件路径>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      FCB fcb;
      if (my_dir.query_file(file_path, fcb))
      {
        print_fcb_info(fcb);
      }
      else
      {
        std::cout << "[ERROR] 查询FCB失败（文件不存在）：" << file_path << std::endl;
      }
    }
    // 18. 锁定文件
    else if (cmd == "lock")
    {
      if (args.size() < 2)
      {
        std::cout << "[ERROR] 参数不足！用法：lock <文件路径>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      if (my_dir.lock_file(file_path, current_pid))
      {
        std::cout << "[SUCCESS] 文件锁定成功：" << file_path << "（进程ID：" << current_pid << "）" << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 文件锁定失败（文件不存在/已被锁定）：" << file_path << std::endl;
      }
    }
    // 19. 解锁文件
    else if (cmd == "unlock")
    {
      if (args.size() < 2)
      {
        std::cout << "[ERROR] 参数不足！用法：unlock <文件路径>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      if (my_dir.unlock_file(file_path))
      {
        std::cout << "[SUCCESS] 文件解锁成功：" << file_path << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 文件解锁失败（文件不存在）：" << file_path << std::endl;
      }
    }
    // 20. 截断文件
    else if (cmd == "truncate")
    {
      if (args.size() < 3)
      {
        std::cout << "[ERROR] 参数不足！用法：truncate <文件路径> <最大逻辑块号>" << std::endl;
        continue;
      }
      std::string file_path = args[1];
      int max_block = std::stoi(args[2]);
      if (my_file_if.truncate_file(file_path, max_block))
      {
        std::cout << "[SUCCESS] 文件截断成功：" << file_path << "（保留到逻辑块" << max_block << "）" << std::endl;
      }
      else
      {
        std::cout << "[ERROR] 文件截断失败（文件不存在/块号超出范围）：" << file_path << std::endl;
      }
    }
    // 未知命令
    else
    {
      std::cout << "[ERROR] 未知命令！输入 'help' 查看所有可用命令" << std::endl;
    }
  }

  return 0;
}

// cl /EHsc /std:c++17 /utf-8 /I"./mock/buffer_pool_mock" /I"./src" test/test_interactive_full.cpp src/backend/fs_core/directory.cpp src/backend/fs_core/file_interface.cpp /Fe:test_interactive_full.exe

// cl /EHsc /std:c++17 /utf-8 /I"./mock/buffer_pool_mock" /I"./src" test/test_interactive_full.cpp src/backend/fs_core/directory.cpp src/backend/fs_core/file_interface.cpp src/backend/storage/disk.cpp src/common/common.cpp src/backend/concurrency/sync.cpp src/backend/storage/fat_table.cpp /Fe:test_interactive_full.exe

// cl /EHsc /std:c++17 /utf-8 /I"./src" test/test_interactive_full.cpp src/backend/fs_core/directory.cpp src/backend/buffer/buffer_pool.cpp src/backend/fs_core/file_interface.cpp src/backend/storage/disk.cpp src/common/common.cpp src/backend/concurrency/sync.cpp src/backend/storage/fat_table.cpp /Fe:test_interactive_full.exe