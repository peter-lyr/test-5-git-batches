// 输入一系列文件和文件夹路径，写一个算法输出分组，要求：
// 1. 每个分组所有文件总大小不超过100M，但尽可能接近100M
// 2. 文件大小超过100M则跳过
// 3. 文件夹大小超过100M则获取子文件和子文件夹，作为新的输入
// 注意：
// 1. 文件夹大小是指它所有子文件的总大小
// 2. 用c实现
// 3. 每个分组可以包括文件和文件夹
// 4. 所有数字用long long类型
// 5. 代码写出后分析问题并优化，进行5轮后输出代码
// 输出分组后打印格式：
// 1. 先打印每个分组的情况：总大小，有多少个文件和文件夹
// 2. 再详细打印每组的文件和文件夹和它们的大小，文件在前文件夹在后
// 3. 最后统计并总结此次分组的情况：平均每组大小，算法耗时
// 程序最后：
// 1.
// 验证分组后的所有文件和文件夹有没有遗漏（总大小+跳过的文件的总大小是否小于输入的总大小）
// 2. 打印验证结果的具体情况

// 解决中文乱码问题
// 解决异常推出不报错误信息的问题

// 1. 把运行算法，打印结果，释放内存等放到新定义的测试函数里
// 2. 封装一个函数，参数两个：输入文件文件夹数组和输出结构体GroupResult

// 扫描文件夹: d/a5/
// 文件夹 d/a5/ 总大小: 759.55 MB, 包含 5198 个文件, 2 个文件夹
// 没有其他文件夹了，扫描出来的总文件数却有10400，会不会是a5文件夹重复扫描了2遍？

// 改成当git_file_count == 0时直接退出程序

// 每组进行git add和git commit操作
// git add后面跟多个路径，命令长度不超过20000
// git commit -F commit-info.txt
// commit-info.txt为exe文件传入的参数
// 最后git push

// 每次commit时commit-info.txt的内容做下区分

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define MAX_PATH_LENGTH 4096
#define MAX_GROUP_SIZE (50 * 1024 * 1024LL) // 50MB
#define MAX_ITEMS 100000
#define MAX_COMMAND_LENGTH 20000
#define SPLIT_PART_SIZE (45 * 1024 * 1024LL) // 45MB，确保小于MAX_GROUP_SIZE

typedef enum { TYPE_FILE, TYPE_DIRECTORY } ItemType;

typedef struct {
  char path[MAX_PATH_LENGTH];
  long long size;
  ItemType type;
} FileItem;

typedef struct {
  FileItem *items;
  int count;
  int capacity;
  long long total_size;
} FileGroup;

typedef struct {
  FileGroup *groups;
  int group_count;
  long long total_input_size;
  long long skipped_size;
  int total_files;
  int total_directories;
} GroupResult;

// 字符编码转换函数
wchar_t *char_to_wchar(const char *str) {
  int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
  wchar_t *wstr = (wchar_t *)malloc(len * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len);
  return wstr;
}

char *wchar_to_char(const wchar_t *wstr) {
  int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
  char *str = (char *)malloc(len);
  WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
  return str;
}

// 获取文件扩展名
const char *get_file_extension(const char *filename) {
  const char *dot = strrchr(filename, '.');
  if (!dot || dot == filename)
    return "";
  return dot + 1;
}

// 获取文件名（不含路径）
const char *get_file_name(const char *path) {
  const char *filename = strrchr(path, '\\');
  if (filename)
    return filename + 1;

  filename = strrchr(path, '/');
  if (filename)
    return filename + 1;

  return path;
}

// 获取不带扩展名的文件名（需要调用者释放内存）
char *get_file_name_without_extension(const char *path) {
  const char *filename = get_file_name(path);
  const char *dot = strrchr(filename, '.');

  if (!dot || dot == filename)
    return strdup(filename);

  size_t len = dot - filename;
  char *result = malloc(len + 1);
  if (result) {
    strncpy(result, filename, len);
    result[len] = '\0';
  }
  return result;
}

// 获取目录路径
void get_directory_path(const char *filepath, char *dirpath,
                        size_t buffer_size) {
  const char *last_slash = strrchr(filepath, '\\');
  if (!last_slash) {
    last_slash = strrchr(filepath, '/');
  }

  if (last_slash) {
    size_t len = last_slash - filepath;
    if (len < buffer_size) {
      strncpy(dirpath, filepath, len);
      dirpath[len] = '\0';

      // 如果是根目录（如 "C:\"），确保不以反斜杠结尾（除了根目录本身）
      if (len == 2 && filepath[1] == ':') {
        // 这是根目录，如 "C:"，保持原样
      } else if (len == 3 && filepath[1] == ':' &&
                 (filepath[2] == '\\' || filepath[2] == '/')) {
        // 这是根目录，如 "C:\"，保持原样
      } else {
        // 去除末尾的反斜杠（如果不是根目录）
        if (dirpath[len - 1] == '\\' || dirpath[len - 1] == '/') {
          dirpath[len - 1] = '\0';
        }
      }
    } else {
      dirpath[0] = '\0';
    }
  } else {
    dirpath[0] = '\0';
  }
}

// 分割大文件
int split_large_file(const char *filepath, long long file_size,
                     char split_files[][MAX_PATH_LENGTH], int *num_parts) {
  FILE *src_file = fopen(filepath, "rb");
  if (!src_file) {
    printf("错误: 无法打开文件 %s\n", filepath);
    return 0;
  }

  // 创建分割文件目录
  char split_dir[MAX_PATH_LENGTH];
  _snprintf_s(split_dir, sizeof(split_dir), _TRUNCATE, "%s-split", filepath);

  wchar_t *wsplit_dir = char_to_wchar(split_dir);
  DWORD attr = GetFileAttributesW(wsplit_dir);

  // 检查分割目录是否存在
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
    printf("分割目录已存在: %s\n", split_dir);

    // 计算现有分割文件的总大小
    long long existing_total_size = 0;
    int existing_file_count = 0;
    char existing_files[100][MAX_PATH_LENGTH];

    // 遍历分割目录中的文件
    wchar_t search_path[MAX_PATH_LENGTH];
    _snwprintf_s(search_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\*", wsplit_dir);

    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(search_path, &find_data);

    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        if (wcscmp(find_data.cFileName, L".") == 0 ||
            wcscmp(find_data.cFileName, L"..") == 0) {
          continue;
        }

        // 只处理文件，忽略子目录
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
          wchar_t full_path[MAX_PATH_LENGTH];
          _snwprintf_s(full_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\%s",
                       wsplit_dir, find_data.cFileName);

          // 获取文件大小
          HANDLE hFile =
              CreateFileW(full_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
          if (hFile != INVALID_HANDLE_VALUE) {
            DWORD sizeLow, sizeHigh;
            sizeLow = GetFileSize(hFile, &sizeHigh);
            long long part_size = ((ULONGLONG)sizeHigh << 32) | sizeLow;
            CloseHandle(hFile);

            existing_total_size += part_size;

            // 保存现有文件路径
            if (existing_file_count < 100) {
              char *char_path = wchar_to_char(full_path);
              strcpy_s(existing_files[existing_file_count], MAX_PATH_LENGTH,
                       char_path);
              existing_file_count++;
              free(char_path);
            }
          }
        }
      } while (FindNextFileW(hFind, &find_data));
      FindClose(hFind);
    }

    printf("现有分割文件总大小: %.2f MB, 原文件大小: %.2f MB\n",
           existing_total_size / (1024.0 * 1024.0),
           file_size / (1024.0 * 1024.0));

    // 检查大小是否一致
    if (existing_total_size == file_size && existing_file_count > 0) {
      printf("分割文件大小匹配，使用现有文件\n");

      // 复制现有文件路径到结果数组
      *num_parts = existing_file_count;
      for (int i = 0; i < existing_file_count; i++) {
        strcpy_s(split_files[i], MAX_PATH_LENGTH, existing_files[i]);
        printf("使用现有分割文件: %s\n", existing_files[i]);
      }

      fclose(src_file);
      free(wsplit_dir);
      return 1;
    } else {
      printf("分割文件大小不匹配或文件数量为0，删除现有文件并重新分割\n");

      // 删除目录中的所有文件（但不删除目录本身）
      wchar_t delete_search_path[MAX_PATH_LENGTH];
      _snwprintf_s(delete_search_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\*",
                   wsplit_dir);

      WIN32_FIND_DATAW delete_find_data;
      HANDLE hDeleteFind =
          FindFirstFileW(delete_search_path, &delete_find_data);

      if (hDeleteFind != INVALID_HANDLE_VALUE) {
        do {
          if (wcscmp(delete_find_data.cFileName, L".") == 0 ||
              wcscmp(delete_find_data.cFileName, L"..") == 0) {
            continue;
          }

          wchar_t delete_file_path[MAX_PATH_LENGTH];
          _snwprintf_s(delete_file_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\%s",
                       wsplit_dir, delete_find_data.cFileName);

          if (delete_find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // 递归删除子目录
            SHFILEOPSTRUCTW file_op = {.hwnd = NULL,
                                       .wFunc = FO_DELETE,
                                       .pFrom = delete_file_path,
                                       .pTo = NULL,
                                       .fFlags = FOF_NOCONFIRMATION |
                                                 FOF_SILENT | FOF_NOERRORUI};
            SHFileOperationW(&file_op);
          } else {
            // 删除文件
            DeleteFileW(delete_file_path);
          }
        } while (FindNextFileW(hDeleteFind, &delete_find_data));
        FindClose(hDeleteFind);
      }

      printf("已清理分割目录中的文件\n");
    }
  } else {
    // 目录不存在，创建新目录
    if (!CreateDirectoryA(split_dir, NULL)) {
      printf("错误: 无法创建分割目录 %s\n", split_dir);
      fclose(src_file);
      free(wsplit_dir);
      return 0;
    }
    printf("创建分割目录: %s\n", split_dir);
  }

  free(wsplit_dir);

  // 计算需要分割的份数
  *num_parts = (file_size + SPLIT_PART_SIZE - 1) / SPLIT_PART_SIZE;

  const char *filename = get_file_name(filepath);
  const char *extension = get_file_extension(filename);

  // 分割文件
  int success = 1;
  for (int i = 0; i < *num_parts; i++) {
    char part_filename[MAX_PATH_LENGTH];
    _snprintf_s(part_filename, sizeof(part_filename), _TRUNCATE,
                "%s\\%s-part%04d", split_dir, filename, i + 1);

    // 检查文件是否已存在（在清理后应该不存在，但为了安全还是检查）
    FILE *test_file = fopen(part_filename, "rb");
    if (test_file) {
      fclose(test_file);
      printf("分割文件已存在，跳过: %s\n", part_filename);
      strcpy_s(split_files[i], MAX_PATH_LENGTH, part_filename);
      continue;
    }

    FILE *dst_file = fopen(part_filename, "wb");
    if (!dst_file) {
      printf("错误: 无法创建分割文件 %s\n", part_filename);
      success = 0;
      break;
    }

    // 复制数据
    long long bytes_remaining = (i == *num_parts - 1)
                                    ? file_size - (SPLIT_PART_SIZE * i)
                                    : SPLIT_PART_SIZE;
    char buffer[64 * 1024]; // 64KB缓冲区

    while (bytes_remaining > 0) {
      size_t bytes_to_read = (bytes_remaining > sizeof(buffer))
                                 ? sizeof(buffer)
                                 : (size_t)bytes_remaining;
      size_t bytes_read = fread(buffer, 1, bytes_to_read, src_file);

      if (bytes_read == 0)
        break;

      size_t bytes_written = fwrite(buffer, 1, bytes_read, dst_file);
      if (bytes_written != bytes_read) {
        printf("错误: 写入分割文件失败 %s\n", part_filename);
        success = 0;
        break;
      }

      bytes_remaining -= bytes_read;
    }

    fclose(dst_file);

    if (!success)
      break;

    // 保存分割文件路径
    strcpy_s(split_files[i], MAX_PATH_LENGTH, part_filename);
    printf("创建分割文件: %s (%.2f MB)\n", part_filename,
           SPLIT_PART_SIZE / (1024.0 * 1024.0));
  }

  fclose(src_file);

  if (success) {
    printf("成功分割文件 %s 为 %d 个部分\n", filepath, *num_parts);
  } else {
    printf("分割文件失败: %s\n", filepath);
  }

  return success;
}

// 更新.gitignore文件（在文件所在目录）
void update_gitignore(const char *filepath, FileItem *items, int *item_count,
                      long long *total_scanned_size, int *total_file_count) {
  // 获取文件所在目录
  char file_dir[MAX_PATH_LENGTH];
  get_directory_path(filepath, file_dir, sizeof(file_dir));

  // 如果无法获取目录，使用当前目录
  if (strlen(file_dir) == 0) {
    if (!GetCurrentDirectoryA(sizeof(file_dir), file_dir)) {
      printf("错误: 无法获取当前目录\n");
      return;
    }
  }

  // 构建.gitignore文件路径
  char gitignore_path[MAX_PATH_LENGTH];
  _snprintf_s(gitignore_path, sizeof(gitignore_path), _TRUNCATE,
              "%s\\.gitignore", file_dir);

  const char *filename = get_file_name(filepath);
  char *filename_without_ext = get_file_name_without_extension(filename);

  char ignore_pattern[MAX_PATH_LENGTH];
  const char *extension = get_file_extension(filename);
  if (extension[0] != '\0') {
    _snprintf_s(ignore_pattern, sizeof(ignore_pattern), _TRUNCATE,
                "%s-merged.%s", filename_without_ext, extension);
  } else {
    _snprintf_s(ignore_pattern, sizeof(ignore_pattern), _TRUNCATE, "%s-merged",
                filename_without_ext);
  }

  FILE *gitignore = fopen(gitignore_path, "a+");
  if (!gitignore) {
    gitignore = fopen(gitignore_path, "w");
    if (!gitignore) {
      printf("错误: 无法创建或打开 .gitignore 文件: %s\n", gitignore_path);
      free(filename_without_ext);
      return;
    }
  } else {
    // 检查是否已存在该模式
    char line[MAX_PATH_LENGTH];
    int already_exists = 0;
    fseek(gitignore, 0, SEEK_SET);

    while (fgets(line, sizeof(line), gitignore)) {
      // 去除换行符
      line[strcspn(line, "\r\n")] = '\0';
      if (strcmp(line, ignore_pattern) == 0) {
        already_exists = 1;
        break;
      }
    }

    if (already_exists) {
      fclose(gitignore);
      free(filename_without_ext);
      return;
    }
  }

  fprintf(gitignore, "%s\n", ignore_pattern);
  fclose(gitignore);

  printf("已在 %s 中添加: %s\n", gitignore_path, ignore_pattern);

  // 将.gitignore文件添加到项目列表
  if (*item_count < MAX_ITEMS) {
    // 获取.gitignore文件大小
    WIN32_FILE_ATTRIBUTE_DATA file_attr;
    wchar_t *wgitignore_path = char_to_wchar(gitignore_path);
    long long gitignore_size = 0;

    if (GetFileAttributesExW(wgitignore_path, GetFileExInfoStandard,
                             &file_attr)) {
      gitignore_size =
          ((ULONGLONG)file_attr.nFileSizeHigh << 32) | file_attr.nFileSizeLow;
    }

    strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, gitignore_path);
    items[*item_count].size = gitignore_size;
    items[*item_count].type = TYPE_FILE;
    (*item_count)++;

    // 更新统计信息
    (*total_file_count)++;
    *total_scanned_size += gitignore_size;

    printf("已将 .gitignore 文件添加到列表: %s (%.2f MB)\n", gitignore_path,
           gitignore_size / (1024.0 * 1024.0));

    free(wgitignore_path);
  } else {
    printf("警告: 项目数量达到上限，无法添加 .gitignore 文件\n");
  }

  free(filename_without_ext);
}

// 移动原文件到备份目录（保持目录结构）
int move_to_backup(const char *filepath) {
  char current_dir[MAX_PATH_LENGTH];
  if (!GetCurrentDirectoryA(sizeof(current_dir), current_dir)) {
    printf("错误: 无法获取当前目录\n");
    return 0;
  }

  // 创建备份目录路径（当前目录加-backup1后缀）
  char backup_dir[MAX_PATH_LENGTH];
  _snprintf_s(backup_dir, sizeof(backup_dir), _TRUNCATE, "%s-backup1",
              current_dir);

  // 创建备份目录
  if (!CreateDirectoryA(backup_dir, NULL)) {
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
      printf("错误: 无法创建备份目录 %s\n", backup_dir);
      return 0;
    }
  }

  // 处理文件路径 - 如果是相对路径，转换为相对于当前目录的完整路径
  char full_filepath[MAX_PATH_LENGTH];
  if (filepath[0] == '/' || filepath[0] == '\\' ||
      (strlen(filepath) > 1 && filepath[1] == ':')) {
    // 已经是绝对路径
    strcpy_s(full_filepath, sizeof(full_filepath), filepath);
  } else {
    // 相对路径，转换为绝对路径
    _snprintf_s(full_filepath, sizeof(full_filepath), _TRUNCATE, "%s\\%s",
                current_dir, filepath);
  }

  printf("处理文件: %s\n", full_filepath);
  printf("当前目录: %s\n", current_dir);
  printf("备份目录: %s\n", backup_dir);

  // 计算相对于当前目录的路径
  char relative_path[MAX_PATH_LENGTH];
  if (_strnicmp(full_filepath, current_dir, strlen(current_dir)) == 0) {
    // 文件在当前目录或其子目录中
    strcpy_s(relative_path, sizeof(relative_path),
             full_filepath + strlen(current_dir));
    // 去除开头的反斜杠
    if (relative_path[0] == '\\' || relative_path[0] == '/') {
      memmove(relative_path, relative_path + 1, strlen(relative_path));
    }
    printf("文件在当前目录下，相对路径: %s\n", relative_path);
  } else {
    // 文件不在当前目录下，只使用文件名
    const char *filename = get_file_name(full_filepath);
    strcpy_s(relative_path, sizeof(relative_path), filename);
    printf("文件不在当前目录下，只使用文件名: %s\n", relative_path);
  }

  // 构建完整的备份文件路径
  char backup_path[MAX_PATH_LENGTH];
  _snprintf_s(backup_path, sizeof(backup_path), _TRUNCATE, "%s\\%s", backup_dir,
              relative_path);

  printf("备份路径: %s\n", backup_path);

  // 创建备份文件所需的目录结构
  char backup_dir_path[MAX_PATH_LENGTH];
  get_directory_path(backup_path, backup_dir_path, sizeof(backup_dir_path));

  printf("需要创建的备份目录: %s\n", backup_dir_path);

  // 递归创建目录
  if (strlen(backup_dir_path) > 0) {
    char temp_path[MAX_PATH_LENGTH];
    strcpy_s(temp_path, sizeof(temp_path), backup_dir_path);

    // 如果路径以备份目录开头，跳过备份目录部分
    char *path_to_create = temp_path;
    if (_strnicmp(temp_path, backup_dir, strlen(backup_dir)) == 0) {
      path_to_create = temp_path + strlen(backup_dir);
      if (path_to_create[0] == '\\' || path_to_create[0] == '/') {
        path_to_create++;
      }
    }

    printf("需要创建的相对目录: %s\n", path_to_create);

    // 逐个创建目录
    char current_create_path[MAX_PATH_LENGTH];
    strcpy_s(current_create_path, sizeof(current_create_path), backup_dir);

    char *token = strtok(path_to_create, "\\/");
    while (token != NULL) {
      strcat_s(current_create_path, sizeof(current_create_path), "\\");
      strcat_s(current_create_path, sizeof(current_create_path), token);

      printf("创建目录: %s\n", current_create_path);

      if (!CreateDirectoryA(current_create_path, NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
          printf("警告: 无法创建备份子目录 %s，错误代码: %lu\n",
                 current_create_path, GetLastError());
        } else {
          printf("目录已存在: %s\n", current_create_path);
        }
      } else {
        printf("成功创建目录: %s\n", current_create_path);
      }

      token = strtok(NULL, "\\/");
    }
  }

  // 检查备份文件是否已存在，如果存在则删除
  wchar_t *wbackup_path = char_to_wchar(backup_path);
  DWORD backup_attr = GetFileAttributesW(wbackup_path);
  if (backup_attr != INVALID_FILE_ATTRIBUTES) {
    printf("备份文件已存在，正在删除: %s\n", backup_path);

    if (backup_attr & FILE_ATTRIBUTE_DIRECTORY) {
      // 如果是目录，递归删除
      SHFILEOPSTRUCTW file_op = {.hwnd = NULL,
                                 .wFunc = FO_DELETE,
                                 .pFrom = wbackup_path,
                                 .pTo = NULL,
                                 .fFlags = FOF_NOCONFIRMATION | FOF_SILENT |
                                           FOF_NOERRORUI};
      int result = SHFileOperationW(&file_op);
      if (result != 0) {
        printf("错误: 无法删除已存在的备份目录，错误代码: %d\n", result);
        free(wbackup_path);
        return 0;
      }
    } else {
      // 如果是文件，直接删除
      if (!DeleteFileW(wbackup_path)) {
        DWORD error = GetLastError();
        printf("错误: 无法删除已存在的备份文件，错误代码: %lu\n", error);
        free(wbackup_path);
        return 0;
      }
    }
    printf("已成功删除已存在的备份文件\n");
  }
  free(wbackup_path);

  // 移动文件
  printf("移动文件: %s -> %s\n", full_filepath, backup_path);
  if (MoveFileA(full_filepath, backup_path)) {
    printf("已移动原文件到备份: %s -> %s\n", full_filepath, backup_path);
    return 1;
  } else {
    DWORD error = GetLastError();
    printf("错误: 无法移动文件到备份目录，错误代码: %lu\n", error);

    // 如果是因为目录不存在，尝试创建父目录后重试
    if (error == ERROR_PATH_NOT_FOUND) {
      printf("尝试创建父目录后重试...\n");
      char parent_dir[MAX_PATH_LENGTH];
      get_directory_path(backup_path, parent_dir, sizeof(parent_dir));

      printf("创建父目录: %s\n", parent_dir);
      if (CreateDirectoryA(parent_dir, NULL) ||
          GetLastError() == ERROR_ALREADY_EXISTS) {
        if (MoveFileA(full_filepath, backup_path)) {
          printf("重试成功: 已移动原文件到备份\n");
          return 1;
        } else {
          printf("重试失败，错误代码: %lu\n", GetLastError());
        }
      }
    }
    return 0;
  }
}

// 计算文件夹大小（不进行统计，只计算大小）
long long calculate_directory_size_only(const wchar_t *wpath) {
  long long total_size = 0;
  wchar_t search_path[MAX_PATH_LENGTH];
  _snwprintf_s(search_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\*", wpath);

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path, &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return 0;
  }

  do {
    if (wcscmp(find_data.cFileName, L".") == 0 ||
        wcscmp(find_data.cFileName, L"..") == 0) {
      continue;
    }

    wchar_t full_path[MAX_PATH_LENGTH];
    _snwprintf_s(full_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\%s", wpath,
                 find_data.cFileName);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      total_size += calculate_directory_size_only(full_path);
    } else {
      long long file_size =
          ((ULONGLONG)find_data.nFileSizeHigh << 32) | find_data.nFileSizeLow;
      total_size += file_size;
    }
  } while (FindNextFileW(hFind, &find_data));

  FindClose(hFind);
  return total_size;
}

// 递归收集文件和文件夹信息（主要函数）
void collect_items_recursive(const wchar_t *wpath, FileItem *items,
                             int *item_count, int is_root,
                             long long *total_scanned_size,
                             long long *skipped_files_size,
                             int *total_file_count, int *total_dir_count) {
  wchar_t search_path[MAX_PATH_LENGTH];
  _snwprintf_s(search_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\*", wpath);

  WIN32_FIND_DATAW find_data;
  HANDLE hFind = FindFirstFileW(search_path, &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return;
  }

  do {
    if (wcscmp(find_data.cFileName, L".") == 0 ||
        wcscmp(find_data.cFileName, L"..") == 0) {
      continue;
    }

    wchar_t full_path[MAX_PATH_LENGTH];
    _snwprintf_s(full_path, MAX_PATH_LENGTH, _TRUNCATE, L"%s\\%s", wpath,
                 find_data.cFileName);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      (*total_dir_count)++;

      // 计算文件夹大小（不重复统计）
      long long dir_size = calculate_directory_size_only(full_path);

      if (dir_size <= MAX_GROUP_SIZE) {
        // 文件夹大小合适，添加到列表
        if (*item_count < MAX_ITEMS) {
          char *char_path = wchar_to_char(full_path);
          strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, char_path);
          items[*item_count].size = dir_size;
          items[*item_count].type = TYPE_DIRECTORY;
          (*item_count)++;
          free(char_path);
        }
      } else {
        // 文件夹太大，递归处理子项
        collect_items_recursive(full_path, items, item_count, 0,
                                total_scanned_size, skipped_files_size,
                                total_file_count, total_dir_count);
      }
    } else {
      (*total_file_count)++;

      // 处理文件
      long long file_size =
          ((ULONGLONG)find_data.nFileSizeHigh << 32) | find_data.nFileSizeLow;
      *total_scanned_size += file_size;

      if (file_size > MAX_GROUP_SIZE) {
        // 分割大文件
        char *char_path = wchar_to_char(full_path);
        printf("发现大文件，开始分割: %s (%.2f MB)\n", char_path,
               file_size / (1024.0 * 1024.0));

        char split_files[100][MAX_PATH_LENGTH]; // 假设最多100个分割文件
        int num_parts = 0;

        if (split_large_file(char_path, file_size, split_files, &num_parts)) {
          // 添加分割文件到列表
          for (int i = 0; i < num_parts && *item_count < MAX_ITEMS; i++) {
            // 获取分割文件大小
            WIN32_FILE_ATTRIBUTE_DATA file_attr;
            wchar_t *wsplit_path = char_to_wchar(split_files[i]);
            if (GetFileAttributesExW(wsplit_path, GetFileExInfoStandard,
                                     &file_attr)) {
              long long part_size = ((ULONGLONG)file_attr.nFileSizeHigh << 32) |
                                    file_attr.nFileSizeLow;

              strcpy_s(items[*item_count].path, MAX_PATH_LENGTH,
                       split_files[i]);
              items[*item_count].size = part_size;
              items[*item_count].type = TYPE_FILE;
              (*item_count)++;

              printf("添加分割文件: %s (%.2f MB)\n", split_files[i],
                     part_size / (1024.0 * 1024.0));
            }
            free(wsplit_path);
          }

          // 更新.gitignore
          update_gitignore(char_path, items, item_count, total_scanned_size,
                           total_file_count);

          // 移动原文件到备份目录
          move_to_backup(char_path);
        } else {
          printf("分割大文件失败: %s\n", char_path);
          *skipped_files_size += file_size;
        }

        free(char_path);
      } else {
        // 文件大小合适，添加到列表
        if (*item_count < MAX_ITEMS) {
          char *char_path = wchar_to_char(full_path);
          strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, char_path);
          items[*item_count].size = file_size;
          items[*item_count].type = TYPE_FILE;
          (*item_count)++;
          free(char_path);
        }
      }
    }
  } while (FindNextFileW(hFind, &find_data));

  FindClose(hFind);

  // 如果是根目录且文件夹大小合适，添加根文件夹本身
  if (is_root) {
    long long root_size = calculate_directory_size_only(wpath);
    if (root_size <= MAX_GROUP_SIZE && *item_count < MAX_ITEMS) {
      // 检查是否已经添加（可能在递归过程中添加了）
      int already_added = 0;
      char *root_char_path = wchar_to_char(wpath);

      for (int i = 0; i < *item_count; i++) {
        if (strcmp(items[i].path, root_char_path) == 0) {
          already_added = 1;
          break;
        }
      }

      if (!already_added) {
        strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, root_char_path);
        items[*item_count].size = root_size;
        items[*item_count].type = TYPE_DIRECTORY;
        (*item_count)++;
      }
      free(root_char_path);
    }
  }
}

// 处理输入路径
void process_input_path(const char *path, FileItem *items, int *item_count,
                        long long *total_input_size,
                        long long *total_scanned_size,
                        long long *skipped_files_size, int *total_file_count,
                        int *total_dir_count) {
  wchar_t *wpath = char_to_wchar(path);

  DWORD attr = GetFileAttributesW(wpath);
  if (attr == INVALID_FILE_ATTRIBUTES) {
    printf("警告: 无法访问路径 %s，将其当作大小为0的文件处理\n", path);

    // 当作大小为0的文件添加到列表中
    if (*item_count < MAX_ITEMS) {
      strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, path);
      items[*item_count].size = 0;
      items[*item_count].type = TYPE_FILE; // 假设为文件类型
      (*item_count)++;

      // 更新统计信息
      (*total_file_count)++;
      printf("已将无法访问的路径添加到列表: %s (大小: 0)\n", path);
    } else {
      printf("错误: 项目数量达到上限，无法添加路径 %s\n", path);
    }

    free(wpath);
    return;
  }

  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    (*total_dir_count)++;
    printf("扫描文件夹: %s\n", path);

    // 计算文件夹总大小（不重复统计）
    long long dir_size = calculate_directory_size_only(wpath);
    *total_input_size += dir_size;

    printf("文件夹 %s 总大小: %.2f MB\n", path, dir_size / (1024.0 * 1024.0));

    if (dir_size > MAX_GROUP_SIZE) {
      printf("文件夹太大，递归处理子项...\n");
    } else {
      printf("文件夹大小合适，直接添加...\n");
    }

    // 收集文件和文件夹信息
    collect_items_recursive(wpath, items, item_count, 1, total_scanned_size,
                            skipped_files_size, total_file_count,
                            total_dir_count);
  } else {
    // 处理文件
    (*total_file_count)++;
    HANDLE hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
      DWORD sizeLow, sizeHigh;
      sizeLow = GetFileSize(hFile, &sizeHigh);
      long long file_size = ((ULONGLONG)sizeHigh << 32) | sizeLow;
      CloseHandle(hFile);

      *total_input_size += file_size;
      *total_scanned_size += file_size;

      if (file_size > MAX_GROUP_SIZE) {
        // 分割大文件
        printf("发现大文件，开始分割: %s (%.2f MB)\n", path,
               file_size / (1024.0 * 1024.0));

        char split_files[100][MAX_PATH_LENGTH]; // 假设最多100个分割文件
        int num_parts = 0;

        if (split_large_file(path, file_size, split_files, &num_parts)) {
          // 添加分割文件到列表
          for (int i = 0; i < num_parts && *item_count < MAX_ITEMS; i++) {
            // 获取分割文件大小
            WIN32_FILE_ATTRIBUTE_DATA file_attr;
            wchar_t *wsplit_path = char_to_wchar(split_files[i]);
            if (GetFileAttributesExW(wsplit_path, GetFileExInfoStandard,
                                     &file_attr)) {
              long long part_size = ((ULONGLONG)file_attr.nFileSizeHigh << 32) |
                                    file_attr.nFileSizeLow;

              strcpy_s(items[*item_count].path, MAX_PATH_LENGTH,
                       split_files[i]);
              items[*item_count].size = part_size;
              items[*item_count].type = TYPE_FILE;
              (*item_count)++;

              printf("添加分割文件: %s (%.2f MB)\n", split_files[i],
                     part_size / (1024.0 * 1024.0));
            }
            free(wsplit_path);
          }

          // 更新.gitignore
          update_gitignore(path, items, item_count, total_scanned_size,
                           total_file_count);
          // 移动原文件到备份目录
          move_to_backup(path);
        } else {
          printf("分割大文件失败: %s\n", path);
          *skipped_files_size += file_size;
        }
      } else {
        if (*item_count < MAX_ITEMS) {
          strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, path);
          items[*item_count].size = file_size;
          items[*item_count].type = TYPE_FILE;
          (*item_count)++;
        }
      }
    } else {
      // 无法打开文件，当作大小为0的文件处理
      printf("警告: 无法打开文件 %s，将其当作大小为0的文件处理\n", path);

      if (*item_count < MAX_ITEMS) {
        strcpy_s(items[*item_count].path, MAX_PATH_LENGTH, path);
        items[*item_count].size = 0;
        items[*item_count].type = TYPE_FILE;
        (*item_count)++;
        printf("已将无法打开的文件添加到列表: %s (大小: 0)\n", path);
      }
    }
  }

  free(wpath);
}

// 比较函数：文件在前，文件夹在后，按大小降序
int compare_items(const void *a, const void *b) {
  const FileItem *item1 = (const FileItem *)a;
  const FileItem *item2 = (const FileItem *)b;

  if (item1->type != item2->type) {
    return item1->type - item2->type; // 文件在前
  }

  // 按大小降序排列
  if (item1->size > item2->size)
    return -1;
  if (item1->size < item2->size)
    return 1;

  return strcmp(item1->path, item2->path);
}

// 改进的分组算法
GroupResult group_files(FileItem *items, int item_count) {
  GroupResult result = {0};

  // 为分组分配合理的内存
  int initial_group_count = (item_count / 100) + 10; // 预估分组数
  result.groups = malloc(sizeof(FileGroup) * initial_group_count);
  result.group_count = 0;

  // 按类型和大小排序
  printf("正在排序 %d 个项...\n", item_count);
  qsort(items, item_count, sizeof(FileItem), compare_items);

  // 初始化分组
  for (int i = 0; i < initial_group_count; i++) {
    result.groups[i].items = malloc(sizeof(FileItem) * 1000); // 合理的初始容量
    result.groups[i].count = 0;
    result.groups[i].capacity = 1000;
    result.groups[i].total_size = 0;
  }

  printf("开始分组处理...\n");

  // 改进的最佳适应算法 - 添加进度显示
  for (int i = 0; i < item_count; i++) {
    if (i % 1000 == 0) {
      printf("处理进度: %d/%d (%.1f%%)\n", i, item_count,
             (double)i / item_count * 100);
    }

    int best_group = -1;
    long long best_remaining = MAX_GROUP_SIZE;

    // 寻找最适合的分组
    for (int j = 0; j < result.group_count; j++) {
      long long remaining = MAX_GROUP_SIZE - result.groups[j].total_size;
      if (remaining >= items[i].size && remaining < best_remaining) {
        best_remaining = remaining;
        best_group = j;
      }
    }

    if (best_group != -1) {
      // 放入现有分组
      FileGroup *group = &result.groups[best_group];

      // 检查是否需要扩展容量
      if (group->count >= group->capacity) {
        group->capacity *= 2;
        group->items =
            realloc(group->items, sizeof(FileItem) * group->capacity);
      }

      group->items[group->count++] = items[i];
      group->total_size += items[i].size;
    } else {
      // 创建新分组
      if (result.group_count >= initial_group_count) {
        // 需要扩展分组数组
        initial_group_count *= 2;
        result.groups =
            realloc(result.groups, sizeof(FileGroup) * initial_group_count);

        // 初始化新分配的分组
        for (int k = result.group_count; k < initial_group_count; k++) {
          result.groups[k].items = malloc(sizeof(FileItem) * 1000);
          result.groups[k].count = 0;
          result.groups[k].capacity = 1000;
          result.groups[k].total_size = 0;
        }
      }

      FileGroup *new_group = &result.groups[result.group_count];
      new_group->items[new_group->count++] = items[i];
      new_group->total_size += items[i].size;
      result.group_count++;
    }
  }

  printf("分组完成，共 %d 个分组\n", result.group_count);
  return result;
}

// 打印分组结果
void print_groups(const GroupResult *result) {
  printf("=== 分组结果 ===\n\n");

  int max_groups_to_show = 10; // 只显示前10组详情
  int groups_to_show = result->group_count < max_groups_to_show
                           ? result->group_count
                           : max_groups_to_show;

  for (int i = 0; i < groups_to_show; i++) {
    printf("分组 %d:\n", i + 1);
    printf("  总大小: %.2f MB\n",
           result->groups[i].total_size / (1024.0 * 1024.0));

    int file_count = 0, dir_count = 0;
    for (int j = 0; j < result->groups[i].count; j++) {
      if (result->groups[i].items[j].type == TYPE_FILE) {
        file_count++;
      } else {
        dir_count++;
      }
    }
    printf("  文件数: %d, 文件夹数: %d\n", file_count, dir_count);

    // 只显示前5个项
    printf("  前5个项:\n");
    int items_to_show =
        result->groups[i].count < 5 ? result->groups[i].count : 5;
    for (int j = 0; j < items_to_show; j++) {
      const char *type_str =
          result->groups[i].items[j].type == TYPE_FILE ? "文件" : "文件夹";
      printf("    %s: %s (%.2f MB)\n", type_str,
             result->groups[i].items[j].path,
             result->groups[i].items[j].size / (1024.0 * 1024.0));
    }
    if (result->groups[i].count > 5) {
      printf("    ... 还有 %d 个项\n", result->groups[i].count - 5);
    }
    printf("\n");
  }

  if (result->group_count > max_groups_to_show) {
    printf("... 还有 %d 个分组未显示\n\n",
           result->group_count - max_groups_to_show);
  }
}

// 打印统计信息
void print_statistics(const GroupResult *result, long long total_scanned_size,
                      long long skipped_files_size, int total_file_count,
                      int total_dir_count) {
  printf("=== 统计信息 ===\n\n");

  long long total_grouped_size = 0;
  int total_grouped_files = 0;
  int total_grouped_dirs = 0;

  for (int i = 0; i < result->group_count; i++) {
    total_grouped_size += result->groups[i].total_size;
    for (int j = 0; j < result->groups[i].count; j++) {
      if (result->groups[i].items[j].type == TYPE_FILE) {
        total_grouped_files++;
      } else {
        total_grouped_dirs++;
      }
    }
  }

  double avg_group_size = result->group_count > 0
                              ? (double)total_grouped_size / result->group_count
                              : 0;

  printf("总分组数: %d\n", result->group_count);
  printf("平均每组大小: %.2f MB\n", avg_group_size / (1024.0 * 1024.0));
  printf("\n");
  printf("分组文件数: %d\n", total_grouped_files);
  printf("分组文件夹数: %d\n", total_grouped_dirs);
  printf("分组总大小: %.2f MB\n", total_grouped_size / (1024.0 * 1024.0));
  printf("扫描总大小: %.2f MB\n", total_scanned_size / (1024.0 * 1024.0));
  printf("跳过大文件总大小: %.2f MB\n", skipped_files_size / (1024.0 * 1024.0));
  printf("总文件数: %d\n", total_file_count);
  printf("总文件夹数: %d\n", total_dir_count);
}

// 验证结果
void validate_result(const GroupResult *result, long long input_total_size,
                     long long total_scanned_size,
                     long long skipped_files_size) {
  printf("=== 验证结果 ===\n\n");

  long long total_grouped_size = 0;
  for (int i = 0; i < result->group_count; i++) {
    total_grouped_size += result->groups[i].total_size;
  }

  long long calculated_total = total_grouped_size + skipped_files_size;

  printf("输入总大小: %.2f MB\n", input_total_size / (1024.0 * 1024.0));
  printf("分组总大小: %.2f MB\n", total_grouped_size / (1024.0 * 1024.0));
  printf("跳过大文件总大小: %.2f MB\n", skipped_files_size / (1024.0 * 1024.0));
  printf("扫描总大小: %.2f MB\n", total_scanned_size / (1024.0 * 1024.0));
  printf("计算总大小: %.2f MB\n", calculated_total / (1024.0 * 1024.0));

  if (calculated_total == input_total_size) {
    printf("✓ 验证成功: 没有文件遗漏\n");
  } else if (calculated_total < input_total_size) {
    printf("⚠ 警告: 可能有文件遗漏 (相差 %.2f MB)\n",
           (input_total_size - calculated_total) / (1024.0 * 1024.0));
    printf("可能原因:\n");
    printf("  1. 隐藏文件或系统文件未被统计\n");
    printf("  2. 权限问题导致某些文件无法访问\n");
    printf("  3. 符号链接或特殊文件类型\n");
  } else {
    printf("✗ 错误: 数据不一致 (计算值大于输入值)\n");
    printf("calculated_total:%lld - input_total_size:%lld = %lld\n",
           calculated_total, input_total_size,
           calculated_total - input_total_size);
  }
}

// 释放内存
void free_group_result(GroupResult *result) {
  for (int i = 0; i < result->group_count; i++) {
    free(result->groups[i].items);
  }
  free(result->groups);
}

// 新封装的函数：处理输入路径并生成分组结果
GroupResult process_input_paths(char *paths[], int path_count,
                                long long *total_scanned_size,
                                long long *skipped_files_size,
                                int *total_file_count, int *total_dir_count) {
  // 收集所有文件和文件夹信息
  FileItem *items = malloc(sizeof(FileItem) * MAX_ITEMS);
  int item_count = 0;
  long long total_input_size = 0;

  // 初始化统计变量
  *total_scanned_size = 0;
  *skipped_files_size = 0;
  *total_file_count = 0;
  *total_dir_count = 0;

  printf("正在扫描文件和文件夹...\n\n");

  for (int i = 0; i < path_count; i++) {
    process_input_path(paths[i], items, &item_count, &total_input_size,
                       total_scanned_size, skipped_files_size, total_file_count,
                       total_dir_count);
  }

  printf("\n扫描完成:\n");
  printf("  总文件数: %d\n", *total_file_count);
  printf("  总文件夹数: %d\n", *total_dir_count);
  printf("  总项目数: %d\n", *total_file_count + *total_dir_count);
  printf("  共收集到 %d 个有效项\n", item_count);
  printf("  输入总大小: %.2f MB\n", total_input_size / (1024.0 * 1024.0));
  printf("  扫描总大小: %.2f MB\n", *total_scanned_size / (1024.0 * 1024.0));
  printf("  跳过大文件总大小: %.2f MB\n\n",
         *skipped_files_size / (1024.0 * 1024.0));

  // 执行分组
  printf("正在进行分组...\n");
  GroupResult result = group_files(items, item_count);
  result.total_input_size = total_input_size;
  result.skipped_size = *skipped_files_size;

  // 释放items内存
  free(items);

  return result;
}

// 执行 git 命令
int execute_git_command(const char *command) {
  printf("执行命令: %s\n", command);
  int result = system(command);
  if (result != 0) {
    printf("警告: 命令执行失败 (返回码: %d)\n", result);
  }
  return result;
}

// 创建分组特定的 commit 信息文件
char *create_group_commit_file(const char *base_commit_file, int group_index,
                               int total_groups, const FileGroup *group) {
  char temp_filename[MAX_PATH_LENGTH];
  _snprintf_s(temp_filename, sizeof(temp_filename), _TRUNCATE,
              "commit-group-%d-%d.txt", group_index, (int)time(NULL));

  // 读取基础 commit 信息文件
  FILE *base_file = fopen(base_commit_file, "r");
  if (base_file == NULL) {
    printf("错误: 无法打开基础 commit 文件 '%s'\n", base_commit_file);
    return NULL;
  }

  // 创建临时文件
  FILE *temp_file = fopen(temp_filename, "w");
  if (temp_file == NULL) {
    printf("错误: 无法创建临时 commit 文件 '%s'\n", temp_filename);
    fclose(base_file);
    return NULL;
  }

  // 复制基础内容
  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), base_file) != NULL) {
    fputs(buffer, temp_file);
  }

  // 添加分组信息
  fprintf(temp_file, "%d/%d, ", group_index + 1, total_groups);
  fprintf(temp_file, "%.2f MB, ", group->total_size / (1024.0 * 1024.0));

  // 统计文件和文件夹数量
  int file_count = 0, dir_count = 0;
  for (int i = 0; i < group->count; i++) {
    if (group->items[i].type == TYPE_FILE) {
      file_count++;
    } else {
      dir_count++;
    }
  }
  fprintf(temp_file, "files: %d, folders: %d\n", file_count, dir_count);

  // 添加时间戳
  time_t now = time(NULL);
  struct tm *timeinfo = localtime(&now);
  char time_str[64];
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);
  fprintf(temp_file, "提交时间: %s\n", time_str);

  fclose(base_file);
  fclose(temp_file);

  char *result_filename = malloc(MAX_PATH_LENGTH);
  strcpy_s(result_filename, MAX_PATH_LENGTH, temp_filename);
  return result_filename;
}

// 删除临时文件
void delete_temp_file(const char *filename) { remove(filename); }

// 对每个分组执行 git add 和 git commit
void process_git_groups(GroupResult *result, const char *commit_info_file) {
  printf("\n=== 开始处理 Git 分组 ===\n\n");

  for (int i = 0; i < result->group_count; i++) {
    printf("处理分组 %d/%d:\n", i + 1, result->group_count);
    printf("  分组大小: %.2f MB\n",
           result->groups[i].total_size / (1024.0 * 1024.0));
    printf("  文件数: %d\n", result->groups[i].count);

    // 分批执行 git add（确保命令长度不超过限制）
    char git_add_command[MAX_COMMAND_LENGTH] = "git add";
    int current_length = strlen(git_add_command);

    for (int j = 0; j < result->groups[i].count; j++) {
      char *file_path = result->groups[i].items[j].path;
      int path_length = strlen(file_path);

      // 检查添加这个文件后是否会超过命令长度限制
      // 预留空格和引号的空间
      if (current_length + path_length + 10 > MAX_COMMAND_LENGTH) {
        // 执行当前的 git add 命令
        execute_git_command(git_add_command);

        // 重置命令
        strcpy(git_add_command, "git add");
        current_length = strlen(git_add_command);
      }

      // 添加文件路径到命令（用引号包围路径以处理空格）
      char temp_path[MAX_PATH_LENGTH + 10];
      _snprintf_s(temp_path, sizeof(temp_path), _TRUNCATE, " \"%s\"",
                  file_path);

      strcat_s(git_add_command, MAX_COMMAND_LENGTH, temp_path);
      current_length += strlen(temp_path);
    }

    // 执行最后的 git add 命令（如果还有文件）
    if (current_length > strlen("git add")) {
      execute_git_command(git_add_command);
    }

    // 创建分组特定的 commit 信息文件
    char *group_commit_file = create_group_commit_file(
        commit_info_file, i, result->group_count, &result->groups[i]);

    if (group_commit_file != NULL) {
      // 执行 git commit
      char commit_command[512];
      _snprintf_s(commit_command, sizeof(commit_command), _TRUNCATE,
                  "git commit -F \"%s\"", group_commit_file);
      execute_git_command(commit_command);

      // 删除临时文件
      delete_temp_file(group_commit_file);
      free(group_commit_file);
    } else {
      // 如果创建临时文件失败，使用原始文件
      char commit_command[512];
      _snprintf_s(commit_command, sizeof(commit_command), _TRUNCATE,
                  "git commit -F \"%s\"", commit_info_file);
      execute_git_command(commit_command);
    }

    execute_git_command("git push");

    printf("分组 %d 处理完成\n\n", i + 1);
  }

  printf("所有分组处理完成！\n");
}

// 新的测试函数：运行算法，打印结果，执行 git 操作，释放内存
void run_grouping_test(char *paths[], int path_count,
                       const char *commit_info_file) {
  long long total_scanned_size, skipped_files_size;
  int total_file_count, total_dir_count;

  // 处理输入路径并生成分组结果
  GroupResult result = process_input_paths(
      paths, path_count, &total_scanned_size, &skipped_files_size,
      &total_file_count, &total_dir_count);

  // 输出结果
  print_groups(&result);
  print_statistics(&result, total_scanned_size, skipped_files_size,
                   total_file_count, total_dir_count);
  validate_result(&result, result.total_input_size, total_scanned_size,
                  skipped_files_size);

  // 执行 git 操作
  process_git_groups(&result, commit_info_file);

  // 释放内存
  free_group_result(&result);
}

// 从 git status --porcelain 获取文件列表
char **get_git_status_files(int *file_count) {
  FILE *fp;
  char buffer[MAX_PATH_LENGTH];
  char **files = NULL;
  int count = 0;
  int capacity = 100;

  // 分配初始内存
  files = malloc(sizeof(char *) * capacity);

  // 执行 git status --porcelain 命令
  fp = _popen("git status --porcelain", "r");
  if (fp == NULL) {
    printf("错误: 无法执行 git 命令\n");
    *file_count = 0;
    // 返回一个空数组而不是NULL
    char **empty_files = malloc(sizeof(char *));
    empty_files[0] = NULL;
    return empty_files;
  }

  printf("从 git status 获取文件列表...\n");

  // 读取每一行输出
  while (fgets(buffer, sizeof(buffer), fp) != NULL) {
    // 去除行尾的换行符
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
    }

    // 跳过 git status 的状态码（前2个字符加一个空格）
    char *file_path = buffer;
    if (strlen(file_path) >= 3) {
      file_path += 3; // 跳过状态码和空格

      // 检查是否需要扩展数组
      if (count >= capacity) {
        capacity *= 2;
        files = realloc(files, sizeof(char *) * capacity);
      }

      // 复制文件路径
      files[count] = malloc(strlen(file_path) + 1);
      strcpy(files[count], file_path);
      count++;

      printf("  找到文件: %s\n", file_path);
    }
  }

  _pclose(fp);

  // 添加结束标记
  if (count >= capacity) {
    files = realloc(files, sizeof(char *) * (count + 1));
  }
  files[count] = NULL;

  *file_count = count;
  printf("共找到 %d 个 git 跟踪的文件\n", count);
  return files;
}

// 释放 git 文件列表内存
void free_git_files(char **files, int count) {
  for (int i = 0; i < count; i++) {
    free(files[i]);
  }
  free(files);
}

int main(int argc, char *argv[]) {
  // 检查参数
  if (argc < 2) {
    printf("用法: %s <commit-info.txt>\n", argv[0]);
    printf("请提供 commit 信息文件路径\n");
    return 1;
  }

  const char *commit_info_file = argv[1];

  // 检查文件是否存在
  FILE *test_file = fopen(commit_info_file, "r");
  if (test_file == NULL) {
    printf("错误: 无法打开 commit 信息文件 '%s'\n", commit_info_file);
    return 1;
  }
  fclose(test_file);

  // 设置控制台输出为UTF-8，防止中文乱码
  SetConsoleOutputCP(CP_UTF8);

  printf("文件分组工具 - 基于 Git 状态的文件分组\n\n");
  printf("Commit 信息文件: %s\n\n", commit_info_file);

  // 从 git status 获取文件列表
  int git_file_count = 0;
  char **git_files = get_git_status_files(&git_file_count);

  if (git_file_count == 0) {
    printf("没有找到 git 跟踪的文件，程序退出\n");

    // 释放 git 文件列表内存
    free_git_files(git_files, git_file_count);

    return 0; // 直接退出程序
  } else {
    printf("使用 git status 找到的文件进行分组:\n");
    for (int i = 0; i < (git_file_count < 10 ? git_file_count : 10); i++) {
      printf("  %d. %s\n", i + 1, git_files[i]);
    }
    if (git_file_count > 10) {
      printf("  ... 还有 %d 个文件\n", git_file_count - 10);
    }
    printf("\n");

    // 使用 git 文件进行分组并执行 git 操作
    run_grouping_test(git_files, git_file_count, commit_info_file);

    // 释放 git 文件列表内存
    free_git_files(git_files, git_file_count);
  }

  return 0;
}
