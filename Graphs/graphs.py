import matplotlib.pyplot as plt

# Global settings for PPT readability (Larger fonts)
plt.rcParams.update({'font.size': 14})
plt.rcParams['figure.figsize'] = (10, 6)
plt.rcParams['lines.linewidth'] = 3
plt.rcParams['lines.markersize'] = 10

def create_dual_axis_graph(filename, x_labels, y1_data, y2_data, 
                           y1_label, y2_label, 
                           title, color1, color2, 
                           y2_max=None, is_util=False):
    
    fig, ax1 = plt.subplots()
    
    # X-Axis setup
    x = range(len(x_labels))
    ax1.set_xlabel('Load Level (Threads)', fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels(x_labels)

    # Primary Y-Axis (Left)
    ax1.set_ylabel(y1_label, color=color1, fontweight='bold')
    ax1.plot(x, y1_data, color=color1, marker='o', label=y1_label)
    ax1.tick_params(axis='y', labelcolor=color1)
    ax1.grid(True, which='major', linestyle='--', alpha=0.5)

    # Secondary Y-Axis (Right)
    ax2 = ax1.twinx()
    ax2.set_ylabel(y2_label, color=color2, fontweight='bold')
    
    # If it's a utilization graph, use a dashed line to distinguish it
    linestyle = '--' if is_util else '-'
    marker = '^' if is_util else 's'
    
    ax2.plot(x, y2_data, color=color2, marker=marker, linestyle=linestyle, label=y2_label)
    ax2.tick_params(axis='y', labelcolor=color2)

    if y2_max:
        ax2.set_ylim(0, y2_max)

    plt.title(title, fontweight='bold', pad=20)
    plt.tight_layout()
    plt.savefig(filename, dpi=300) # High Res for PPT
    plt.close()
    print(f"Generated: {filename}")

# ==========================================
# 1. CPU WORKLOAD DATA (Excluding T-64)
# ==========================================
l1_threads = ['T-1', 'T-2', 'T-4', 'T-8', 'T-12', 'T-16', 'T-20', 'T-32']
l1_thrpt   = [1219, 1694, 1897, 1937, 1941, 1940, 1941, 1961]
l1_lat     = [1.26, 1.29, 1.66, 3.71, 5.75, 7.81, 9.86, 15.86]
l1_util    = [70, 80, 92.7, 98, 98.4, 99, 99.2, 100]

# Graph 1A: Performance
create_dual_axis_graph('Slide_CPU_Performance.png', l1_threads, l1_thrpt, l1_lat,
                       'Throughput (req/s)', 'Latency (ms)',
                       'Load 1: Performance Degradation', 'tab:blue', 'tab:red')

# Graph 1B: Bottleneck
create_dual_axis_graph('Slide_CPU_Bottleneck.png', l1_threads, l1_thrpt, l1_util,
                       'Throughput (req/s)', 'CPU Utilization (%)',
                       'Load 1: CPU Saturation Analysis', 'tab:blue', 'black', y2_max=110, is_util=True)


# ==========================================
# 2. BANDWIDTH WORKLOAD DATA (Excluding T-10)
# ==========================================
l2_threads = ['T-1', 'T-2', 'T-3', 'T-4', 'T-5', 'T-6', 'T-8']
l2_thrpt   = [533, 991, 1038, 1045, 1039, 1057, 1048]
l2_lat     = [1.13, 1.28, 2.22, 3.32, 4.3, 5.16, 7.13]
l2_io_spd  = [4.87, 9.33, 9.95, 10.2, 10.5, 10.5, 10.5]

# Graph 2A: Performance
create_dual_axis_graph('Slide_BW_Performance.png', l2_threads, l2_thrpt, l2_lat,
                       'Throughput (req/s)', 'Latency (ms)',
                       'Load 2: Network Bandwidth Constraints', 'green', 'orange')

# Graph 2B: Bottleneck
create_dual_axis_graph('Slide_BW_Bottleneck.png', l2_threads, l2_thrpt, l2_io_spd,
                       'Throughput (req/s)', 'IO Speed (MB/s)',
                       'Load 2: Bandwidth Ceiling (10.5 MB/s)', 'green', 'black', y2_max=12, is_util=True)


# ==========================================
# 3. DISK WORKLOAD DATA
# ==========================================
l3_threads = ['T-1', 'T-2', 'T-4', 'T-8', 'T-16']
l3_thrpt   = [360, 932, 1571, 2556, 2442]
l3_lat     = [2.35, 1.42, 2.1, 2.63, 6.04]
l3_util    = [30, 60, 93, 99.1, 98.7]

# Graph 3A: Performance
create_dual_axis_graph('Slide_Disk_Performance.png', l3_threads, l3_thrpt, l3_lat,
                       'Throughput (req/s)', 'Latency (ms)',
                       'Load 3: Disk Thrashing Effect', 'purple', 'brown')

# Graph 3B: Bottleneck
create_dual_axis_graph('Slide_Disk_Bottleneck.png', l3_threads, l3_thrpt, l3_util,
                       'Throughput (req/s)', 'Disk Utilization (%)',
                       'Load 3: Physical Disk Saturation', 'purple', 'black', y2_max=110, is_util=True)