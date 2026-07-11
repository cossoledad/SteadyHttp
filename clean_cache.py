import os
import shutil
import sys

def remove_pycache(target_dir):
    if not os.path.exists(target_dir):
        print(f"目录 '{target_dir}' 不存在")
        return

    print(f"扫描清理目录: {target_dir}\n" + "-"*40)
    deleted_count = 0
    for root, dirs, files in os.walk(target_dir, topdown=False):
        for name in dirs:
            if name == "__pycache__":
                full_path = os.path.join(root, name)
                try:
                    shutil.rmtree(full_path)
                    print(f"删除: {full_path}")
                    deleted_count += 1
                except Exception as e:
                    print(f"无法删除 {full_path}: {e}")

    print("-"*40 + f"\n清理完成，删除 {deleted_count} 个 __pycache__ 文件夹")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        target = sys.argv[1]
    else:
        target = os.path.dirname(os.path.abspath(__file__))
    remove_pycache(target)