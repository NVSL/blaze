#ifndef AGILE_GRAPH_H
#define AGILE_GRAPH_H

#include <string>
#include <unordered_map>
#include <map>
#include <omp.h>
//#include <locale.h>
#include <boost/iterator/counting_iterator.hpp>
#include "filesystem.h"
//#include "galois/Galois.h"
#include "Bitmap.h"
#include "Util.h"

namespace agile {

class MemGraph {
 public:
    using iterator = boost::counting_iterator<VID>;

 public:
    MemGraph(): _input_index_file_base(nullptr), _input_index_file_len(0),
                _num_disks(0), _num_nodes(0), _num_empty_nodes(0), _num_edges(0),
                _index_offsets(nullptr), _index_degrees(nullptr), _edges(nullptr), _vertex_bitmap(nullptr), _p2v_map(nullptr) {}
    ~MemGraph() {
        if (_input_index_file_base) {
            munmap(_input_index_file_base, _input_index_file_len);
        }
        if (_vertex_bitmap) delete _vertex_bitmap;
        if (_p2v_map) delete _p2v_map;
        if (_edges) {
            for (int i = 0; i < _num_disks; i++) {
                free(_edges[i]);
            }
            delete [] _edges;
        }
    }

    VID NumberOfNodes() const { return _num_nodes; }

    VID NumberOfEmptyNodes() const { return _num_empty_nodes; }

    VID NumberOfNonEmptyNodes() const { return _num_nodes - _num_empty_nodes; }

    uint64_t NumberOfEdges() const { return _num_edges; }

    uint64_t GetEdgeSize() const { return NumberOfEdges() * sizeof(VID); }

    int NumberOfDisks() const { return _num_disks; }

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

    std::pair<PAGEID, PAGEID> GetPageRange(VID node) const {
        uint32_t degree = GetDegree(node);
        uint64_t offset = GetOffset(node);
        uint64_t on_disk_offset = offset * sizeof(VID);
        return std::make_pair(PAGE_NUM(on_disk_offset), PAGE_NUM(on_disk_offset + degree * sizeof(VID)));
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

    char* GetEdgePage(int idx, PAGEID pid) {
        uint32_t *e = _edges[idx];
        return ((char *)e + pid * PAGE_SIZE);
    }

    Bitmap& GetVertexBitmap() {
        return *_vertex_bitmap;
    }

    VidRange& GetP2VMap() {
        return *_p2v_map;
    }

    void BuildGraph(const std::string& input_index_file, std::vector<std::string>& input_edge_files) {
        // Load metadata, index
        LoadGraph(input_index_file);

        InitVertices();

        InitEdges(input_edge_files);

        InitPage2VertexMap();

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
        for (int64_t i = 0; i < _num_nodes; i++) {
            if (GetDegree(i) == 0) _num_empty_nodes++;
        }

        assert(_vertex_bitmap == nullptr);
        _vertex_bitmap = new Bitmap(_num_nodes);
        _vertex_bitmap->reset_parallel();
    }

    void InitPage2VertexMap() {
        size_t num_pages = GetTotalNumPages();

        // Page to vertex map
        assert(_p2v_map == nullptr);
        _p2v_map = new VidRange[num_pages];

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

    void InitEdges(std::vector<std::string>& files) {
        _num_disks = files.size();
        assert(_num_disks);

        _edges = new uint32_t * [_num_disks];

        int fd, ret;
        uint64_t fsiz;
        ssize_t rret;
        for (int i = 0; i < _num_disks; i++) {
            fd = open(files[i].c_str(), O_RDONLY | O_DIRECT);
            fsiz = file_size(files[i]);
            ret = posix_memalign((void**)(&_edges[i]), PAGE_SIZE, fsiz);
            assert(ret == 0);
            assert(_edges[i] != nullptr);
            rret = big_read(fd, (char *)_edges[i], fsiz);
            assert(rret == fsiz);
        }

        for (auto fil : files) {
            _input_edge_files.push_back(fil);
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
    VID                         _num_nodes;
    VID                         _num_empty_nodes;
    uint64_t                    _num_edges;
    uint64_t*                   _index_offsets;
    uint32_t*                   _index_degrees;
    uint32_t**                  _edges;
    Bitmap*                     _vertex_bitmap;
    VidRange*                   _p2v_map;
};

} // namespace agile

#endif // AGILE_GRAPH_H
