#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/ValueSymbolTable.h"
#include "llvm/TypeSymbolTable.h"
#include "llvm/Support/InstVisitor.h"

using namespace llvm;
using namespace std;

enum mapping_kind_t
{
    MappedToNothing = 0,
    MappedToPair = 1,
    MappedToValueAndFlag,
    MappedToHellKnowsWhat,
};

namespace {
    class Marker : public ModulePass, public InstVisitor<Marker>
    {    
    public:    
        bool runOnModule(Module &M);
        Marker(): ModulePass(ID) {}
        static char ID;
        Module * module;
    };

    char Marker::ID = 0;
    INITIALIZE_PASS(Marker,
            "abstract-semantics",
            "Introduces abstract semantics",
            true, true);

};

bool Marker::runOnModule(Module &M) {
    return true;
}

 
