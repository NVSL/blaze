# some helper apps
#foreach(PROG bfs pagerank pagerank_pb kcore wcc bc)
foreach(PROG bfs bfs_pb pagerank pagerank_pb wcc wcc_pb spmv spmv_pb bc bc_pb kcore)
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
