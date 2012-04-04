#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "misc/logger.h"

using namespace llvm;
using namespace std;

namespace {
    class SingleExit : public ModulePass {        
    public:
        Logger * logger;
        static char ID;
        SingleExit(): ModulePass(ID) {
            logger = new ErrLogger("Single exit");
        }

        ~SingleExit() {
            delete logger;
        }
        
        bool runOnModule(Module &M);
        void ensure_single_returning_block(Function* f);
    };

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
        if (!isa<Function>(F))
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
    
    BasicBlock* result = BasicBlock::Create(
            currentContext,
            "ensuredSingleExit",
            f
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
            ReturnInst* t = static_cast<ReturnInst*>(
                returning_blocks[i]->getTerminator());
            new StoreInst(t->getReturnValue(), return_value, t);
            BranchInst * returnBranch = BranchInst::Create(
                    result,
                    t
                    );

            t->removeFromParent();
            delete t;
        }
        logger->log() << "function " << f->getName() 
            << " single exited from " << returning_blocks.size() 
            << " exits" << "\n";
    }
    else
    {
        assert(false && "void functions not yet supported");
    }
}


    
