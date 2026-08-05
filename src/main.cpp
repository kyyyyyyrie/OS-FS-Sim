#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <windows.h>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <locale>
#include "backend/concurrency/json.hpp"
#include "backend/fs_core/directory.hpp"
#include "backend/fs_core/file_interface.hpp"
#include "common/common.hpp"
#include "backend/storage/disk.hpp"
#include "backend/concurrency/sync.hpp"
#include "backend/storage/fat_table.hpp"
#include "backend/buffer/buffer_pool.hpp"
#include "backend/concurrency/process.hpp"
#include "backend/concurrency/ipc.hpp"
using json = nlohmann::json;
using namespace std;
extern mutex g_mtx;
vector<string> split_command(const string &input)
{
  vector<string> args;
  string current_arg;
  bool in_quote = false;
  bool in_json = false;
  for (char c : input)
  {
    if (c == '{')
    {
      in_json = true;
      current_arg += c;
    }
    else if (c == '}')
    {
      in_json = false;
      current_arg += c;
    }
    else if (c == '"')
    {
      in_quote = !in_quote;
      current_arg += c;
    }
    else if (c == ' ' && !in_quote && !in_json)
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
  {
    args.push_back(current_arg);
  }
  return args;
}
string safe_getline_utf8()
{
#ifdef _WIN32
  SetConsoleCP(CP_UTF8);
  SetConsoleOutputCP(CP_UTF8);
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  if (hStdin == INVALID_HANDLE_VALUE)
  {
    string fallback;
    getline(cin, fallback);
    return fallback;
  }
  DWORD charsRead = 0;
  const DWORD bufferSize = 1024;
  vector<wchar_t> buffer(bufferSize, 0);
  if (!ReadConsoleW(hStdin, buffer.data(), bufferSize - 1, &charsRead, nullptr))
  {
    string fallback;
    getline(cin, fallback);
    return fallback;
  }
  while (charsRead > 0 && (buffer[charsRead - 1] == L'\n' || buffer[charsRead - 1] == L'\r'))
  {
    charsRead--;
    buffer[charsRead] = L'\0';
  }
  if (charsRead == 0)
    return "";
  int utf8Size = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(charsRead),
                                     nullptr, 0, nullptr, nullptr);
  if (utf8Size <= 0)
    return "";
  string utf8Str(utf8Size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(charsRead),
                      &utf8Str[0], utf8Size, nullptr, nullptr);
  return utf8Str;
#else
  string fallback;
  getline(cin, fallback);
  return fallback;
#endif
}
void print_help()
{
  cout << "\n===== 完整文件系统交互式测试命令列表 =====\n"
       << "==========================================\n"
       << "【基础目录操作】\n"
       << "  pwd                 - 查看当前工作目录\n"
       << "  ls [目录路径]       - 查看目录内容（默认当前目录）\n"
       << "  tree                - 递归列出所有目录树结构\n"
       << "  mkdir <目录路径>    - 创建多级目录（如：mkdir /a/b/c）\n"
       << "  cd <目录路径>       - 切换目录（如：cd /a/b 或 cd ..）\n"
       << "  rmdir [-f] <路径>   - 删除目录（-f强制删除非空目录）\n"
       << "==========================================\n"
       << "【基础文件操作】\n"
       << "  touch <文件路径>    - 创建文件（如：touch /a/test.txt）\n"
       << "  write <文件> <内容> - 写入整个文件内容（自动分配多块）\n"
       << "  read <文件路径>     - 读取整个文件的完整内容\n"
       << "  cat <文件> <块号>   - 查看文件指定逻辑块内容\n"
       << "  edit <文件> <块号> <内容> - 修改文件块（内容支持空格）\n"
       << "  rm <文件路径>       - 删除文件\n"
       << "==========================================\n"
       << "【高级文件操作】\n"
       << "  blocks <文件路径>   - 查看文件所有物理块分布\n"
       << "  stat <文件路径>     - 查询文件FCB详细信息\n"
       << "  lock <文件路径>     - 锁定文件（防止其他进程修改）\n"
       << "  unlock <文件路径>   - 解锁文件\n"
       << "  truncate <文件> <块号> - 截断文件到指定逻辑块\n"
       << "==========================================\n"
       << "【进程调度+通信操作】\n"
       << "  start_scheduler     - 初始化时间片轮转调度器（默认1ms）\n"
       << "  run_scheduler       - 启动调度执行（按时间片运行就绪队列进程）\n"
       << "  stop_scheduling     - 停止调度执行（保留就绪队列）\n"
       << "  stop_scheduler      - 停止调度器并终止所有进程\n"
       << "  create_proc <类型> <JSON> - 创建进程（所有操作均支持，仅入队不立即执行）\n"
       << "                        ├─ 目录操作示例：\n"
       << "                        │  create_proc DIR_PWD {}\n"
       << "                        │  create_proc DIR_LS {\"path\":\"/\"}\n"
       << "                        │  create_proc DIR_MKDIR {\"path\":\"/a/b/c\"}\n"
       << "                        │  create_proc DIR_CD {\"path\":\"/a/b\"}\n"
       << "                        │  create_proc DIR_RMDIR {\"path\":\"/a/b\",\"force\":false}\n"
       << "                        ├─ 文件操作示例：\n"
       << "                        │  create_proc FILE_TOUCH {\"path\":\"/test.txt\"}\n"
       << "                        │  create_proc FILE_WRITE {\"path\":\"/test.txt\",\"content\":\"hello\"}\n"
       << "                        │  create_proc FILE_READ {\"path\":\"/test.txt\"}\n"
       << "                        │  create_proc FILE_CAT {\"path\":\"/test.txt\",\"block\":0}\n"
       << "                        │  create_proc FILE_EDIT {\"path\":\"/test.txt\",\"block\":0,\"content\":\"new\"}\n"
       << "                        │  create_proc FILE_RM {\"path\":\"/test.txt\"}\n"
       << "                        └─ 原有类型：CREATE_FILE/DELETE_FILE/MKDIR/QUERY_DIR\n"
       << "  list_procs          - 查看所有进程ID及运行状态\n"
       << "  list_ready          - 查看就绪队列中的进程（等待调度）\n"
       << "  recv_msg [PID]      - 接收进程执行结果（PID=0接收所有）\n"
       << "  send_msg <PID> <内容> - 发送自定义消息到指定进程\n"
       << "==========================================\n"
       << "【通用操作】\n"
       << "  help                - 查看此帮助信息\n"
       << "  clear               - 清空控制台\n"
       << "  exit/quit           - 退出程序\n"
       << "==========================================\n"
       << endl;
}
void clear_console()
{
#ifdef _WIN32
  system("cls");
#else
  system("clear");
#endif
}
void print_fcb_info(const FCB &fcb)
{
  cout << "\n===== 文件FCB详细信息 =====\n"
       << "文件名：" << fcb.filename << "\n"
       << "创建时间：" << fcb.create_time << "\n"
       << "权限：";
  if (fcb.permission & FilePermission::File_READ)
    cout << "读 ";
  if (fcb.permission & FilePermission::File_WRITE)
    cout << "写 ";
  if (fcb.permission & FilePermission::File_DEL)
    cout << "删除 ";
  cout << "\n起始物理块：" << fcb.start_block << "\n"
       << "总块数：" << fcb.total_blocks << "\n"
       << "文件大小：" << fcb.total_blocks * BLOCK_SIZE << " 字节\n"
       << "锁定状态：" << (fcb.is_in_use ? "已锁定" : "未锁定") << "\n";
  if (fcb.is_in_use)
  {
    cout << "锁定进程ID：" << fcb.holder_pid << "\n";
  }
  cout << "===========================\n"
       << endl;
}
void print_process_list(ProcessManager &proc_mgr)
{
  cout << "\n===== 进程列表 =====\n"
       << left << setw(8) << "PID" << "运行状态\n"
       << "-------------------\n";
  try
  {
    auto &proc_running = proc_mgr.get_process_running();
    auto &proc_completed = proc_mgr.get_process_completed(); // 新增：获取进程完成状态
    auto ready_pids = proc_mgr.get_ready_queue();            // 获取就绪队列PID
    lock_guard<mutex> lock(g_mtx);
    if (proc_running.empty())
    {
      cout << "  暂无进程\n";
    }
    else
    {
      for (auto &pair : proc_running)
      {
        pid_t pid = pair.first;
        bool is_running = pair.second.load();
        string status;
        if (is_running)
        {
          status = "运行中";
        }
        else if (proc_completed.find(pid) != proc_completed.end() && proc_completed[pid].load())
        {
          status = "已完成";
        }
        else if (find(ready_pids.begin(), ready_pids.end(), pid) != ready_pids.end())
        {
          status = "就绪（等待调度）";
        }
        else
        {
          status = "未知状态";
        }
        cout << left << setw(8) << pid << status << "\n";
      }
    }
  }
  catch (const exception &e)
  {
    cout << "  读取进程状态失败：" << e.what() << "\n";
  }
  cout << "===================\n"
       << endl;
}
void print_ready_queue(ProcessManager &proc_mgr)
{
  cout << "\n===== 就绪队列（等待调度） =====\n"
       << "-------------------------------\n";
  try
  {
    auto ready_pids = proc_mgr.get_ready_queue();
    if (ready_pids.empty())
    {
      cout << "  就绪队列为空\n";
    }
    else
    {
      cout << "  进程顺序：";
      for (size_t i = 0; i < ready_pids.size(); ++i)
      {
        cout << ready_pids[i];
        if (i != ready_pids.size() - 1)
          cout << " → ";
      }
      cout << "\n  就绪进程总数：" << ready_pids.size() << "\n";
    }
  }
  catch (const exception &e)
  {
    cout << "  读取就绪队列失败：" << e.what() << "\n";
  }
  cout << "===============================\n"
       << endl;
}
int main()
{
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  setlocale(LC_ALL, "zh-CN.UTF-8");
#pragma comment(lib, "AdvAPI32.lib")
#endif
  try
  {
    Disk disk("./mock_disk.bin");
    if (!disk.init_disk())
    {
      cerr << "[ERROR] 磁盘初始化失败！\n";
      return -1;
    }
    FATTable fat_table(disk);
    fat_table.init_fat();
    BufferPool buffer_pool(disk);
    Directory my_dir(disk, fat_table);
    if (!my_dir.init_directory())
    {
      cerr << "[ERROR] 目录模块初始化失败！\n";
      return -1;
    }
    FileInterface my_file_if(disk, fat_table, my_dir, buffer_pool);
    MessageQueue msg_queue;
    ProcessManager proc_mgr(my_file_if, msg_queue);
    proc_mgr.set_time_slice(1);
    cout << "===== 完整文件系统+进程调度交互式测试工具 =====\n"
         << "💡 提示：\n"
         << "  1. start_scheduler - 初始化调度器\n"
         << "  2. create_proc     - 创建进程（支持所有目录/文件操作，仅入队不执行）\n"
         << "  3. run_scheduler   - 启动时间片轮转执行就绪队列进程\n"
         << "输入 'help' 查看所有命令，'exit' 退出\n"
         << endl;
    string input;
    pid_t current_pid = GetCurrentThreadId();
    while (true)
    {
      string cwd = my_dir.get_current_work_dir_path();
      cout << "[" << cwd << "] > ";
      input = safe_getline_utf8();
      input.erase(0, input.find_first_not_of(" \t\n\r"));
      input.erase(input.find_last_not_of(" \t\n\r") + 1);
      if (input.empty())
        continue;
      vector<string> args = split_command(input);
      if (args.empty())
        continue;
      string cmd = args[0];
      if (cmd == "exit" || cmd == "quit")
      {
        cout << "[INFO] 正在停止所有进程...";
        proc_mgr.stop_all_processes();
        cout << "完成！\n";
        cout << "👋 退出文件系统测试工具...\n";
        break;
      }
      else if (cmd == "help")
      {
        print_help();
      }
      else if (cmd == "clear")
      {
        clear_console();
      }
      else if (cmd == "pwd")
      {
        cout << "✅ 当前工作目录：" << my_dir.get_current_work_dir_path() << "\n";
      }
      else if (cmd == "ls")
      {
        string dir_path = (args.size() >= 2) ? args[1] : my_dir.get_current_work_dir_path();
        string dir_content = my_file_if.query_directory(dir_path);
        if (!dir_content.empty())
        {
          cout << "\n📂 目录内容 (" << dir_path << ")：\n"
               << dir_content << "\n";
        }
        else
        {
          cout << "⚠️  目录为空或路径不存在：" << dir_path << "\n";
        }
      }
      else if (cmd == "tree")
      {
        cout << "\n🌳 完整目录树结构：\n\n"
             << my_dir.list_all_dirs(nullptr, 0) << "\n";
      }
      else if (cmd == "mkdir")
      {
        if (args.size() < 2)
        {
          cout << "❌ 参数不足！用法：mkdir <目录路径>\n";
          continue;
        }
        string dir_path = args[1];
        if (my_file_if.create_directory(dir_path))
        {
          cout << "✅ 目录创建成功：" << dir_path << "\n";
        }
        else
        {
          cout << "❌ 目录创建失败（路径已存在/解析错误）：" << dir_path << "\n";
        }
      }
      else if (cmd == "cd")
      {
        if (args.size() < 2)
        {
          cout << "❌ 参数不足！用法：cd <目录路径>\n";
          continue;
        }
        string dir_path = args[1];
        if (my_file_if.change_directory(dir_path))
        {
          cout << "✅ 切换到目录：" << my_dir.get_current_work_dir_path() << "\n";
        }
        else
        {
          cout << "❌ 目录切换失败（路径不存在）：" << dir_path << "\n";
        }
      }
      else if (cmd == "rmdir")
      {
        if (args.size() < 2)
        {
          cout << "❌ 参数不足！用法：rmdir [-f] <目录路径>\n";
          continue;
        }
        bool force = false;
        string dir_path;
        if (args[1] == "-f")
        {
          if (args.size() < 3)
          {
            cout << "❌ 参数不足！用法：rmdir -f <目录路径>\n";
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
          cout << "✅ 目录删除成功：" << dir_path << "\n";
        }
        else
        {
          cout << "❌ 目录删除失败（" << (force ? "路径不存在/被占用" : "非空/路径不存在/被占用") << "）：" << dir_path << "\n";
        }
      }
      else if (cmd == "touch")
      {
        if (args.size() < 2)
        {
          cout << "❌ 参数不足！用法：touch <文件路径>\n";
          continue;
        }
        string file_path = args[1];
        FilePermission perm = static_cast<FilePermission>(File_READ | File_WRITE | File_DEL);
        if (my_file_if.create_file(file_path, perm, ""))
        {
          cout << "✅ 文件创建成功：" << file_path << "\n";
        }
        else
        {
          cout << "❌ 文件创建失败（路径不存在/已存在/磁盘满）：" << file_path << "\n";
        }
      }
      else if (cmd == "write")
      {
        if (args.size() < 3)
        {
          cout << "❌ 参数不足！用法：write <文件路径> <内容>\n";
          continue;
        }
        string file_path = args[1];
        size_t pos1 = input.find(' ');
        size_t pos2 = input.find(' ', pos1 + 1);
        string content = (pos2 != string::npos) ? input.substr(pos2 + 1) : "";
        if (my_file_if.write_file(file_path, content))
        {
          cout << "✅ 文件内容写入完成！\n";
        }
        else
        {
          cout << "❌ 文件内容写入失败！\n";
        }
      }
      else if (cmd == "read")
      {
        if (args.size() < 2)
        {
          cout << "❌ 参数不足！用法：read <文件路径>\n";
          continue;
        }
        string file_path = args[1];
        string full_content = my_file_if.read_file(file_path);
        if (!full_content.empty())
        {
          cout << "\n📄 文件完整内容 (" << file_path << ")：\n"
               << "------------------------\n"
               << full_content
               << "\n------------------------\n";
        }
        else
        {
          cout << "❌ 读取整个文件失败（文件不存在/无读权限）：" << file_path << "\n";
        }
      }
      else if (cmd == "cat")
      {
        if (args.size() < 3)
        {
          cout << "❌ 参数不足！用法：cat <文件路径> <逻辑块号>\n";
          continue;
        }
        string file_path = args[1];
        int logic_block = 0;
        try
        {
          logic_block = stoi(args[2]);
        }
        catch (...)
        {
          cout << "❌ 块号必须是数字！\n";
          continue;
        }
        string content = my_file_if.view_file_block(file_path, logic_block);
        if (!content.empty())
        {
          cout << "\n📝 文件块内容 (" << file_path << " - 逻辑块" << logic_block << ")：\n"
               << "------------------------\n"
               << content
               << "\n------------------------\n";
        }
        else
        {
          cout << "❌ 读取文件块失败（文件不存在/块号无效）：" << file_path << "\n";
        }
      }
      else if (cmd == "edit")
      {
        if (args.size() < 4)
        {
          cout << "❌ 参数不足！用法：edit <文件路径> <逻辑块号> <修改内容>\n";
          continue;
        }
        string file_path = args[1];
        int logic_block = 0;
        try
        {
          logic_block = stoi(args[2]);
        }
        catch (...)
        {
          cout << "❌ 块号必须是数字！\n";
          continue;
        }
        size_t pos1 = input.find(' ');
        size_t pos2 = input.find(' ', pos1 + 1);
        size_t pos3 = input.find(' ', pos2 + 1);
        string content = (pos3 != string::npos) ? input.substr(pos3 + 1) : "";
        if (my_file_if.modify_file_block(file_path, logic_block, content))
        {
          cout << "✅ 文件块修改成功！新内容：\n"
               << content << "\n";
        }
        else
        {
          cout << "❌ 文件块修改失败（文件不存在/块号无效）：" << file_path << "\n";
        }
      }
      else if (cmd == "rm")
      {
        if (args.size() < 2)
        {
          cout << "❌ 参数不足！用法：rm <文件路径>\n";
          continue;
        }
        string file_path = args[1];
        if (my_file_if.delete_file(file_path, current_pid))
        {
          cout << "✅ 文件删除成功：" << file_path << "\n";
        }
        else
        {
          cout << "❌ 文件删除失败（文件不存在/被锁定）：" << file_path << "\n";
        }
      }
      else if (cmd == "blocks")
      {
        if (args.size() < 2)
        {
          cout << "❌ 参数不足！用法：blocks <文件路径>\n";
          continue;
        }
        string file_path = args[1];
        vector<int> block_list = my_file_if.get_file_all_blocks(file_path);
        if (!block_list.empty())
        {
          cout << "🖧 文件物理块列表 (" << file_path << ")：";
          for (size_t i = 0; i < block_list.size(); ++i)
          {
            cout << block_list[i];
            if (i != block_list.size() - 1)
              cout << " → ";
          }
          cout << "\n";
        }
        else
        {
          cout << "⚠️  文件无物理块或路径不存在：" << file_path << "\n";
        }
      }
      else if (cmd == "stat")
      {
        if (args.size() < 2)
        {
          cout << "❌ 参数不足！用法：stat <文件路径>\n";
          continue;
        }
        string file_path = args[1];
        FCB fcb;
        if (my_dir.query_file(file_path, fcb))
        {
          print_fcb_info(fcb);
        }
        else
        {
          cout << "❌ 查询FCB失败（文件不存在）：" << file_path << "\n";
        }
      }
      else if (cmd == "lock")
      {
        if (args.size() < 2)
        {
          cout << "❌ 参数不足！用法：lock <文件路径>\n";
          continue;
        }
        string file_path = args[1];
        if (my_dir.lock_file(file_path, current_pid))
        {
          cout << "🔒 文件锁定成功：" << file_path << "（进程ID：" << current_pid << "）\n";
        }
        else
        {
          cout << "❌ 文件锁定失败（文件不存在/已被锁定）：" << file_path << "\n";
        }
      }
      else if (cmd == "unlock")
      {
        if (args.size() < 2)
        {
          cout << "❌ 参数不足！用法：unlock <文件路径>\n";
          continue;
        }
        string file_path = args[1];
        if (my_dir.unlock_file(file_path))
        {
          cout << "🔓 文件解锁成功：" << file_path << "\n";
        }
        else
        {
          cout << "❌ 文件解锁失败（文件不存在/未被锁定）：" << file_path << "\n";
        }
      }
      else if (cmd == "truncate")
      {
        if (args.size() < 3)
        {
          cout << "❌ 参数不足！用法：truncate <文件路径> <最大逻辑块号>\n";
          continue;
        }
        string file_path = args[1];
        int max_block = 0;
        try
        {
          max_block = stoi(args[2]);
        }
        catch (...)
        {
          cout << "❌ 块号必须是数字！\n";
          continue;
        }
        if (my_file_if.truncate_file(file_path, max_block))
        {
          cout << "✂️  文件截断成功：" << file_path << "（保留到逻辑块" << max_block << "）\n";
        }
        else
        {
          cout << "❌ 文件截断失败（文件不存在/块号超出范围）：" << file_path << "\n";
        }
      }
      else if (cmd == "start_scheduler")
      {
        try
        {
          proc_mgr.start_scheduler();
        }
        catch (const exception &e)
        {
          cout << "❌ 调度器初始化失败：" << e.what() << "\n";
        }
      }
      else if (cmd == "run_scheduler")
      {
        try
        {
          proc_mgr.run_scheduler();
        }
        catch (const exception &e)
        {
          cout << "❌ 启动调度执行失败：" << e.what() << "\n";
        }
      }
      else if (cmd == "stop_scheduling")
      {
        try
        {
          proc_mgr.stop_scheduling();
        }
        catch (const exception &e)
        {
          cout << "❌ 停止调度执行失败：" << e.what() << "\n";
        }
      }
      else if (cmd == "stop_scheduler")
      {
        try
        {
          proc_mgr.stop_all_processes();
          cout << "🛑 调度器已停止，所有进程已终止，就绪队列已清空\n";
        }
        catch (const exception &e)
        {
          cout << "❌ 调度器停止失败：" << e.what() << "\n";
        }
      }
      else if (cmd == "list_ready")
      {
        print_ready_queue(proc_mgr);
      }
      else if (cmd == "create_proc")
      {
        if (args.size() < 3)
        {
          cout << "❌ 参数不足！用法：create_proc <命令类型> <参数JSON>\n";
          cout << "📌 示例参考 help 命令中的进程创建示例\n";
          continue;
        }
        string cmd_type_str = args[1];
        ::CommandType cmd_type; // 显式指定全局命名空间
        try
        {
          if (cmd_type_str == "DIR_PWD")
            cmd_type = ::CommandType::DIR_PWD;
          else if (cmd_type_str == "DIR_LS")
            cmd_type = ::CommandType::DIR_LS;
          else if (cmd_type_str == "DIR_MKDIR")
            cmd_type = ::CommandType::DIR_MKDIR;
          else if (cmd_type_str == "DIR_CD")
            cmd_type = ::CommandType::DIR_CD;
          else if (cmd_type_str == "DIR_RMDIR")
            cmd_type = ::CommandType::DIR_RMDIR;
          else if (cmd_type_str == "FILE_TOUCH")
            cmd_type = ::CommandType::FILE_TOUCH;
          else if (cmd_type_str == "FILE_WRITE")
            cmd_type = ::CommandType::FILE_WRITE;
          else if (cmd_type_str == "FILE_READ")
            cmd_type = ::CommandType::FILE_READ;
          else if (cmd_type_str == "FILE_CAT")
            cmd_type = ::CommandType::FILE_CAT;
          else if (cmd_type_str == "FILE_EDIT")
            cmd_type = ::CommandType::FILE_EDIT;
          else if (cmd_type_str == "FILE_RM")
            cmd_type = ::CommandType::FILE_RM;
          else if (cmd_type_str == "CREATE_FILE")
            cmd_type = ::CommandType::CREATE_FILE;
          else if (cmd_type_str == "DELETE_FILE")
            cmd_type = ::CommandType::DELETE_FILE;
          else if (cmd_type_str == "MKDIR")
            cmd_type = ::CommandType::DIR_MKDIR; // 兼容原有MKDIR
          else if (cmd_type_str == "QUERY_DIR")
            cmd_type = ::CommandType::DIR_LS; // 兼容原有QUERY_DIR
          else
            throw runtime_error("不支持的命令类型：" + cmd_type_str);
        }
        catch (const exception &e)
        {
          cout << "❌ " << e.what() << "\n";
          continue;
        }
        size_t json_start = input.find('{');
        size_t json_end = input.rfind('}');
        if (json_start == string::npos || json_end == string::npos || json_start > json_end)
        {
          cout << "❌ JSON格式错误！必须包含 {} 包裹参数（无参数填 {}）\n";
          continue;
        }
        string args_json_str = input.substr(json_start, json_end - json_start + 1);
        try
        {
          json::parse(args_json_str);
        }
        catch (const json::exception &e) // 捕获所有JSON相关异常
        {
          cout << "❌ JSON格式错误：" << e.what() << "\n";
          continue;
        }
        try
        {
          pid_t new_pid = proc_mgr.create_process(cmd_type, args_json_str);
          cout << "📌 进程创建请求已提交，PID：" << new_pid << "\n";
        }
        catch (const exception &e)
        {
          cout << "❌ 进程创建失败：" << e.what() << "\n";
        }
      }
      else if (cmd == "list_procs")
      {
        print_process_list(proc_mgr);
      }
      else if (cmd == "recv_msg")
      {
        pid_t recv_pid = 0;
        if (args.size() >= 2)
        {
          try
          {
            recv_pid = stoi(args[1]);
          }
          catch (...)
          {
            cout << "❌ PID必须是数字！\n";
            continue;
          }
        }
        try
        {
          Message msg;
          cout << "📩 等待接收消息（PID=" << recv_pid << "）...（按Ctrl+C中断）\n";
          if (msg_queue.receive_message(recv_pid, msg))
          {
            cout << "\n===== 收到消息 =====\n"
                 << "发送方PID：" << msg.sender_pid << "\n"
                 << "接收方PID：" << msg.receiver_pid << "\n"
                 << "消息类型：" << (msg.type == MessageType::RES_RESULT ? "执行结果" : "自定义消息") << "\n"
                 << "消息内容：" << msg.content << "\n"
                 << "===================\n";
          }
          else
          {
            cout << "⚠️  未收到消息（超时/队列空）\n";
          }
        }
        catch (const exception &e)
        {
          cout << "❌ 接收消息失败：" << e.what() << "\n";
        }
      }
      else if (cmd == "send_msg")
      {
        if (args.size() < 3)
        {
          cout << "❌ 参数不足！用法：send_msg <接收PID> <内容>\n";
          continue;
        }
        pid_t recv_pid = 0;
        try
        {
          recv_pid = stoi(args[1]);
        }
        catch (...)
        {
          cout << "❌ PID必须是数字！\n";
          continue;
        }
        size_t pos1 = input.find(' ');
        size_t pos2 = input.find(' ', pos1 + 1);
        if (pos2 == string::npos)
        {
          cout << "❌ 消息内容不能为空！\n";
          continue;
        }
        string content = input.substr(pos2 + 1);
        Message msg;
        msg.sender_pid = current_pid;
        msg.receiver_pid = recv_pid;
        msg.type = MessageType::MSG_NORMAL;
        msg.content = content;
        try
        {
          if (msg_queue.send_message(msg))
          {
            cout << "✅ 消息发送成功（接收PID：" << recv_pid << "）\n";
          }
          else
          {
            cout << "❌ 消息发送失败！\n";
          }
        }
        catch (const exception &e)
        {
          cout << "❌ 消息发送失败：" << e.what() << "\n";
        }
      }
      else
      {
        cout << "❌ 未知命令！输入 'help' 查看所有可用命令\n";
      }
    }
  }
  catch (const exception &e)
  {
    cerr << "\n💥 程序异常崩溃：" << e.what() << "\n";
    return -1;
  }
  return 0;
}
