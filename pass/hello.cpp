#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "misc/logger.h"

using namespace llvm;

namespace {

    struct Hello: public FunctionPass {
        static char ID;
        Logger * logger;
        
        Hello(): FunctionPass(ID) {
            logger = new ErrLogger("Hello");
        }
        
        bool runOnFunction(Function& F) {
                logger->log() << F.getName() << "\n";
            return false;
        }

        ~Hello() {
            delete logger;
        }
    };

    char Hello::ID = 0;

    INITIALIZE_PASS(Hello, "hello", "Hello World Pass",
            false, false);
}
