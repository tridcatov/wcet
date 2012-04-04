#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Support/CFG.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Analysis/IntervalPartition.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/Debug.h"

#include "misc/logger.h"

#include <vector>
#include <map>
#include <set>
#include <queue>

using namespace llvm;
using namespace std;

// Defining additional information structure required to 
// hold data of each edge in cfg

struct ControlTransferData {
    BasicBlock* source;
    Interval* target;
    Value* condition;
    Value* combinedCondition;
};

namespace {

class LinearizePass : public FunctionPass {
private:
    Logger * logger;

    Function * currentFunction;
    Function * mergeFunction;
    Function * mergeOutFunction;

    IntervalPartition * currentPartition;

    bool findScc(const Interval &, set<BasicBlock *> &);

    void processPartition(IntervalPartition &, Function &);
public:
    static char ID;
    
    LinearizePass() : FunctionPass(ID) {
        logger = new ErrLogger("Linearize");
    }

    ~LinearizePass() {
        delete logger;
    }

    void getAnalysisUsage(AnalysisUsage & info) const;

    bool runOnFunction(Function & f);
};

}

char LinearizePass::ID = 0;
INITIALIZE_PASS(LinearizePass,
        "linearize",
        "Remove all conditionals",
        true, false);

void LinearizePass::getAnalysisUsage(AnalysisUsage & info) const {
    info.setPreservesCFG();
    info.addRequired<IntervalPartition>();
}

bool LinearizePass::runOnFunction(Function & f) {
    logger->log() << "Processing function " << f.getName() << "\n";

    currentFunction = &f;
    Module * parent = f.getParent();
    LLVMContext & context = f.getContext();

    vector<const Type*> params;
    params.push_back(Type::getInt32Ty(context));
    FunctionType* ft = FunctionType::get(
            Type::getInt32Ty(context),
            params,
            true);
    parent->getOrInsertFunction ("merge_values", ft);
    mergeFunction = parent->getFunction("merge_values");

    params.clear();
    params.push_back(PointerType::getUnqual(Type::getInt32Ty(context)));
    params.push_back(Type::getInt32Ty(context));
    params.push_back(Type::getInt1Ty(context));
    ft = FunctionType::get(
            Type::getVoidTy(context),
            params,
            true
            );
    parent->getOrInsertFunction("merge_out_value", ft);
    mergeOutFunction = parent->getFunction("merge_out_value");

    IntervalPartition& intervals = getAnalysis<IntervalPartition>();
    intervals.print(logger->log());

    bool reducible = true;
    vector<IntervalPartition *> partitions;
    partitions.push_back(&intervals);
    for (IntervalPartition* current = &intervals;
            ! current->isDegeneratePartition();) {
        IntervalPartition* next = new IntervalPartition(*current, false);
        partitions.push_back(next);
        if (next->getIntervals().size() == current->getIntervals().size()) {
            reducible = false;
            break;
        } else {
            current = next;
        }
    }

    for(unsigned i = 0, e = partitions.size(); i < e; i++) {
        logger->log() << i + 1 << "-order partition\n"; 
        processPartition(*partitions[i], f);
    }

    assert(reducible && "CFG is not reducible");

    return true;
}

void LinearizePass::processPartition(IntervalPartition & ip, Function & f) {
    logger->log(1) << "Partition has " << ip.getIntervals().size() << " intervals\n";

    currentPartition = &ip;
    const vector<Interval *> & intervals = ip.getIntervals();
    for (int i = 0, e = intervals.size(); i < e; i++) {
        Interval * currentInterval = intervals[i];
        logger->log(2) << "Processing interval:\n";
        currentInterval->print(logger->log(2));

        set<BasicBlock *> scc;
        bool hasScc = findScc(*currentInterval, scc);

        if ( hasScc )
            logger->log(2) << "SCC found\n";
        else
            logger->log(2) << "No SCCs\n";
    }
}

bool LinearizePass::findScc(const Interval & interval, set<BasicBlock *> & scc) {
    BasicBlock * header = interval.getHeaderNode();
    
    queue<BasicBlock *> unprocessedBlocks;
    scc.insert(header);
    unprocessedBlocks.push(header);

    while(! unprocessedBlocks.empty()) {
        BasicBlock * next = unprocessedBlocks.front();
        unprocessedBlocks.pop();
         
    }

    return interval.isLoop();
}
