#!/usr/bin/env python
import re
import os
import json
from collections import defaultdict

# 文件路径
input_file = 'media/test_scenes/Classroom_light2/Classroom_light1.gltf'
backup_file = 'media/test_scenes/Classroom_light2/Classroom_light1.gltf.all_names_backup'
output_file = 'media/test_scenes/Classroom_light2/Classroom_light1.gltf.new'

# 创建备份
if not os.path.exists(backup_file):
    with open(input_file, 'r', encoding='utf-8') as src:
        with open(backup_file, 'w', encoding='utf-8') as dst:
            dst.write(src.read())
    print(f"已创建备份文件: {backup_file}")

# 打开输入文件
with open(input_file, 'r', encoding='utf-8') as f:
    lines = f.readlines()

# 创建两个字典：一个用于跟踪所有name的出现次数，一个用于生成唯一的后缀
name_count = defaultdict(int)
name_suffix = {}

# 第一遍：只统计出现次数
for line in lines:
    match = re.search(r'"name"\s*:\s*"([^"]+)"', line)
    if match:
        original_name = match.group(1)
        name_count[original_name] += 1

# 初始化输出文件
output_lines = []

# 第二遍：处理每一行并修改name
for i, line in enumerate(lines):
    match = re.search(r'"name"\s*:\s*"([^"]+)"', line)
    if match and '"name":' in line:
        original_name = match.group(1)

        # 如果这个名称只出现一次，保持不变
        if name_count[original_name] == 1:
            output_lines.append(line)
            continue

        # 如果这个名称出现多次，为其添加后缀
        if original_name not in name_suffix:
            name_suffix[original_name] = 0

        name_suffix[original_name] += 1
        new_name = f"{original_name}_{name_suffix[original_name]}"

        # 替换行中的名称
        new_line = line.replace(f'"name": "{original_name}"', f'"name": "{new_name}"')
        output_lines.append(new_line)

        # 打印修改信息
        print(f"Line {i+1}: 修改 '{original_name}' 为 '{new_name}'")
    else:
        output_lines.append(line)

# 先写入到新文件
with open(output_file, 'w', encoding='utf-8') as f:
    f.writelines(output_lines)

# 验证新文件是否为有效的JSON
try:
    with open(output_file, 'r', encoding='utf-8') as f:
        json.load(f)
    print("验证成功：生成的文件是有效的JSON格式")

    # 只有在验证成功后，才将新文件复制到原始文件
    os.replace(output_file, input_file)
    print(f"已成功更新原始文件: {input_file}")

except json.JSONDecodeError as e:
    print(f"验证失败：生成的文件不是有效的JSON格式: {e}")
    print(f"原始文件未被修改，新文件保存在: {output_file}")

# 打印统计信息
print("\n修改统计:")
for name, count in name_count.items():
    if count > 1:
        print(f"  {name}: 出现 {count} 次，已全部添加后缀")
