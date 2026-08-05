#pragma once
#include "../../common/common.hpp"
#include "../storage/disk.hpp"
#include "../storage/fat_table.hpp"
#include "directory.hpp"
#include "../buffer/buffer_pool.hpp"
// 模拟使用
// #pragma once
// #include "../mock/common_mock.hpp"
// #include "../mock/disk_file_mock.hpp"
// #include "../mock/fat_table_file_mock.hpp"
// #include "directory.hpp"
// #include "../mock/buffer_pool_mock.hpp"

/**
 * @brief 文件/目录操作统一接口类
 * @details 封装磁盘、FAT表、目录管理器、缓冲池的底层操作，
 *          对外提供简洁统一的文件/目录操作接口，屏蔽底层实现细节，
 *          是进程模块与文件系统核心交互的唯一入口。
 * @note Windows适配：所有路径兼容/和\\分隔符，接口返回值统一为bool/字符串，便于调用
 */
class FileInterface
{
private:
  Disk &disk;              // 关联的模拟磁盘（引用）
  FATTable &fat_table;     // 关联的FAT表（引用）
  Directory &directory;    // 关联的目录管理器（引用）
  BufferPool &buffer_pool; // 关联的缓冲池（引用）

  // 权限检查辅助函数
  bool check_permission(FilePermission fcb_perm, FilePermission required_perm);

public:
  /**
   * @brief 构造函数：关联所有底层模块
   * @param d 模拟磁盘实例（已初始化）
   * @param ft FAT表实例（已初始化）
   * @param dir 目录管理器实例（已初始化）
   * @param bp 缓冲池实例（已初始化）
   * @example
   *        Disk disk("./sim_disk.bin");
   *        FATTable fat(disk);
   *        Directory dir(disk, fat);
   *        BufferPool bp(disk);
   *        FileInterface fi(disk, fat, dir, bp);
   */
  FileInterface(Disk &d, FATTable &ft, Directory &dir, BufferPool &bp);

  // ========== 文件操作核心方法 ==========
  /**
   * @brief 创建文件并写入初始内容
   * @param file_path 文件完整路径（绝对/相对，如"/test.txt"或"doc/test.txt"）
   * @param perm 文件权限（如FilePermission::READ | FilePermission::WRITE）
   * @param content 文件初始内容（长度≤BLOCK_SIZE，超出部分截断）
   * @return true=创建成功，false=失败（如路径已存在/磁盘满/权限不足）
   * @details 流程：
   *          1. 解析文件路径，获取父目录节点
   *          2. 从FAT表分配空闲盘块
   *          3. 创建文件FCB并添加到父目录
   *          4. 将内容写入缓冲池（自动刷入磁盘）
   * @example
   *        fi.create_file("/test.txt", FilePermission::READ|FilePermission::WRITE, "Hello FS!");
   */
  bool create_file(const std::string &file_path, FilePermission perm, const std::string &content);

  /**
   * @brief 查看文件指定盘块的内容
   * @param file_path 文件完整路径
   * @param block_num 文件逻辑块号（从0开始，如0表示第一个盘块）
   * @return 盘块内容字符串（失败返回空字符串）
   * @details 优先从缓冲池读取（减少磁盘IO），缓冲池无数据则从磁盘读取
   * @example
   *        std::cout << fi.view_file_block("/test.txt", 0); // 打印test.txt第0块内容
   */
  std::string view_file_block(const std::string &file_path, int block_num);

  /**
   * @brief 修改文件指定盘块的内容
   * @param file_path 文件完整路径
   * @param block_num 文件逻辑块号（从0开始）
   * @param new_content 新内容（长度≤BLOCK_SIZE，超出部分截断）
   * @return true=修改成功，false=失败（如文件不存在/被占用/权限不足）
   * @details 流程：
   *          1. 锁定文件，防止并发修改
   *          2. 从缓冲池获取文件盘块
   *          3. 修改缓冲页内容（标记为脏页）
   *          4. 解锁文件（脏页会在置换时自动刷入磁盘）
   * @example
   *        fi.modify_file_block("/test.txt", 0, "Hello File System!");
   */
  bool modify_file_block(const std::string &file_path, int block_num, const std::string &new_content);

  /**
   * @brief 删除文件
   * @param file_path 文件完整路径
   * @param pid 执行删除操作的进程ID（用于解锁文件）
   * @return true=删除成功，false=失败（如文件不存在/被其他进程占用）
   * @details 流程：
   *          1. 解析文件路径，获取文件FCB
   *          2. 解锁文件（若当前进程锁定）
   *          3. 从目录中移除文件FCB
   *          4. 通过FAT表释放文件占用的所有盘块
   * @example
   *        fi.delete_file("/test.txt", GetCurrentThreadId()); // 删除test.txt
   */
  bool delete_file(const std::string &file_path, pid_t pid);

  // ========== 新增块管理方法 ==========
  // 5. 获取文件所有物理块列表（操作前查看文件块分布）
  std::vector<int> get_file_all_blocks(const std::string &file_path);

  // 6. 截断文件到指定逻辑块（删除后续所有块）
  bool truncate_file(const std::string &file_path, int max_logical_block);

  // ========== 目录操作方法（封装Directory类接口） ==========
  /**
   * @brief 创建多级目录（封装Directory::create_directory）
   * @param dir_path 目录路径（绝对/相对）
   * @return true=创建成功，false=失败
   */
  bool create_directory(const std::string &dir_path);

  /**
   * @brief 删除目录（封装Directory::delete_directory）
   * @param dir_path 目录路径（绝对/相对）
   * @param force 是否强制删除
   * @return true=删除成功，false=失败
   */
  bool delete_directory(const std::string &dir_path, bool force = false);

  /**
   * @brief 切换当前工作目录（封装Directory::change_directory）
   * @param dir_path 目标目录路径
   * @return true=切换成功，false=失败
   */
  bool change_directory(const std::string &dir_path);

  /**
   * @brief 回退到父目录（封装Directory::change_directory("..")）
   * @return true=回退成功，false=失败（如当前已是根目录）
   * @example
   *        fi.cd_back(); // 从/user/doc回退到/user
   */
  bool cd_back();

  /**
   * @brief 查询目录内容（封装Directory::query_directory）
   * @param dir_path 目标目录路径（默认"."表示当前目录）
   * @return 格式化的目录内容字符串
   */
  std::string query_directory(const std::string &dir_path = ".");

  /**
   * @brief 获取当前工作目录路径（封装Directory::get_current_work_dir_path）
   * @return 当前目录完整路径
   */
  std::string get_current_work_dir();

  /**
   * @brief 获取父目录路径（封装Directory::get_parent_dir_path）
   * @return 父目录完整路径
   */
  std::string get_parent_dir_path();

  /**
   * @brief 判断是否在根目录（封装Directory::is_current_dir_root）
   * @return true=是根目录，false=不是
   */
  bool is_in_root_dir();

  /**
   * 写入整个文件内容（自动分配多块，覆盖原有内容）
   * @param file_path 文件路径
   * @param content 要写入的完整内容
   * @return 是否写入成功
   */
  bool write_file(const std::string &file_path, const std::string &content);

  /**
   * 读取整个文件的完整内容（自动拼接所有块）
   * @param file_path 文件路径
   * @return 完整文件内容（空字符串表示失败）
   */
  std::string read_file(const std::string &file_path);
};