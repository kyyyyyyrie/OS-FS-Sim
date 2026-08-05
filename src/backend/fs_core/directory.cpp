#include "directory.hpp"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <iostream>
using namespace std;

// ===================== DirNode 类实现 =====================
/**
 * DirNode构造函数实现
 */
DirNode::DirNode(const std::string &name, DirNode *parent_node)
    : dir_name(name), create_time(get_current_time()),
      // 核心修改：显式转换位运算结果为DirPermission类型
      permission(static_cast<DirPermission>(DirPermission::Dir_READ | DirPermission::Dir_WRITE | DirPermission::Dir_DEL)),
      is_in_use(false), holder_pid(0)
{
  // 根目录特殊处理：父节点指向自身
  if (name.empty() || parent_node == nullptr)
  {
    parent = this;
  }
  else
  {
    parent = parent_node;
  }
}

/**
 * DirNode析构函数实现：递归删除所有子目录
 */
DirNode::~DirNode()
{
  // 递归删除子目录节点
  for (auto &pair : sub_dirs)
  {
    delete pair.second;
    pair.second = nullptr;
  }
  sub_dirs.clear();
  files.clear();
}

// ===================== FCB 类实现 =====================
/**
 * FCB无参构造函数
 */
FCB::FCB()
    : filename(""), create_time(get_current_time()),
      // 显式转换：int → FilePermission
      permission(static_cast<FilePermission>(FilePermission::File_READ | FilePermission::File_WRITE | FilePermission::File_DEL)),
      start_block(-1), total_blocks(0), is_in_use(false), holder_pid(0)
{
}

/**
 * FCB有参构造函数
 */
FCB::FCB(const std::string &name, FilePermission perm, int start, int total)
    : filename(name), create_time(get_current_time()),
      permission(perm), // perm本身是FilePermission类型，无需转换
      start_block(start), total_blocks(total),
      is_in_use(false), holder_pid(0)
{
}

// ===================== Directory 类实现 =====================
/**
 * Directory构造函数
 */
Directory::Directory(Disk &disk, FATTable &fat_table)
    : disk(disk), fat_table(fat_table), root(nullptr), current_work_dir(nullptr),
      dir_meta_start_block(1)
{ // 目录元数据存在磁盘第1块（元数据区）
}

/**
 * Directory析构函数
 */
Directory::~Directory()
{
  // 析构前将目录树刷入磁盘
  if (root != nullptr)
  {
    write_dir_to_disk();
    delete root;
    root = nullptr;
    current_work_dir = nullptr;
  }
}

/**
 * 私有方法：解析多级路径（核心工具方法）
 */
void Directory::parse_path(const std::string &path, bool is_file,
                           DirNode *&target_node, std::string &target_name, bool &success)
{
  // 初始化输出参数
  target_node = nullptr;
  target_name = "";
  success = false;

  // 空路径直接返回失败
  if (path.empty())
  {
    return;
  }

  // 统一路径分隔符：将\\转换为/（Windows适配）
  std::string normalized_path = path;
  std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');

  // 拆分路径为部件
  std::vector<std::string> parts = split_path(normalized_path);
  if (parts.empty())
  {
    return;
  }

  // 确定起始节点：绝对路径从根目录开始，相对路径从当前工作目录开始
  DirNode *current = nullptr;
  if (is_absolute_path(normalized_path))
  {
    current = root;
  }
  else
  {
    current = current_work_dir;
  }

  if (current == nullptr)
  {
    return;
  }

  // 遍历路径部件（最后一个部件是目标名，前面是父路径）
  int part_count = parts.size();
  for (int i = 0; i < part_count; ++i)
  {
    const std::string &part = parts[i];

    // 处理.（当前目录）和..（父目录）
    if (part == ".")
    {
      continue;
    }
    else if (part == "..")
    {
      current = current->parent;
      continue;
    }

    // 最后一个部件：目标名（文件/目录）
    if (i == part_count - 1)
    {
      target_node = current;
      target_name = part;
      success = true;
      return;
    }

    // 非最后一个部件：查找子目录
    auto it = current->sub_dirs.find(part);
    if (it == current->sub_dirs.end())
    {
      // 路径不存在
      return;
    }
    current = it->second;
  }

  // 特殊情况：路径以/结尾（如"/user/"），目标节点是最后一个目录
  if (normalized_path.back() == '/')
  {
    target_node = current;
    target_name = "";
    success = true;
  }
}

/**
 * 私有方法：递归序列化目录树到缓冲区
 */
/**
 * 私有方法：递归序列化目录树到缓冲区
 */
void Directory::serialize_dir_tree(DirNode *node, char *buf, int &offset)
{
  // 边界检查：节点为空/偏移量越界，直接返回
  if (node == nullptr || offset < 0 || offset >= BLOCK_SIZE)
  {
    return;
  }

  // 1. 序列化目录名（长度+内容）
  int name_len = node->dir_name.size();
  if (offset + sizeof(int) + name_len > BLOCK_SIZE)
  {
    std::cout << "[WARN] 目录名序列化越界，跳过：" << node->dir_name << std::endl
              << std::flush;
    return;
  }
  memcpy(buf + offset, &name_len, sizeof(int));
  offset += sizeof(int);
  memcpy(buf + offset, node->dir_name.c_str(), name_len);
  offset += name_len;

  // 2. 序列化子目录数量
  int sub_dir_count = node->sub_dirs.size();
  if (offset + sizeof(int) > BLOCK_SIZE)
  {
    std::cout << "[WARN] 子目录数量序列化越界，跳过" << std::endl
              << std::flush;
    return;
  }
  memcpy(buf + offset, &sub_dir_count, sizeof(int));
  offset += sizeof(int);

  // 3. 递归序列化子目录
  for (auto &pair : node->sub_dirs)
  {
    serialize_dir_tree(pair.second, buf, offset);
    if (offset >= BLOCK_SIZE)
      break; // 越界后立即终止
  }

  // 4. 序列化文件数量
  int file_count = node->files.size();
  if (offset + sizeof(int) > BLOCK_SIZE)
  {
    std::cout << "[WARN] 文件数量序列化越界，跳过" << std::endl
              << std::flush;
    return;
  }
  memcpy(buf + offset, &file_count, sizeof(int));
  offset += sizeof(int);

  // 5. 序列化文件FCB
  for (auto &pair : node->files)
  {
    if (offset >= BLOCK_SIZE)
      break; // 越界后立即终止

    const FCB &fcb = pair.second;
    int f_name_len = fcb.filename.size();
    // 计算FCB所需空间，提前检查
    int fcb_need = sizeof(int) + f_name_len + sizeof(int) + sizeof(int);
    if (offset + fcb_need > BLOCK_SIZE)
    {
      std::cout << "[WARN] 文件FCB序列化越界，跳过：" << fcb.filename << std::endl
                << std::flush;
      continue;
    }

    memcpy(buf + offset, &f_name_len, sizeof(int));
    offset += sizeof(int);
    memcpy(buf + offset, fcb.filename.c_str(), f_name_len);
    offset += f_name_len;
    memcpy(buf + offset, &fcb.start_block, sizeof(int));
    offset += sizeof(int);
    memcpy(buf + offset, &fcb.total_blocks, sizeof(int));
    offset += sizeof(int);
  }
}

/**
 * 私有方法：递归反序列化缓冲区为目录树
 */
DirNode *Directory::deserialize_dir_tree(char *buf, int &offset, DirNode *parent)
{
  // 核心边界检查：偏移量超出块大小，直接返回null
  if (offset < 0 || offset >= BLOCK_SIZE)
  {
    std::cout << "[DEBUG] 反序列化越界，偏移量：" << offset << "，已超出块大小" << std::endl
              << std::flush;
    return nullptr;
  }

  // 1. 反序列化目录名长度（先检查偏移量）
  if (offset + sizeof(int) > BLOCK_SIZE)
  {
    std::cout << "[DEBUG] 目录名长度越界，偏移量：" << offset << std::endl
              << std::flush;
    return nullptr;
  }
  int name_len = 0;
  memcpy(&name_len, buf + offset, sizeof(int));
  offset += sizeof(int);

  // 检查目录名长度合法性（防止超大值导致越界）
  if (name_len <= 0 || name_len > 255)
  {
    std::cout << "[DEBUG] 目录名长度非法：" << name_len << "，终止反序列化" << std::endl
              << std::flush;
    return nullptr;
  }

  // 2. 反序列化目录名内容（检查偏移量）
  if (offset + name_len > BLOCK_SIZE)
  {
    std::cout << "[DEBUG] 目录名内容越界，偏移量：" << offset << "，长度：" << name_len << std::endl
              << std::flush;
    return nullptr;
  }
  char name_buf[256] = {0}; // 固定大小，防止栈溢出
  memcpy(name_buf, buf + offset, name_len);
  offset += name_len;
  std::string dir_name(name_buf);
  std::cout << "[DEBUG] 反序列化目录名：" << dir_name << std::endl
            << std::flush;

  // 3. 创建目录节点
  DirNode *node = new DirNode(dir_name, parent);

  // 4. 反序列化子目录数量（检查偏移量）
  if (offset + sizeof(int) > BLOCK_SIZE)
  {
    std::cout << "[DEBUG] 子目录数量越界，偏移量：" << offset << std::endl
              << std::flush;
    delete node; // 防止内存泄漏
    return nullptr;
  }
  int sub_dir_count = 0;
  memcpy(&sub_dir_count, buf + offset, sizeof(int));
  offset += sizeof(int);
  std::cout << "[DEBUG] 子目录数量：" << sub_dir_count << std::endl
            << std::flush;

  // 5. 递归反序列化子目录（限制数量，防止死循环）
  if (sub_dir_count > 0 && sub_dir_count < 100)
  { // 限制最大子目录数，防止恶意数据
    for (int i = 0; i < sub_dir_count; ++i)
    {
      DirNode *sub_node = deserialize_dir_tree(buf, offset, node);
      if (sub_node != nullptr)
      {
        node->sub_dirs[sub_node->dir_name] = sub_node;
      }
    }
  }

  // 6. 反序列化文件数量（检查偏移量）
  if (offset + sizeof(int) > BLOCK_SIZE)
  {
    std::cout << "[DEBUG] 文件数量越界，偏移量：" << offset << std::endl
              << std::flush;
    delete node;
    return nullptr;
  }
  int file_count = 0;
  memcpy(&file_count, buf + offset, sizeof(int));
  offset += sizeof(int);
  std::cout << "[DEBUG] 文件数量：" << file_count << std::endl
            << std::flush;

  // 7. 反序列化文件FCB（检查偏移量+限制数量）
  if (file_count > 0 && file_count < 100)
  {
    for (int i = 0; i < file_count; ++i)
    {
      // 检查文件名长度偏移
      if (offset + sizeof(int) > BLOCK_SIZE)
      {
        std::cout << "[DEBUG] 文件名长度越界，偏移量：" << offset << std::endl
                  << std::flush;
        break;
      }
      int f_name_len = 0;
      memcpy(&f_name_len, buf + offset, sizeof(int));
      offset += sizeof(int);

      if (f_name_len <= 0 || f_name_len > 255)
      {
        std::cout << "[DEBUG] 文件名长度非法：" << f_name_len << std::endl
                  << std::flush;
        break;
      }

      // 检查文件名内容偏移
      if (offset + f_name_len > BLOCK_SIZE)
      {
        std::cout << "[DEBUG] 文件名内容越界，偏移量：" << offset << "，长度：" << f_name_len << std::endl
                  << std::flush;
        break;
      }
      char f_name_buf[256] = {0};
      memcpy(f_name_buf, buf + offset, f_name_len);
      offset += f_name_len;
      std::string filename(f_name_buf);

      // 检查起始块+总块数偏移
      if (offset + sizeof(int) * 2 > BLOCK_SIZE)
      {
        std::cout << "[DEBUG] 文件块信息越界，偏移量：" << offset << std::endl
                  << std::flush;
        break;
      }
      int start_block = 0;
      memcpy(&start_block, buf + offset, sizeof(int));
      offset += sizeof(int);
      int total_blocks = 0;
      memcpy(&total_blocks, buf + offset, sizeof(int));
      offset += sizeof(int);

      // 创建FCB
      FCB fcb(
          filename,
          static_cast<FilePermission>(FilePermission::File_READ | FilePermission::File_WRITE),
          start_block,
          total_blocks);
      node->files[filename] = fcb;
      std::cout << "[DEBUG] 反序列化文件：" << filename << "，起始块：" << start_block << std::endl
                << std::flush;
    }
  }

  return node;
}

/**
 * 私有方法：从磁盘加载目录树
 */
void Directory::load_dir_from_disk()
{
  std::cout << "[DEBUG] 开始从磁盘加载目录树，读取块：" << dir_meta_start_block << std::endl
            << std::flush;

  char buf[BLOCK_SIZE] = {0};
  bool read_success = disk.read_block(dir_meta_start_block, buf);
  std::cout << "[DEBUG] 磁盘读取结果：" << (read_success ? "成功" : "失败/空数据") << std::endl
            << std::flush;

  root = nullptr;
  int offset = 0; // 显式初始化偏移量，避免随机值

  if (read_success)
  {
    // 反序列化前检查缓冲区是否为空（首次运行时磁盘块全0）
    bool buf_is_empty = true;
    for (int i = 0; i < BLOCK_SIZE; ++i)
    {
      if (buf[i] != 0)
      {
        buf_is_empty = false;
        break;
      }
    }
    std::cout << "[DEBUG] 磁盘缓冲区是否为空：" << (buf_is_empty ? "是" : "否") << std::endl
              << std::flush;

    if (!buf_is_empty)
    {
      root = deserialize_dir_tree(buf, offset, nullptr);
      std::cout << "[DEBUG] 反序列化完成，根目录指针：" << (void *)root << "，最终偏移量：" << offset << std::endl
                << std::flush;
    }
    else
    {
      std::cout << "[DEBUG] 磁盘缓冲区为空，跳过反序列化" << std::endl
                << std::flush;
    }
  }

  // 若反序列化失败/磁盘无数据，创建默认根目录
  if (root == nullptr)
  {
    std::cout << "[DEBUG] 反序列化失败/无目录数据，创建默认根目录" << std::endl
              << std::flush;
    root = new DirNode(""); // 根目录名空，parent指向自身
  }

  current_work_dir = root;
  std::cout << "[DEBUG] 目录树加载完成，当前工作目录：" << (void *)current_work_dir << std::endl
            << std::flush;
}

/**
 * 私有方法：将目录树刷入磁盘
 */
void Directory::write_dir_to_disk()
{
  // 根节点检查
  if (root == nullptr)
  {
    return;
  }

  // 缓冲区初始化
  char buf[BLOCK_SIZE] = {0};
  int offset = 0;

  // 执行序列化
  serialize_dir_tree(root, buf, offset);

  // 检查序列化是否越界
  if (offset > BLOCK_SIZE)
  {
    return;
  }

  // 执行磁盘写入
  disk.write_block(dir_meta_start_block, buf);
}

/**
 * 公有方法：初始化目录管理器
 */
bool Directory::init_directory()
{
  std::cout << "[DEBUG] 开始初始化目录管理器" << std::endl
            << std::flush;

  // 从磁盘加载目录树
  load_dir_from_disk();

  bool init_ok = (root != nullptr && current_work_dir != nullptr);
  std::cout << "[DEBUG] 目录管理器初始化结果：" << (init_ok ? "成功" : "失败")
            << "，root=" << (void *)root << "，current_work_dir=" << (void *)current_work_dir
            << std::endl
            << std::flush;

  return init_ok;
}

/**
 * 公有方法：创建多级目录
 */
bool Directory::create_directory(const std::string &dir_path)
{
  DirNode *parent_node = nullptr;
  std::string dir_name = "";
  bool parse_success = false;

  // 解析路径，获取父节点和新目录名
  parse_path(dir_path, false, parent_node, dir_name, parse_success);
  if (!parse_success || parent_node == nullptr || dir_name.empty())
  {
    return false;
  }

  // 检查目录是否已存在
  if (parent_node->sub_dirs.find(dir_name) != parent_node->sub_dirs.end())
  {
    return false;
  }

  // 创建新目录节点
  DirNode *new_dir = new DirNode(dir_name, parent_node);
  parent_node->sub_dirs[dir_name] = new_dir;

  // 刷入磁盘持久化
  write_dir_to_disk();
  return true;
}

/**
 * 公有方法：删除目录
 */
bool Directory::delete_directory(const std::string &dir_path, bool force)
{
  DirNode *parent_node = nullptr;
  std::string dir_name = "";
  bool parse_success = false;

  // 解析路径
  parse_path(dir_path, false, parent_node, dir_name, parse_success);
  if (!parse_success || parent_node == nullptr || dir_name.empty())
  {
    return false;
  }

  // 查找待删除目录
  auto it = parent_node->sub_dirs.find(dir_name);
  if (it == parent_node->sub_dirs.end())
  {
    return false;
  }
  DirNode *del_dir = it->second;

  // 检查目录是否被占用
  if (del_dir->is_in_use)
  {
    return false;
  }

  // 非强制删除时，检查目录是否为空
  if (!force && (!del_dir->sub_dirs.empty() || !del_dir->files.empty()))
  {
    return false;
  }

  // 强制删除时，递归删除所有子目录和文件
  if (force)
  {
    // 删除子目录（DirNode析构会递归删除）
    del_dir->sub_dirs.clear();
    // 删除文件（仅移除FCB，盘块释放由FileInterface处理）
    del_dir->files.clear();
  }

  // 从父目录移除并删除节点
  parent_node->sub_dirs.erase(it);
  delete del_dir;

  // 刷入磁盘
  write_dir_to_disk();
  return true;
}

/**
 * 公有方法：切换当前工作目录
 */
bool Directory::change_directory(const std::string &dir_path)
{
  DirNode *parent_node = nullptr;
  std::string dir_name = "";
  bool parse_success = false;

  if (dir_path == "..")
  {
    if (current_work_dir == root || current_work_dir->parent == root)
    {
      current_work_dir = root;
      return true;
    }
    current_work_dir = current_work_dir->parent;
    return true;
  }

  // 特殊处理：直接切换到根目录
  if (dir_path == "/" || dir_path == "\\")
  {
    current_work_dir = root;
    return true;
  }

  // 解析路径
  parse_path(dir_path, false, parent_node, dir_name, parse_success);
  if (!parse_success || parent_node == nullptr)
  {
    return false;
  }

  // 路径是单个目录名（如"user"）
  if (!dir_name.empty())
  {
    auto it = parent_node->sub_dirs.find(dir_name);
    if (it == parent_node->sub_dirs.end())
    {
      return false;
    }
    current_work_dir = it->second;
  }
  else
  {
    // 路径是目录本身（如"/user/"）
    current_work_dir = parent_node;
  }

  return true;
}

/**
 * 公有方法：查询目录内容
 */
std::string Directory::query_directory(const std::string &dir_path)
{
  DirNode *parent_node = nullptr;
  std::string dir_name = "";
  bool parse_success = false;

  // 解析路径
  parse_path(dir_path, false, parent_node, dir_name, parse_success);
  if (!parse_success || parent_node == nullptr)
  {
    return "";
  }

  // 目标目录节点（路径是目录名时，parent_node是父节点，dir_name是目标目录）
  DirNode *target_dir = parent_node;
  if (!dir_name.empty())
  {
    auto it = parent_node->sub_dirs.find(dir_name);
    if (it == parent_node->sub_dirs.end())
    {
      return "";
    }
    target_dir = it->second;
  }

  // 格式化输出目录内容
  std::ostringstream oss;
  oss << "目录: " << get_current_work_dir_path() << "/" << dir_name << "\n";
  oss << "------------------------\n";
  oss << "[子目录]\n";
  for (auto &pair : target_dir->sub_dirs)
  {
    oss << "  " << pair.first << "/\n";
  }
  oss << "[文件]\n";
  for (auto &pair : target_dir->files)
  {
    const FCB &fcb = pair.second;
    oss << "  " << fcb.filename << " (起始块：" << fcb.start_block << "，大小："
        << fcb.total_blocks * BLOCK_SIZE << "字节)\n";
  }

  return oss.str();
}

/**
 * 公有方法：递归列出所有目录树
 */
std::string Directory::list_all_dirs(DirNode *node, int depth)
{
  if (node == nullptr)
  {
    node = root;
  }

  std::ostringstream oss;
  // 根目录显示为/
  if (depth == 0)
  {
    oss << "/\n";
  }
  else
  {
    // 缩进格式化
    for (int i = 0; i < depth - 1; ++i)
    {
      oss << "│  ";
    }
    oss << "├─ " << node->dir_name << "/\n";
  }

  // 递归列出子目录
  for (auto &pair : node->sub_dirs)
  {
    oss << list_all_dirs(pair.second, depth + 1);
  }

  // 列出当前目录下的文件
  for (auto &pair : node->files)
  {
    for (int i = 0; i < depth; ++i)
    {
      oss << "│  ";
    }
    oss << "├─ " << pair.first << "\n";
  }

  return oss.str();
}

/**
 * 公有方法：获取当前工作目录路径
 */
std::string Directory::get_current_work_dir_path()
{
  if (current_work_dir == nullptr || current_work_dir == root)
  {
    return "/";
  }

  // 从当前目录向上回溯到根目录
  std::vector<std::string> path_parts;
  DirNode *current = current_work_dir;
  while (current != root && current != nullptr)
  {
    path_parts.push_back(current->dir_name);
    current = current->parent;
  }

  // 反转路径部件（从根到当前）
  std::reverse(path_parts.begin(), path_parts.end());

  // 拼接路径
  std::ostringstream oss;
  oss << "/";
  for (size_t i = 0; i < path_parts.size(); ++i)
  {
    oss << path_parts[i];
    if (i != path_parts.size() - 1)
    {
      oss << "/";
    }
  }

  return oss.str();
}

/**
 * 公有方法：获取父目录路径
 */
std::string Directory::get_parent_dir_path()
{
  if (current_work_dir == nullptr || current_work_dir == root)
  {
    return "/";
  }

  DirNode *parent = current_work_dir->parent;
  if (parent == root)
  {
    return "/";
  }

  // 复用当前目录路径逻辑
  std::vector<std::string> path_parts;
  DirNode *current = parent;
  while (current != root && current != nullptr)
  {
    path_parts.push_back(current->dir_name);
    current = current->parent;
  }

  std::reverse(path_parts.begin(), path_parts.end());
  std::ostringstream oss;
  oss << "/";
  for (size_t i = 0; i < path_parts.size(); ++i)
  {
    oss << path_parts[i];
    if (i != path_parts.size() - 1)
    {
      oss << "/";
    }
  }

  return oss.str();
}

/**
 * 公有方法：判断是否是根目录
 */
bool Directory::is_current_dir_root() const
{
  return current_work_dir == root;
}

/**
 * 公有方法：获取父节点
 */
DirNode *Directory::get_parent_node(DirNode *node) const
{
  if (node == nullptr)
  {
    return nullptr;
  }
  return node->parent;
}

/**
 * 公有方法：添加文件FCB
 */
bool Directory::add_file(const std::string &file_path, const FCB &fcb)
{
  DirNode *parent_node = nullptr;
  std::string filename = "";
  bool parse_success = false;

  // 解析文件路径
  parse_path(file_path, true, parent_node, filename, parse_success);

  // 路径解析失败/父节点为空/文件名空，直接返回失败
  if (!parse_success || parent_node == nullptr || filename.empty())
  {
    return false;
  }

  // 检查文件是否已存在
  if (parent_node->files.find(filename) != parent_node->files.end())
  {
    return false;
  }

  // 添加FCB到目录节点
  parent_node->files[filename] = fcb;

  // 将目录树刷入磁盘持久化
  write_dir_to_disk();

  return true;
}
/**
 * 公有方法：移除文件FCB
 */
bool Directory::remove_file(const std::string &file_path)
{
  DirNode *parent_node = nullptr;
  std::string filename = "";
  bool parse_success = false;

  // 解析路径
  parse_path(file_path, true, parent_node, filename, parse_success);
  if (!parse_success || parent_node == nullptr || filename.empty())
  {
    return false;
  }

  // 查找文件
  auto it = parent_node->files.find(filename);
  if (it == parent_node->files.end())
  {
    return false;
  }

  // 检查文件是否被占用
  if (it->second.is_in_use)
  {
    return false;
  }

  // 移除FCB
  parent_node->files.erase(it);

  // 刷入磁盘
  write_dir_to_disk();
  return true;
}

/**
 * 公有方法：查询文件FCB
 */
bool Directory::query_file(const std::string &file_path, FCB &fcb)
{
  DirNode *parent_node = nullptr;
  std::string filename = "";
  bool parse_success = false;

  // 解析路径
  parse_path(file_path, true, parent_node, filename, parse_success);
  if (!parse_success || parent_node == nullptr || filename.empty())
  {
    return false;
  }

  // 查找文件
  auto it = parent_node->files.find(filename);
  if (it == parent_node->files.end())
  {
    return false;
  }

  // 输出FCB
  fcb = it->second;
  return true;
}

/**
 * 公有方法：锁定文件
 */
bool Directory::lock_file(const std::string &file_path, pid_t pid)
{
  DirNode *parent_node = nullptr;
  std::string filename = "";
  bool parse_success = false;

  // 解析路径
  parse_path(file_path, true, parent_node, filename, parse_success);
  if (!parse_success || parent_node == nullptr || filename.empty())
  {
    return false;
  }

  // 查找文件
  auto it = parent_node->files.find(filename);
  if (it == parent_node->files.end())
  {
    return false;
  }

  FCB &fcb = it->second;
  // 检查是否已被其他进程锁定
  if (fcb.is_in_use && fcb.holder_pid != pid)
  {
    return false;
  }

  // 锁定文件
  fcb.is_in_use = true;
  fcb.holder_pid = pid;

  return true;
}

/**
 * 公有方法：解锁文件
 */
bool Directory::unlock_file(const std::string &file_path)
{
  DirNode *parent_node = nullptr;
  std::string filename = "";
  bool parse_success = false;

  // 解析路径
  parse_path(file_path, true, parent_node, filename, parse_success);
  if (!parse_success || parent_node == nullptr || filename.empty())
  {
    return false;
  }

  // 查找文件
  auto it = parent_node->files.find(filename);
  if (it == parent_node->files.end())
  {
    return false;
  }

  // 解锁文件
  FCB &fcb = it->second;
  fcb.is_in_use = false;
  fcb.holder_pid = 0;

  return true;
}

// directory.cpp 中新增实现
bool Directory::update_file_fcb(const std::string &file_path, const FCB &fcb)
{
  DirNode *parent_node = nullptr;
  std::string filename = "";
  bool parse_success = false;

  // 1. 解析文件路径
  parse_path(file_path, true, parent_node, filename, parse_success);
  if (!parse_success || parent_node == nullptr || filename.empty())
  {
    return false;
  }

  // 2. 查找已存在的文件FCB
  auto it = parent_node->files.find(filename);
  if (it == parent_node->files.end())
  {
    std::cerr << "[ERROR] 更新FCB失败：文件" << file_path << "不存在\n";
    return false;
  }

  // 3. 更新FCB内容
  it->second = fcb;

  // 4. 持久化到磁盘
  write_dir_to_disk();
  return true;
}
