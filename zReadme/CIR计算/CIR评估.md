基于你提供的Falcor路径追踪CIR计算方案，我来说明如何评估计算出的CIR质量。你的方案通过路径追踪收集了关键的光学传播参数，可以从以下几个层面来评估CIR的好坏：

## 一、路径追踪数据质量评估

**路径完整性检查**

* 验证路径数据的完整性和有效性
* 检查是否存在异常的路径长度(`d_i`)或角度值
* 统计不同反射次数(`K_i`)的路径分布

```python
# 路径数据质量检查示例
def validate_path_data(path_data):
    # 检查路径长度合理性
    valid_lengths = (path_data['d_i'] > 0) & (path_data['d_i'] < max_room_distance)
  
    # 检查角度范围
    valid_angles = (path_data['cos_phi'] >= 0) & (path_data['cos_theta'] >= 0)
  
    # 检查反射率
    valid_reflectivity = (path_data['total_reflectivity'] > 0) & (path_data['total_reflectivity'] <= 1)
  
    return valid_lengths & valid_angles & valid_reflectivity
```

## 二、CIR时域特性评估

**基于你的离散化CIR向量进行分析**

```python
def evaluate_cir_temporal(cir_vector, time_resolution=1e-9):
    # 1. RMS时延扩展
    time_bins = np.arange(len(cir_vector)) * time_resolution
    power_profile = cir_vector / np.sum(cir_vector)
  
    mean_delay = np.sum(time_bins * power_profile)
    rms_delay_spread = np.sqrt(np.sum((time_bins - mean_delay)**2 * power_profile))
  
    # 2. 最大时延扩展
    first_significant = np.where(cir_vector > 0.01 * np.max(cir_vector))[0][0]
    last_significant = np.where(cir_vector > 0.01 * np.max(cir_vector))[0][-1]
    max_delay_spread = (last_significant - first_significant) * time_resolution
  
    # 3. 直达路径与多径分量比
    los_power = cir_vector[first_significant]  # 假设第一个峰为直达路径
    total_power = np.sum(cir_vector)
    los_ratio = los_power / total_power
  
    return {
        'rms_delay_spread': rms_delay_spread,
        'max_delay_spread': max_delay_spread, 
        'los_ratio': los_ratio,
        'mean_delay': mean_delay
    }
```

## 三、基于路径追踪特有的评估指标

**多径成分分析**

```python
def analyze_multipath_components(path_data):
    # 按反射次数分组分析
    bounce_groups = {}
    for k in range(max_bounces + 1):
        mask = path_data['K_i'] == k
        if np.any(mask):
            bounce_groups[k] = {
                'count': np.sum(mask),
                'total_power': np.sum(path_data['power'][mask]),
                'mean_delay': np.mean(path_data['delay'][mask]),
                'power_ratio': np.sum(path_data['power'][mask]) / np.sum(path_data['power'])
            }
  
    return bounce_groups
```

**空间角度分布评估**

```python
def evaluate_angular_distribution(path_data):
    # 发射角分布评估
    emission_angles = np.arccos(path_data['cos_phi'])
    reception_angles = np.arccos(path_data['cos_theta'])
  
    # LED朗伯模式匹配度
    expected_lambertian = np.cos(emission_angles)**path_data['m']
    actual_distribution = path_data['cos_phi']**path_data['m']
  
    # 接收角度集中度
    angle_concentration = np.std(reception_angles)
  
    return {
        'emission_angle_spread': np.std(emission_angles),
        'reception_angle_spread': np.std(reception_angles),
        'lambertian_matching': np.corrcoef(expected_lambertian, actual_distribution)[0,1]
    }
```

## 四、频域特性评估

**相干带宽计算**

```python
def calculate_coherence_bandwidth(cir_vector, time_resolution):
    # FFT变换到频域
    freq_response = np.fft.fft(cir_vector)
    freqs = np.fft.fftfreq(len(cir_vector), time_resolution)
  
    # 计算相干带宽（相关系数降至0.5的带宽）
    magnitude = np.abs(freq_response)
    normalized_mag = magnitude / np.max(magnitude)
  
    # 找到-3dB带宽
    coherence_bw_idx = np.where(normalized_mag < 0.707)[0]
    if len(coherence_bw_idx) > 0:
        coherence_bandwidth = freqs[coherence_bw_idx[0]]
    else:
        coherence_bandwidth = freqs[-1]  # 整个频段都保持相干
  
    return coherence_bandwidth
```

## 五、VLC系统性能相关评估

**链路预算分析**

```python
def evaluate_link_budget(path_data, system_params):
    # 总接收功率
    total_received_power = np.sum(path_data['power'])
  
    # 链路损耗
    path_loss_db = -10 * np.log10(total_received_power / system_params['P_t'])
  
    # SNR估算（基于接收功率和噪声功率）
    snr_db = 10 * np.log10(total_received_power / system_params['noise_power'])
  
    return {
        'total_received_power': total_received_power,
        'path_loss_db': path_loss_db,
        'estimated_snr_db': snr_db
    }
```

## 六、CIR质量综合评估框架

```python
def comprehensive_cir_evaluation(path_data, cir_vector, system_params):
    results = {}
  
    # 1. 数据质量检查
    results['data_quality'] = validate_path_data(path_data)
  
    # 2. 时域特性
    results['temporal'] = evaluate_cir_temporal(cir_vector)
  
    # 3. 多径分析  
    results['multipath'] = analyze_multipath_components(path_data)
  
    # 4. 角度分布
    results['angular'] = evaluate_angular_distribution(path_data)
  
    # 5. 频域特性
    results['frequency'] = {
        'coherence_bandwidth': calculate_coherence_bandwidth(cir_vector, system_params['time_resolution'])
    }
  
    # 6. 链路性能
    results['link_budget'] = evaluate_link_budget(path_data, system_params)
  
    # 综合评分
    results['quality_score'] = calculate_quality_score(results)
  
    return results

def calculate_quality_score(results):
    score = 0
  
    # RMS时延扩展评分（越小越好）
    if results['temporal']['rms_delay_spread'] < 5e-9:  # 5ns
        score += 25
    elif results['temporal']['rms_delay_spread'] < 10e-9:
        score += 15
    else:
        score += 5
  
    # LOS分量比评分（越高越好）
    if results['temporal']['los_ratio'] > 0.7:
        score += 25
    elif results['temporal']['los_ratio'] > 0.5:
        score += 15
    else:
        score += 5
  
    # 相干带宽评分
    if results['frequency']['coherence_bandwidth'] > 100e6:  # 100MHz
        score += 25
    elif results['frequency']['coherence_bandwidth'] > 50e6:
        score += 15
    else:
        score += 5
  
    # SNR评分
    if results['link_budget']['estimated_snr_db'] > 20:
        score += 25
    elif results['link_budget']['estimated_snr_db'] > 10:
        score += 15
    else:
        score += 5
  
    return score
```

## 七、针对你的Falcor实现的建议

**在CIRPass中添加实时评估**

* 在计算CIR的同时计算这些评估指标
* 将评估结果输出到专门的缓冲区
* 支持可视化显示CIR质量指标

**路径追踪参数优化**

* 根据评估结果调整最大反射次数
* 优化采样密度和路径数量
* 动态调整时间分辨率

你的路径追踪方案提供了丰富的物理参数，这使得CIR评估可以更加精确和全面。建议重点关注RMS时延扩展、LOS/NLOS功率比和相干带宽这三个核心指标。
