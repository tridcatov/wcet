#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Support/CFG.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace std;

namespace {
    struct RecBrakes: public ModulePass {
        static char ID;
        RecBrakes(): ModulePass(ID) {}

        bool runOnModule(Module & );
    };

    char RecBrakes::ID = 0;
    INITIALIZE_PASS(RecBrakes, "rb",
            "Breaks recursion calls using counters constraints",
            true, true);
};

bool RecBrakes::runOnModule(Module & m) {
    outs() << "Transforming calls in module " << m.getModuleIdentifier() << "\n";
    return false;
}
