#!/usr/bin/env python
import re
import os
import json

# 文件路径
input_file = 'media/test_scenes/Classroom_light/Classroom_light.gltf'
backup_file = 'media/test_scenes/Classroom_light/Classroom_light.gltf.spotlight_backup'

# 创建备份
if not os.path.exists(backup_file):
    with open(input_file, 'r', encoding='utf-8') as src:
        with open(backup_file, 'w', encoding='utf-8') as dst:
            dst.write(src.read())
    print(f"Created backup at {backup_file}")

# 计数器，用于跟踪每种光源名称已经有多少个
light_counters = {}

# 正则表达式，匹配 "name": "SpotLight..." 模式的字符串
pattern = re.compile(r'"name"\s*:\s*"(SpotLight[^"]*)"')

# 逐行读取文件并修改
with open(input_file, 'r', encoding='utf-8') as infile:
    lines = infile.readlines()

modified_lines = []
for line in lines:
    match = pattern.search(line)
    if match:
        original_name = match.group(1)

        # 如果这种名称是第一次出现，初始化计数器
        if original_name not in light_counters:
            light_counters[original_name] = 0

        # 增加计数器
        light_counters[original_name] += 1

        # 创建新名称，添加下划线和后缀
        new_name = f"{original_name}_{light_counters[original_name]}"

        # 替换行中的名称
        new_line = line.replace(f'"name": "{original_name}"', f'"name": "{new_name}"')
        modified_lines.append(new_line)
    else:
        modified_lines.append(line)

# 直接写回原始文件
with open(input_file, 'w', encoding='utf-8') as outfile:
    outfile.writelines(modified_lines)

# 打印修改情况
print("SpotLight名称修改统计:")
for name, count in light_counters.items():
    print(f"  {name}: 修改了 {count} 处")
print(f"修改已保存到原始文件 {input_file}")
