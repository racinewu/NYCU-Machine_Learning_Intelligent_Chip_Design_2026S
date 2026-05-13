import matplotlib.pyplot as plt
import matplotlib.patches as patches
import numpy as np
import glob
import os
import re
import random
from collections import Counter
from matplotlib.colorbar import ColorbarBase

# Routing Algorithms
def get_xy_route(src, dst):
    x1, y1 = src % 4, src // 4
    x2, y2 = dst % 4, dst // 4
    path = [y1 * 4 + x1]
    curr_x, curr_y = x1, y1
    while curr_x != x2:
        curr_x += 1 if x2 > curr_x else -1
        path.append(curr_y * 4 + curr_x)
    while curr_y != y2:
        curr_y += 1 if y2 > curr_y else -1
        path.append(curr_y * 4 + curr_x)
    return path

def get_yx_route(src, dst):
    x1, y1 = src % 4, src // 4
    x2, y2 = dst % 4, dst // 4
    path = [y1 * 4 + x1]
    curr_x, curr_y = x1, y1
    while curr_y != y2:
        curr_y += 1 if y2 > curr_y else -1
        path.append(curr_y * 4 + curr_x)
    while curr_x != x2:
        curr_x += 1 if x2 > curr_x else -1
        path.append(curr_y * 4 + curr_x)
    return path

def get_negative_first_route(src, dst):
    x1, y1 = src % 4, src // 4
    x2, y2 = dst % 4, dst // 4
    path = [y1 * 4 + x1]; curr_x, curr_y = x1, y1
    while curr_x > x2: curr_x -= 1; path.append(curr_y * 4 + curr_x)
    while curr_y > y2: curr_y -= 1; path.append(curr_y * 4 + curr_x)
    while curr_x < x2: curr_x += 1; path.append(curr_y * 4 + curr_x)
    while curr_y < y2: curr_y += 1; path.append(curr_y * 4 + curr_x)
    return path

def get_west_first_route(src, dst):
    x1, y1 = src % 4, src // 4
    x2, y2 = dst % 4, dst // 4
    path = [y1 * 4 + x1]; curr_x, curr_y = x1, y1
    while curr_x > x2: curr_x -= 1; path.append(curr_y * 4 + curr_x)
    while curr_y != y2:
        curr_y += 1 if y2 > curr_y else -1
        path.append(curr_y * 4 + curr_x)
    while curr_x < x2: curr_x += 1; path.append(curr_y * 4 + curr_x)
    return path

def get_north_last_route(src, dst):
    x1, y1 = src % 4, src // 4
    x2, y2 = dst % 4, dst // 4
    path = [y1 * 4 + x1]; curr_x, curr_y = x1, y1
    while curr_x != x2:
        curr_x += 1 if x2 > curr_x else -1
        path.append(curr_y * 4 + curr_x)
    while curr_y > y2: curr_y -= 1; path.append(curr_y * 4 + curr_x)
    while curr_y < y2: curr_y += 1; path.append(curr_y * 4 + curr_x)
    return path

def get_adaptive_route(src, dst):
    x1, y1 = src % 4, src // 4
    x2, y2 = dst % 4, dst // 4
    path = [y1 * 4 + x1]; curr_x, curr_y = x1, y1
    while curr_x != x2 or curr_y != y2:
        opts = []
        if curr_x != x2: opts.append('X')
        if curr_y != y2: opts.append('Y')
        if random.choice(opts) == 'X': curr_x += 1 if x2 > curr_x else -1
        else: curr_y += 1 if y2 > curr_y else -1
        path.append(curr_y * 4 + curr_x)
    return path

def analyze_data(folder, mode="XY"):
    r_load, l_load, c_load = Counter(), Counter(), Counter()
    hops, p_sizes = [], []
    files = glob.glob(os.path.join(folder, "core*.txt"))
    
    routing_funcs = {
        "XY": get_xy_route, "YX": get_yx_route, "Neg-First": get_negative_first_route,
        "West-First": get_west_first_route, "North-Last": get_north_last_route, "Adaptive": get_adaptive_route
    }
    route_func = routing_funcs.get(mode, get_xy_route)

    for f_path in files:
        with open(f_path, "r", encoding="utf-8") as f:
            txt = f.read()
            matches = zip(re.findall(r"FROM\s+(\d+)\s+(\d+)", txt), re.findall(r"TO\s+(\d+)\s+(\d+)", txt))
            for (s_id, s_len), (d_id, d_len) in matches:
                src, dst, size = int(s_id), int(d_id), int(s_len)
                p_sizes.append(size)
                path = route_func(src, dst)
                hops.append(len(path) - 1)
                c_load[src] += size; c_load[dst] += size
                for i, node in enumerate(path):
                    r_load[node] += size
                    if i < len(path) - 1:
                        l_load[tuple(sorted((path[i], path[i+1])))] += size
    return r_load, l_load, c_load, hops, p_sizes

def analyze_data_v9(folder, mode="XY"):
    r_load, l_load, c_load = Counter(), Counter(), Counter()
    hops, p_sizes = [], []
    files = glob.glob(os.path.join(folder, "core*.txt"))
    
    routing_funcs = {
        "XY": get_xy_route, "YX": get_yx_route, "Neg-First": get_negative_first_route,
        "West-First": get_west_first_route, "North-Last": get_north_last_route, "Adaptive": get_adaptive_route
    }
    route_func = routing_funcs.get(mode, get_xy_route)

    for f_path in files:
        with open(f_path, "r", encoding="utf-8") as f:
            txt = f.read()
            matches = zip(re.findall(r"FROM\s+(\d+)\s+(\d+)", txt), re.findall(r"TO\s+(\d+)\s+(\d+)", txt))
            for (s_id, s_len), (d_id, d_len) in matches:
                src, dst, size = int(s_id), int(d_id), int(s_len)
                p_sizes.append(size)
                path = route_func(src, dst)
                hops.append(len(path) - 1)
                c_load[src] += size
                c_load[dst] += size
                for i, node in enumerate(path):
                    r_load[node] += size
                    if i < len(path) - 1:
                        l_load[tuple(sorted((path[i], path[i+1])))] += size
    return r_load, l_load, c_load, hops, p_sizes

def analyze_data_v10(folder, mode="XY"):
    r_load, l_load, c_load = Counter(), Counter(), Counter()
    hops, p_sizes = [], []
    files = glob.glob(os.path.join(folder, "core*.txt"))
    
    routing_funcs = {
        "XY": get_xy_route, "YX": get_yx_route, "Neg-First": get_negative_first_route,
        "West-First": get_west_first_route, "North-Last": get_north_last_route, "Adaptive": get_adaptive_route
    }
    route_func = routing_funcs.get(mode, get_xy_route)

    for f_path in files:
        with open(f_path, "r", encoding="utf-8") as f:
            txt = f.read()
            matches = zip(re.findall(r"FROM\s+(\d+)\s+(\d+)", txt), re.findall(r"TO\s+(\d+)\s+(\d+)", txt))
            for (s_id, s_len), (d_id, d_len) in matches:
                src, dst, size = int(s_id), int(d_id), int(s_len)
                p_sizes.append(size)
                path = route_func(src, dst)
                hops.append(len(path) - 1)
                c_load[src] += size
                c_load[dst] += size
                for i, node in enumerate(path):
                    r_load[node] += size
                    if i < len(path) - 1:
                        l_load[tuple(sorted((path[i], path[i+1])))] += size
    return r_load, l_load, c_load, hops, p_sizes

def draw_noc_comparison(folder):
    modes = ["XY", "YX", "Neg-First", "West-First", "North-Last", "Adaptive"]
    results = {m: analyze_data_v10(folder, m) for m in modes}
    
    max_r = max([max(r[0].values(), default=1) for r in results.values()])
    max_l = max([max(r[1].values(), default=1) for r in results.values()])
    max_c = max([max(r[2].values(), default=1) for r in results.values()])

    fig = plt.figure(figsize=(26, 14))
    cmap_r, cmap_l, cmap_c = plt.cm.Blues, plt.cm.Reds, plt.cm.Greens

    for idx, mode in enumerate(modes):
        r_load, l_load, c_load, hops, p_sizes = results[mode]
        row, col = idx // 3, idx % 3
        
        ax = fig.add_axes([0.04 + col * 0.26, 0.5 - row * 0.38, 0.20, 0.32])
        ax.set_xlim(-0.3, 3.8); ax.set_ylim(-0.6, 3.5); ax.set_aspect('equal'); ax.axis("off")
        
        ax.set_title(f"Routing: {mode}", fontsize=16, fontweight='bold', pad=10)

        for i in range(16):
            x, y = i % 4, 3 - (i // 4)
            if x < 3:
                val = l_load.get(tuple(sorted((i, i+1))), 0)
                l_col = cmap_l(0.1 + 0.9 * (val/max_l)) if val > 0 else '#EEEEEE'
                ax.annotate('', xy=(x+0.75, y), xytext=(x+0.25, y), arrowprops=dict(arrowstyle='<->', lw=2.5, color=l_col))
            if y > 0:
                val = l_load.get(tuple(sorted((i, i+4))), 0)
                l_col = cmap_l(0.1 + 0.9 * (val/max_l)) if val > 0 else '#EEEEEE'
                ax.annotate('', xy=(x, y-0.75), xytext=(x, y-0.25), arrowprops=dict(arrowstyle='<->', lw=2.5, color=l_col))

            ax.add_patch(patches.Rectangle((x+0.15, y-0.45), 0.3, 0.3, facecolor=cmap_c(0.1+0.8*(c_load[i]/max_c)), edgecolor='black', alpha=0.7, zorder=2))
            ax.add_patch(patches.Circle((x, y), 0.18, facecolor=cmap_r(0.1+0.8*(r_load[i]/max_r)), edgecolor='black', zorder=3))
            ax.text(x, y, 'R', ha='center', va='center', fontsize=8, fontweight='bold')
            ax.text(x+0.3, y-0.3, f'C{i}', ha='center', va='center', fontsize=7)

        # 區域統計框
        stats_box = (f"Rmax:{max(r_load.values()):.0f}\n"
                     f"Ravg:{np.mean(list(r_load.values())):.1f}\n"
                     f"Lmax:{max(l_load.values()):.0f}\n"
                     f"Lavg:{np.mean(list(l_load.values())):.1f}\n"
                     f"Hot:R{max(r_load, key=r_load.get)}")
        ax.text(3.8, 1.5, stats_box, fontsize=8, family='monospace', bbox=dict(facecolor='white', alpha=0.8, edgecolor='#dddddd', boxstyle='round'))

    _, _, _, all_hops, all_sizes = results["XY"]
    
    global_stats_text = (
        f"  [ GLOBAL TRAFFIC ]\n"
        f"----------------------\n"
        f"Hop Avg : {np.mean(all_hops):.2f}\n"
        f"Hop Max : {np.max(all_hops)}\n"
        f"Hop Min : {np.min(all_hops)}\n"
        f"Hop Std : {np.std(all_hops):.2f}\n"
        f"----------------------\n"
        f"Pkt Avg : {np.mean(all_sizes):.2f}\n"
        f"Pkt Max : {np.max(all_sizes)}\n"
        f"Pkt Min : {np.min(all_sizes)}\n"
        f"Pkt Std : {np.std(all_sizes):.2f}\n"
        f"Pkt Cnt : {len(all_sizes)}"
    )
    
    fig.text(
        0.83, 0.52, 
        global_stats_text, 
        fontsize=10, 
        family='monospace', 
        va='center', 
        bbox=dict(
            facecolor='white',      # 改成純白背景
            alpha=0.8,              # 透明度與小框一致
            edgecolor='#dddddd',    # 改成淡灰色邊框
            boxstyle='round,pad=0.8' # 保持圓角
        )
    )
    plt.suptitle("NoC Multi-Routing Heatmap & Load Balance Analysis", fontsize=24, y=0.96, fontweight='bold')
    
    cb_y = 0.08
    fig.add_axes([0.15, cb_y, 0.15, 0.015]); ColorbarBase(plt.gca(), cmap=cmap_r, orientation='horizontal', label='Router Load')
    fig.add_axes([0.38, cb_y, 0.15, 0.015]); ColorbarBase(plt.gca(), cmap=cmap_l, orientation='horizontal', label='Link Load')
    fig.add_axes([0.61, cb_y, 0.15, 0.015]); ColorbarBase(plt.gca(), cmap=cmap_c, orientation='horizontal', label='Core I/O Load')

    plt.show()

draw_noc_comparison("pattern")