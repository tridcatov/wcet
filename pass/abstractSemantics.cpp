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
#include "llvm/Support/raw_ostream.h"

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


        using InstVisitor<Marker>::visit;

        Function * merge2Function;
        Function * mergeOut2Function;
        Function * maxFunction;

        set<Value *> newlyAdded;
        vector<Value *> toDeleteLater;

        mapping_kind_t getMappingKind(Value * v) {
            if (isa<Constant>(v)) {
                if (v->getType() == Type::getInt32Ty(module->getContext())) {
                    return MappedToPair;
                } else if (v->getType() == Type::getInt1Ty(module->getContext())) {
                    return MappedToValueAndFlag;
                }
            }
            return mappedKind[v];
        }

        Value * getMinValue(Value * v, Instruction * i) {
            if (isa<UndefValue>(v))
                return v;

            if (isa<Constant>(v) && !isa<GlobalVariable>(v))
                return v;

            if (mappedMin.count(v))
                return mappedMin[v];

            if (mappedValue.count(v)) {
                Value * mapped = mappedValue[v];
                assert(mapped->getType() == PointerType::getUnqual(pairOfInts));

                const Type * type = Type::getInt32Ty(module->getContext());
                vector<Value *> cs;
                Value * c = ConstantInt::get(type, 0);
                cs.push_back(c);
                cs.push_back(c);

                Value * addr = GetElementPtrInst::Create
                    <vector<Value *>::iterator>(
                        mapped,
                        cs.begin(),
                        cs.end(),
                        "min_a", i);

                Value * min = new LoadInst(addr, "min", i);

                return min;
            }

            // No value, creating fake via cloning
            assert(v->getType() == Type::getInt32Ty(module->getContext()));
            Instruction * inst = dyn_cast<Instruction>(v);
            assert(inst);
            mappedFake[v] = true;
            mappedMin[v] = inst->clone();

            return mappedMin[v];

        }

        Value * getMaxValue(Value * v, Instruction * i) {
            if (isa<UndefValue>(v))
                return v;

            if (isa<Constant>(v) && !isa<GlobalVariable>(v))
                return v;

            if (mappedMax.count(v))
                return mappedMax[v];

            if (mappedValue.count(v)) {
                Value * mapped = mappedValue[v];
                assert(mapped->getType() == PointerType::getUnqual(pairOfInts));

                const Type * type = Type::getInt32Ty(module->getContext());
                vector<Value *> cs;
                Value * c = ConstantInt::get(type, 0);
                cs.push_back(c);
                c = ConstantInt::get(type, 1);
                cs.push_back(c);

                Value * addr = GetElementPtrInst::Create
                    <vector<Value *>::iterator>(
                        mapped,
                        cs.begin(),
                        cs.end(),
                        "max_a", i);

                Value * max = new LoadInst(addr, "max", i);

                return max;
            }

            // No value, creating fake via cloning
            assert(v->getType() == Type::getInt32Ty(module->getContext()));
            Instruction * inst = dyn_cast<Instruction>(v);
            assert(inst);
            mappedFake[v] = true;
            mappedMax[v] = inst->clone();

            return mappedMax[v];
        }

        Value * getValueProper(Value * v, Instruction *) {
            if (isa<Constant>(v))
                return v;

            assert(mappedValueProper.count(v));
            return mappedValueProper[v];
        }

        Value * getValueDefined(Value * v, Instruction *) {
            if (isa<Constant>(v))
                return ConstantInt::getTrue(module->getContext());

            assert(mappedValueDefined.count(v));
            return mappedValueDefined[v];
        }

        void visitLoadInst(LoadInst &) {}
        void storeToField(Value *, Value *,
                int, const string &, Instruction *) {}
        Value * loadField(Value *, int, Instruction *) {
            return 0;
        }
        void visitStoreInst(StoreInst &) {}
        //void visitSetCondInst(SetCondInst &);
        void visitBranchInst(BranchInst & branch) {
            if (! branch.isConditional())
                return;
            outs() << "Processing branch: " << & branch << "\n";

            if (newlyAdded.count(& branch))
                return;

            Value * cond =  branch.getCondition();

            assert(mappedKind[cond] = MappedToValueAndFlag);
            // The branch should be taken either condition is true
            // or not strictly defined so
            Value * notSure = BinaryOperator::Create(
                    Instruction::Xor, getValueDefined(cond, &branch),
                    ConstantInt::getTrue(module->getContext()),
                    cond->getName() + "_notSure",
                    &branch);

            Value * shouldBeTaken = BinaryOperator::Create(
                    Instruction::Or, getValueProper(cond, &branch),
                    notSure, cond->getName() + "_shouldBeTaken",
                    &branch);

            Value * nv = BranchInst::Create(branch.getSuccessor(0),
                    branch.getSuccessor(1), shouldBeTaken, branch.getParent());
            newlyAdded.insert(nv);
            branch.eraseFromParent();

        }
        
        void visitAdd(BinaryOperator & op) {
            LLVMContext & context = module->getContext();
            assert(op.getType() == Type::getInt32Ty(context));

            Value * min = BinaryOperator::Create(Instruction::Add,
                    getMinValue(op.getOperand(0), &op),
                    getMinValue(op.getOperand(1), &op),
                    op.getName() + "_min", &op);

            Value * max = BinaryOperator::Create(Instruction::Add,
                    getMinValue(op.getOperand(0), &op),
                    getMinValue(op.getOperand(1), &op),
                    op.getName() + "_max", &op);

            mappedKind[&op] = MappedToPair;
            mappedMin[&op] = min;
            mappedMax[&op] = max;
        }

        void visitSub(BinaryOperator & op) {
            LLVMContext & context = module->getContext();
            assert(op.getType() == Type::getInt32Ty(context));

            Value * min = BinaryOperator::Create(Instruction::Sub,
                    getMinValue(op.getOperand(0), &op),
                    getMinValue(op.getOperand(1), &op),
                    op.getName() + "_min", &op);

            Value * max = BinaryOperator::Create(Instruction::Sub,
                    getMinValue(op.getOperand(0), &op),
                    getMinValue(op.getOperand(1), &op),
                    op.getName() + "_max", &op);

            mappedKind[&op] = MappedToPair;
            mappedMin[&op] = min;
            mappedMax[&op] = max;
        }
        
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

    visit(M);

    for (int i = 0; i < toDeleteLater.size(); i++) {
        toDeleteLater[i]->replaceAllUsesWith(ConstantInt::getSigned(
                    Type::getInt32Ty(context), 0));
        delete toDeleteLater[i];
    }

    M.dump();

    return true;
}

 
