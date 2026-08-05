#pragma once
#include <string>
#include <ctime>
#include <cstdint>
#include <vector>
#include <windows.h> // Windows核心API头文件

// ========== Windows适配：定义进程/线程ID类型 ==========
typedef DWORD pid_t;

/** 全局常量定义 **/
const int BLOCK_SIZE = 128;
const int TOTAL_BLOCKS = 1024;
const int BUFFER_PAGE_NUM = 8;
const int METADATA_BLOCKS = 64;
const int BLOCK_WRITE_DELAY_MS = 1000; // 块与块之间的写入延时（1s）
const int BUFFER_SWAP_DELAY_MS = 1000; // 缓冲页换页延时（1秒）

/** 权限/状态枚举定义 **/
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

// 命令类型枚举（修复：确保闭合符+分号）
enum class CommandType
{
  CREATE_FILE,
  DELETE_FILE,
  MKDIR,
  QUERY_DIR,
  RMDIR,
  DIR_CD,
  CD_BACK,
  VIEW_FILE_BLOCK,
  MODIFY_FILE_BLOCK,
  DIR_PWD,
  DIR_LS,
  DIR_MKDIR,
  DIR_RMDIR,
  FILE_TOUCH,
  FILE_WRITE,
  FILE_READ,
  FILE_CAT,
  FILE_EDIT,
  FILE_RM
}; // 修复：补充枚举闭合的}和分号

// 进程通信消息类型枚举（修复：确保闭合）
enum class MessageType
{
  REQ_DISK,
  REQ_BUFFER,
  RES_RESULT,
  MSG_NORMAL
}; // 修复：补充枚举闭合的}和分号

// 进程通信消息结构体（修复：确保闭合+分号）
struct Message
{
  pid_t sender_pid;
  pid_t receiver_pid;
  MessageType type;
  std::string content;
}; // 修复：补充结构体闭合的}和分号

/** 通用工具函数声明（修复：确保类型完整） **/
// 获取格式化当前时间
std::string get_current_time();

// 拆分路径为部件列表
std::vector<std::string> split_path(const std::string &path);

// 拼接路径部件为完整路径
std::string join_path(const std::vector<std::string> &parts);

// 判断是否为绝对路径
bool is_absolute_path(const std::string &path);