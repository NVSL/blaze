#!/usr/bin/env bash

result_dir=result
disks=/mnt/nvme
threads=16

# Run workloads

# 1. Online binning-based (figure 8.a)

# BFS
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs -d rmat27 --start_node 0
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs -d uran27 --start_node 0
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs -d twitter --start_node 12
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs -d sk2005 --start_node 50395005
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs -d friendster --start_node 101
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs -d rmat30 --start_node 0

# PageRank 
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank -d rmat27
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank -d uran27
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank -d twitter
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank -d sk2005
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank -d friendster
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank -d rmat30

# WCC
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc -d rmat27
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc -d uran27
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc -d twitter
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc -d sk2005
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc -d friendster
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc -d rmat30

# SpMV
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv -d rmat27 --max_iterations 1
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv -d uran27 --max_iterations 1
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv -d twitter --max_iterations 1
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv -d sk2005 --max_iterations 1
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv -d friendster --max_iterations 1
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv -d rmat30 --max_iterations 1

# BC
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc -d rmat27 --start_node 0
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc -d uran27 --start_node 0
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc -d twitter --start_node 12
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc -d sk2005 --start_node 50395005
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc -d friendster --start_node 101
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc -d rmat30 --start_node 0

# 2. Synchronization-based (figure 8.b)

# BFS
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs_sync -d rmat27 --start_node 0
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs_sync -d uran27 --start_node 0
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs_sync -d twitter --start_node 12
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs_sync -d sk2005 --start_node 50395005
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs_sync -d friendster --start_node 101
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bfs_sync -d rmat30 --start_node 0

# PageRank 
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank_sync -d rmat27
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank_sync -d uran27
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank_sync -d twitter
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank_sync -d sk2005
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank_sync -d friendster
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k pagerank_sync -d rmat30

# WCC
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc_sync -d rmat27
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc_sync -d uran27
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc_sync -d twitter
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc_sync -d sk2005
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc_sync -d friendster
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k wcc_sync -d rmat30

# SpMV
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv_sync -d rmat27 --max_iterations 1
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv_sync -d uran27 --max_iterations 1
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv_sync -d twitter --max_iterations 1
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv_sync -d sk2005 --max_iterations 1
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv_sync -d friendster --max_iterations 1
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k spmv_sync -d rmat30 --max_iterations 1

# BC
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc_sync -d rmat27 --start_node 0
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc_sync -d uran27 --start_node 0
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc_sync -d twitter --start_node 12
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc_sync -d sk2005 --start_node 50395005
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc_sync -d friendster --start_node 101
./run.py --result_dir ${result_dir} --disks ${disks} -t ${threads} -k bc_sync -d rmat30 --start_node 0


# Produce a csv file
./generate_datafile.py blaze configs/figure8_a.csv io_bw figure8_a.csv
./generate_datafile.py blaze configs/figure8_b.csv io_bw figure8_b.csv
