#pragma once
#include "../../common/common.hpp"
#include "../storage/disk.hpp"
#include "../storage/fat_table.hpp"
#include <map>
#include <string>
#include <vector>

// 模拟测试使用
// #pragma once
// #include "../mock/common_mock.hpp"
// #include "../mock/disk_file_mock.hpp"
// #include "../mock/fat_table_file_mock.hpp"
// #include <map>
// #include <string>
// #include <vector>

// 前置声明：文件控制块类，解决循环包含问题
class FCB;

/**
 * 目录节点类（多级目录树形结构的核心单元）
 * 每个目录节点包含自身元数据、父目录指针、子目录映射、文件映射，
 *          支持Windows路径格式（兼容/和\\分隔符），用于模拟文件系统的目录管理。
 * 根目录的parent指针指向自身，dir_name为空字符串""
 */
class DirNode
{
public:
  std::string dir_name;                      // 目录名（如"doc"，根目录为空""）
  std::string create_time;                   // 目录创建时间（格式：YYYY-MM-DD HH:MM:SS）
  DirPermission permission;                  // 目录权限（读/写/删除，支持位运算组合）
  DirNode *parent;                           // 父目录指针（根目录指向自身）
  std::map<std::string, DirNode *> sub_dirs; // 子目录映射（键：子目录名，值：子目录指针）
  std::map<std::string, FCB> files;          // 目录下的文件映射（键：文件名，值：文件控制块）
  bool is_in_use;                            // 目录是否被进程占用（防止并发删除）
  pid_t holder_pid;                          // 占用目录的进程ID（Windows DWORD类型）

  /**
   * 构造函数：初始化目录节点
   * name 目录名（根目录传空字符串""）
   * parent_node 父目录节点指针（根目录传nullptr，内部会指向自身）
   *        DirNode* root = new DirNode(""); // 创建根目录节点
   *        DirNode* user = new DirNode("user", root); // 创建/user目录节点
   */
  DirNode(const std::string &name, DirNode *parent_node = nullptr);

  /**
   * 析构函数：递归删除所有子目录节点
   * 释放当前目录下所有子目录的内存，避免内存泄漏，
   * 调用时会自动遍历sub_dirs删除所有子目录指针。
   */
  ~DirNode();
};

/**
 * 文件控制块类（FCB，存储文件元数据）
 * 记录文件的核心属性，不存储文件数据（数据存在磁盘盘块中），
 * 是文件系统管理文件的核心元数据结构。
 */
class FCB
{
public:
  std::string filename;      // 纯文件名（不含路径，如"test.txt"）
  std::string create_time;   // 文件创建时间（格式：YYYY-MM-DD HH:MM:SS）
  FilePermission permission; // 文件权限（读/写/删除，支持位运算组合）
  int start_block;           // 文件起始盘块号（数据区起始，≥METADATA_BLOCKS）
  int total_blocks;          // 文件占用的总盘块数（≥1）
  bool is_in_use;            // 文件是否被进程占用（防止并发修改/删除）
  pid_t holder_pid;          // 占用文件的进程ID（Windows DWORD类型）
  int file_size = 0;         // 新增：文件真实字节数（非块大小×块数）

  /**
   * 无参构造函数：初始化空FCB
   * 所有成员变量设为默认值（如start_block=-1，total_blocks=0），
   * 用于后续通过赋值填充元数据。
   */
  FCB();

  /**
   *有参构造函数：初始化带元数据的FCB
   * name 纯文件名（不含路径）
   * perm 文件权限（如FilePermission::READ | FilePermission::WRITE）
   * start 文件起始盘块号（必须是数据区合法盘块号）
   * total 文件占用的总盘块数（≥1）
   *        FCB fcb("test.txt", FilePermission::READ|FilePermission::WRITE, 64, 1);
   */
  FCB(const std::string &name, FilePermission perm, int start, int total);
};

/**
 * 多级目录管理器类
 *  封装目录树的创建、删除、切换、查询等核心操作，
 *  支持绝对路径（如"/user/doc"）和相对路径（如"../test"）解析，
 *  目录元数据持久化存储到磁盘元数据区，保证重启后数据不丢失。
 */
class Directory
{
private:
  Disk &disk;                // 关联的模拟磁盘（引用，不可修改）
  FATTable &fat_table;       // 关联的FAT表（引用，用于管理文件盘块）
  DirNode *root;             // 根目录节点（唯一，生命周期由本类管理）
  DirNode *current_work_dir; // 当前工作目录指针（默认指向根目录）
  int dir_meta_start_block;  // 目录树元数据在磁盘的起始盘块号（固定为1）

  /**
   * 私有核心方法：解析多级路径
   * 将输入路径（绝对/相对）解析为目标目录/文件所在的父节点和目标名，
   * 是所有路径相关操作的底层支撑函数。
   * path 待解析的路径（如"/user/doc/test.txt"或"../doc"）
   * is_file 是否是文件路径（true=文件，false=目录）
   * target_node 输出参数：解析后的目标父节点指针
   * target_name 输出参数：解析后的目标名（文件名/目录名）
   *  success 输出参数：解析是否成功（true=成功，false=路径不存在/非法）
   * 解析失败时target_node设为nullptr，target_name设为空字符串
   */
  void parse_path(const std::string &path, bool is_file,
                  DirNode *&target_node, std::string &target_name, bool &success);

  /**
   * 私有方法：递归序列化目录树到缓冲区
   * 将目录树的所有元数据（目录名、权限、文件FCB等）转换为字节流，
   *          用于写入磁盘元数据区持久化存储。
   * node 当前待序列化的目录节点
   *  buf 输出缓冲区（需提前分配足够内存）
   *  offset 缓冲区偏移量（引用，记录当前写入位置）
   */
  void serialize_dir_tree(DirNode *node, char *buf, int &offset);

  /**
   *  私有方法：递归反序列化缓冲区为目录树
   *  从磁盘读取的字节流恢复为目录树结构，用于系统启动时加载目录元数据。
   *  buf 输入缓冲区（磁盘读取的目录元数据字节流）
   *  offset 缓冲区偏移量（引用，记录当前读取位置）
   *  parent 当前节点的父目录指针
   *  反序列化后的目录节点指针（失败返回nullptr）
   */
  DirNode *deserialize_dir_tree(char *buf, int &offset, DirNode *parent);

  /**
   *  私有方法：从磁盘加载目录树到内存
   *  从磁盘元数据区（dir_meta_start_block）读取目录元数据，
   *          调用deserialize_dir_tree恢复目录树结构。
   *  若磁盘无目录元数据，则创建默认根目录
   */
  void load_dir_from_disk();

public:
  /**
   * @brief 构造函数：关联磁盘和FAT表
   * @param disk 模拟磁盘实例（引用，必须已初始化）
   * @param fat_table FAT表实例（引用，必须已初始化）
   * @example
   *        Disk disk("./sim_disk.bin");
   *        FATTable fat(disk);
   *        Directory dir(disk, fat);
   */
  Directory(Disk &disk, FATTable &fat_table);

  /**
   * @brief 析构函数：递归删除目录树
   * @details 调用DirNode的析构函数，递归释放所有目录节点内存，
   *          并将最终的目录树刷入磁盘。
   */
  ~Directory();

  /**
   * @brief 初始化目录管理器（创建根目录）
   * @return true=初始化成功，false=失败（如磁盘写入失败）
   * @note 系统启动时必须先调用此方法，否则后续目录操作会失败
   */
  bool init_directory();

  // ========== 目录操作核心方法 ==========
  /**
   * @brief 创建多级目录
   * @param dir_path 目录路径（绝对/相对，如"/user/doc"或"doc/test"）
   * @return true=创建成功，false=失败（如路径已存在/权限不足/磁盘满）
   * @example
   *        dir.create_directory("/user/doc"); // 创建/user/doc目录
   */
  bool create_directory(const std::string &dir_path);

  /**
   * @brief 删除目录
   * @param dir_path 目录路径（绝对/相对）
   * @param force 是否强制删除（true=删除非空目录，false=仅删除空目录）
   * @return true=删除成功，false=失败（如目录不存在/被占用/非空且未强制）
   * @example
   *        dir.delete_directory("/user/doc"); // 删除空的/user/doc
   *        dir.delete_directory("/user", true); // 强制删除/user及所有子目录/文件
   */
  bool delete_directory(const std::string &dir_path, bool force = false);

  /**
   * @brief 切换当前工作目录
   * @param dir_path 目标目录路径（绝对/相对，如"/user/doc"或".."）
   * @return true=切换成功，false=失败（如目录不存在）
   * @example
   *        dir.change_directory("/user/doc"); // 切换到/user/doc
   *        dir.change_directory(".."); // 切换到/user
   */
  bool change_directory(const std::string &dir_path);

  /**
   * @brief 查询目录内容
   * @param dir_path 目标目录路径（默认"."表示当前工作目录）
   * @return 格式化的目录内容字符串（失败返回空字符串）
   * @example
   *        std::cout << dir.query_directory(); // 打印当前目录内容
   */
  std::string query_directory(const std::string &dir_path = ".");

  /**
   * @brief 递归列出所有目录树结构（供可视化/测试）
   * @param node 起始目录节点（默认nullptr表示根目录）
   * @param depth 递归深度（默认0，内部使用，外部调用无需传参）
   * @return 格式化的目录树字符串（如"/\n├─ user/\n│  └─ doc/"）
   */
  std::string list_all_dirs(DirNode *node = nullptr, int depth = 0);

  /**
   * @brief 获取当前工作目录的完整路径
   * @return 完整路径字符串（如"/user/doc"，根目录返回"/"）
   * @example
   *        std::cout << dir.get_current_work_dir_path(); // 输出当前目录路径
   */
  std::string get_current_work_dir_path();

  /**
   * @brief 获取当前工作目录的父目录路径
   * @return 父目录路径字符串（根目录返回"/"）
   * @example
   *        // 当前目录是/user/doc，返回"/user"
   *        std::cout << dir.get_parent_dir_path();
   */
  std::string get_parent_dir_path();

  /**
   * @brief 判断当前工作目录是否是根目录
   * @return true=是根目录，false=不是
   */
  bool is_current_dir_root() const;

  /**
   * @brief 获取指定节点的父节点（内部/测试用）
   * @param node 目标目录节点指针
   * @return 父节点指针（根目录返回自身）
   */
  DirNode *get_parent_node(DirNode *node) const;

  // ========== 文件元数据操作方法 ==========
  /**
   * @brief 向指定目录添加文件FCB
   * @param file_path 文件完整路径（如"/user/doc/test.txt"）
   * @param fcb 已初始化的文件控制块
   * @return true=添加成功，false=失败（如文件已存在/路径非法）
   * @note 仅添加元数据，文件数据需通过磁盘/缓冲池写入
   */
  bool add_file(const std::string &file_path, const FCB &fcb);

  /**
   * @brief 从目录中移除文件FCB
   * @param file_path 文件完整路径
   * @return true=移除成功，false=失败（如文件不存在/被占用）
   * @note 仅移除元数据，文件数据对应的盘块需通过FAT表释放
   */
  bool remove_file(const std::string &file_path);

  /**
   * @brief 查询文件FCB元数据
   * @param file_path 文件完整路径
   * @param fcb 输出参数：查询到的文件控制块
   * @return true=查询成功，false=失败（如文件不存在）
   * @example
   *        FCB fcb;
   *        if (dir.query_file("/test.txt", fcb)) {
   *            std::cout << fcb.filename << " " << fcb.start_block;
   *        }
   */
  bool query_file(const std::string &file_path, FCB &fcb);

  /**
   * @brief 锁定文件（防止并发修改/删除）
   * @param file_path 文件完整路径
   * @param pid 锁定文件的进程ID（Windows DWORD）
   * @return true=锁定成功，false=失败（如文件不存在/已被其他进程锁定）
   */
  bool lock_file(const std::string &file_path, pid_t pid);

  /**
   * @brief 解锁文件
   * @param file_path 文件完整路径
   * @return true=解锁成功，false=失败（如文件不存在/未被锁定）
   */
  bool unlock_file(const std::string &file_path);

  /**
   * @brief 私有方法：将内存目录树刷入磁盘
   * @details 调用serialize_dir_tree将目录树序列化为字节流，
   *          写入磁盘元数据区持久化存储，保证修改后数据不丢失。
   */
  void write_dir_to_disk();

  bool update_file_fcb(const std::string &file_path, const FCB &fcb);
};