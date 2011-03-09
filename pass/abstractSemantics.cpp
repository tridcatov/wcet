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

#include <vector>

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

        StructType * pairOfInts;
        StructType * pairOfBools;

        map<Value *, mapping_kind_t> mappedKind;
        map<Value *, Value *> mappedValue;
        map<Value *, Value *> mappedValueProper;
        map<Value *, Value *> mappedValueDefined;
        map<Value *, Value *> mappedMin;
        map<Value *, Value *> mappedMax;
        map<Value *, bool> mappedFake;


        Function * merge2Function;
        Function * mergeOut2Function;
        Function * maxFunction;

        vector<Value *> toDeleteLater;
    };

    char Marker::ID = 0;
    INITIALIZE_PASS(Marker,
            "abstract-semantics",
            "Introduces abstract semantics",
            true, true);

};

/* TODO: incomplete function */
bool Marker::runOnModule(Module &M) {
    LLVMContext & context = M.getContext();
    M.getOrInsertFunction("max",
            Type::getInt32Ty(context),
            Type::getInt32Ty(context),
            Type::getInt32Ty(context),
            0);
    maxFunction = M.getFunction("max");

    module = &M;

    vector<const Type *> types;
    
    types.push_back(Type::getInt32Ty(context));
    types.push_back(Type::getInt32Ty(context));
    pairOfInts = StructType::get(context, types);
    M.getTypeSymbolTable().insert("pairOfInts", pairOfInts);

    types.clear();
    types.push_back(Type::getInt1Ty(context));
    types.push_back(Type::getInt1Ty(context));
    pairOfBools = StructType::get(context, types);
    M.getTypeSymbolTable().insert("pairOfBools", pairOfBools);

    {
        vector<GlobalVariable *> valuesToProcess;

        /* Globals handling */
        Module::global_iterator i,e;
        for(i = M.global_begin(), e = M.global_end(); i != e; i++) {
            const PointerType * t = i->getType();
            const Type * el = t->getElementType();
            el->dump();

            if (isa<FunctionType>(el))
                continue;

            // Otherwise values will be left along
            // until in won't produce unknown values
            if (el->isPrimitiveType() &&
                    (el == Type::getInt32Ty(context) ||
                     el == Type::getInt32Ty(context)))
                valuesToProcess.push_back(i);
        }

        for (int i = 0; i < valuesToProcess.size(); i++) {
            GlobalVariable * v = valuesToProcess[i];

            Constant * init = v->getInitializer();

            vector<Constant *> values;
            values.push_back(init);
            values.push_back(init);

            const StructType * newType = 0;
            const Type * elementType = v->getType()->getElementType();
            if (elementType == Type::getInt32Ty(context))
                newType = pairOfInts;
            else if (elementType == Type::getInt1Ty(context))
                newType = pairOfBools;
            
            assert(newType);

            Constant * newInit = ConstantStruct::get(newType, values);

            mapping_kind_t kind = (elementType == Type::getInt32Ty(context)) ?
                MappedToPair : MappedToValueAndFlag;

            mappedKind[v] = kind;

            GlobalVariable * nv = new GlobalVariable(
                    newType,
                    v->isConstant(),
                    v->getLinkage(),
                    newInit,
                    v->getName(), v->getParent());

            mappedValue[v] = nv;
        }
    }

    Module::iterator i, e;
    for(i = M.begin(), e = M.end(); i != e; i++) {
        // TODO: add correct parameter type handling
    }

    visit(module);

    for (int i = 0; i < toDeleteLater.size(); i++) {
        toDeleteLater[i]->replaceAllUsesWith(ConstantInt::getSigned(
                    Type::getInt32Ty(context), 0));
        delete toDeleteLater[i];
    }

    return true;
}

 
