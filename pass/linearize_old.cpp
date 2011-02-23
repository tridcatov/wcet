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

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/assign/std/vector.hpp>
using namespace boost;
using namespace boost::assign;

/*
  TODO: phis at the beginning of a looping interval that merge
  values from previous intervals (not loop body).
*/


#include <iostream>

using namespace llvm;
using namespace std;


#if 0
#include <boost/graph/graph_traits.h>

class CFG_Wrapper {};


template<>
graph_traits<CFG_Wrapper>
{
    typedef vertex_descriptor;
    typedef incidence_graph_type traversal_category;
    typedef succ_iterator out_edge_iterator;
    typedef int degree_size_type;
};
#endif

namespace {

    enum colors { white = 0, gray, black };

    void topo_sort(BasicBlock* bb, const set<BasicBlock*>& limit,
                   set<BasicBlock*>& visited,
                   vector<BasicBlock*>& result)
    {
        visited.insert(bb);
        for(succ_iterator i = succ_begin(bb), e = succ_end(bb); i != e; ++i)
        {
            if (limit.count(*i) && visited.count(*i) == 0)
            {
                topo_sort(*i, limit, visited, result);                
            }
        }
        result.push_back(bb);
    }
}




#include <queue>

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
    class LinearizePass : public FunctionPass {        

        void getAnalysisUsage(AnalysisUsage &Info) const;

        map<const Interval*, vector<control_transfer_data> > control_transfers;
        map<const Interval*, BasicBlock*> first_basic_block;        
        map<const Interval*, BasicBlock*> last_basic_block;

        // Construct control transfers data for zero-order intervals.
        void init_control_transfer_data(const IntervalPartition& P);


//      BasicBlock* runOnRange(BasicBlock* start, BasicBlock* end);
        void processPartition(const IntervalPartition& P, Function& F);
        void prcessIntervals(const vector<Interval*> intervals, Function& F);
        void processNonLoopingInterval(const Interval& current, 
                                       Function& F);
        void processLoopingInterval(const Interval& current, 
                                    set<Interval*>* scc,
                                    Function& F);
        void processLowerInverval(const Interval& lower, 
                                  const Interval& current, bool merge_bypassed);
        void createGates(const Interval& current, Function& F,              
                         set<Interval*>* scc = 0);
        bool runOnFunction(Function& F);
        
        void convertPHINodes(const Interval& current);
        
        void find_bbs(const Interval* I, set<BasicBlock*>& result);
        void find_bbs_v(const vector<Interval*>& I, set<BasicBlock*>& result);
        void find_live_vars(const Interval* interval, set<Value*>& live);
        void find_live_vars_v(const vector<Interval*>& I, set<Value*>& live);

        vector<pair<BasicBlock*, BasicBlock*> > back_edges_to_add;

        Function* m_merge_func;
        Function* m_merge_out_func;

        map<const Interval*, bool> looping;

        // Basic blocks we've added to interval.
        map<const Interval*, set<BasicBlock*> > extra_bbs;
        
        Function* current_function_;

    public: // Information local for each processPartition invocation.
        const IntervalPartition* current_partition;

    public: // information local for each processNonLoopingInterval
        // invocation.        

        map<const Interval*, vector<Value*> > execution_condition;

        // The instruction which evaluates execution condition.
        map<const Interval*, Value*> gate;
        // The basic block where gate instruction is placed.
        // The basic block is before the interval.
        map<const Interval*, BasicBlock*> gate_block;
        // The gate block after interval -- most of the time
        // it's a gate block for the next interval.
        map<const Interval*, BasicBlock*> next_gate_block;
    };
    
    RegisterOpt<LinearizePass> X("linearize",
                                "Remote all conditionals");
}

void LinearizePass::getAnalysisUsage(AnalysisUsage &Info) const
{
    Info.setPreservesCFG();
    Info.addRequired<IntervalPartition>();
//  Info.addRequired<DominatorTree>();
//  Info.addRequired<DominanceFrontier>();
}

bool find_scc(const Interval& I, set<Interval*>& scc)
{
    Interval* header = I.getHeaderNode();

    // Since all cycles should pass through the header,
    // we can find all the members of SCC by finding headers's
    // predecessors (including indirect ones) 
    // that are not outside of interval.
    queue<Interval*> worklist;
    scc.insert(header);
    worklist.push(header);
    bool has_back_edges = false;

    for(;!worklist.empty();)
    {
        Interval* next = worklist.front();
        worklist.pop();
        for (Interval::pred_iterator p = pred_begin(next), e = pred_end(next); 
             p != e; ++p)
        {
            if (*p == header)
                has_back_edges = true;
            // Technically, this test is only needed when 'next' == 'header'.
            // Since interval can only be entered through the header,
            // predecessors for all other vertices are in interval.
            if (I.contains(*p))
            {
                if (scc.count(*p) == 0)
                {
                    worklist.push(*p);
                    scc.insert(*p);
                }
            }
        }
    }
    
    return has_back_edges;
}


void find_defs(const Interval* I, set<Value*>& result)
{
    if (I->Nodes.empty())
    {
        BasicBlock* b = I->getHeaderBlock();
        for(BasicBlock::iterator i = b->begin(); i != b->end(); ++i)
            result.insert(i);
    }
    else
    {
        for(unsigned i = 0; i < I->Nodes.size(); ++i)
        {
            find_defs(I->Nodes[i], result);
        }
    }
}
void find_defs_v(const vector<Interval*>& I, set<Value*>& result)
{
    for_each(I.begin(), I.end(), boost::bind(find_defs, _1, ref(result)));
}

void LinearizePass::find_bbs(const Interval* I, set<BasicBlock*>& result)
{
    if (I->Nodes.empty())
    {
        BasicBlock* b = I->getHeaderBlock();
        result.insert(b);
    }
    else
    {
        for(unsigned i = 0; i < I->Nodes.size(); ++i)
        {
            find_bbs(I->Nodes[i], result);
        }
    }
    result.insert(extra_bbs[I].begin(), extra_bbs[I].end());
}

void LinearizePass::find_bbs_v(const vector<Interval*>& I, set<BasicBlock*>& result)
{
    for_each(I.begin(), I.end(), boost::bind(&LinearizePass::find_bbs, this, _1, ref(result)));
}

void LinearizePass::find_live_vars(const Interval* interval, set<Value*>& live)
{
    set<BasicBlock*> bbs;
    find_bbs(interval, bbs);

    set<Value*> defs;
    find_defs(interval, defs);

    for(set<Value*>::iterator i = defs.begin();
        i != defs.end();
        ++i)
    {
        if ((*i)->getType() == Type::VoidTy)
            continue;

        for(Value::use_iterator j = (*i)->use_begin();
            j != (*i)->use_end(); ++j)
        {
            User* u = (*j);            
            Instruction* inst = dyn_cast<Instruction>(u);
            // Is this use outside of this interval.
            if (inst && bbs.count(inst->getParent()) == 0)
            {
                live.insert(*i);
                cout << "Live var: " << **i << "\n";
                cout << "Used by: " << *u << "\n";
                cout << "Interval: " << *i << "\n";
                cout << "User's parent: " << *inst->getParent() << "\n";
            }
        }        
    }    
}

void LinearizePass::find_live_vars_v(const vector<Interval*>& I, set<Value*>& live)
{
    for_each(I.begin(), I.end(), boost::bind(&LinearizePass::find_live_vars, this, _1, ref(live)));
}

/* Replace all users of 'from' which are not amoung 'restricted_users',
   and which are not in basic blocks that are in 'restricted_blocks', with
   'to'.
*/
void replaceSomeUsers(Value* from, Value* to,
                      const set<BasicBlock*>& restricted_blocks,
                      const set<Value*>& restricted_users)
{
    std::vector<User*> users(from->use_begin(), from->use_end());
    for(unsigned i = 0; i < users.size(); ++i)
    {
        Instruction* user = cast<Instruction>(users[i]);
        cout << "user " << *user << "\n";
        
        if (restricted_users.count(user))
        {
            cout << "user " << *user << " is inside restricted users\n";
        }
        if (restricted_users.count(user->getParent()))
        {
            cout << "user " << *user << " is inside restricted block\n";
        }
        
        if (restricted_users.count(user) == 0 &&
            restricted_blocks.count(user->getParent()) == 0)
        {
            user->replaceUsesOfWith(from, to);
        }
    }
}

/** Folds the passed values from right with the specified
    binary operation. Adds new instruction to 'add_to'.
    Results the resulting value.
*/
Value* foldr(const vector<Value*>& values, Instruction::BinaryOps op, 
             BasicBlock* add_to)
{
    Value* result = values[0];
    for(unsigned i = 1; i < values.size(); ++i)
    {
        result = BinaryOperator::create(op,
                                        result, values[i], "",
                                        add_to);
    }
    return result;
}

Value* convertToAlloca(Value* v, Function* f)
{
    AllocaInst* result = new AllocaInst(v->getType(), 0, "local", f->front().getFirstNonPHI());
            
            // All uses must be replaced with loads. Do this before adding initialization,
            // to avoid replacing initializations too.
    std::vector<User*> uses(v->use_begin(), v->use_end());
            
    for(unsigned i = 0; i < uses.size(); ++i)
    {
        Instruction* user = dyn_cast<Instruction>(uses[i]);
        assert(user);
                
        cout << "Replacing in " << *user << "\n";
                
        if (PHINode* n = dyn_cast<PHINode>(user))
        {
                    // Should add load into predecessor block.
            BasicBlock* predecessor;
            for(unsigned i = 0; i < n->getNumIncomingValues(); ++i)
            {
                if (n->getIncomingValue(i) == v)
                {
                    predecessor = n->getIncomingBlock(i);
                }
            }
            
            Value* loaded = new LoadInst(result, "", &predecessor->back());
            user->replaceUsesOfWith(v, loaded);
        }
        else
        {
            Value* loaded = new LoadInst(result, "", user);
            user->replaceUsesOfWith(v, loaded);
        }
    }
            
    if (Instruction* inst = dyn_cast<Instruction>(v))
    {
        BasicBlock* bb = inst->getParent();
        if (inst == &bb->back())
            new StoreInst(inst, result, bb);
        else
            new StoreInst(inst, result, inst->getNext());
    }
    else
    {
                // Must be a global value, or argument, or something. Copy into alloca right
                // after alloca definition.
        new StoreInst(v, result, result->getNext());
    }
            

    return result;
}

/** Processes a single interval from a lower-level partition.
    Adds correct branch to the gate block for this interval,
    and adds phi nodes for all variables modified in this
    interval to the next gate block.

    If 'merge_bypassed' is true, add phi instructions to
    the gate following 'lower' for every value set in that
    block.
*/
void
LinearizePass::processLowerInverval(const Interval& lower, 
                                    const Interval& current,
                                    bool merge_bypassed = true)
{
    DEBUG(std::cout << "Processing interval\n" << &lower << " " << lower);

    // 1. CREATE GATE BRANCH.

    // execution_condition[&lower] is a vector of values
    // We want to 'or' them all together and branch to 'lower' from
    // gate block of the result is true.

    if (&lower != current.Nodes.front())
    {
        assert(execution_condition.count(&lower));

        Value* final_condition = foldr(execution_condition[&lower],
                                       BinaryOperator::Or,
                                       gate_block[&lower]);

#if 0
        vector<Value*> ex = execution_condition[&lower];
        Value* final_condition = ex[0];
        
        for(unsigned i = 1; i < ex.size(); ++i)
        {
            final_condition = BinaryOperator::create(
                BinaryOperator::Or,
                final_condition,
                ex[i],
                "", gate_block[&lower]);
        }               
#endif
        gate[&lower] = final_condition;


        new BranchInst(first_basic_block[&lower], next_gate_block[&lower],
                       final_condition, gate_block[&lower]);
    }

    // 2. PROPAGATE EXECUTION CONDITION
    // We have a list of control transfers associated with this interval, and
    // each of them has a condition -- basically an instruction that
    // computes bool value that tells if that control transfer is possible.
    // After gate block is created, we have something like:
    //
    // gate:
    //      br %something, label %int1, label %gate2
    // int1:
    //      %condition = ...
    // gate2:
    //
    // The control transfer can be done if both the condition is true,
    // and current interval is executed. So, we add
    //
    //  %cond_merged = phi bool [ %condition, label %int1 ] ,
    //                          [ false, label %gate2 ]
    //
    // as use 'cond_merge' as final execution condition
    
    // The set 
    set<Value*> accounted_phis;

    vector<control_transfer_data>& cts = control_transfers[&lower];
    for (unsigned i = 0, e = cts.size(); i != e; ++i)
    {
        control_transfer_data& ct = cts[i];

        DEBUG(std::cout << "Control transfer to " << *ct.target << "\n");

        Value* propagated_condition;

        if (&lower != current.getHeaderNode())
        {
            BasicBlock* last = last_basic_block[&lower];
            PHINode* phi = new PHINode(Type::BoolTy);
            next_gate_block[&lower]->getInstList().push_front(phi);
            phi->addIncoming(ConstantBool::get(false),
                             gate_block[&lower]);
            phi->addIncoming(ct.condition, last);
            propagated_condition = phi;
            accounted_phis.insert(phi);

            ct.combined_condition = phi;
        }
        else
        {
            propagated_condition = ct.condition;
            ct.combined_condition = ct.condition;
        }

        if (current.contains(ct.target))
        {
            // Local control transfer (inside 'current')
            execution_condition[ct.target].push_back(propagated_condition);
        }
        else
        {  
            control_transfer_data ct2 = ct;
            ct2.target = current_partition->getInterval(ct2.target);
            control_transfers[&current].push_back(ct2);
            
            std::cout << "External control transfer!!!!!!!!!!!!!\n";
            std::cout << "To" << *ct2.target << "\n";
        }        
    }


    // 3. Add PHI nodes for all modified values.
    // If a basic block sets some value, then adding gate block
    // that can bypass this definition breaks SSA properties.
    // Create PHI nodes that would merge the values.
    // FIXME: I forgot what this code does!
    if (&lower != current.getHeaderNode() && merge_bypassed)
    {
        set<BasicBlock*> bbs;
        find_bbs(&lower, bbs);
        

        set<Value*> defs;
        find_live_vars(&lower, defs);
        
        for(set<Value*>::iterator i = defs.begin();
            i != defs.end();
            ++i)
        {
            if ((*i)->getType() == Type::VoidTy)
                continue;
            
            cout << "Found bypassed value: " << **i << "\n";
            
            //convertToAlloca(*i, current_function_);

             
#if 1
            PHINode* phi = new PHINode((*i)->getType(), "merge_bypassed");
            next_gate_block[&lower]->getInstList().push_front(phi);

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

            replaceSomeUsers(*i, phi, bbs, accounted_phis);

            assert(gate_block[&lower]);
            BasicBlock* last = last_basic_block[&lower];
            phi->addIncoming(UndefValue::get((*i)->getType()),
                             gate_block[&lower]);
            phi->addIncoming(*i, last);
            DEBUG(std::cout << "Added " << *phi);
#endif            
        }
    }

    // 4. Just to the next gate block.
    new BranchInst(next_gate_block[&lower], last_basic_block[&lower]);
}

/** For each subinterval in 'current', create empty gate
    block.
    Initialize the 'gate_block' and 'next_gate_block' maps.
    Add jump instructions to the gate blocks.
*/
void
LinearizePass::createGates(const Interval& current, Function& F,
                           set<Interval*>* scc)
{
    // First of all create a new basic block that will end this interval.    
    BasicBlock* interval_end;
    {
        // This 'last' has no semantic meaning, it's just the position where
        // new basic block will be added and can be arbitrary.
        BasicBlock* last = last_basic_block[current.Nodes.back()];
        interval_end = new BasicBlock("interval_finish", &F, last->getNext());
        
        // If any nested intervals have 'return' statement, we need to move
        // that return statement to 'interval_finish', and then add jump
        // to interval_finish.
        bool seen_return = false;
        // Iterate over last basic block of all nested intervals
        for(unsigned i = 0; i < current.Nodes.size(); ++i)
        {
            BasicBlock* last = last_basic_block[current.Nodes[i]];
            Instruction* term = last->getTerminator();
            if (term && isa<ReturnInst>(term))
            {
                // The entire function is supposed to have just one exiting
                // basic block.
                assert(!seen_return);
                seen_return = true;
                term->removeFromParent();
                interval_end->getInstList().push_back(term);   
                assert(term->getParent() == interval_end);
            }
        }
    }


    vector<Interval*> Nodes;
    if (scc)
    {
        // First process intervals in scc
        assert(scc->count(current.Nodes[0]));
        for (unsigned i = 0; i < current.Nodes.size(); ++i)
        {
            if (scc->count(current.Nodes[i]) != 0)
                Nodes.push_back(current.Nodes[i]);
        }
        for (unsigned i = 0; i < current.Nodes.size(); ++i)
        {
            if (scc->count(current.Nodes[i]) == 0)
                Nodes.push_back(current.Nodes[i]);
        }
    }
    else
    {
        Nodes.assign(current.Nodes.begin(), current.Nodes.end());
    }

    // Create gate blocks. The first interval does not need any.
    for(unsigned i = 1; i < Nodes.size(); ++i)
    {
        Interval* Int = Nodes[i];

        BasicBlock* gb = new BasicBlock("gate", &F, first_basic_block[Int]);
        extra_bbs[&current].insert(gb);
        
        gate_block[Int] = gb;

        next_gate_block[Nodes[i-1]] = gb;
    }

    next_gate_block[Nodes.back()] = interval_end;            
    
    last_basic_block[&current] = interval_end;
}

/** Processes one interval of the current partition.
    Sets the first_basic_block, last_basic_block and
    control_transfers data for that interval.
*/
void 
LinearizePass::processNonLoopingInterval(const Interval& current, 
                                         Function& F)
{
    execution_condition.clear();
    gate.clear();
    gate_block.clear();
    next_gate_block.clear();
    

    createGates(current, F);

#if 0
    // 1. SPECIAL-CASE the first interval. It does not need
    // any gate blocks or phi nodes.

    // Propagate execution condition from the header interval.
    vector<control_transfer_data>& cts = 
        control_transfers[current.Nodes.front()];
    for(unsigned i = 0; i < cts.size(); ++i)
    {
        execution_condition[cts[i].target].push_back(cts[i].condition);
    }

    new BranchInst(next_gate_block[current.Nodes.front()],
                   last_basic_block[current.Nodes.front()]);
#endif


    // 2. Add real gate conditition and phi nodes for all intervals.
    for(unsigned i = 0; i < current.Nodes.size(); ++i)
    {
        processLowerInverval(*current.Nodes[i], current);
    }

    // 3. Convert PHI nodes to calls to a function that merges
    // the values.
    convertPHINodes(current);

    // SET FIRST AND LAST BASIC BLOCKS
    first_basic_block[&current] = first_basic_block[current.getHeaderNode()];
    
    // last basic block is set in createGates
    
    //last_basic_block[&current] = next_gate_block[current.Nodes.back()];
}


void LinearizePass::convertPHINodes(const Interval& current)
{
    for(unsigned i = 1; i < current.Nodes.size(); ++i)
    {
        Interval* Int = current.Nodes[i];

        if (looping[Int])
        {
            BasicBlock* bb = Int->getHeaderBlock();
            Instruction* Inst = bb->begin();
            for(; isa<PHINode>(Inst); Inst = Inst->getNext())
            {
                PHINode* phi = cast<PHINode>(Inst);
                BasicBlock* bb = phi->getIncomingBlock(0);
                Value* v = phi->removeIncomingValue(0u, false);
                phi->addIncoming(v, gate_block[Int]);
            }
            continue;
        }
        
        // All of PHI nodes in the interval, except in the first
        // block, were processed previously. We only need
        // to look at the first block.
        BasicBlock* bb = Int->getHeaderBlock();
        
        Instruction* Inst = bb->begin();
        for(;; Inst = Inst->getNext())
        {            
            if (PHINode* phi = dyn_cast<PHINode>(Inst))
            {
                // The phi node has a list of predecessor 
                // basic blocks and a values corresponding
                // to those basic blocks.
                // We want for each basic block to compute
                // if we possible can arrive from that
                // basic block. 
                map<BasicBlock*, control_transfer_data> bb2ct;
                for(Interval::pred_iterator p = pred_begin(Int);
                    p != pred_end(Int);
                    ++p)
                {
                    const vector<control_transfer_data>& ct2
                            = control_transfers[*p];
                    
                    for(unsigned k = 0; k < ct2.size(); ++k)
                    {
                        control_transfer_data d = ct2[k];
                        if (d.target == Int)
                        {
                            assert(bb2ct.count(d.source) == 0);
                            bb2ct[d.source] = d;
                        }
                    }
                }
                
                DEBUG(std::cout << "PHINode: " << *phi << "\n");
                vector<Value*> parameters;
                parameters.push_back(ConstantInt::get(Type::IntTy,
                                     phi->getNumIncomingValues()));
                for(unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
                {
                    BasicBlock* pred = phi->getIncomingBlock(k);
                    assert(bb2ct.count(pred) != 0);
                    Value* cond = bb2ct[pred].combined_condition;
                    // Cast to int, so that passing via "..."
                    // works.
                    Value* cond_int = 
                            new CastInst(cond, Type::IntTy, "", phi);
                    parameters.push_back(cond_int);                            
                    DEBUG(std::cout << "Incoming value " << k 
                            << " -- condition is " << *cond
                            << "\n");
                    parameters.push_back(phi->getIncomingValue(k));
                }                        
                Instruction* call = 
                        new CallInst(m_merge_func, parameters, "", phi);
                Inst->replaceAllUsesWith(call);
                Inst->eraseFromParent();
                Inst = call;
            }
            else
            {
                // No more phi nodes.
                break;
            }
        }                                                
    }
}

void 
LinearizePass::processLoopingInterval(const Interval& current, 
                                      set<Interval*>* scc,
                                      Function& F)
{
    Interval* header = current.getHeaderNode();
    
    looping[&current] = true;

    execution_condition.clear();
    gate.clear();
    gate_block.clear();
    next_gate_block.clear();
    
    createGates(current, F, scc);
    
    for(unsigned i = 0; i < current.Nodes.size(); ++i)
    {
        processLowerInverval(*current.Nodes[i], current);
    }
    
    // Compute some information about SCC
    // Last interval in SCC
    Interval* last_in_scc = 0;
    Interval* first_outside_scc = 0;
    vector<Interval*> intervals_in_scc;
    vector<Interval*> intervals_outside_scc;
    for(unsigned i = 0; i < current.Nodes.size(); ++i)
    {
        if (scc->count(current.Nodes[i])) {
            last_in_scc = current.Nodes[i];
            intervals_in_scc.push_back(current.Nodes[i]);
        } else {
            if (!first_outside_scc)
                first_outside_scc = current.Nodes[i];
            intervals_outside_scc.push_back(current.Nodes[i]);
        }
    }
    
    
    // We're about to add basic blocks that cause loop to iterate.
    
    // However, we need to merge bypassed values from loop body before
    // going to the next iteration, not on the next. So, move all
    // phi nodes form the next gate into the loop.
    
    BasicBlock* first_gate_outside = gate_block[first_outside_scc];
    
    BasicBlock* merge_values_bypassed_in_last_interval =
            new BasicBlock("merge_values_bypassed_in_last_interval",
                           &F, gate_block[first_outside_scc]);
    extra_bbs[&current].insert(merge_values_bypassed_in_last_interval);
    
    BasicBlock* check_if_jump_out_is_possible =
            new BasicBlock("check_if_jump_out_is_possible",
                           &F, first_gate_outside);
    extra_bbs[&current].insert(check_if_jump_out_is_possible);
    
    BasicBlock* merge_out_values = 
            new BasicBlock("merge_out_values",
                           &F, first_gate_outside);
    extra_bbs[&current].insert(merge_out_values);
    
    BasicBlock* branch_back = new BasicBlock("branch_back",
                                             &F, first_gate_outside);
    extra_bbs[&current].insert(branch_back);
    
    BasicBlock* load_out_values = new BasicBlock("load_out_values",
            &F, first_gate_outside);
    extra_bbs[&current].insert(load_out_values);
    

    new BranchInst(branch_back, merge_out_values);
    
    new BranchInst(check_if_jump_out_is_possible, merge_values_bypassed_in_last_interval);
    
    
    if (first_outside_scc)
        new BranchInst(gate_block[first_outside_scc], load_out_values);
    else
        new BranchInst(last_basic_block[&current], load_out_values);
    
    // The jump from last gate in SCC should go to 'merge_values_bypassed_in_last_interval',
    // not to gate block outsude.
    gate_block[last_in_scc]->getTerminator()->replaceUsesOfWith(
            first_gate_outside, merge_values_bypassed_in_last_interval);
    
    // Same for last interval
    last_basic_block[last_in_scc]->getTerminator()->replaceUsesOfWith(
            first_gate_outside, merge_values_bypassed_in_last_interval);
    
    
            
    
    

    
    for(;;)
    {
        if (!first_gate_outside)
            break;
        
        PHINode* phi = dyn_cast<PHINode>(&(first_gate_outside->front()));
        if (!phi)
            break;
        phi->moveBefore(merge_values_bypassed_in_last_interval->getTerminator());
    }
             
    
    set<BasicBlock*> bbs_in_scc;
    find_bbs_v(intervals_in_scc, bbs_in_scc);
    // Fix the PHI nodes at the entry block. For values from the loop, they
    // refer to the basic block that originally generated this value.
    // Now, it should refer to the 'branch_back' basic block.
    {
        BasicBlock* first = first_basic_block[current.getHeaderNode()];
        BasicBlock::iterator i = first->begin(), e = first->end();
        
        for(;i != e; ++i)
        {
            PHINode* phi = dyn_cast<PHINode>(i);
            if (!phi)
                break;
            int jumps_from_inside = 0;
            for(unsigned j = 0; j < phi->getNumIncomingValues(); ++j)
            {
                if (bbs_in_scc.count(phi->getIncomingBlock(j)))
                    ++jumps_from_inside;
            }
            
            if (jumps_from_inside == 1)
            {
                for(unsigned j = 0; j < phi->getNumIncomingValues(); ++j)
                {
                    if (bbs_in_scc.count(phi->getIncomingBlock(j)))
                        phi->setIncomingBlock(j, branch_back);
                }
            }
            else if (jumps_from_inside > 1)
            {
                // whatever = phi [ %1, before_loop ], [ %2, bb_in_loop1 ], [%3, bb_in_loop2]
                // We need to merge value ranges of values that go from loop basic blocks.
                // and since that merge is a call instructoin it can'be be placed in
                // loop header, but only in loop branch back block.
                
                PHINode* merge_from_loop = new PHINode(phi->getType(), "", 
                        branch_back);
                
                for(unsigned j = 0; j < phi->getNumIncomingValues();)
                {
                    if (bbs_in_scc.count(phi->getIncomingBlock(j)))
                    {
                        merge_from_loop->addIncoming(phi->getIncomingValue(j),
                                phi->getIncomingBlock(j));
                        
                        phi->removeIncomingValue(j);
                        // Don't increment 'j', as removal shifts indixes
                    }
                    else
                    {
                        ++j;
                    }
                }
                phi->addIncoming(merge_from_loop, branch_back);                
            }
            
            
        }
    
    }        
    
    
    // Find the expression that determines is the next iteration of
    // the loop will be taken. Add branch back to header subject
    // to that condition.
    {
    // Create the back branch to loop header.
        Interval* header = current.getHeaderNode();
        assert(execution_condition.count(header));
        vector<Value*> ex = execution_condition[header];
        Value* final_condition = ex[0];
    
        for(unsigned i = 1; i < ex.size(); ++i)
        {
            final_condition = BinaryOperator::create(
                    BinaryOperator::Or,
            final_condition,
            ex[i],
            "", branch_back);
        }               
        gate[header] = final_condition;
       
        new BranchInst(first_basic_block[header], 
                       load_out_values,
                       final_condition, branch_back);
        
        //new BranchInst(first_basic_block[current.getHeaderNode()], branch_back);
    }
    
    // First expresion that determines if we will jump out of the loop.
    {
        vector<Value*> jumps_out_of_loop_condition;
        BOOST_FOREACH(const Interval* I, intervals_in_scc) 
        {
            BOOST_FOREACH(const control_transfer_data& ct, control_transfers[I]) 
            {
                if (bbs_in_scc.count(ct.target->getHeaderBlock()) == 0) {
                    DEBUG(std::cout << "Jump out of loop on " 
                            << *ct.condition << "\n");
                    assert(ct.combined_condition);
                    jumps_out_of_loop_condition.push_back(ct.combined_condition);
                }
            }
        }
            
        if (!jumps_out_of_loop_condition.empty())
        {
            Value* possibly_jumping_out = foldr(jumps_out_of_loop_condition, 
                    BinaryOperator::Or,
                    check_if_jump_out_is_possible);
        
            new BranchInst(merge_out_values, branch_back, possibly_jumping_out,
                           check_if_jump_out_is_possible);
        }
        else
        {
            new BranchInst(branch_back, check_if_jump_out_is_possible);
        }
            
            
    }
    
    
    
    // For all values set in the loop, and possible used outside the loop,
    // we need to merge value ranges for all iterations on which we can possible
    // jump out.
    {
        // First, add 'first_iteration' variable at the header block,
        // so that it's possible to find out if we need to merge new values, or
        // just copy it.
        PHINode* first_iteration = new PHINode(Type::BoolTy, "first_iteration", 
                                               first_basic_block[header]->begin());
        // 'imm' is bogus here, will be fixed when processing higher-level partition.
        // FIXME: Don't remember how that 'fixup' will happen.
        first_iteration->addIncoming(ConstantBool::get(true), branch_back); 
        first_iteration->addIncoming(ConstantBool::get(false), branch_back);
        
        set<Value*> live_at_loop_exit;
        find_live_vars_v(intervals_in_scc, live_at_loop_exit);
        
        
        set<Value*>::iterator i = live_at_loop_exit.begin(), e = live_at_loop_exit.end();
        
        for(; i != e; ++i)
        {
            // Create global alloca for this value, to avoid messing with PHI nodes.
            
            Instruction* allocated = new AllocaInst((*i)->getType(), 0, (*i)->getName() + "_alloca",
                    F.begin()->getTerminator());
            
                      
            
            // Load the merged value after the loop.
            
            Instruction* load = new LoadInst(allocated, (*i)->getName() + "_reloaded", 
                                       load_out_values->getTerminator());
            
            // Replace all uses of *i outside loop with uses of the reloaded variable.
            
            set<BasicBlock*> bbs;
            find_bbs_v(intervals_in_scc, bbs);
            bbs.insert(extra_bbs[&current].begin(), extra_bbs[&current].end());
            
            set<Value*> dummy;
            replaceSomeUsers(*i, load, bbs, dummy);
            
            // It looks like live vars determination is not exactly correct. Cleanup
            // unused loads.
            if (load->getNumUses() == 0)
            {
                load->eraseFromParent();
                allocated->eraseFromParent();
            }
            else
            {
                // There are some uses after all.
                // Add a call to merge the new value with out value on each
                // iterator.
                if ((*i)->getType() == Type::IntTy)
                {
                    vector<Value*> params;
                    params.push_back(allocated);
                    params.push_back(*i);
                    params.push_back(first_iteration);
                    
                    Instruction* inst = dyn_cast<Instruction>(*i);
                    assert(inst);
            
                    new CallInst(m_merge_out_func, params, "", merge_out_values->getTerminator());
                }
                else
                {
                    // Have some uses but it's not int. Kill load, replace with undef.
                    load->replaceAllUsesWith(UndefValue::get((*i)->getType()));
                    load->eraseFromParent();
                    allocated->eraseFromParent();
                }
            }
            
            
        }
    }
    
    convertPHINodes(current);

    
    
#if 0    
    
    

    


    

    BasicBlock* gb = next_gate_block[last_in_scc];

    BasicBlock* imm = new BasicBlock("branch_back", &F, gb);
    extra_bbs[&current].insert(imm);

    // Create the back branch to loop header.
    Interval* header = current.getHeaderNode();
    assert(execution_condition.count(header));
    vector<Value*> ex = execution_condition[header];
    Value* final_condition = ex[0];
    
    for(unsigned i = 1; i < ex.size(); ++i)
    {
        final_condition = BinaryOperator::create(
            BinaryOperator::Or,
            final_condition,
            ex[i],
            "", imm);
    }               
    gate[header] = final_condition;

    last_basic_block[last_in_scc]->getTerminator()->eraseFromParent();

    new BranchInst(first_basic_block[header], 
                   gb,
                   final_condition, imm);

    set<BasicBlock*> bbs;
    find_bbs(&current, bbs);

    // Now handle PHI nodes at header. Some of the values are changed in the 
    // loop. We want to merge values of those nodes after loop body is executed
    // and replace as uses of that value in PHINode with use of the merge value.
    for(BasicBlock::iterator Inst = first_basic_block[header]->begin(); isa<PHINode>(Inst); ++Inst)
    {
        PHINode* phi = cast<PHINode>(Inst);

        vector<Value*> internal_values;
        vector<int> internal_preds;
        for(unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            BasicBlock* b = phi->getIncomingBlock(i);
            if (bbs.count(b))
            {
                DEBUG(std::cout << "Incoming phi node from self " 
                      << phi->getIncomingValue(i) << "\n");
                internal_values.push_back(phi->getIncomingValue(i));
                internal_preds.push_back(i);
            }            
            assert(internal_values.size() <= 1);
        }


        if (internal_values.size() == 1)
        {
            phi->removeIncomingValue(internal_preds[0]);
            assert(phi->getNumIncomingValues() == 1);
            phi->addIncoming(internal_values[0], imm);
        }
    }
    // Create a new phi node that will evaluate to 'true' on first execution, 
    // and to 'false' on subsequent ones.
    PHINode* first_iteration = new PHINode(Type::BoolTy, "first_iteration", 
                                           first_basic_block[header]->begin());
    // 'imm' is bogus here, will be fixed when processing higher-level partition.
    first_iteration->addIncoming(ConstantBool::get(true), imm); 
    first_iteration->addIncoming(ConstantBool::get(false), imm); 

    // For each value modified in the loop, create a new variable that will hold
    // possible values of the original value after loop has exited.
    // The resulting structure is something like:
    // header:
    //  i_out_top = [ undef, %prev], [%i_out, %imm]
    //    ...............
    //    ...............
    //
    //  imm2:
    //         br %may_exit_from_loop, imm3, imm
    //  imm3:
    //     %i_out_m = call %merge_out (%i, %i_out_top, %first_iteration)
    //  imm:
    //     %i_out = phi [ %i_out_top, %imm2], [ %i_out_m, %imm3]
    //  branch to header



    set<Value*> live_at_loop_exit;
    find_live_vars_v(intervals_in_scc, live_at_loop_exit);
    Instruction* first = first_basic_block[header]->begin();
    Instruction* imm_term = imm->getTerminator();

    set<BasicBlock*> bbs_in_scc;
    find_bbs_v(intervals_in_scc, bbs_in_scc);
    bbs_in_scc.insert(imm);

    vector<Value*> jumps_out_of_loop_condition;
    BOOST_FOREACH(const Interval* I, intervals_in_scc) {
        BOOST_FOREACH(const control_transfer_data& ct, control_transfers[I]) {
            if (bbs_in_scc.count(ct.target->getHeaderBlock()) == 0) {
                DEBUG(std::cout << "Jump out of loop on " 
                      << *ct.condition << "\n");
                assert(ct.combined_condition);
                jumps_out_of_loop_condition.push_back(ct.combined_condition);
            }
        }
    }

    BasicBlock* imm2 = new BasicBlock("maybe_merge_out_values", &F, imm);
    
    // If there jump to gate block of the first block outside SCC, replace
    // them with jumps to meybe_merge_out_values block.
    BasicBlock* gate_of_last_in_scc = gate_block[last_in_scc];
    BasicBlock* gate_of_first_outside = gate_block[first_outside_scc];
    if (BranchInst* branch = dyn_cast<BranchInst>(gate_of_last_in_scc->getTerminator()))
    {
        assert(branch->isConditional());
        {
            if (branch->getOperand(1) == gate_of_first_outside)
            {
                branch->setOperand(1, imm2);                
            }
            else if (branch->getOperand(2) == gate_of_first_outside)
            {
                branch->setOperand(1, imm2);
            }
        }
    }
    // Move 'merge_bypassed' phis from the next gate block to 'maybe_merge_out_values'
/*    {
        while(isa<PHINode>(&gate_of_first_outside->front()))
        {
            Instruction* first = gate_of_first_outside->begin();
            Instruction* IB = gate_of_last_in_scc->getFirstNonPHI();
            first->moveBefore(IB);
            
        }
        
    }*/
    
    
    
    Value* possibly_jumping_out = foldr(jumps_out_of_loop_condition, 
                                        BinaryOperator::Or,
                                        imm2);
    BasicBlock* imm3 = new BasicBlock("really_merge_out_values", &F, imm);
    new BranchInst(imm3, imm, possibly_jumping_out, imm2);
    new BranchInst(imm, imm3);
    new BranchInst(imm2, last_basic_block[last_in_scc]);

    extra_bbs[&current].insert(imm);
    extra_bbs[&current].insert(imm2);
    extra_bbs[&current].insert(imm3);

    Instruction* imm3_term = imm3->getTerminator();
    Instruction* imm_first= imm->begin();
    

    set<Value*> inserted;
    BOOST_FOREACH(Value* live, live_at_loop_exit) {

        if (live->getType() != Type::IntTy)
            continue;

        PHINode* out_top = new PHINode(live->getType(), live->getName() + ".out_top", 
                                 first);

        vector<Value*> args;
        args += live, out_top, first_iteration;
        Value* out_m = new CallInst(m_merge_out_func, args, 
                                    live->getName() + ".out_m",
                                    imm3_term);                                            

        PHINode* out = new PHINode(live->getType(), live->getName() + ".out",
                                   imm_first);
        out->addIncoming(out_top, imm2);
        out->addIncoming(out_m, imm3);

        inserted.insert(out);
        inserted.insert(out_m);
        inserted.insert(out_top);


        // 'imm' is bogus will be fixed later when processing higher order 
        // interval
        out_top->addIncoming(UndefValue::get(Type::IntTy), imm);
        out_top->addIncoming(out, imm);


        replaceSomeUsers(live, out, bbs_in_scc, inserted);
    }
#endif
          
    // SET FIRST AND LAST BASIC BLOCKS
    first_basic_block[&current] = first_basic_block[current.getHeaderNode()];
    
    // last basic block is set in createGates
    
    //last_basic_block[&current] = next_gate_block[current.Nodes.back()];    
}


void 
LinearizePass::processPartition(const IntervalPartition& P, Function& F)
{
    cout << "Partition has " << P.getIntervals().size() << " intervals\n";
    current_partition = &P;

    const std::vector<Interval*>& I = P.getIntervals();
    for (unsigned i = 0, e = I.size(); i != e; ++i)
    {
        Interval* current = I[i];

//        cout << "Interval is\n";
//        current->print(cout);

        set<Interval*> scc;
        bool has_scc = find_scc(*current, scc);

    
        if (has_scc)
            cout << "SCC FOUND\n";
        else
            cout << "NO SCCS\n";


        
        if (has_scc) {

            processLoopingInterval(*current, &scc, F);


        } else {

            processNonLoopingInterval(*current, F);

        }
    }

}

void LinearizePass::init_control_transfer_data(const IntervalPartition& PP)
{
    assert(PP.zero_order);
    const IntervalPartition& P = *(PP.zero_order);

    map<BasicBlock*, Interval*> bb2int;
    for(unsigned i = 0, e = P.getIntervals().size(); i != e; ++i)
    {
        Interval* Int = P.getIntervals()[i];
        assert(Int->Nodes.empty());

        bb2int[Int->getHeaderBlock()] = Int;

        first_basic_block[Int] = Int->getHeaderBlock();
        last_basic_block[Int] = Int->getHeaderBlock();        
    }

    for(unsigned i = 0, e = P.getIntervals().size(); i != e; ++i)
    {
        Interval* Int = P.getIntervals()[i];
        assert(Int->Nodes.empty());

        BasicBlock* bb = Int->getHeaderBlock();

        TerminatorInst* term = bb->getTerminator();

        assert(isa<ReturnInst>(term) || isa<BranchInst>(term));

        if (BranchInst* b = dyn_cast<BranchInst>(term))
        {
            if (b->isConditional())
            {
                control_transfer_data d1 = {bb,
                                            bb2int[b->getSuccessor(0)],
                                            b->getCondition()};
                control_transfers[Int].push_back(d1);

                // Negate the branch condition.
                Value* inverted = BinaryOperator::create(
                    BinaryOperator::Xor, b->getCondition(), 
                    ConstantBool::get(true), "", term);

                control_transfer_data d2 = {bb,
                                            bb2int[b->getSuccessor(1)],
                                            inverted};
                control_transfers[Int].push_back(d2);
            }
            else
            {
                control_transfer_data d = {bb,
                                           bb2int[b->getSuccessor(0)], 
                                           ConstantBool::get(true)};
                control_transfers[Int].push_back(d);                                           
            }

            term->removeFromParent();
            delete term;
        }

    }

}

bool LinearizePass::runOnFunction(Function& f)
{    
    cout << "Processing function " << f.getName() << "\n";
    
    current_function_ = &f;
    
    
    vector<const Type*> params;
    params.push_back(Type::IntTy);
    FunctionType* ft = FunctionType::get(Type::IntTy, params, true);                                         
    m_merge_func = f.getParent()->getOrInsertFunction ("merge_values", ft);


    m_merge_out_func = f.getParent()->getOrInsertFunction(
        "merge_out_value",
        Type::VoidTy,
        PointerType::get(Type::IntTy),
        Type::IntTy,
        Type::BoolTy, 
        0);
  


    

#if 0
    BasicBlock* single_return = 0;

    // For all basic blocks.
    for(Function::iterator bb = f.begin(); bb != f.end(); ++bb)
    {
        if (isa<ReturnInst>(bb->getTerminator())) {
            assert(!single_return && "Function should have single returning basci block");
            single_return = bb;
        }         
    }

    runOnRange(f.begin(), single_return);
#endif

    IntervalPartition& Intervals = getAnalysis<IntervalPartition>();

    Intervals.zero_order->print(cout);
    init_control_transfer_data(Intervals);

    bool reducible = true;
    vector<IntervalPartition*> tmp;
    tmp.push_back(&Intervals);
    for(IntervalPartition* current = &Intervals; 
        !current->isDegeneratePartition();)
    {
        IntervalPartition* next = new IntervalPartition(*current, false);
        tmp.push_back(next);
        if (next->getIntervals().size() == current->getIntervals().size()) {
            reducible = false;
            break;
        }            
        else
        {
            current = next;
        }
    }

    for(unsigned i = 0, e = tmp.size(); i != e; ++i)
    {
        cout << (i+1) << "-order partition\n";        
        processPartition(*tmp[i], f);
        cout << "Code now " << f << endl;
    }
    
    cout << flush;


    assert(reducible);

//    cout << f << "\n";

    


#if 0
    // We should have a single path though the function now.
    // Convert PHI nodes into value range merges.
    
    //Value* merge_function = M.getOrInsertFunction(
    //    "lvk.wcet.merge_time_estimation", pvoid, pvoid, Type::UIntTy, 0);
    vector<Instruction*> phis;
    for(inst_iterator i = inst_begin(f), e = inst_end(f); i != e; ++i)
    {
        if (isa<PHINode>(&*i))
            phis.push_back(&*i);
    }
    for(unsigned i = 0; i < phis.size(); ++i) {        
        phis[i]->replaceAllUsesWith(UndefValue::get(phis[i]->getType()));
        phis[i]->removeFromParent();
        delete phis[i];
    }
#endif        

    

      

#if 0
    // For all basic blocks.
    for(Function::iterator bb = f.begin(); bb != f.end(); ++bb)
    {
        // For all instructions
        for(BasicBlock::iterator inst = bb->begin(); inst != bb->end(); ++inst)
        {
            if (BranchInst* branch = dyn_cast<BranchInst>(inst))
            {
                if (branch->isConditional())
                {
                    cout << "Branch " << *branch << "\n";
                    // Get dominance frontiers for both branch targets.                    
                    const std::set<BasicBlock*>& df1 = DF.find(
                        branch->getSuccessor(0))->second;
                    const std::set<BasicBlock*>& df2 = DF.find(
                        branch->getSuccessor(1))->second;

                    if (df1.size() == 1 && df2.size() == 1 && 
                        *df1.begin() == *df2.begin())
                        cout << "Merge point: " << *df1.begin() << "\n";
                    else
                        cout << "No merge point detected\n";
                }
            }
            
        }
    }

#endif

    return true;
}

