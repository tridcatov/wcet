#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/System/DataTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Constant.h"
#include "misc/logger.h"

#include <iostream>

using namespace llvm;
using namespace std;

namespace {
    class Marker : public ModulePass {
            Logger * logger;
        public:
            static char ID;
            
            Marker(): ModulePass(ID)
            {
                logger = new ErrLogger("Basic Blocks");
            }
            ~Marker()
            {
                delete logger;
            }
            
            bool runOnModule(Module &M);
    };

    char Marker::ID = 0;
    INITIALIZE_PASS(Marker, "basicBlocksNumber", "Add sequental numbering for basic blocks", true, false);
}

bool Marker::runOnModule(Module &M)
{
    LLVMContext & context = M.getContext();

    vector<const Type *> params;
    params.push_back(Type::getInt32Ty(context));
    FunctionType * ft = FunctionType::get(
            Type::getVoidTy(context),
            params,
            false
            );
    Value * updating_function = M.getOrInsertFunction(
        StringRef("lvk.wcet.basicBlockNumber"), ft);

    unsigned BBNumber = 0;
    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F)
    {
        if (!isa<Function>(F))
            continue;

        for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) 
        {
            BasicBlock::iterator InsertPos = BB->begin();
            while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos))
                ++InsertPos;

            Value * arg = Constant::getIntegerValue(Type::getInt32Ty(M.getContext()), APInt(32,BBNumber));
            CallInst::Create(updating_function, arg, "", InsertPos);
            logger->log() << BB->getName() << " in " 
                << F->getName() << " numbered as " << BBNumber << "\n";

            ++BBNumber;
        }
    }

    return true;
}



