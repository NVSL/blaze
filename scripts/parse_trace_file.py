#!/usr/bin/env python3

from collections import defaultdict

PAGE_SIZE = 4096

def parse_blaze_trace_file(file_name, field):
    result = {}
    try:
        f = open(file_name)
        lines = f.readlines()
        for line in lines:
            line = line.strip()
            if line.startswith("# PAGE CACHE  : Benefit"):
                # total bytes accessed by cache & IO
                total_bytes = int(line.split()[10].replace(',', ''))
                result['total_bytes'] = total_bytes
                # cache hit ratio
                cache_hit_ratio = line.split()[7][1:][:-1]
                result['cache_hit_ratio'] = cache_hit_ratio
            if line.startswith("# IO SUMMARY"):
                # total bytes accessed by IO
                total_io_bytes = int(line.split()[4].replace(',', ''))
                result['total_io_bytes'] = total_io_bytes
                io_bandwidth = float(line.split()[8])
                result['io_bandwidth'] = io_bandwidth 
            if "_MAIN, Time," in line:
                # time in second
                time = int(line.split()[4].replace(',', '')) / 1000.0
                result['time'] = time
            if line.startswith("# SUMMARY"):
                total_accessed_edges = int(line.split()[3])
                result['total_accessed_edges'] = total_accessed_edges
        io_bw = float(result['total_io_bytes']) / (1000 * 1000) / float(result['time'])
        result['io_bw'] = int(io_bw)
        # io amplification
        if result.get('total_accessed_edges'):
            result['io_amp'] = round(float(result['total_io_bytes']) / result['total_accessed_edges'] / 4, 1)
    except:
        return None
    return result.get(field)

def parse_flashgraph_trace_file(file_name, field):
    result = {}
    iteration = 0
    compute_times = defaultdict(list)
    try:
        f = open(file_name)
        lines = f.readlines()
        stats = lines[-2].strip().split(',')
        # total bytes accessed by IO
        total_io_bytes = int(stats[4])
        result['total_io_bytes'] = total_io_bytes
        # cache hit ratio
        cache_hit_stat  = stats[6]
        cache_hit_ratio = cache_hit_stat.split(' ')[0][:-1]
        result['cache_hit_ratio'] = float(cache_hit_ratio)
        # total bytes accessed by cache + IO
        hit_over_all = cache_hit_stat.split(' ')[1]
        cache_hit_pages = int(hit_over_all.split('/')[0][1:])
        all_pages       = int(hit_over_all.split('/')[1][:-1])
        total_bytes     = all_pages * PAGE_SIZE
        result['total_bytes'] = total_bytes
        # time in second
        time = float(stats[2])
        result['time'] = time
        # total accessed edges
        result['total_accessed_edges'] = int(stats[3])
        result['io_amp'] = round(float(stats[5]) / 4, 1)
        # io bandwidth
        io_bw = float(total_io_bytes) / (1000 * 1000) / time
        result['io_bw'] = int(io_bw)
        # compute time
        # [WORKER   1 BREAKDOWN] 6.20 sec proc_stol
        for line in lines:
            if line.startswith('[WORKER'):
                compute_time = float(line.split()[3])
                compute_times[iteration].append(compute_time)
            elif line.startswith('Iter '):
                iteration += 1
    except:
        return None

    # Compute time
    max_time = sum([max(values) for iteration, values in compute_times.items()])
    min_time = sum([min(values) for iteration, values in compute_times.items()])
    result['compute_skew'] = round(float(max_time) / min_time, 2) if min_time > 0 else 0.0

    return result.get(field)

def parse_graphene_trace_file(file_name, field):
    result = {}
    total_io_bytes = 0
    io_sizes = defaultdict(list)
    iteration = 0
    try:
        f = open(file_name)
        lines = f.readlines()
        for line in lines:
            words = line.split()
            if line.startswith("@level"):
                iteration = line.split('-')[1]
                total_io_bytes += int(words[7])

            if line.startswith("IO size"):
                io_sizes[iteration].append(int(words[3]))

            if line.startswith("Total time:"):
                time = float(words[2])
                result['time'] = time

            if line.startswith("total accessed edges:"):
                total_accessed_edges = int(words[3])
                result['total_accessed_edges'] = total_accessed_edges

        result['total_io_bytes'] = total_io_bytes
        if result.get('total_accessed_edges'):
          result['io_amp'] = round(float(result['total_io_bytes']) / result['total_accessed_edges'] / 4, 1)

        # io bandwidth
        io_bw = float(total_io_bytes) / (1000 * 1000) / result['time']
        result['io_bw'] = int(io_bw)
    except:
        return None

    # IO skew
    max_bytes = sum([max(values) for iteration, values in io_sizes.items()])
    min_bytes = sum([min(values) for iteration, values in io_sizes.items()])
    result['io_skew'] = round(float(max_bytes) / min_bytes, 2) if min_bytes > 0 else 0.0

    return result.get(field)
