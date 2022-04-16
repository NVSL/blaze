# queries
foreach(PROG bfs bfs_sync pagerank pagerank_sync wcc wcc_sync spmv spmv_sync bc bc_sync)
    add_executable(${PROG} ${PROG}.cpp boilerplate.cpp ../src/Runtime.cpp ${CORE_HEADERS})
    target_link_libraries(${PROG} PUBLIC Threads::Threads
                            gllvm
                            galois_shmem
                            tcmalloc)
endforeach()

# tools
foreach(PROG convert)
    add_executable(${PROG} ${PROG}.cpp boilerplate.cpp ../src/Runtime.cpp ${CORE_HEADERS})
    target_link_libraries(${PROG} PUBLIC Threads::Threads
                            gllvm
                            galois_shmem
                            tcmalloc)
endforeach()
