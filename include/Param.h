#ifndef AGILE_PARAM_H
#define AGILE_PARAM_H

#define CACHE_LINE 64

// IO
#define PAGE_SIZE               4096
#define PAGE_SHIFT              12
#define IO_QUEUE_DEPTH          256
#define IO_MAX_PAGES_PER_REQ    32

// IO page queue
#define IO_PAGE_QUEUE_INIT_SIZE 16384
#define IO_PAGE_QUEUE_BULK_DEQ  2048

// Sparse dense
#define DENSE_THRESHOLD         0.005

// Prop blocking
#define BINNING_WORKER_RATIO    0.67
#define BIN_COUNT               4096
#define BIN_SHIFT               12
#define BIN_BUF_SIZE            128

#endif // AGILE_PARAM_H
