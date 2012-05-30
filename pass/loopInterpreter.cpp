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
#include "llvm/Support/InstVisitor.h"
#include "llvm/Support/Debug.h"

#include "misc/logger.h"

#include <vector>
#include <stack>
#include <map>

using namespace std;
using namespace llvm;

typedef std::stack<Value *> DominanceTree;
typedef std::map<Value *, int> ValueMap;

namespace {

class LoopInterpreter : public LoopPass, public InstVisitor<LoopInterpreter> {
private:
    Logger * logger;

public:
    static char ID;
    DominanceTree currentDominanceTree;
    ValueMap currentMinimums;
    ValueMap currentMaximums;

    LoopInterpreter() : LoopPass(ID) {
        logger = new OutLogger("Loop interpreter");
    }

    ~LoopInterpreter() {
        delete logger;
    }

    using InstVisitor<LoopInterpreter>::visit;

    void visitLoadInst(LoadInst & load);
    void visitStoreInst(StoreInst & store);

    void visitAdd(BinaryOperator & op);
    void visitSub(BinaryOperator & op);
    
    void visitMul(BinaryOperator & op);
    void visitMod(BinaryOperator & op);
    void visitDiv(BinaryOperator & op);

    void visitXor(BinaryOperator & op);
    void visitOr(BinaryOperator & op);

    bool runOnLoop(Loop * loop, LPPassManager & manager);
    DominanceTree getDominanceTree(Loop * loop);
};

char LoopInterpreter::ID = 0;
INITIALIZE_PASS(LoopInterpreter,
        "loopInterpreter",
        "Multipass loop bound interpreter",
        false, false);
}

bool LoopInterpreter::runOnLoop(Loop * loop, LPPassManager & manager) {
    currentDominanceTree = getDominanceTree(loop);
    if (currentDominanceTree.empty()) {
        logger->log(1) << "Postponed analysis of loop:\n";
        loop->dump();
        return false;
    }

    return false;
}

DominanceTree LoopInterpreter::getDominanceTree(Loop * loop) {
    DominanceTree result;
    return result;
}
