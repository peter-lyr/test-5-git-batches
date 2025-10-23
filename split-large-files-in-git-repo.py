import os
import subprocess
import shutil
from pathlib import Path
import time


def find_git_repo(start_path="."):
    current = Path(start_path).resolve()
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
            ["git", "status", "--porcelain"], capture_output=True, text=True, check=True
        )
        files = set()
        for line in result.stdout.strip().split("\n"):
            if not line.strip():
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            filename = " ".join(parts[1:]).strip('"')
            if "->" in line:
                filename = line[line.find("->") + 2 :].strip().strip('"')
            files.add((repo_path / filename).resolve())
        os.chdir(original_cwd)
        return list(files)
    except (subprocess.CalledProcessError, Exception) as e:
        print(f"获取Git状态错误: {e}")
        return []


def find_large_files(directory, min_size=50 * 1024 * 1024):
    if not directory.is_dir():
        return []
    large_files = []
    try:
        for item in directory.rglob("*"):
            if item.is_file() and item.stat().st_size > min_size:
                large_files.append(item)
    except (OSError, IOError):
        pass
    return large_files


def calculate_chunks(file_size, chunk_size):
    num_chunks = (file_size + chunk_size - 1) // chunk_size
    sizes = [chunk_size] * (num_chunks - 1)
    if num_chunks > 0:
        sizes.append(file_size - (num_chunks - 1) * chunk_size)
    return sizes, num_chunks


def split_large_file(file_path, chunk_size=50 * 1024 * 1024):
    if not file_path.is_file():
        print(f"文件不存在: {file_path}")
        return False
    file_size = file_path.stat().st_size
    if file_size <= chunk_size:
        return False
    stem, suffix = file_path.stem, file_path.suffix
    split_dir = file_path.parent / f"{file_path.name}-split"
    chunk_sizes, num_chunks = calculate_chunks(file_size, chunk_size)
    if split_dir.exists():
        all_exist = all(
            (split_dir / f"{stem}-part{i+1:04d}{suffix}").exists()
            and (split_dir / f"{stem}-part{i+1:04d}{suffix}").stat().st_size == size
            for i, size in enumerate(chunk_sizes)
        )
        if not all_exist:
            print(f"拆分文件不完整，删除拆分目录重新开始: {split_dir}")
            try:
                shutil.rmtree(split_dir)
            except Exception as e:
                print(f"删除拆分目录失败: {e}")
                return False
    try:
        split_dir.mkdir(exist_ok=True)
        print(f"创建拆分目录: {split_dir}")
        with open(file_path, "rb") as src:
            for i, size in enumerate(chunk_sizes):
                part_name = f"{stem}-part{i+1:04d}{suffix}"
                part_path = split_dir / part_name
                if part_path.exists() and part_path.stat().st_size == size:
                    print(f"跳过已存在的部分文件: {part_name}")
                    src.seek(size, 1)
                    continue
                with open(part_path, "wb") as dst:
                    dst.write(src.read(size))
                print(f"创建部分文件: {part_path} ({size / (1024 * 1024):.2f} MB)")
        return True
    except Exception as e:
        print(f"拆分文件时出错: {e}")
        return False


def update_gitignore(file_path):
    if not file_path.is_file():
        print(f"文件不存在: {file_path}")
        return False
    repo_path = find_git_repo(file_path)
    if not repo_path:
        print(f"无法找到Git仓库根目录: {file_path}")
        return False
    repo_parent = repo_path.parent
    repo_name = repo_path.name
    backup_repo_name = f"{repo_name}-bak"
    relative_path = file_path.relative_to(repo_path)
    backup_path = repo_parent / backup_repo_name / relative_path
    backup_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        print(f"移动文件: {file_path} -> {backup_path}")
        shutil.move(str(file_path), str(backup_path))
        print(f"文件已移动到备份位置: {backup_path}")
    except Exception as e:
        print(f"移动文件到备份位置失败: {e}")
        return False
    gitignore_path = file_path.parent / ".gitignore"
    entry = f"{file_path.stem}-merged{file_path.suffix}"
    try:
        entries = set()
        if gitignore_path.exists():
            with open(gitignore_path, "r", encoding="utf-8") as f:
                entries.update(
                    line.strip()
                    for line in f
                    if line.strip() and not line.startswith("#")
                )
        if entry not in entries:
            entries.add(entry)
            with open(gitignore_path, "w", encoding="utf-8") as f:
                f.write("\n".join(sorted(entries)) + "\n")
            print(f"更新.gitignore文件: {gitignore_path}")
        return True
    except Exception as e:
        print(f"更新.gitignore文件时出错: {e}")
        return False


def process_file(file_path):
    if not file_path.is_file():
        print(f"文件不存在: {file_path}")
        return False
    size_mb = file_path.stat().st_size / (1024 * 1024)
    print(f"\n处理文件: {file_path} ({size_mb:.2f} MB)")
    if size_mb > 50:
        return update_gitignore(file_path) and split_large_file(file_path)
    print("文件未超过50MB，跳过处理")
    return False


def process_git_files():
    repo_path = find_git_repo()
    if not repo_path:
        print("未找到Git仓库")
        return
    print(f"找到Git仓库: {repo_path}")
    files = get_git_status_files(repo_path)
    if not files:
        print("未找到需要处理的文件")
        return
    print(f"找到 {len(files)} 个需要处理的文件:")
    for f in files:
        print(f"  - {f}")
    processed = 0
    for file_path in files:
        if not file_path.exists():
            print(f"文件不存在，跳过: {file_path}")
            continue
        if file_path.is_dir():
            print(f"处理目录: {file_path}")
            processed += sum(
                process_file(large_file) for large_file in find_large_files(file_path)
            )
        else:
            processed += process_file(file_path)
    print(f"处理完成！共处理了 {processed} 个大文件")


def format_time(seconds):
    if seconds < 60:
        return f"{seconds:.2f}秒"
    elif seconds < 3600:
        minutes = seconds // 60
        seconds = seconds % 60
        return f"{int(minutes)}分{seconds:.2f}秒"
    else:
        hours = seconds // 3600
        minutes = (seconds % 3600) // 60
        seconds = seconds % 60
        return f"{int(hours)}小时{int(minutes)}分{seconds:.2f}秒"


if __name__ == "__main__":
    start_time = time.time()
    process_git_files()
    end_time = time.time()
    total_time = end_time - start_time
    time_str = format_time(total_time)
    print(f"总耗时: {time_str}")
