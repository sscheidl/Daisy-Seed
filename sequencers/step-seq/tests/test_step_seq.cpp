// Phase 0 placeholder: checks that the version source parses; real tests follow
// with the core in its own phase (docs/acid-seq/ARBEITSAUFTRAG.md, section 5).
#include <cstdio>
#include <cstring>

#include "../src/version.h"

int main()
{
    if(std::strlen(STEP_SEQ_VERSION) == 0 || std::strlen(STEP_SEQ_FIRMWARE_VERSION) == 0)
    {
        std::printf("FAIL: empty version\n");
        return 1;
    }
    std::printf("PASS: step-seq scaffold (version %s)\n", STEP_SEQ_VERSION);
    return 0;
}
