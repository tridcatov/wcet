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
    struct InitUnreachable: public FunctionPass {
        static char ID;
        InitUnreachable() : FunctionPass(ID) {}

        bool runOnFunction(Function &);
    };

    char InitUnreachable::ID = 0;
    INITIALIZE_PASS (InitUnreachable, "iu",
            "Initialises values that are not reached by data flow",
            true, true);
};

bool InitUnreachable::runOnFunction(Function & f) {
    outs() << "Remarking " << f.getName() << " function.\n";
    return false;
}
