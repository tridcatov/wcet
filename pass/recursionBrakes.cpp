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

#include "misc/logger.h"

#include <map>
#include <string>

using namespace llvm;
using namespace std;

namespace {

class RecBrakes: public ModulePass {
private:
    Logger * logger;
public:
    static char ID;
    
    RecBrakes(): ModulePass(ID) {
        logger = new OutLogger("Recursion braker");
    }

    ~RecBrakes() {
        delete logger;
    }

    map<CallInst *, string> brakes;

    bool runOnModule(Module & );
    void processFunction(Function &);
    void processBasicBlock(BasicBlock &);
    void processCallInst(Instruction &);

    void createBrake(CallInst *, string &);
};

char RecBrakes::ID = 0;
INITIALIZE_PASS(RecBrakes, "rb",
        "Breaks recursion calls using counters constraints",
        true, true);
};

bool RecBrakes::runOnModule(Module & m) {
    logger->log() << "Transforming calls in module " << m.getModuleIdentifier() << "\n";
    for (Module::iterator f = m.begin(), e = m.end(); f != e; ++f)
    {
        if (! isa<Function>(f))
            continue;

        processFunction(*f);
    }
    return false;
}

void RecBrakes::processFunction(Function & f) {
    logger->log(1) << "Transforming calls in function " << f.getName() << "\n";
    for (Function::iterator bb = f.begin(), e = f.end(); bb != e; ++bb)
    {
        processBasicBlock(*bb);
    }
}

void RecBrakes::processBasicBlock(BasicBlock & bb) {
    const int logLevel = 2;
    logger->log(logLevel) << "Transforming calls in basic block " << bb.getName() << "\n";
    for (BasicBlock::iterator i = bb.begin(), e = bb.end(); i != e; i++) {
        Instruction & instruction = *i;
        if (! isa<CallInst>(i))
            continue;
        processCallInst(*i);
    }
}

void RecBrakes::processCallInst(Instruction & inst) {
    const int logLevel = 3;
    BasicBlock * bb = inst.getParent();
    Function * callerFunction = bb->getParent();
    string callerName = callerFunction->getName();
    
    CallInst * call = dynamic_cast<CallInst *>(&inst);
    Function * calleeFunction = call->getCalledFunction();    
    string calleeName = calleeFunction->getName();
    string name = callerName + "." + calleeName;

    createBrake(call, name);
}

void RecBrakes::createBrake(CallInst * call, string & name) {
    const int logLevel = 3;
    if (brakes[call] == name)
        return;

    brakes[call] = name;

    BasicBlock * parent = call->getParent();
    BasicBlock::iterator splitPoint;
    for (BasicBlock::iterator i = parent->begin(),
            e = parent->end(); i != e; i++)
    {
       if ((*i).isIdenticalTo(call))
           splitPoint = i;
    }

    string presplitName = name + "_presplit";
    BasicBlock * preSplit = parent->splitBasicBlock(splitPoint, presplitName); 


    logger->log(logLevel) << "Created break for call " << name << "\n";
}
