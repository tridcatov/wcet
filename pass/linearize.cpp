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

        map<const BasicBlock *, vector<control_transfer_data> > controlTransfers;
        map<const BasicBlock *, BasicBlock *> firstBasicBlock;
        map<const BasicBlock *, BasicBlock *> lastBasicBlock;

        IntervalPartition* currentPartition;

        bool findScc(const Interval &, set<BasicBlock *> &);
        void findBbs(const BasicBlock *, set<BasicBlock *> & result);
        void findBbsV(const vector<BasicBlock *> &, set<BasicBlock *> &);
        void findLiveVars(const BasicBlock *, set<Value *> &);
        void findLiveVarsV(const vector<BasicBlock *> &, set<Value *> &);

        void processPartition(IntervalPartition &, Function &);
        void processNonLoopingInterval(const Interval &, Function &);
        void processLoopingInterval(const Interval &, set<BasicBlock *> *, Function &);
        void processLowerInterval(const BasicBlock &, const Interval &, bool mergeBypassed = true);
        void createGates(const Interval &, Function &,
                set<BasicBlock *> * scc = 0 );

        Value * foldr(const vector<Value *>&, Instruction::BinaryOps, BasicBlock *);

        void replaceSomeUsers(Value * form, Value * to,
                const set<BasicBlock *> &, const set<Value *> &);

        void convertPHINodes(const Interval & current);

        map<const Interval *, set<BasicBlock *> > extraBbs;
        map<const Interval *, bool> looping;

        map<const BasicBlock *, vector<Value *> > executionCondition;
        map<const BasicBlock *, Value *> gate;
        map<const BasicBlock *, BasicBlock *> gateBlock;
        map<const BasicBlock *, BasicBlock *> nextGateBlock;
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

Value * LinearizePass::foldr(const vector<Value *> & values, Instruction::BinaryOps op, BasicBlock * parent) {
    Value * result = values[0];
    for (int i = 1; i < values.size(); i++) {
        result = BinaryOperator::Create(op, result, values[i], "", parent);
    }
    return result;
}

/* TODO: incomplete function */
void LinearizePass::convertPHINodes(const Interval & current) {

}

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

    /* Compute some information about scc */
    /* WARNING: this patch is meant to be used with BasicBlocks
     * although in old version it works with Intervals
     * TODO: Check semantics
     */ 

    BasicBlock * lastInScc = 0;
    BasicBlock * firstOutsideScc = 0;
    vector<BasicBlock *> blocksInScc;
    vector<BasicBlock *> blocksOutsideScc;

    for (int i = 0; i < current.Nodes.size(); i++) {
        if (scc->count(current.Nodes[i])) {
            lastInScc = current.Nodes[i];
            blocksInScc.push_back(current.Nodes[i]);
        } else {
            if (! firstOutsideScc) 
                firstOutsideScc = current.Nodes[i];
            blocksOutsideScc.push_back(current.Nodes[i]);
        }
    }

    /* Resolving basic block, that causes loop iteration */

    /* Merging bypassed values from loop body 
     * Resolving al PHI's from next gate to loop
     */

    LLVMContext & currentContext = f.getContext();

    BasicBlock* firstGateOutside = gateBlock[firstOutsideScc];
    
    BasicBlock * mergeValuesBypassedInLastInterval = BasicBlock::Create(
            currentContext, "mergeValuesBypassedInLastInterval",
            &f, firstGateOutside);
    extraBbs[&current].insert(mergeValuesBypassedInLastInterval);

    BasicBlock * checkIfJumpOutIsPossible = BasicBlock::Create(
            currentContext, "checkIfJumpOutIsPossible",
            &f, firstGateOutside);
    extraBbs[&current].insert(checkIfJumpOutIsPossible);

    BasicBlock * mergeOutValues = BasicBlock::Create(
            currentContext, "mergeOutValues",
            &f, firstGateOutside);
    extraBbs[&current].insert(mergeOutValues);

    BasicBlock * branchBack = BasicBlock::Create(
            currentContext, "branchBack", &f, firstGateOutside);
    extraBbs[&current].insert(branchBack);

    BasicBlock * loadOutValues = BasicBlock::Create(
            currentContext, "loadOutValues", &f, firstGateOutside);
    extraBbs[&current].insert(loadOutValues);

    BranchInst::Create(branchBack, mergeOutValues);
    BranchInst::Create(checkIfJumpOutIsPossible,
             mergeValuesBypassedInLastInterval); 

    if (firstOutsideScc)
        BranchInst::Create(gateBlock[firstOutsideScc], loadOutValues);
    else
        BranchInst::Create(lastBasicBlock[current.getHeaderNode()], loadOutValues);

    /* Replacing jumps in last gate and interval
     * to mergeValuesBypasseedInLastInterval
     */
    gateBlock[lastInScc]->getTerminator()->replaceUsesOfWith(
            firstGateOutside, mergeValuesBypassedInLastInterval);
    lastBasicBlock[lastInScc]->getTerminator()->replaceUsesOfWith(
            firstGateOutside, mergeValuesBypassedInLastInterval);

    while(true) {
        if (! firstGateOutside) break;

        PHINode * phi = dyn_cast<PHINode>(&(firstGateOutside->front()));
        if (! phi) break;

        phi->moveBefore(mergeValuesBypassedInLastInterval->getTerminator());

    }

    set<BasicBlock *> bbsInScc;
    findBbsV(blocksInScc, bbsInScc);

    /* Fix PHI nosed at entry block. We have to dereference
     * values from the loop to the ''branchBack
     */

    {
        BasicBlock * first = firstBasicBlock[current.getHeaderNode()];
        BasicBlock::iterator i = first->begin(), e=first->end();

        for(;i != e; i++) {
            PHINode * phi = dyn_cast<PHINode>(i);
            if (! phi) break;
            int jumpsFromInside = 0;
            for(int j = 0; j < phi->getNumIncomingValues(); j++) {
                if (bbsInScc.count(phi->getIncomingBlock(j)))
                   jumpsFromInside; 
            }

            if (jumpsFromInside == 1) {
                for (int j = 0; j < phi->getNumIncomingValues(); j++) {
                    if (bbsInScc.count(phi->getIncomingBlock(j)))
                        phi->setIncomingBlock(j, branchBack);
                }
            } else if (jumpsFromInside > 1) {
                /* Processing merge values for loop branch block
                 */

                PHINode * mergeFromLoop = PHINode::Create(
                       phi->getType(), "", branchBack);

                for (int j = 0; j < phi->getNumIncomingValues();) {
                    if (bbsInScc.count(phi->getIncomingBlock(j))) {
                        mergeFromLoop->addIncoming(phi->getIncomingValue(j),
                                phi->getIncomingBlock(j));

                        /* Removal will shift index */
                        phi->removeIncomingValue(j);
                    } else {
                        j++;
                    }
                }
                phi->addIncoming(mergeFromLoop, branchBack);
            }

            /* Locating loop iterator. ADd branch back to header subject
             * upon that condition
             */

            /* Create the back branch to loop header */
            {
                BasicBlock * header = current.getHeaderNode();
                assert(executionCondition.count(header));
                vector<Value *> ex = executionCondition[header];
                Value * finalCondition = ex[0];

                for(int i = 1; i < ex.size(); i++) {
                    finalCondition = BinaryOperator::Create(
                            BinaryOperator::Or, finalCondition,
                            ex[i], "", branchBack);
                }
                gate[header] = finalCondition;

                BranchInst::Create(firstBasicBlock[header],
                        loadOutValues, finalCondition, branchBack);
            }

            /* First expression deciding to jump out of the loop */
            {
                vector<Value *> jumpsOutOfLoopCondition;
                for (vector<BasicBlock *>::iterator b = blocksInScc.begin(),
                        e = blocksInScc.end(); b != e; b++) {
                    for (vector<control_transfer_data>::iterator ct = controlTransfers[*b].begin(),
                            ctb = controlTransfers[*b].end(); ct != ctb; ct++) {
                        if (bbsInScc.count((*ct).target->getHeaderNode()) == 0) {
                            outs() << "Jump out of loop on "
                                << ((*ct).condition) << "\n";
                            assert((*ct).combined_condition);
                            jumpsOutOfLoopCondition.push_back((*ct).combined_condition);
                        
                        }
                    }
                }

                if (! jumpsOutOfLoopCondition.empty()) {
                    Value * possiblyJumpingOut = foldr(jumpsOutOfLoopCondition,
                            BinaryOperator::Or, checkIfJumpOutIsPossible);
                    BranchInst::Create(mergeOutValues, branchBack,
                            possiblyJumpingOut, checkIfJumpOutIsPossible);
                } else {
                    BranchInst::Create(branchBack, checkIfJumpOutIsPossible);
                }
            }

            /* Merging values range for all iterations we may jump out of */

            {
                /* Adding 'firstIteration' variable at the header block
                 * to find out if we need to merge new values */

                PHINode * firstIteration = PHINode::Create(
                        Type::getInt1Ty(currentContext),
                        "firstIteration", firstBasicBlock[header]->begin());
                
                firstIteration->addIncoming(ConstantInt::getTrue(currentContext),
                        branchBack);
                firstIteration->addIncoming(ConstantInt::getFalse(currentContext), 
                        branchBack);

                set<Value *> liveAtLoopExit;
                findLiveVarsV(blocksInScc, liveAtLoopExit);

                set<Value *>::iterator i = liveAtLoopExit.begin(),
                    e = liveAtLoopExit.end();

                for (; i != e; i++) {
                    /* Create gloval allocation to avoid phi messing */
                    Instruction * allocated = new AllocaInst((*i)->getType(), 0,
                            (*i)->getName() + "_alloca", f.begin()->getTerminator());

                    /* Load merged values */
                    Instruction * load = new LoadInst(allocated, (*i)->getName() + "_reload",
                            loadOutValues->getTerminator());

                    /* Replace all uses of source variable with 
                     * reloaded variable */
                    set<BasicBlock *> bbs;
                    findBbsV(blocksInScc, bbs);
                    bbs.insert(extraBbs[&current].begin(), extraBbs[&current].end());

                    set<Value *> dummy;
                    replaceSomeUsers(*i, load, bbs, dummy);

                    /* Compatibility cleanup */
                    if (load->getNumUses() == 0) {
                        load->eraseFromParent();
                        allocated->eraseFromParent(); 
                    } else {
                        /* Adding call to merge with out value */
                        if ((*i)->getType() == Type::getInt32Ty(currentContext)) {
                            vector<Value *> params;
                            params.push_back(allocated);
                            params.push_back(*i);
                            params.push_back(firstIteration);

                            Instruction * inst = dyn_cast<Instruction>(*i);
                            assert(inst);

                            CallInst::Create<vector<Value *>::iterator>(mergeOutFunction,
                                    params.begin(), params.end(), "",
                                    mergeOutValues->getTerminator());
                        } else {
                            /* noninterger uses are terminated and undefined */
                            load->replaceAllUsesWith(UndefValue::get((*i)->getType()));
                            load->eraseFromParent();
                            allocated->eraseFromParent();
                        }
                    }
                }
            }
        }
    }
    convertPHINodes(current);

    /* Seems weird? Compatibility issue! */
    firstBasicBlock[current.getHeaderNode()] = current.getHeaderNode();

}

void LinearizePass::processNonLoopingInterval(const Interval & current,
        Function & f) {
    executionCondition.clear();
    gate.clear();
    gateBlock.clear();
    nextGateBlock.clear();

    createGates(current, f);

    /* 1. Add real gate condition and phi nodes for all intervals */
    for (int i = 0; i < current.Nodes.size(); i++) {
        processLowerInterval(*current.Nodes[i], current);
    }

    /* 2. Convert PHI nodes to calls to merge function */
    convertPHINodes(current);

    /* 3. Set first basic block */
    /* Seems strange? Compatibility issue! */
    firstBasicBlock[current.getHeaderNode()] = current.getHeaderNode(); 
}

void LinearizePass::replaceSomeUsers(Value * from, Value * to,
        const set<BasicBlock *> & restrictedBlocks,
        const set<Value *> & restrictedUsers) {
    vector<User *> users(from->use_begin(), from->use_end());
    for(int i = 0; i < users.size(); i++) {
        Instruction * user = cast<Instruction>(users[i]);
        outs() << "user " << *user << "\n";

        if (restrictedUsers.count(user))
            outs() << "user " << *user << " is inside restricred users\n";
        if (restrictedUsers.count(user->getParent()))
            outs() << "user " << *user << " is inside restricted block\n";

        if (restrictedUsers.count(user) == 0 &&
                restrictedBlocks.count(user->getParent()) == 0)
            user->replaceUsesOfWith(from, to);
    }

}

void LinearizePass::createGates(const Interval& current, Function& f,
        set<BasicBlock *> * scc) {
    /* Create basic block that will enclose interval */
    BasicBlock * intervalEnd;
    {
        BasicBlock * last = lastBasicBlock[current.Nodes.back()];
        intervalEnd = BasicBlock::Create(f.getContext(), "intervalFinish", &f);

        /* Removing intervals with return and 
         * adding jump to them */
        bool seenReturn = false;
        for(int i = 0; i < current.Nodes.size(); i++) {
            BasicBlock * last = lastBasicBlock[current.Nodes[i]];
            Instruction * term = last->getTerminator();
            if (term && isa<ReturnInst>(term)) {
                /* Single exit basic block for all function */
                assert(! seenReturn);
                seenReturn = true;
                term->removeFromParent();
                intervalEnd->getInstList().push_back(term);
                assert(term->getParent() == intervalEnd);
            }
        }
    }
    vector<BasicBlock *> nodes;
    if (scc) {
        /* Processing intervals in scc */
        assert(scc->count(current.Nodes[0]));
        for(int i = 0; i < current.Nodes.size(); i++) {
            if (scc->count(current.Nodes[i]) != 0)
                nodes.push_back(current.Nodes[i]);
        }
        for(int i = 0; i < current.Nodes.size(); i++) {
            if (scc->count(current.Nodes[i]) == 0)
                nodes.push_back(current.Nodes[i]);
        }
    } else {
        nodes.assign(current.Nodes.begin(), current.Nodes.end());
    }

    /* Creating gate blocks for intervals other than first */
    /* Seems strange to build gates for BasicBlocks
     * TODO: check semantics */

    for(int i = 1; i < nodes.size(); i++) {
        BasicBlock * pseudo = nodes[i];
        BasicBlock * gb = BasicBlock::Create(f.getContext(), "gate",
                &f, firstBasicBlock[pseudo]);
        extraBbs[&current].insert(gb);
        gateBlock[pseudo] = gb;
        nextGateBlock[nodes[i-1]] = gb;
    }

    nextGateBlock[nodes.back()] = intervalEnd;
    lastBasicBlock[current.getHeaderNode()] = intervalEnd;
}

void LinearizePass::processLowerInterval(const BasicBlock & lower, const Interval & current, bool mergeBypassed) {
    outs() << "Processing interval " << & lower << " " << lower;

    /* 1. Create gate Branch
     *
     * Merging them together with 'or' and branch
     * them to 'lower' from gate block */

    if (&lower != current.Nodes.front()) {
        assert(executionCondition.count(& lower));

        Value * finalCondition = foldr(executionCondition[&lower],
                BinaryOperator::Or, gateBlock[&lower]);

        gate[&lower] = finalCondition;
        BranchInst::Create(firstBasicBlock[&lower], nextGateBlock[&lower],
                finalCondition, gateBlock[&lower]);
    }

    /* 2. Propagate Execution condition
     * We have a list of control transfers associated with this interval, and
     * each of them has a condition -- basically an instruction that
     * computes bool value that tells if that control transfer is possible.
     * After gate block is created, we have something like:
    
     * gate:
     *      br %something, label %int1, label %gate2
     * int1:
     *      %condition = ...
     * gate2:
     *
     * The control transfer can be done if both the condition is true,
     * and current interval is executed. So, we add
     
     *  %cond_merged = phi bool [ %condition, label %int1 ] ,
     *                          [ false, label %gate2 ]
     *
     * as use 'cond_merge' as final execution condition
    
     */

    set<Value *> accountedPhis;

    vector<control_transfer_data> & cts = controlTransfers[&lower];
    for (int i = 0, e = cts.size(); i != e; i++) {
        control_transfer_data & ct = cts[i];

        Value * propagatedCondition;
        LLVMContext & context = current.getHeaderNode()->getParent()->getContext();

        if (& lower != current.getHeaderNode()) {
            BasicBlock * last = lastBasicBlock[&lower];
            PHINode * phi = PHINode::Create(Type::getInt1Ty(context));
            nextGateBlock[&lower]->getInstList().push_front(phi);
            phi->addIncoming(ConstantInt::getFalse(context), gateBlock[&lower]);
            phi->addIncoming(ct.condition, last);
            propagatedCondition = phi;
            accountedPhis.insert(phi);

            ct.combined_condition = phi;
        } else {
            propagatedCondition = ct.condition;
            ct.combined_condition = ct.condition;
        }

        BasicBlock * targetHeader = ct.target->getHeaderNode();

        if (current.contains(targetHeader)) {
            /* Local control transfer */
            executionCondition[targetHeader].push_back(propagatedCondition);
        } else {
            control_transfer_data ct2 = ct;
            ct2.target = currentPartition->getBlockInterval(targetHeader);
            controlTransfers[current.getHeaderNode()].push_back(ct2);

            outs() << "Extrernal control transfer\n";
        }
    }

    /* 3. Add PHI nodes for all modified values 
     * Refreshing consistency of SSA properties for 
     * merged values that can bypass previous definitions.
     * Creating PHI nodes to merge them */
    // FIXME: I forgot what this code does!
    // TODO: Make out what this code does.

    if (&lower != current.getHeaderNode() && mergeBypassed) {
        set<BasicBlock *> bbs;
        findBbs(&lower, bbs);

        set<Value *> defs;
        findLiveVars(&lower, defs);

        LLVMContext & context = current.getHeaderNode()->getParent()->getContext();

        for (set<Value *>::iterator i = defs.begin(); i != defs.end(); i++){
            if ((*i)->getType() == Type::getVoidTy(context))
                continue;
            outs() << "Bypassed value found\n";

            PHINode * phi = PHINode::Create((*i)->getType(), "mergeBypassed");
            nextGateBlock[&lower]->getInstList().push_front(phi);
            // PHI instructons in 'accounted_phi' compute if a certian 
            // control transfer should be taken, and shoul not be modified.
            // Consider that %x is a jump condition in basic block 0
            //
            // Then, the gate block for a target of branch which contain:
            //   %x_here = phi [ false, %gate0 ], [ %x, %bb0 ]
            // encoding the information that if bb0 is not executed, then
            // this jump cannot happen.
            // The current loop will also add
            //   %x_merged = phi [ undef, %gate0], [%x, %bb0 ]
            // and updated all former users of 'x'. But we should not update
            // the previously created '%x_here', because that would mean
            // x_here can can any value if 'bb0' is not executed, which is
            // not accurate.
            replaceSomeUsers(*i, phi, bbs, accountedPhis);

            assert(gateBlock[&lower]);
            BasicBlock * last = lastBasicBlock[&lower];
            phi->addIncoming(UndefValue::get((*i)->getType()), gateBlock[& lower]);
            phi->addIncoming(*i, last);

        }
    }

    // 4. Just to the next gate block
    BranchInst::Create(nextGateBlock[&lower], lastBasicBlock[&lower]);
}

/* TODO: incomplete function */
void LinearizePass::findBbs(const BasicBlock * b,
        set<BasicBlock *> & result) {

}

/* TODO: incomplete function */
void LinearizePass::findLiveVars(const BasicBlock * b,
        set<Value *> & result) {

}

/* TODO: incomplete function */
void LinearizePass::findBbsV(const vector<BasicBlock *>& b,
        set<BasicBlock *> & result) {

}

/* TODO: incomplete function */
void LinearizePass::findLiveVarsV(const vector<BasicBlock *>& b,
        set<Value *> & result) {

}

void LinearizePass::processPartition(IntervalPartition& p,
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
