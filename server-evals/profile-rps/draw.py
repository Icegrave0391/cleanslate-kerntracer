#!/usr/bin/env python3
import glob
import csv
import statistics
import re
import matplotlib.pyplot as plt

import matplotlib.font_manager as fm
from matplotlib.font_manager import FontProperties

# 如果你有 Times New Roman 字体文件，把它路径填到这里
font_path = './times-new-roman.ttf'
# 向 matplotlib 注册字体，并设置全局使用
fm.fontManager.addfont(font_path)
# 注意：这里的名称要与字体实际内部名称一致
plt.rcParams['font.family'] = 'Times New Roman'

plt.rcParams.update({'font.size': 20})

# Pipeline depth (only affects Redis latency calc)
PIPELINE = 32
# 并发列表对应 idx = 0..6
clients = [1, 2, 4, 8, 16, 32, 64]
# 四个目录
folders = {
    'redis-native': 'out-redis-native',
    'redis-profile': 'out-redis-profile',
    'nginx-native': 'out-nginx-native',
    'nginx-profile': 'out-nginx-profile'
}
pattern = re.compile(r'run(\d+)_idx(\d+)\.csv')

def load_redis(folder):
    """
    读取 Redis 目录，返回 { idx: {'set':(KQPS, ms), 'get':(KQPS, ms)} }
    """
    files = glob.glob(f'./{folder}/run*_idx*.csv')
    grouped = {}
    for path in files:
        m = pattern.search(path)
        if not m: continue
        idx = int(m.group(2))
        grouped.setdefault(idx, []).append(path)

    results = {}
    for idx, c in enumerate(clients):
        paths = grouped.get(idx, [])
        if not paths: continue
        set_runs = []
        get_runs = []
        for p in paths:
            with open(p, newline='') as f:
                rows = list(csv.reader(f))
            # Redis CSV: 2 lines, ["SET","rps"], ["GET","rps"]
            set_runs.append(float(rows[0][1]))
            get_runs.append(float(rows[1][1]))
        mean_set = statistics.mean(set_runs)
        mean_get = statistics.mean(get_runs)
        results[idx] = {
            'set': (mean_set / 1000, c * PIPELINE / mean_set * 1000),
            'get': (mean_get / 1000, c * PIPELINE / mean_get * 1000)
        }
    return results


def load_nginx(folder):
    """
    读取 Nginx 目录，返回 { idx: (KQPS, latency_ms) }
    CSV format: concurrency,rps,latency(ms)
    """
    files = glob.glob(f'./{folder}/run*_idx*.csv')
    grouped = {}
    for path in files:
        m = pattern.search(path)
        if not m: continue
        idx = int(m.group(2))
        grouped.setdefault(idx, []).append(path)

    results = {}
    for idx, c in enumerate(clients):
        paths = grouped.get(idx, [])
        if not paths: continue
        rps_runs = []
        lat_runs = []
        for p in paths:
            with open(p, newline='') as f:
                row = next(csv.reader(f))
            # row = [concurrency, rps, lat_ms]
            rps_runs.append(float(row[1]))
            lat_runs.append(float(row[2]))
        results[idx] = (statistics.mean(rps_runs) / 1000,
                        statistics.mean(lat_runs))
    return results

# 加载所有数据
data_redis_native  = load_redis(folders['redis-native'])
data_redis_profile = load_redis(folders['redis-profile'])
data_nginx_native  = load_nginx(folders['nginx-native'])
data_nginx_profile = load_nginx(folders['nginx-profile'])

# 创建两个子图: 左 Redis, 右 Nginx
fig, axes = plt.subplots(1, 2, figsize=(12, 6))
ax_r, ax_n = axes

# ----- Redis 子图 -----
# Redis-native
items = sorted(data_redis_native.items())
th_set = [v['set'][0] for _, v in items]
lat_set = [v['set'][1] for _, v in items]
th_get = [v['get'][0] for _, v in items]
lat_get = [v['get'][1] for _, v in items]
ax_r.plot(th_set, lat_set, marker='o', label='native SET')
ax_r.plot(th_get, lat_get, marker='s', label='native GET')

# Redis-profile
items = sorted(data_redis_profile.items())
th_set = [v['set'][0] for _, v in items]
lat_set = [v['set'][1] for _, v in items]
th_get = [v['get'][0] for _, v in items]
lat_get = [v['get'][1] for _, v in items]
ax_r.plot(th_set, lat_set, marker='^', label='profile SET')
ax_r.plot(th_get, lat_get, marker='d', label='profile GET')

ax_r.set_xlabel('Throughput (KQPS)')
ax_r.set_ylabel('Average Latency (ms)')
# ax_r.set_title('Redis')
ax_r.legend()
ax_r.grid(True)

# ----- Nginx 子图 -----
# Nginx-native
items = sorted(data_nginx_native.items())
th_ng_native = [v[0] for _, v in items]
lat_ng_native = [v[1] for _, v in items]
# Nginx-profile
items = sorted(data_nginx_profile.items())
th_ng_profile = [v[0] for _, v in items]
lat_ng_profile = [v[1] for _, v in items]

ax_n.plot(th_ng_native, lat_ng_native, marker='v', label='native')
ax_n.plot(th_ng_profile, lat_ng_profile, marker='x', label='profile')

ax_n.set_xlabel('Throughput (KQPS)')
# ax_n.set_ylabel('Average Latency (ms)')
# ax_n.set_title('Nginx')
ax_n.legend()
ax_n.grid(True)

plt.tight_layout()
plt.savefig('profile-slowdown.pdf')
# plt.show()