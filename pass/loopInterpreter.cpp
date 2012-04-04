#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Support/CFG.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopDependenceAnalysis.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/Debug.h"

#include "misc/logger.h"

#include <vector>
#include <map>

using namespace std;
using namespace llvm;

namespace {

class LoopInterpreter : public LoopPass {
private:
    Logger * logger;

public:
    static char ID;

    LoopInterpreter() : LoopPass(ID) {
        logger = new OutLogger("Loop interpreter");
    }

    ~LoopInterpreter() {
        delete logger;
    }

    bool runOnLoop(Loop * loop, LPPassManager & manager);
};

char LoopInterpreter::ID = 0;
INITIALIZE_PASS(LoopInterpreter,
        "loopInterpreter",
        "Multipass loop bound interpreter",
        false, false);


}

bool LoopInterpreter::runOnLoop(Loop * loop, LPPassManager & manager) {
    loop->dump();
}
