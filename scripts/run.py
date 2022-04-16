#!/usr/bin/env python3

import sys
import os
import subprocess
import argparse

BLAZE_BINARY_PATH = "../build/bin"

def exec_cmd(cmd, ignore_exception=False, dry=False):
    if dry:
        print(cmd)
        return None

    p = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0 and not ignore_exception:
        # Should not use p.stdout or p.stderr. Following from the subprocess doc.
        # Warning Use communicate() rather than .stdin.write, .stdout.read or .stderr.read
        # to avoid deadlocks due to any of the other OS pipe buffers filling up and blocking
        # the child process.
        msg = "STDOUT\n"
        msg += p.stdout.decode("utf-8")
        msg += "STDERR\n"
        msg += p.stderr.decode("utf-8")
        raise Exception(msg)
    return p

def get_index_file_name(disks, dataset, data_format):
    return "{0}/{1}.{2}.index".format(disks[0], dataset, data_format)

def get_adj_file_names(disks, dataset, data_format):
    return ["{0}/{1}.{2}.adj.{3}".format(disk, dataset, data_format, i) for i, disk in enumerate(disks)]

def build_command(args, data_format):
    cmd = BLAZE_BINARY_PATH + "/" + args.kernel
    cmd += ' ' + '-computeWorkers=' + str(args.threads)
    if args.kernel.startswith('bfs') or args.kernel.startswith('bc'):
        cmd += ' ' + '-startNode=' + str(args.start_node)
    if args.kernel.startswith("pagerank") or args.kernel.startswith("spmv"):
        cmd += ' ' + '-maxIterations=' + str(args.max_iterations)
    if not args.kernel.endswith('_sync'):
        cmd += ' ' + '-binSpace=' + str(args.bin_size)
        cmd += ' ' + '-binCount=' + str(args.bin_count)
        cmd += ' ' + '-binningRatio=' + str(args.bin_ratio)
    cmd += ' ' + get_index_file_name(args.disks, args.dataset, data_format)
    for edge_file in get_adj_file_names(args.disks, args.dataset, data_format):
        cmd += ' ' + edge_file

    if args.kernel in ('bc', 'bc_sync', 'wcc', 'wcc_sync'):
        cmd += ' ' + '-inIndexFilename=' + get_index_file_name(args.disks, args.dataset, 't' + data_format)
        cmd += ' ' + '-inAdjFilenames'
        for edge_file in get_adj_file_names(args.disks, args.dataset, 't' + data_format):
            cmd += ' ' + edge_file

    return cmd

def get_read_bytes_from_iostat(filename):
    with open(filename) as f:
        lines = f.readlines()
        for line in lines:
            words = line.split()
            if not words: continue
            if words[0] == 'nvme0n1':
                return int(words[4])

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Arguments for running FastGraph")
    # Optional
    parser.add_argument('-t', '--threads', default=4, type=int,
                      help='Number of compute threads (default: 4)')
    # For BFS, BC
    parser.add_argument('--start_node', default=-1, type=int,
                      help='Start node for BFS and BC (default: -1)')
    # For PR, SpMV
    parser.add_argument('--max_iterations', default=1000, type=int,
                      help='Max iterations for PageRank and SpMV (default: 1000)')

	# For binning
    parser.add_argument('--bin_size', default=256, type=int,
                      help='Bin size in MB (default: 256)')
    parser.add_argument('--bin_count', default=1024, type=int,
                      help='Bin count (default: 1024)')
    parser.add_argument('--bin_ratio', default=0.5, type=float,
                      help='Ratio of scatter workers among all workers (default: 0.5)')
    # Others
    parser.add_argument('--result_dir', default="result",
                      help='Directory to store result files (default: result)')
    parser.add_argument('--print_cmd', action='store_true',
                      help='Print the command for debugging')
    parser.add_argument('--print_stdout', action='store_true',
                      help='Print the standard output')
    parser.add_argument('--dry', action='store_true',
                      help='Dry run')
    parser.add_argument('--force', action='store_true', default=True,
                      help='Forcefully run even when the result file exists')
    # Required
    parser.add_argument('-k', '--kernel',
                      help='Graph kernel', required=True)
    parser.add_argument('--disks', nargs='+',
                      help='List of disk paths', required=True)
    parser.add_argument('-d', '--dataset',
                      help='Dataset ID', required=True)
    args = parser.parse_args()

    if args.kernel in ('bfs', 'bc') and args.start_node < 0:
        parser.error('the argument --start_node is required for bfs, bc')

    if args.kernel in ("pagerank", "spmv") and args.max_iterations < 0:
        parser.error('the argument --max_iterations is required for pagerank and spmv')

    num_disks = len(args.disks)

    # Build command
    cmd = build_command(args, data_format)

    result_dir = args.result_dir
    kernel = args.kernel
    dataset = args.dataset
    threads = args.threads
    bin_size = args.bin_size
    bin_count = args.bin_count
    bin_ratio = args.bin_ratio

    path = f'{result_dir}/{kernel}'
    if (args.kernel.startswith('pagerank') or args.kernel.startswith('spmv')) and args.max_iterations > 0 and args.max_iterations < 1000:
        path += str(args.max_iterations)

    if not os.path.exists(path):
        os.makedirs(path)

    if args.kernel.endswith('_sync'):
    	result_file = f'{path}/{dataset}_{data_format}_{threads}_{num_disks}.txt'
	else:
		b = int(threads * bin_ratio)
		a = threads - b
		result_file = f'{path}/{dataset}_{data_format}_{threads}_{num_disks}__{bin_count}_{b}_{a}__{bin_size}.txt'

    if not args.force and os.path.exists(result_file):
        print(f'{result_file} exists.')
        sys.exit()

    if not args.print_stdout:
        cmd += f' > {result_file}'

    if args.print_cmd:
        print(cmd)

    if args.dry:
        print(cmd)
        sys.exit()

    iostat_beg = 'iostat_beg.txt'
    iostat_end = 'iostat_end.txt'

    # Collect IO stat
    exec_cmd(f'iostat > {iostat_beg}')

    # Collect IO stat log
    os.system(f'iostat -m -d 1 > {result_file}.iostat &')

    # Run workload
    exec_cmd(cmd)

    # Kill IO stat logging
    exec_cmd(f'killall iostat')

    # Collect IO stat
    exec_cmd(f'iostat > {iostat_end}')

    # Collect IO stat
    beg = get_read_bytes_from_iostat(iostat_beg)
    end = get_read_bytes_from_iostat(iostat_end)
    bytes_read = (end - beg) * 1024
    cmd = f'echo "Bytes read: {bytes_read}" >> {result_file}'
    exec_cmd(cmd)
    exec_cmd(f'rm {iostat_beg}')
    exec_cmd(f'rm {iostat_end}')
