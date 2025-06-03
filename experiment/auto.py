from falcor import *
import json
import time

def automated_viewpoint_data_collection_simplified():
    """使用当前已加载的render graph进行数据收集"""

    # 获取当前活动的render graph
    current_graph = m.activeGraph  # 假设已有活动graph

    # 确保graph包含IncomingLightPowerPass
    power_pass = current_graph.getPass("IncomingLightPower")
    if not power_pass:
        print("错误：当前graph中未找到IncomingLightPowerPass")
        return

    # 定义viewpoint列表
    viewpoints = define_viewpoints()
    collected_data = []

    # 遍历viewpoint并收集数据
    for i, viewpoint in enumerate(viewpoints):
        move_camera_to_viewpoint(viewpoint)
        data = collect_data_from_current_graph(i)
        collected_data.append(data)

    export_final_statistics(collected_data)
    return collected_data