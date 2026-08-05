#include "common.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

// 获取当前时间（Windows适配）
std::string get_current_time()
{
  time_t now = time(nullptr);
  tm local_tm;
  localtime_s(&local_tm, &now); // Windows安全函数
  std::ostringstream oss;
  oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

// 拆分路径为部件（如"/user/doc" → ["user", "doc"]）
std::vector<std::string> split_path(const std::string &path)
{
  std::vector<std::string> parts;
  std::stringstream ss(path);
  std::string part;
  while (std::getline(ss, part, '/'))
  {
    if (!part.empty())
    {
      parts.push_back(part);
    }
  }
  return parts;
}

// 判断是否为绝对路径
bool is_absolute_path(const std::string &path)
{
  return !path.empty() && (path[0] == '/' || path[0] == '\\');
}