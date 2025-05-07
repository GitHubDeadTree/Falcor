#!/usr/bin/env python
import re
import os
import json

# 文件路径
input_file = 'media/test_scenes/Classroom_light/Classroom_light.gltf'
backup_file = 'media/test_scenes/Classroom_light/Classroom_light.gltf.all_lights_backup'

# 创建备份
if not os.path.exists(backup_file):
    with open(input_file, 'r', encoding='utf-8') as src:
        with open(backup_file, 'w', encoding='utf-8') as dst:
            dst.write(src.read())
    print(f"Created backup at {backup_file}")

# 读取整个文件内容
with open(input_file, 'r', encoding='utf-8') as infile:
    content = infile.read()

# 查找所有 "name": "..." 的模式
name_pattern = re.compile(r'"name"\s*:\s*"([^"]+)"')
matches = name_pattern.findall(content)

# 统计每个名称出现的次数
name_counts = {}
for name in matches:
    if name in name_counts:
        name_counts[name] += 1
    else:
        name_counts[name] = 1

# 找出重复的名称
duplicates = {name: count for name, count in name_counts.items() if count > 1}

print("检测到以下重复名称:")
for name, count in duplicates.items():
    print(f"  {name}: 出现 {count} 次")

# 如果没有重复，直接结束
if not duplicates:
    print("没有检测到重复名称。")
    exit(0)

# 为重复的名称创建计数器
name_counters = {name: 0 for name in duplicates.keys()}

# 逐行读取文件并修改
with open(input_file, 'r', encoding='utf-8') as infile:
    lines = infile.readlines()

modified_lines = []
for i, line in enumerate(lines):
    # 检查是否包含名称定义
    match = name_pattern.search(line)
    if match:
        name = match.group(1)
        # 如果是重复的名称，则修改
        if name in duplicates:
            name_counters[name] += 1
            new_name = f"{name}_{name_counters[name]}"
            new_line = line.replace(f'"name": "{name}"', f'"name": "{new_name}"')
            modified_lines.append(new_line)
            print(f"Line {i+1}: 修改 '{name}' 为 '{new_name}'")
        else:
            modified_lines.append(line)
    else:
        modified_lines.append(line)

# 写回原始文件
with open(input_file, 'w', encoding='utf-8') as outfile:
    outfile.writelines(modified_lines)

print("\n修改统计:")
for name, count in name_counters.items():
    print(f"  {name}: 修改了 {count} 处")
print(f"修改已保存到原始文件 {input_file}")
