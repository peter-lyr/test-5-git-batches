#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_PATH_LENGTH 4096
#define BUFFER_SIZE (1024 * 1024) // 1MB缓冲区

// 字符编码转换函数
wchar_t *char_to_wchar(const char *str) {
  int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
  wchar_t *wstr = (wchar_t *)malloc(len * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len);
  return wstr;
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

// 获取文件扩展名
const char *get_file_extension(const char *filename) {
  const char *dot = strrchr(filename, '.');
  if (!dot || dot == filename)
    return "";
  return dot + 1;
}

// 检查文件是否存在
int file_exists(const char *path) {
  wchar_t *wpath = char_to_wchar(path);
  DWORD attr = GetFileAttributesW(wpath);
  free(wpath);
  return (attr != INVALID_FILE_ATTRIBUTES &&
          !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

// 检查目录是否存在
int directory_exists(const char *path) {
  wchar_t *wpath = char_to_wchar(path);
  DWORD attr = GetFileAttributesW(wpath);
  free(wpath);
  return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
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
    } else {
      dirpath[0] = '\0';
    }
  } else {
    dirpath[0] = '\0';
  }
}

// 比较函数用于排序分割文件
int compare_split_files(const void *a, const void *b) {
  const char *file1 = *(const char **)a;
  const char *file2 = *(const char **)b;
  return strcmp(file1, file2);
}

// 查找分割文件
int find_split_files(const char *split_dir, char ***split_files,
                     int *num_parts) {
  WIN32_FIND_DATAA find_data;
  char search_pattern[MAX_PATH_LENGTH];
  char **files = NULL;
  int count = 0;
  int capacity = 100;

  // 分配初始内存
  files = malloc(sizeof(char *) * capacity);
  if (!files) {
    printf("错误: 内存分配失败\n");
    return 0;
  }

  // 构建搜索模式
  _snprintf_s(search_pattern, sizeof(search_pattern), _TRUNCATE, "%s\\*-part*",
              split_dir);

  HANDLE hFind = FindFirstFileA(search_pattern, &find_data);
  if (hFind == INVALID_HANDLE_VALUE) {
    printf("错误: 在目录 %s 中未找到分割文件\n", split_dir);
    free(files);
    return 0;
  }

  do {
    if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      // 检查文件名是否包含 "-part"
      if (strstr(find_data.cFileName, "-part") != NULL) {
        // 检查是否需要扩展数组
        if (count >= capacity) {
          capacity *= 2;
          char **new_files = realloc(files, sizeof(char *) * capacity);
          if (!new_files) {
            printf("错误: 内存重新分配失败\n");
            // 释放已分配的内存
            for (int i = 0; i < count; i++) {
              free(files[i]);
            }
            free(files);
            FindClose(hFind);
            return 0;
          }
          files = new_files;
        }

        // 构建完整文件路径
        char *file_path = malloc(MAX_PATH_LENGTH);
        if (!file_path) {
          printf("错误: 内存分配失败\n");
          for (int i = 0; i < count; i++) {
            free(files[i]);
          }
          free(files);
          FindClose(hFind);
          return 0;
        }

        _snprintf_s(file_path, MAX_PATH_LENGTH, _TRUNCATE, "%s\\%s", split_dir,
                    find_data.cFileName);
        files[count++] = file_path;
      }
    }
  } while (FindNextFileA(hFind, &find_data));

  FindClose(hFind);

  if (count == 0) {
    printf("错误: 未找到有效的分割文件\n");
    free(files);
    return 0;
  }

  // 按文件名排序（确保正确的合并顺序）
  qsort(files, count, sizeof(char *), compare_split_files);

  *split_files = files;
  *num_parts = count;
  return 1;
}

// 从分割文件名推断合并后的文件名（正确格式：d0000-merged.bin）
int deduce_merged_filename(const char *split_dir, char *merged_filename,
                           size_t buffer_size) {
  // 分割目录的格式是 "原文件名-split"，如 "d0000.bin-split"
  const char *dir_name = get_file_name(split_dir);

  printf("分割目录名: %s\n", dir_name);

  // 查找 "-split" 后缀
  const char *split_suffix = strstr(dir_name, "-split");
  if (!split_suffix) {
    printf("错误: 分割目录名格式不正确，应为 '原文件名-split'\n");
    return 0;
  }

  // 计算基础文件名长度（去掉-split）
  size_t base_length = split_suffix - dir_name;

  // 获取基础文件名（如 "d0000.bin"）
  char base_filename[MAX_PATH_LENGTH];
  if (base_length >= sizeof(base_filename)) {
    printf("错误: 基础文件名过长\n");
    return 0;
  }
  strncpy(base_filename, dir_name, base_length);
  base_filename[base_length] = '\0';

  printf("基础文件名: %s\n", base_filename);

  // 分离文件名和扩展名
  const char *extension = get_file_extension(base_filename);
  const char *filename_only = base_filename;

  // 如果文件有扩展名，去掉扩展名部分
  char name_without_ext[MAX_PATH_LENGTH];
  if (extension[0] != '\0') {
    const char *dot = strrchr(base_filename, '.');
    if (dot && dot != base_filename) {
      size_t name_len = dot - base_filename;
      if (name_len < sizeof(name_without_ext)) {
        strncpy(name_without_ext, base_filename, name_len);
        name_without_ext[name_len] = '\0';
        filename_only = name_without_ext;
      }
    }
  }

  printf("文件名（无扩展名）: %s\n", filename_only);
  printf("扩展名: %s\n", extension);

  // 构建合并后的文件名：文件名-merged.扩展名
  if (extension[0] != '\0') {
    _snprintf_s(merged_filename, buffer_size, _TRUNCATE, "%s-merged.%s",
                filename_only, extension);
  } else {
    _snprintf_s(merged_filename, buffer_size, _TRUNCATE, "%s-merged",
                filename_only);
  }

  printf("合并后文件名: %s\n", merged_filename);

  return 1;
}

// 合并分割文件
int merge_split_files(const char *split_dir, const char *output_file) {
  char **split_files = NULL;
  int num_parts = 0;
  char merged_filename[MAX_PATH_LENGTH];
  char final_output_file[MAX_PATH_LENGTH];

  printf("开始合并分割文件...\n");
  printf("分割文件目录: %s\n", split_dir);

  // 查找所有分割文件
  if (!find_split_files(split_dir, &split_files, &num_parts)) {
    return 0;
  }

  printf("找到 %d 个分割文件:\n", num_parts);
  for (int i = 0; i < num_parts; i++) {
    printf("  %d. %s\n", i + 1, split_files[i]);
  }

  // 确定输出文件名
  if (output_file == NULL || strlen(output_file) == 0) {
    // 自动推断合并后的文件名（格式：d0000-merged.bin）
    if (!deduce_merged_filename(split_dir, merged_filename,
                                sizeof(merged_filename))) {
      // 如果推断失败，使用默认名称
      printf("使用默认文件名: merged_file.bin\n");
      strcpy_s(merged_filename, sizeof(merged_filename), "merged_file.bin");
    }

    // 构建输出文件路径
    char split_parent_dir[MAX_PATH_LENGTH];
    get_directory_path(split_dir, split_parent_dir, sizeof(split_parent_dir));

    if (strlen(split_parent_dir) > 0) {
      _snprintf_s(final_output_file, sizeof(final_output_file), _TRUNCATE,
                  "%s\\%s", split_parent_dir, merged_filename);
    } else {
      strcpy_s(final_output_file, sizeof(final_output_file), merged_filename);
    }
  } else {
    strcpy_s(final_output_file, sizeof(final_output_file), output_file);
  }

  printf("输出文件: %s\n", final_output_file);

  // 检查输出文件是否已存在
  if (file_exists(final_output_file)) {
    printf("警告: 输出文件已存在，将被覆盖: %s\n", final_output_file);

    // 删除已存在的文件
    if (remove(final_output_file) != 0) {
      printf("错误: 无法删除已存在的输出文件\n");
      for (int i = 0; i < num_parts; i++) {
        free(split_files[i]);
      }
      free(split_files);
      return 0;
    }
  }

  // 创建输出文件
  FILE *output = fopen(final_output_file, "wb");
  if (!output) {
    printf("错误: 无法创建输出文件 %s\n", final_output_file);
    for (int i = 0; i < num_parts; i++) {
      free(split_files[i]);
    }
    free(split_files);
    return 0;
  }

  // 合并所有分割文件
  char *buffer = malloc(BUFFER_SIZE);
  if (!buffer) {
    printf("错误: 无法分配缓冲区内存\n");
    fclose(output);
    for (int i = 0; i < num_parts; i++) {
      free(split_files[i]);
    }
    free(split_files);
    return 0;
  }

  long long total_size = 0;
  int success = 1;

  for (int i = 0; i < num_parts && success; i++) {
    printf("合并文件 %d/%d: %s\n", i + 1, num_parts, split_files[i]);

    FILE *input = fopen(split_files[i], "rb");
    if (!input) {
      printf("错误: 无法打开分割文件 %s\n", split_files[i]);
      success = 0;
      break;
    }

    // 获取文件大小
    fseek(input, 0, SEEK_END);
    long long file_size = _ftelli64(input);
    fseek(input, 0, SEEK_SET);

    printf("  文件大小: %.2f MB\n", file_size / (1024.0 * 1024.0));

    // 复制文件内容
    long long bytes_remaining = file_size;
    long long total_copied = 0;

    while (bytes_remaining > 0 && success) {
      size_t bytes_to_read = (bytes_remaining > BUFFER_SIZE)
                                 ? BUFFER_SIZE
                                 : (size_t)bytes_remaining;
      size_t bytes_read = fread(buffer, 1, bytes_to_read, input);

      if (bytes_read == 0) {
        if (feof(input)) {
          break;
        } else {
          printf("错误: 读取文件失败\n");
          success = 0;
          break;
        }
      }

      size_t bytes_written = fwrite(buffer, 1, bytes_read, output);
      if (bytes_written != bytes_read) {
        printf("错误: 写入输出文件失败\n");
        success = 0;
        break;
      }

      bytes_remaining -= bytes_read;
      total_copied += bytes_read;
      total_size += bytes_read;

      // 每拷贝 10MB 输出一次进度
      if (total_copied % (10 * 1024 * 1024) == 0) {
        printf("  进度: %.2f/%.2f MB (%.1f%%)\n",
               total_copied / (1024.0 * 1024.0), file_size / (1024.0 * 1024.0),
               (double)total_copied / file_size * 100);
      }
    }

    fclose(input);

    if (success) {
      printf("  完成: %.2f MB\n", file_size / (1024.0 * 1024.0));
    }
  }

  free(buffer);
  fclose(output);

  // 释放分割文件路径内存
  for (int i = 0; i < num_parts; i++) {
    free(split_files[i]);
  }
  free(split_files);

  if (success) {
    printf("\n合并成功完成!\n");
    printf("总文件大小: %.2f MB\n", total_size / (1024.0 * 1024.0));
    printf("输出文件: %s\n", final_output_file);
    return 1;
  } else {
    printf("\n合并失败!\n");
    // 删除不完整的输出文件
    remove(final_output_file);
    return 0;
  }
}

// 显示使用说明
void print_usage(const char *program_name) {
  printf("用法: %s [选项] <分割文件目录>\n\n", program_name);
  printf("选项:\n");
  printf("  -o <输出文件>   指定输出文件名（可选）\n");
  printf("  -h              显示此帮助信息\n\n");
  printf("示例:\n");
  printf("  %s d0000.bin-split\n", program_name);
  printf("     → 输出: d0000-merged.bin\n");
  printf("  %s -o custom_name.bin d0000.bin-split\n", program_name);
}

int main(int argc, char *argv[]) {
  // 设置控制台输出为UTF-8，防止中文乱码
  SetConsoleOutputCP(CP_UTF8);

  printf("分割文件合并工具\n");
  printf("================\n\n");

  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  const char *split_dir = NULL;
  const char *output_file = NULL;

  // 解析命令行参数
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "-o") == 0) {
      if (i + 1 < argc) {
        output_file = argv[++i];
      } else {
        printf("错误: -o 选项需要指定输出文件名\n");
        return 1;
      }
    } else if (argv[i][0] != '-') {
      split_dir = argv[i];
    } else {
      printf("错误: 未知选项 %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  if (!split_dir) {
    printf("错误: 必须指定分割文件目录\n");
    print_usage(argv[0]);
    return 1;
  }

  // 检查分割目录是否存在
  if (!directory_exists(split_dir)) {
    printf("错误: 分割文件目录不存在: %s\n", split_dir);
    return 1;
  }

  // 执行合并
  if (merge_split_files(split_dir, output_file)) {
    printf("\n合并过程完成!\n");
    printf("注意: 分割文件目录已保留: %s\n", split_dir);
    printf("注意: .gitignore 中的忽略规则未移除\n");
    return 0;
  } else {
    printf("\n合并过程失败!\n");
    return 1;
  }
}
