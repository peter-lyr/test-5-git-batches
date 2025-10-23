import os
import sys
import subprocess
import time
from pathlib import Path
from collections import defaultdict
import shutil
import signal


class GitBatchCommiter:
    def __init__(self):
        self.repo_path = None
        self.original_cwd = os.getcwd()

    def __enter__(self):
        def signal_handler(sig, frame):
            print(f"\n\n收到中断信号，正在清理...")
            self.cleanup()
            sys.exit(1)

        signal.signal(signal.SIGINT, signal_handler)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.cleanup()

    def cleanup(self):
        if os.getcwd() != self.original_cwd:
            os.chdir(self.original_cwd)


def find_git_repo():
    current = Path(".").resolve()
    while current != current.parent:
        if (current / ".git").exists():
            return current
        current = current.parent
    return None


def get_git_status_files(repo_path):
    try:
        original_cwd = os.getcwd()
        os.chdir(repo_path)
        result = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True,
            text=True,
            check=True,
            encoding="utf-8",
        )
        files = []
        for line in result.stdout.strip().split("\n"):
            if not line.strip():
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            filename = " ".join(parts[1:]).strip('"')
            if "->" in line:
                filename = line[line.find("->") + 2 :].strip().strip('"')
            file_path = (repo_path / filename).resolve()
            if file_path.exists() and file_path.is_dir():
                dir_files = get_files_from_directory(file_path, repo_path)
                files.extend(dir_files)
            else:
                files.append(
                    {
                        "path": str(file_path.relative_to(repo_path)),
                        "size": (
                            file_path.stat().st_size
                            if file_path.exists() and file_path.is_file()
                            else 0
                        ),
                        "dir": (
                            str(file_path.parent.relative_to(repo_path))
                            if file_path.parent != repo_path
                            else "."
                        ),
                    }
                )
        os.chdir(original_cwd)
        return files
    except (subprocess.CalledProcessError, Exception) as e:
        print(f"获取Git状态错误: {e}")
        return []


def get_files_from_directory(directory, repo_path):
    files = []
    try:
        for item in directory.iterdir():
            if item.is_file():
                files.append(
                    {
                        "path": str(item.relative_to(repo_path)),
                        "size": item.stat().st_size,
                        "dir": (
                            str(item.parent.relative_to(repo_path))
                            if item.parent != repo_path
                            else "."
                        ),
                    }
                )
            elif item.is_dir() and item.name != ".git":
                subdir_files = get_files_from_directory(item, repo_path)
                files.extend(subdir_files)
    except (OSError, PermissionError) as e:
        print(f"访问目录 {directory} 时出错: {e}")
    return files


def get_git_files():
    repo_path = find_git_repo()
    if not repo_path:
        print("未找到Git仓库")
        return [], [], []
    git_files = get_git_status_files(repo_path)
    if not git_files:
        return [], [], []
    filtered_files = []
    skipped_files = []
    for file_info in git_files:
        if file_info["size"] > 50 * 1024 * 1024:
            print(f"跳过超过50M的文件: {file_info['path']}")
            skipped_files.append(file_info["path"])
            continue
        filtered_files.append(file_info)
    return filtered_files, repo_path, skipped_files


def organize_files_by_directory(git_files):
    dir_files = defaultdict(list)
    dir_sizes = defaultdict(int)
    for file_info in git_files:
        dir_path = file_info["dir"]
        dir_files[dir_path].append(file_info)
        dir_sizes[dir_path] += file_info["size"]
    return dir_files, dir_sizes


def create_batches(git_files, max_batch_size=100 * 1024 * 1024):
    if not git_files:
        return []
    dir_files, dir_sizes = organize_files_by_directory(git_files)
    batches = []
    sorted_dirs = sorted(dir_sizes.keys(), key=lambda d: dir_sizes[d])
    for dir_path in sorted_dirs[:]:
        if dir_sizes[dir_path] <= max_batch_size:
            batches.append(dir_files[dir_path])
            sorted_dirs.remove(dir_path)
    sorted_dirs = sorted(sorted_dirs, key=lambda d: dir_sizes[d], reverse=True)
    for dir_path in sorted_dirs:
        files = dir_files[dir_path]
        current_batch = []
        current_batch_size = 0
        files.sort(key=lambda x: x["size"], reverse=True)
        for file_info in files:
            if current_batch_size + file_info["size"] <= max_batch_size:
                current_batch.append(file_info)
                current_batch_size += file_info["size"]
            else:
                if current_batch:
                    batches.append(current_batch)
                current_batch = [file_info]
                current_batch_size = file_info["size"]
        if current_batch:
            batches.append(current_batch)
    return batches


def simplify_batch_files(batch_files, repo_path, all_git_files):
    dir_files = defaultdict(list)
    for file_info in batch_files:
        dir_path = file_info["dir"]
        dir_files[dir_path].append(file_info)
    simplified_files = []
    simplified_dirs = []
    all_dir_files, _ = organize_files_by_directory(all_git_files)
    for dir_path, files in dir_files.items():
        if dir_path == ".":
            simplified_files.extend([f["path"] for f in files])
            continue
        batch_files_in_dir = set(f["path"] for f in files)
        all_files_in_dir = set(f["path"] for f in all_dir_files.get(dir_path, []))
        if batch_files_in_dir == all_files_in_dir:
            simplified_dirs.append(dir_path)
        else:
            simplified_files.extend([f["path"] for f in files])
    return simplified_files, simplified_dirs


def create_commit_message_file(original_commit_info_file, batch_index, total_batches):
    try:
        with open(original_commit_info_file, "r", encoding="utf-8") as f:
            original_message = f.read().strip()
    except UnicodeDecodeError:
        with open(original_commit_info_file, "r", encoding="gbk") as f:
            original_message = f.read().strip()
    if total_batches == 1:
        return original_commit_info_file, original_message
    suffix = f" (批次 {batch_index}/{total_batches})"
    if original_message and not original_message.endswith("\n"):
        new_message = original_message + "\n" + suffix
    else:
        new_message = original_message + suffix
    temp_file_path = f"{original_commit_info_file}.batch{batch_index}"
    try:
        with open(temp_file_path, "w", encoding="utf-8") as f:
            f.write(new_message)
    except Exception as e:
        print(f"创建临时提交信息文件失败: {e}")
        return original_commit_info_file, original_message
    return temp_file_path, new_message


def cleanup_temp_files(original_commit_info_file, total_batches):
    for i in range(1, total_batches + 1):
        temp_file_path = f"{original_commit_info_file}.batch{i}"
        if os.path.exists(temp_file_path):
            try:
                os.remove(temp_file_path)
            except OSError:
                pass


def batch_git_add_files(file_paths, repo_path, max_command_length=32000):
    if not file_paths:
        return True, 0, 0
    batches = []
    current_batch = []
    current_length = 0
    base_command_length = len("git add ")
    for file_path in file_paths:
        file_length = len(file_path) + 1
        if current_length + file_length + base_command_length > max_command_length:
            if current_batch:
                batches.append(current_batch)
            current_batch = [file_path]
            current_length = file_length
        else:
            current_batch.append(file_path)
            current_length += file_length
    if current_batch:
        batches.append(current_batch)
    total_add_time = 0
    batch_times = []
    for i, batch in enumerate(batches):
        add_command = ["git", "add"] + batch
        print(f"执行: git add [批次 {i+1}/{len(batches)}, 包含 {len(batch)} 个文件]")
        print(batch)
        try:
            start_time = time.time()
            subprocess.run(
                add_command,
                capture_output=True,
                text=True,
                check=True,
                encoding="utf-8",
            )
            end_time = time.time()
            batch_time = end_time - start_time
            batch_times.append(batch_time)
            total_add_time += batch_time
            print(f"  ↳ 耗时: {batch_time:.2f} 秒")
        except subprocess.CalledProcessError as e:
            print(f"Git add 执行失败: {e}")
            stderr_output = e.stderr.strip() if e.stderr else ""
            if stderr_output:
                print(f"错误信息: {stderr_output}")
            return False, total_add_time, batch_times
    print(f"git add 总耗时: {total_add_time:.2f} 秒 ({len(batches)} 个批次)")
    return True, total_add_time, batch_times


def execute_git_add_commit(
    files, commit_info_file, repo_path, all_git_files, batch_index, total_batches
):
    if not files:
        print("没有文件需要提交")
        return False, 0, []
    temp_commit_file, commit_message = create_commit_message_file(
        commit_info_file, batch_index, total_batches
    )
    simplified_files, simplified_dirs = simplify_batch_files(
        files, repo_path, all_git_files
    )
    if simplified_files:
        print(f"提交文件: {len(simplified_files)} 个")
    if simplified_dirs:
        print("提交文件夹:")
        for dir_path in simplified_dirs:
            print(f"  📁 {dir_path}")
    print(f"提交信息: {commit_message}")
    all_paths = simplified_files + simplified_dirs
    try:
        original_cwd = os.getcwd()
        os.chdir(repo_path)
        add_success, add_time, batch_times = batch_git_add_files(all_paths, repo_path)
        if not add_success:
            print("文件添加失败")
            os.chdir(original_cwd)
            return False, add_time, batch_times
        commit_command = ["git", "commit", "-F", temp_commit_file]
        print(f"执行: {' '.join(commit_command)}")
        commit_start_time = time.time()
        result = subprocess.run(
            commit_command, capture_output=True, text=True, check=True, encoding="utf-8"
        )
        commit_end_time = time.time()
        commit_time = commit_end_time - commit_start_time
        stdout_output = result.stdout.strip() if result.stdout else ""
        if stdout_output:
            print(f"提交结果: {stdout_output}")
        print(f"git commit 耗时: {commit_time:.2f} 秒")
        print(f"本批次总耗时: {add_time + commit_time:.2f} 秒")
        os.chdir(original_cwd)
        return True, add_time + commit_time, batch_times
    except subprocess.CalledProcessError as e:
        print(f"Git命令执行失败: {e}")
        stderr_output = e.stderr.strip() if e.stderr else ""
        if stderr_output:
            print(f"错误信息: {stderr_output}")
        return (
            False,
            add_time if "add_time" in locals() else 0,
            batch_times if "batch_times" in locals() else [],
        )
    except Exception as e:
        print(f"执行Git命令时发生错误: {e}")
        return False, 0, []
    finally:
        if temp_commit_file != commit_info_file and os.path.exists(temp_commit_file):
            try:
                os.remove(temp_commit_file)
            except OSError:
                pass


def execute_git_push(repo_path):
    try:
        original_cwd = os.getcwd()
        os.chdir(repo_path)
        push_command = ["git", "push"]
        print(f"执行: {' '.join(push_command)}")
        push_start_time = time.time()
        result = subprocess.run(
            push_command, capture_output=True, text=True, check=True, encoding="utf-8"
        )
        push_end_time = time.time()
        push_time = push_end_time - push_start_time
        stdout_output = result.stdout.strip() if result.stdout else ""
        if stdout_output:
            print(f"推送结果: {stdout_output}")
        print(f"git push 耗时: {push_time:.2f} 秒")
        os.chdir(original_cwd)
        return True, push_time
    except subprocess.CalledProcessError as e:
        print(f"Git push执行失败: {e}")
        stderr_output = e.stderr.strip() if e.stderr else ""
        if stderr_output:
            print(f"错误信息: {stderr_output}")
        return False, push_time if "push_time" in locals() else 0
    except Exception as e:
        print(f"执行Git push时发生错误: {e}")
        return False, 0


def main():
    if len(sys.argv) != 2:
        print("用法: python script.py commit-info.txt")
        sys.exit(1)
    commit_info_file = sys.argv[1]
    if not os.path.exists(commit_info_file):
        print(f"错误: 文件 {commit_info_file} 不存在")
        sys.exit(1)
    with GitBatchCommiter() as commiter:
        git_files, repo_path, skipped_files = get_git_files()
        commiter.repo_path = repo_path
        if not git_files and not skipped_files:
            print("没有需要提交的文件")
            return
        if skipped_files:
            print(f"\n发现 {len(skipped_files)} 个超过50M的文件，已跳过这些文件")
        if not git_files:
            print("没有需要提交的文件")
            return
        total_size = sum(f["size"] for f in git_files)
        print(
            f"检测到 {len(git_files)} 个需要提交的文件，总大小: {total_size / 1024 / 1024:.2f} MB"
        )
        batches = create_batches(git_files)
        if not batches:
            print("没有需要提交的文件")
            return
        total_batches = len(batches)
        print(f"\n将分 {total_batches} 批进行提交")
        print("\n批次概览:")
        for i, batch in enumerate(batches, 1):
            batch_size = sum(f["size"] for f in batch)
            simplified_files, simplified_dirs = simplify_batch_files(
                batch, repo_path, git_files
            )
            file_count = len(simplified_files)
            dir_count = len(simplified_dirs)
            print(
                f"  批次 {i}: {len(batch)} 个文件, {batch_size / 1024 / 1024:.2f} MB -> {file_count} 个文件, {dir_count} 个文件夹"
            )
        successful_batches = 0
        total_processing_time = 0
        all_batch_times = []
        try:
            for i, batch in enumerate(batches, 1):
                batch_size = sum(f["size"] for f in batch)
                simplified_files, simplified_dirs = simplify_batch_files(
                    batch, repo_path, git_files
                )
                print(f"\n{'='*50}")
                print(f"第 {i}/{total_batches} 批提交")
                print(f"{'='*50}")
                print(f"文件数量: {len(batch)} 个")
                print(f"批次大小: {batch_size / 1024 / 1024:.2f} MB")
                print(
                    f"简化后: {len(simplified_files)} 个文件, {len(simplified_dirs)} 个文件夹"
                )
                batch_start_time = time.time()
                success, batch_time, batch_times = execute_git_add_commit(
                    batch, commit_info_file, repo_path, git_files, i, total_batches
                )
                batch_end_time = time.time()
                total_batch_time = batch_end_time - batch_start_time
                if success:
                    successful_batches += 1
                    total_processing_time += batch_time
                    all_batch_times.extend(batch_times)
                    print(
                        f"✓ 第 {i}/{total_batches} 批提交成功 (实际耗时: {total_batch_time:.2f} 秒)"
                    )
                else:
                    print(f"✗ 第 {i}/{total_batches} 批提交失败，停止执行")
                    break
            if successful_batches == total_batches:
                print(f"\n{'='*50}")
                print("所有批次提交成功，开始推送到远程仓库")
                print(f"{'='*50}")
                push_success, push_time = execute_git_push(repo_path)
                if push_success:
                    print("✓ 所有更改已成功推送到远程仓库")
                else:
                    print("✗ 推送失败")
            else:
                print(f"\n{'='*50}")
                print("提交过程中出现问题，未执行推送")
                print(f"成功提交: {successful_batches}/{total_batches} 批")
                print(f"{'='*50}")
        except KeyboardInterrupt:
            print(
                f"\n\n用户中断执行，已成功提交 {successful_batches}/{total_batches} 批"
            )
            sys.exit(1)
        print(f"\n{'='*60}")
        print("执行统计:")
        print(f"{'='*60}")
        print(f"总批次数: {total_batches}")
        print(f"成功批次数: {successful_batches}")
        if all_batch_times:
            print(f"git add 批次数量: {len(all_batch_times)}")
            print(
                f"git add 平均耗时: {sum(all_batch_times)/len(all_batch_times):.2f} 秒"
            )
            print(f"git add 最长耗时: {max(all_batch_times):.2f} 秒")
            print(f"git add 最短耗时: {min(all_batch_times):.2f} 秒")
        print(f"总处理时间: {total_processing_time:.2f} 秒")
        print(f"{'='*60}")
        cleanup_temp_files(commit_info_file, total_batches)


if __name__ == "__main__":
    main()
