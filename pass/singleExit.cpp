#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>

using namespace llvm;
using namespace std;

namespace {
    class SingleExit : public ModulePass {        
    public:
        static char ID;
        SingleExit(): ModulePass(ID) {}
        
        bool runOnModule(Module &M);
        void ensure_single_returning_block(Function* f);
    };
/* Registering in 2.8 style */
/*    
    RegisterOpt<SingleExit> X(
        "single-exit",
        "Makes sure each function has a single exit block");
*/
    char SingleExit::ID = 0;
    INITIALIZE_PASS(SingleExit, 
            "singleExit",
            "Makes sure each function has a single exit block",
            true, false);
}

bool SingleExit::runOnModule(Module &M)
{
    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F)
    {
        /* All functions in modules are assumed to be internal */
        if (!isa<Function>(F)/* || F->isExternal() */)
            continue;

        ensure_single_returning_block(F);

    }

    return true;
}

void SingleExit::ensure_single_returning_block(Function* f)
{
    LLVMContext & currentContext = f->getContext();
    vector<BasicBlock*> returning_blocks;

    for(Function::iterator bb = f->begin(); bb != f->end(); ++bb)
    {
        if (TerminatorInst* t = bb->getTerminator())
        {
            if (dyn_cast<ReturnInst>(t))
            {
                returning_blocks.push_back(bb);
            }
        }
    }

    assert(!returning_blocks.empty() && "function without returns?");
    if (returning_blocks.size() == 1)
        return;
    
    /* Construncting new terminator basic block */
//    BasicBlock* result = new BasicBlock("really_single_exit", f);
    BasicBlock* result = BasicBlock::Create(
            currentContext, // context
            "ensuredSingleExit", // name
            f // parent function
            );

    if (f->getReturnType() != Type::getVoidTy(currentContext))
    {
        Instruction* return_value = new AllocaInst(f->getReturnType(), 0, "", 
                                             f->begin()->begin());
        Value* load_return_value = new LoadInst(return_value, "", result);
        ReturnInst* returnInstruction = ReturnInst::Create(
                currentContext,
                load_return_value,
                result
                );

        for(unsigned i = 0; i < returning_blocks.size(); ++i)
        {
            // Get the terminator
            ReturnInst* t = static_cast<ReturnInst*>(
                returning_blocks[i]->getTerminator());
            // Copy the return value into variable.
            new StoreInst(t->getReturnValue(), return_value, t);
            // Add branch instruction before it.
            BranchInst * returnBranch = BranchInst::Create(
                    result,
                    t
                    );

            t->removeFromParent();
            delete t;
        }
        errs() << "[se] " << "function " << f->getName() 
            << " single exited from " << returning_blocks.size() 
            << " exits";
    }
    else
    {
        assert(false && "void functions not yet supported");
    }
}


    
