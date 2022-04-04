#include <iostream>
#include <string>
#include "llvm/Support/CommandLine.h"
#include "Type.h"
#include "filesystem.h"
#include "Util.h"
#include "boilerplate.h"
#include "AsyncIo.h"
#include "Runtime.h"

namespace cll = llvm::cl;

static cll::opt<int>
  numDisks("numDisks",
         cll::desc("Number of disks (default value 1)"),
         cll::init(1));

static cll::opt<bool>
  weighted("weighted",
         cll::desc("Flag for weighted graph (default value: false)"),
         cll::init(false));


static cll::opt<std::string>
  inputFilename(cll::Positional, cll::desc("<input file>"), cll::Required);

using namespace blaze;

std::string get_index_file_name(const std::string& input) {
  return input + ".index";
}

void get_adj_file_names(const std::string& input, int num_disks, std::vector<std::string>& out_files) {
  for (int i = 0; i < num_disks; i++) {
    out_files.push_back(input + ".adj." + std::to_string(num_disks) + "." + std::to_string(i));
  }
}

void write_index_file(const std::string& input, std::string& out) {
  char* base; size_t len;
  std::tie(base, len) = map_file(input);

  struct graph_header *header = (struct graph_header *)base;
  uint64_t index_file_size = sizeof(*header) + sizeof(uint64_t) * header->num_nodes;

  // calculate space after compaction
  size_t num_offsets = ((header->num_nodes - 1) / 16) + 1;
  size_t len_header = sizeof(*header) + num_offsets * sizeof(uint64_t);
  size_t len_header_aligned = ALIGN_UPTO(len_header, CACHE_LINE);

  size_t new_len = len_header_aligned + num_offsets * CACHE_LINE;

  printf("# nodes: %lu\n", header->num_nodes);
  printf("[original]\n");
  printf("  index size  : %lu\n", index_file_size);
  printf("\n");
  printf("[compact]\n");
  printf("  header size : %lu\n", len_header_aligned);
  printf("    header size  : %lu\n", sizeof(*header));
  printf("    offset size  : %lu\n", num_offsets * sizeof(uint64_t));
  printf("    before align : %lu\n", len_header);
  printf("+ degree size : %lu\n", num_offsets * CACHE_LINE);
  printf("= index size  : %lu\n", new_len);

  char* new_base = create_and_map_file(out, new_len);

  uint64_t *index = (uint64_t *)(base + sizeof(*header));
  uint32_t degree;
  uint64_t offset;

  uint64_t *np = (uint64_t *)new_base;
  *np++ = 0;
  *np++ = 0;
  *np++ = header->num_nodes;
  *np++ = header->num_edges;

  uint64_t *new_index = (uint64_t *)np;
  uint32_t *degrees = (uint32_t*)(new_base + len_header_aligned);

  for (uint64_t node = 0; node < header->num_nodes; node++) {
    if (node == 0) {
      degree = index[node];
      offset = 0;

    } else {
      degree = index[node] - index[node-1];
      offset = index[node-1];
    }

    if (node % 16 == 0) {
      new_index[node / 16] = offset;
    }
    degrees[node] = degree;
  }

  munmap(base, len);

  msync(new_base, new_len, MS_SYNC);
  munmap(new_base, new_len);
}

void write_adj_files(const std::string& input, std::vector<std::string>& out_files) {
  char* base; size_t len;
  std::tie(base, len) = map_file(input);
  uint64_t* p = (uint64_t*)base;
  p++;   // graph version: 1 or 2
  p++;   // size of edge
  uint64_t num_nodes = *p++;
  uint64_t num_edges = *p++;
  uint64_t edge_starts = sizeof(uint64_t) * (4 + num_nodes);

  int num_disks = out_files.size();
  uint64_t tuple_size = sizeof(VID);
  if (weighted)
    tuple_size += sizeof(EDGEDATA);
  uint64_t total_edge_bytes = num_edges * tuple_size;
  uint64_t total_num_pages = (total_edge_bytes - 1) / PAGE_SIZE + 1;
  uint64_t num_pages_per_disk = total_num_pages / num_disks;    // FIXME may not be equal

  int fd[num_disks];
  for (int i = 0; i < num_disks; i++) {
    fd[i] = open(out_files[i].c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  }

  char* buf = base + edge_starts;
  const char* buf_end = buf + total_edge_bytes;

  uint64_t page_cnt = 0;
  while (buf < buf_end) {
    size_t len = PAGE_SIZE; 
    if (buf + len > buf_end)
      len = buf_end - buf;
    write(fd[page_cnt % num_disks], buf, len);
    if (len < PAGE_SIZE) {
      char tmp[PAGE_SIZE - len];
      write(fd[page_cnt % num_disks], tmp, PAGE_SIZE - len);
    }
    buf += PAGE_SIZE;
    page_cnt++;
  }

  for (int i = 0; i < num_disks; i++) {
    close(fd[i]);
  }
  
  munmap(base, len);
}

void convert(const std::string& input, int num_disks) {
  // write index file
  auto index_file_name = get_index_file_name(input);
  write_index_file(input, index_file_name);

  // write adj files
  std::vector<std::string> adj_file_names;
  get_adj_file_names(input, num_disks, adj_file_names);
  write_adj_files(input, adj_file_names);
}


int main(int argc, char **argv) {
  AgileStart(argc, argv);
  Runtime runtime(numComputeThreads, numIoThreads, ioBufferSize * MB);

  galois::StatTimer timer("Time", "CONVERT");
  timer.start();

  convert(inputFilename, numDisks);

  timer.stop();

  return 0;
}
