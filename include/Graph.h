#ifndef BLAZE_GRAPH_H
#define BLAZE_GRAPH_H

#include <string>
#include <unordered_map>
#include <map>
#include <omp.h>
//#include <locale.h>
#include <boost/iterator/counting_iterator.hpp>
#include "filesystem.h"
#include "galois/Galois.h"
#include "Bitmap.h"
#include "Util.h"

namespace blaze {

class Graph {
 public:
    using iterator = boost::counting_iterator<VID>;

 public:
    Graph(): _input_index_file_base(nullptr), _input_index_file_len(0),
             _num_disks(0), _input_edge_file_descs(nullptr), _num_nodes(0), _num_empty_nodes(0),
             _non_empty_nodes(nullptr), _num_edges(0),
             _index_offsets(nullptr), _index_degrees(nullptr), _num_disk_pages(0), _p2v_map(nullptr),
             _activated_pages(nullptr) {}
    ~Graph() {
        if (_input_edge_file_descs) {
            assert(_num_disks > 0);
            for (int i = 0; i < _num_disks; i++) { 
                int fd = _input_edge_file_descs[i];
                if (fd > 0)
                    close(fd);
            }
            delete [] _input_edge_file_descs;
        }
        if (_input_index_file_base) {
            munmap(_input_index_file_base, _input_index_file_len);
        }
        if (_p2v_map) delete _p2v_map;
        if (_activated_pages) {
            for (int i = 0; i < _num_disks; i++) { 
                delete _activated_pages[i];
            }
            delete [] _activated_pages;
        }
        if (_non_empty_nodes)
            delete _non_empty_nodes;
    }

    VID NumberOfNodes() const { return _num_nodes; }

    VID NumberOfEmptyNodes() const { return _num_empty_nodes; }

    VID NumberOfNonEmptyNodes() const { return _num_nodes - _num_empty_nodes; }

    uint64_t NumberOfEdges() const { return _num_edges; }

    uint64_t GetEdgeSize() const { return NumberOfEdges() * sizeof(VID); }

    int NumberOfDisks() const { return _num_disks; }

    int GetEdgeFileDescriptor(int idx) const { return _input_edge_file_descs[idx]; }

    std::string GetInputFileName() const { return _input_index_file; }   // FIXME

    std::string GetEdgeFileName(int idx) const { return _input_edge_files[idx]; }

    uint32_t GetDegree(VID node) const {
        return _index_degrees[node];
    }

    uint64_t GetOffset(VID node) const {
        uint64_t offset = _index_offsets[node >> 4];
        VID beg = (node >> 4) << 4;
        while (beg < node) {
            offset += _index_degrees[beg++];
        }
        return offset;
    }

    void GetPageRange(VID node, PAGEID *beg, PAGEID *end) const {
        uint32_t degree = GetDegree(node);
        uint64_t offset = GetOffset(node);
        uint64_t on_disk_offset = offset * sizeof(VID);
        *beg = PAGE_NUM(on_disk_offset);
        *end = PAGE_NUM(on_disk_offset + degree * sizeof(VID));
        if (*end == _num_disk_pages) {
            *end -= 1;
        }
    }

    // TODO Use this for debugging purpose only
    std::vector<VID> GetEdges(VID node) {
        assert(_num_disks);
        std::vector<VID> result;

        size_t degree = GetDegree(node);
        uint64_t offset = GetOffset(node);
        uint64_t on_disk_offset = offset * sizeof(VID);

        if (degree == 0) return result;

        PAGEID pid, pid_end;
        GetPageRange(node, &pid, &pid_end);
        size_t page_size = PAGE_SIZE * (pid_end - pid + 1);
        char* buf;
        int ret = posix_memalign((void**)&buf, PAGE_SIZE, page_size);
        assert(ret == 0);

        char *p = buf;

        while (pid <= pid_end) {
            int disk_id = pid % _num_disks;
            PAGEID phy_pid = pid / _num_disks;

            int fd = GetEdgeFileDescriptor(disk_id);
            assert(fd > 0);
            int ret = pread(fd, p, PAGE_SIZE, phy_pid * PAGE_SIZE);

            p = (char *)((uint64_t)p + PAGE_SIZE);
            pid++;
        }

        char* beg = buf + OFFSET_IN_PAGE(on_disk_offset);
        const char* end = beg + degree * sizeof(VID);

        while (beg < end) {
            VID dst = *(VID *)beg;
            result.push_back(dst);
            beg += sizeof(VID);
        }

        free(buf);

        return result;
    }

    uint64_t GetNumPages(int idx) const {
        uint64_t size = GetEdgeFileSize(idx);
        assert(size % PAGE_SIZE == 0);
        return size / PAGE_SIZE;
    }

    uint64_t GetTotalNumPages() const {
        uint64_t sum = 0;
        for (int i = 0; i < _num_disks; i++) {
            sum += GetNumPages(i);
        }
        return sum;
    }

    VidRange& GetP2VMap() {
        return *_p2v_map;
    }

    Bitmap* GetActivatedPages(int idx) {
        return _activated_pages[idx];
    }

    Bitmap* GetNonEmptyNodes() {
        return _non_empty_nodes;
    }

    void BuildGraph(const std::string& input_index_file, std::vector<std::string>& input_edge_files) {
        // Load metadata, index
        LoadGraph(input_index_file);

        InitVertices();

        InitEdgeFileDescriptors(input_edge_files);

        InitPage2VertexMap();

        // Below is per disk
        InitPageActivationStructures();

        Print();
    }

    uint64_t GetEdgeFileSize(int idx) const {
        return file_size(_input_edge_files[idx]);
    }

    uint64_t GetTotalEdgeFileSize() const {
        uint64_t sum = 0;
        for (auto edge_file : _input_edge_files)
            sum += file_size(edge_file);

        return sum;
    }

    iterator begin() const { return iterator(0); }

    iterator end() const { return iterator(_num_nodes); }

    void ResetPageActivation() {
        for (int i = 0; i < _num_disks; i++) {
            _activated_pages[i]->reset_parallel();
        }
    }

    void Print() {
        printf("V: %'15u (%'u, %.1f%%)\n", _num_nodes, NumberOfNonEmptyNodes(), (double)NumberOfNonEmptyNodes() * 100.0 / _num_nodes);
        printf("E: %'15lu\n", _num_edges);
    }

 private:
    void LoadGraph(const std::string& input) {
        std::tie(_input_index_file_base, _input_index_file_len) = map_file(input);
        struct graph_header *header = (struct graph_header *)_input_index_file_base;
        _index_offsets = (uint64_t *)(_input_index_file_base + sizeof(*header));

        size_t num_offsets = ((header->num_nodes - 1) / 16) + 1;
        size_t len_header = sizeof(*header) + num_offsets * sizeof(uint64_t);
        size_t len_header_aligned = ALIGN_UPTO(len_header, CACHE_LINE);
        _index_degrees = (uint32_t *)(_input_index_file_base + len_header_aligned);

        this->_num_nodes = header->num_nodes;
        this->_num_edges = header->num_edges;
        this->_input_index_file = input;
    }

    // TODO: adjust disk offsets to align the first vertex to the first page

    void InitVertices() {
        _non_empty_nodes = new Bitmap(_num_nodes);
        for (int64_t i = 0; i < _num_nodes; i++) {
            if (GetDegree(i) > 0) {
                _non_empty_nodes->set_bit(i);
            } else {
                _num_empty_nodes++;
            }
        }
    }

    void InitPage2VertexMap() {
        // Page to vertex map
        assert(_p2v_map == nullptr);
        _p2v_map = new VidRange[_num_disk_pages];

        VID prev_vid, curr_vid, vid_start;
        PAGEID prev_pid, curr_pid;

        vid_start = prev_vid = curr_vid = 0;
        prev_pid = 0;

        while (curr_vid < _num_nodes) {
            if (GetDegree(curr_vid) == 0) {
                curr_vid++;
                continue;
            }

            uint64_t on_disk_offset = GetOffset(curr_vid) * sizeof(VID);
            curr_pid = PAGE_NUM(on_disk_offset);
            if (prev_pid < curr_pid) {
                _createEntries(&vid_start, prev_vid, curr_vid);
                prev_pid = curr_pid;
            }
            prev_vid = curr_vid;
            curr_vid++;
        }
        _createEntries(&vid_start, prev_vid, curr_vid);
    }

    void InitEdgeFileDescriptors(std::vector<std::string>& files) {
        assert(_input_edge_file_descs == nullptr);
        _num_disks = files.size();
        assert(_num_disks);

        _input_edge_file_descs = new int [_num_disks];
        for (int i = 0; i < _num_disks; i++) {
            _input_edge_file_descs[i] = open(files[i].c_str(), O_RDONLY | O_DIRECT);
        }

        for (auto fil : files) {
            _input_edge_files.push_back(fil);
        }

        _num_disk_pages = GetTotalNumPages();
    }

    void InitPageActivationStructures() {
        // Bitmap for dense
        assert(_num_disks > 0);
        assert(_activated_pages == nullptr);
        _activated_pages = new Bitmap* [_num_disks];
        for (int i = 0; i < _num_disks; i++) {
            _activated_pages[i] = new Bitmap(GetNumPages(i));
            _activated_pages[i]->reset_parallel();
        }
    }

    void _createEntries(VID *vid_start, VID vid, VID next_vid) {
        uint32_t degree = GetDegree(vid);
        uint64_t offset = GetOffset(vid)* sizeof(VID);
        uint64_t offset_end = offset + degree * sizeof(VID);
        assert(degree > 0);

        PAGEID pid = offset >> PAGE_SHIFT;
        _p2v_map[pid++] = std::make_pair(*vid_start, vid);
        PAGEID last_pid = (offset_end - 1) >> PAGE_SHIFT;
        if (pid <= last_pid) {
            while (pid < last_pid) {
                _p2v_map[pid++] = std::make_pair(vid, vid);
            }
            if (offset_end % PAGE_SIZE == 0) {
                _p2v_map[last_pid] = std::make_pair(vid, vid);
            }
        }
        if (offset_end % PAGE_SIZE) {
            *vid_start = vid;
        } else {
            *vid_start = next_vid;
        }
    }

 private:
    std::string                 _input_index_file;
    char*                       _input_index_file_base;
    size_t                      _input_index_file_len;
    std::vector<std::string>    _input_edge_files;
    int                         _num_disks;
    int*                        _input_edge_file_descs;
    VID                         _num_nodes;
    VID                         _num_empty_nodes;
    Bitmap*                     _non_empty_nodes;
    uint64_t                    _num_edges;
    uint64_t*                   _index_offsets;
    uint32_t*                   _index_degrees;
    uint64_t                    _num_disk_pages;
    VidRange*                   _p2v_map;
    // Below data structures need for each disk
    Bitmap**                    _activated_pages;
};

} // namespace blaze

#endif // BLAZE_GRAPH_H
