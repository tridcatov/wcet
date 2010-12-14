#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
    struct Hello: public FunctionPass {
        static char ID;
        Hello(): FunctionPass(ID) {}

        bool runOnFunction(Function& F) {
            errs() << "Hello: " << F.getName() << "\n";
            return false;
        }
    };

    char Hello::ID = 0;
    INITIALIZE_PASS(Hello, "hello", "Hello World Pass",
            false, false);
}
