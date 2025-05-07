#!/usr/bin/env python
import re
import os
import json

# 文件路径
input_file = 'media/test_scenes/Classroom_light/Classroom_light.gltf'
backup_file = 'media/test_scenes/Classroom_light/Classroom_light.gltf.b_backup'

# 创建备份
if not os.path.exists(backup_file):
    with open(input_file, 'r', encoding='utf-8') as src:
        with open(backup_file, 'w', encoding='utf-8') as dst:
            dst.write(src.read())
    print(f"Created backup at {backup_file}")

# 计数器，用于跟踪每种节点名称已经有多少个
node_counters = {}

# 正则表达式，匹配 "name": "G" 模式的字符串 (精确匹配)
pattern = re.compile(r'"name"\s*:\s*"G"')

# 逐行读取文件并修改
with open(input_file, 'r', encoding='utf-8') as infile:
    lines = infile.readlines()

modified_lines = []
for i, line in enumerate(lines):
    match = pattern.search(line)
    if match:
        # 增加计数器
        if "G" not in node_counters:
            node_counters["G"] = 0
        node_counters["G"] += 1

        # 创建新名称，添加下划线和后缀
        new_name = f"B_{node_counters['G']}"

        # 替换行中的名称
        new_line = line.replace('"name": "G"', f'"name": "{new_name}"')
        modified_lines.append(new_line)
        print(f"Line {i+1}: 修改 'G' 为 '{new_name}'")
    else:
        modified_lines.append(line)

# 直接写回原始文件
with open(input_file, 'w', encoding='utf-8') as outfile:
    outfile.writelines(modified_lines)

# 打印修改情况
print("\nB节点名称修改统计:")
for name, count in node_counters.items():
    print(f"  {name}: 修改了 {count} 处")
print(f"修改已保存到原始文件 {input_file}")
