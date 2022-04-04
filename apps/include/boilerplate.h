#ifndef AGILE_TEST_BOILER_PLATE_H
#define AGILE_TEST_BOILER_PLATE_H

#include "llvm/Support/CommandLine.h"
#include "Bin.h"

extern llvm::cl::opt<int> numComputeThreads;
extern llvm::cl::opt<unsigned int> ioBufferSize;
extern llvm::cl::opt<std::string> outIndexFilename;
extern llvm::cl::list<std::string> outAdjFilenames;
extern int numIoThreads;

namespace blaze {

template <typename T = uint32_t>
struct EDGEMAP_F {
    Bins* bins;

    EDGEMAP_F(): bins(nullptr) {}

    EDGEMAP_F(Bins *b): bins(b) {}

    inline bool update(VID src, VID dst) {
        return false;
    }

    inline bool updateAtomic(VID src, VID dst) {
        return false;
    }

    inline bool cond (VID dst) {
        return true;
    }

    inline T scatter(VID src, VID dst) {
        return 0.0;
    }

    inline bool gather(VID dst, T val) {
        return true;
    }

    inline Bins* get_bins() const {
        return bins;
    }

    typedef T value_type;
};

void AgileStart(int argc, char** argv);

} // namespace blaze

#endif // AGILE_TEST_BOILER_PLATE_H
