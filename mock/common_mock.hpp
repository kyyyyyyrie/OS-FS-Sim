#pragma once
#include <string>
#include <ctime>
#include <cstdint>
#include <vector>
#include <windows.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

// ========== 完全对齐你的定义：Windows适配pid_t（避免重复定义） ==========
#ifndef PID_T_DEFINED
#define PID_T_DEFINED
typedef DWORD pid_t;
#endif

// ========== 完全对齐你的全局常量（值和名称1:1匹配） ==========
const int BLOCK_SIZE = 64;
const int TOTAL_BLOCKS = 1024;
const int BUFFER_PAGE_NUM = 8;  // 新增：对齐你的common.hpp
const int METADATA_BLOCKS = 64; // 修正：从16改为64，对齐你的定义

// ========== 完全对齐你的枚举定义（类型、值、作用域1:1匹配） ==========
enum FilePermission
{
  File_READ = 1,
  File_WRITE = 2,
  File_DEL = 4
};

enum DirPermission
{
  Dir_READ = 1,
  Dir_WRITE = 2,
  Dir_DEL = 4
};

// 强类型枚举（enum class），对齐你的定义（FREE=0、END=-1）
enum class FATEntryType
{
  FREE = 0,
  END = -1
};

enum class BufferPageStatus
{
  CLEAN,
  DIRTY
};

enum class CommandType
{
  CREATE_FILE,
  QUERY_DIR,
  VIEW_FILE_BLOCK,
  MODIFY_FILE_BLOCK,
  DELETE_FILE,
  MKDIR,
  RMDIR,
  CD,
  CD_BACK
};

enum class MessageType
{
  REQ_DISK,
  REQ_BUFFER,
  RES_RESULT
};

// ========== 完全对齐你的Message结构体 ==========
struct Message
{
  pid_t sender_pid;
  pid_t receiver_pid;
  MessageType type;
  std::string content;
};

// ========== 实现你的common.hpp声明的工具函数（避免链接错误） ==========
inline std::string get_current_time()
{
  std::time_t now = std::time(nullptr);
  std::tm tm = *std::localtime(&now);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

inline std::vector<std::string> split_path(const std::string &path)
{
  std::vector<std::string> parts;
  std::string temp;
  for (char c : path)
  {
    if (c == '/')
    {
      if (!temp.empty())
      {
        parts.push_back(temp);
        temp.clear();
      }
    }
    else
    {
      temp += c;
    }
  }
  if (!temp.empty())
    parts.push_back(temp);
  return parts;
}

inline std::string join_path(const std::vector<std::string> &parts)
{
  if (parts.empty())
    return "/";
  std::string path = "/";
  for (size_t i = 0; i < parts.size(); ++i)
  {
    path += parts[i];
    if (i != parts.size() - 1)
      path += "/";
  }
  return path;
}

inline bool is_absolute_path(const std::string &path)
{
  return !path.empty() && path[0] == '/';
}