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
#include "llvm/Support/raw_ostream.h"

#include <vector>
#include <map>
#include <set>
#include <queue>

using namespace llvm;
using namespace std;
/** The input to this pass is a function in SSA form. 
  We want to obtain a program that essentially tries all
  possible paths from the original program, and report the possible
  value ranges for all variables at the end of the function.

  The transformation conceptually has two parts. If we have

  B
  /\
  /  \
  /    \
  T1    T2
  |      |
  |      |
  F1    F2 
  \    /
  \  /
  M

  then it's possible that some variables are modified in either left
  or right branch. So, we execute both branches. Then, the M node will
  what phis for all variables which are modified either on right or on
  the left. We convert those phi nodes into value merges.

  To simplify our work, we use the notion of 'interval'. Interval is 
  a maximal single-entry subgraph, suchs that every cycle in that subgraph
  goes through the entry node. Every control flow graph can be partioned
  into a set of intervals. Those intervals are called "first-order" intervals.

  If we form a new graph, with first order intervals as vertices, we can 
  find "second-order" intervals. The process can be repeated. If we finally
  end with a graph with a single vertex, the original graph is called 
  reducible.

  We process the function starting from the first-order intervals.
  For each interval we nuke absolutely any branches we find in the
  interval. If 






*/


// For each control flow edge between two intervals in any partition,
// we need additional information. This is it.
struct control_transfer_data
{
    // Source basic block. Typically, control_transfer_data is
    // assigned to Intervals, but sometimes we want to know
    // source basic blocks.
    BasicBlock* source;
    // Target of edge.
    Interval* target;
    // Condition for passing of the edge.
    Value* condition;
    // Anded condition for passing of the edge and
    // condition for execution of the containing interval.
    Value* combined_condition;
};


namespace {
    class LinearizePass: public FunctionPass {
    private:
        Function * mergeFunction;
        Function * mergeOutFunction;
        Function * currentFunction;

        const IntervalPartition* currentPartition;

        bool findScc(const Interval &, set<BasicBlock *> &);
        void processPartition(const IntervalPartition &, Function &);
        void processNonLoopingInterval(const Interval &, Function &);
        void processLoopingInterval(const Interval &, set<BasicBlock *> *, Function &);
        void processLowerInterval(const BasicBlock &, const Interval &, bool mergeBypassed = true);
        void createGates(const Interval &, Function &,
                set<BasicBlock *> * scc = 0 );

        map<const Interval *, bool> looping;

        map<const Interval *, vector<Value *> > executionCondition;
        map<const Interval *, Value *> gate;
        map<const Interval *, BasicBlock *> gateBlock;
        map<const Interval *, BasicBlock *> nextGateBlock;
    public:
        static char ID;
        LinearizePass(): FunctionPass(ID) {}
        bool runOnFunction(Function & f);
        virtual void print(ostream &, const Module * = 0) const {}
    };

    char LinearizePass::ID =  0;
    INITIALIZE_PASS(LinearizePass,
            "linearize",
            "Remote all conditionals",
            true, true);
};

bool LinearizePass::runOnFunction(Function & f) {
    outs() << "Processing function " << f.getName() << "\n";

    currentFunction = &f;
    Module * parent = f.getParent();
    LLVMContext & context = f.getContext();

    vector<const Type*> params;
    params.push_back(Type::getInt32Ty(context));
    FunctionType* ft = FunctionType::get(
            Type::getInt32Ty(context),
            params,
            true);

    /* Merging via Twine request */
    parent->getOrInsertFunction ("merge_values", ft);
    mergeFunction = parent->getFunction("merge_values");

    parent->getOrInsertFunction(
            "merge_out_value",
            Type::getVoidTy(context),
            PointerType::getUnqual(Type::getInt32Ty(context)),
            Type::getInt32Ty(context),
            Type::getInt1Ty(context),
            0);
    mergeOutFunction = parent->getFunction("merge_out_value");

    IntervalPartition& intervals = getAnalysis<IntervalPartition>();
    intervals.print(outs());

    /* Creating partitions while reducible */
    bool reducible = true;
    vector<IntervalPartition *> tmp;
    tmp.push_back(&intervals);
    for (IntervalPartition* current = &intervals;
            ! current->isDegeneratePartition();) {
        IntervalPartition* next = new IntervalPartition(*current, false);
        tmp.push_back(next);
        if (next->getIntervals().size() == current->getIntervals().size()) {
            reducible = false;
            break;
        } else {
            current = next;
        }
    }

    /* Processing each partition in order */
    for(unsigned i = 0, e = tmp.size(); i < e; i++) {
        outs() << i + 1 << "-order partition\n"; 
        processPartition(*tmp[i], f);
    }
    
    assert(reducible);

    return true;
}

bool LinearizePass::findScc(const Interval& I, set<BasicBlock *>& scc) { 
    BasicBlock * header = I.getHeaderNode();

    queue<BasicBlock *> worklist;
    scc.insert(header);
    worklist.push(header);
    bool hasBackEdges = false;

    while(! worklist.empty()) {
        Interval * next = new Interval(worklist.front());
        worklist.pop();
        for (Interval::pred_iterator p = pred_begin(next),
                e = pred_end(next); p != e; p++) {
            if (*p == header)
                hasBackEdges = true;

            if (I.contains(*p)) {
                if (scc.count(*p) == 0) {
                    worklist.push(*p);
                    scc.insert(*p);
                }
            }

        }
        delete next;
    }

    return hasBackEdges;
}

/* TODO: incomplete function */
void LinearizePass::processLoopingInterval(const Interval & current,
        set<BasicBlock *> * scc, Function & f) {
    BasicBlock * header = current.getHeaderNode();

    looping[& current] = true;

    executionCondition.clear();
    gate.clear();
    gateBlock.clear();
    nextGateBlock.clear();

    createGates(current, f, scc);

    for (int i = 0; i < current.Nodes.size(); i++) { 
        processLowerInterval(*(current.Nodes[i]), current);
    }
}

/* TODO: incomplete function */
void LinearizePass::processNonLoopingInterval(const Interval & current,
        Function & f) {

}

/* TODO: incomplete function */
void LinearizePass::createGates(const Interval& current, Function& f,
        set<BasicBlock *> * scc) {

}

/* TODO: incomplete function */
void LinearizePass::processLowerInterval(const BasicBlock & lower, const Interval & current, bool mergeBypassed) {

}

void LinearizePass::processPartition(const IntervalPartition& p,
        Function& f) {
    outs() << "Partition has " << p.getIntervals().size() << "intervals\n";

    currentPartition = &p;

    const vector<Interval *>& intervals = p.getIntervals();
    for(int i = 0, e = intervals.size(); i < e; i++) {
        Interval* current = intervals[i];
        outs() << "Processing interval:\n";
        current->print(outs());

        set<BasicBlock *> scc;
        bool has_scc = findScc(*current, scc);

        if (has_scc)
            outs() << "SCC found\n";
        else
            outs() << "No SCCs\n";

        if (has_scc)
            processLoopingInterval(*current, &scc, f);
        else
            processNonLoopingInterval(*current, f);
    }

}
