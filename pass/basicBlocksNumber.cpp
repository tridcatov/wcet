#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/System/DataTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Constant.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>

using namespace llvm;
using namespace std;

namespace {
    class Marker : public ModulePass {
        public:
            static char ID;
            Marker(): ModulePass(ID) {}
            bool runOnModule(Module &M);
    };

    char Marker::ID = 0;
    INITIALIZE_PASS(Marker, "basicBlocksNumber", "Add sequental numbering for basic blocks", true, false);
}

bool Marker::runOnModule(Module &M)
{
    /*TODO:[semantics]
     * Supposed to be transitioned to Constant* 
     **/
    /*
    Value* updating_function = M.getOrInsertFunction(
            "lvk.wcet.number_basic_block",
            Type::VoidTyID,
            Type::IntegerTyID, 0);
    */
    Value * updating_function = M.getOrInsertFunction(
        StringRef("lvk.wcet.basicBlockNumber"),
        Type::getVoidTy(M.getContext()),
        Type::getInt32Ty(M.getContext()),
        0        
    );

    unsigned BBNumber = 0;
    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F)
    {
        /* Following isExternal() is omitted, because all functions
         * are presumed to internal.
         */
        if (!isa<Function>(F) /*|| F->isExternal()*/)
            continue;

        for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) 
        {
            // Insert the call after any alloca or PHI instructions...
            BasicBlock::iterator InsertPos = BB->begin();
            while (isa<AllocaInst>(InsertPos) || isa<PHINode>(InsertPos))
                ++InsertPos;

            //std::vector<Value*> Args(1);
            /* Attempting to substitute type ID with 
             * type reference pointer
             */

            Value * arg = Constant::getIntegerValue(Type::getInt32Ty(M.getContext()), APInt(32,BBNumber));
            //Args[0] = ConstantUInt::get(/*Type::UIntTy*/Type::getInt32Ty(M.getContext()), BBNumber);
            //Args[0] = Constant::getIntegerValue(Type::getInt32Ty(M.getContext()), APInt(32,BBNumber));
            CallInst::Create(updating_function, arg, "", InsertPos);
            errs() << "[bbn] " << BB->getName() << " in " 
                << F->getName() << " numbered as " << BBNumber << "\n";

            ++BBNumber;
        }
    }

    return true;
}



