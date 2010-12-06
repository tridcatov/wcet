#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"

#include <iostream>

using namespace llvm;
using namespace std;

namespace {
    class Marker : public ModulePass {        
        bool runOnModule(Module &M);
    };
    
    RegisterOpt<Marker> X("number-basic-blocks",
                          "Add sequental numbering for basic blocks");
}

bool Marker::runOnModule(Module &M)
{
    Value* updating_function = M.getOrInsertFunction(
        "lvk.wcet.number_basic_block", Type::VoidTy, Type::UIntTy, 0);
 
    unsigned BBNumber = 0;
    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F)
    {
        if (!isa<Function>(F) || F->isExternal())
            continue;
        
        for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) 
        {
            // Insert the call after any alloca or PHI instructions...
            BasicBlock::iterator InsertPos = BB->begin();
            while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos))
                ++InsertPos;

            std::vector<Value*> Args(1);
            Args[0] = ConstantUInt::get(Type::UIntTy, BBNumber);
            new CallInst(updating_function, Args, 
                         "", InsertPos);
       
            ++BBNumber;
        }
    }

    return true;
}


    
