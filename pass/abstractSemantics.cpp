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
    MappedToHellKnowsWhat
};

namespace {

class Marker : public ModulePass, public InstVisitor<Marker>
{    
public:    
    bool runOnModule(Module &M);
    Marker(): ModulePass(ID), merge2Function(0),
    mergeOut2Function(0) {}
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

    Value * getMinValue(Value * value, Instruction * instruction);
    Value * getMaxValue(Value * value, Instruction * instruction);

    Value * getValueProper(Value * v, Instruction *) {
        if (isa<Constant>(v))
            return v;

        // assert(mappedValueProper.count(v));
        return mappedValueProper[v];
    }

    Value * getValueDefined(Value * v, Instruction *) {
        if (isa<Constant>(v))
            return ConstantInt::getTrue(module->getContext());

        // assert(mappedValueDefined.count(v));
        return mappedValueDefined[v];
    }

    void visitLoadInst(LoadInst & load);
    void storeToField(Value * what, Value * where,
            int fieldNumber, const string & name,
            Instruction * instruction);
      
    Value * loadField(Value * structPointer,
            int fieldNumber, Instruction * instruction);

    void visitStoreInst(StoreInst & store);
    void visitBranchInst(BranchInst & branch);    
    void visitAdd(BinaryOperator & op);
    void visitSub(BinaryOperator & op);
     
    //void visitShiftInst(ShiftInst & shift);
        
    void visitPHINode(PHINode & op);

    void visitXor(BinaryOperator & op) {
        visitLogical(op);
    }

    void visitOr(BinaryOperator & op) {
        visitLogical(op);
    }
    
    void visitLogical(BinaryOperator & op); 

    void visitAllocaInst(AllocaInst & alloca) {
        // assert(alloca.getAllocatedType() == Type::getInt32Ty(module->getContext()));
        Value * pair = new AllocaInst(pairOfInts, 0, alloca.getName() + "_pair", &alloca);
        mappedKind[&alloca] = MappedToPair;
        mappedValue[&alloca] = pair;
    }

    void visitCallInst(CallInst & call);
    Value * demandMerge2Function() {
        if (! merge2Function) {
            vector<const Type *> paramTypes;
            paramTypes.push_back(PointerType::getUnqual(pairOfInts));
            paramTypes.push_back(Type::getInt32Ty(module->getContext()));
            FunctionType * ft = FunctionType::get(
                    Type::getVoidTy(module->getContext()),
                    paramTypes, true);
            module->getOrInsertFunction("merge2", ft);
            merge2Function = module->getFunction("merge2"); 
        }
        return (Value *)merge2Function;
    }

    Value * demandMergeOut2Function() {
        if (! mergeOut2Function) {
            vector<const Type *> paramTypes;
            paramTypes.push_back(PointerType::getUnqual(pairOfInts));
            paramTypes.push_back(Type::getInt32Ty(module->getContext()));
            paramTypes.push_back(Type::getInt32Ty(module->getContext()));
            paramTypes.push_back(Type::getInt1Ty(module->getContext()));
            FunctionType * ft=  FunctionType::get(
                    Type::getVoidTy(module->getContext()),
                    paramTypes, true);
            module->getOrInsertFunction("merge_out2", ft);
            mergeOut2Function = module->getFunction("merge_out2");
        }
        return (Value *)mergeOut2Function;
    }

    void visit(Instruction & i) {
        if (newlyAdded.count(& i))
            return;

        bool fake = mappedFake[&i];
        Value * fakeMin = mappedMin[&i];
        Value * fakeMax = mappedMax[&i];

        InstVisitor<Marker>::visit(i);

        if (fake) {
            mappedFake[&i] = false;
            fakeMin->replaceAllUsesWith(mappedMin[&i]);
            fakeMax->replaceAllUsesWith(mappedMax[&i]);
            delete fakeMin;
            delete fakeMax;
        }
    }
};

char Marker::ID = 0;
INITIALIZE_PASS(Marker,
        "abstract-semantics",
        "Introduces abstract semantics",
        true, true);

}

Value * Marker::getMinValue(Value * v, Instruction * i) {
    if (isa<UndefValue>(v))
        return v;

    if (isa<Constant>(v) && !isa<GlobalVariable>(v))
        return v;

    if (mappedMin.count(v))
        return mappedMin[v];

    if (mappedValue.count(v)) {
        Value * mapped = mappedValue[v];
        assert((mapped->getType() == PointerType::getUnqual(pairOfInts))
                && "Incorrect mapping of value, integer required");

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
    assert((v->getType() == Type::getInt32Ty(module->getContext()))
            && "Couldn't clone value, type mismatch, integer required");
    Instruction * inst = dyn_cast<Instruction>(v);
    assert(inst && "Instruction cast wasn't successfull, possible mapping problems");
    mappedFake[v] = true;
    mappedMin[v] = inst->clone();

    return mappedMin[v];

}

Value * Marker::getMaxValue(Value * v, Instruction * i) {
    if (isa<UndefValue>(v))
        return v;

    if (isa<Constant>(v) && !isa<GlobalVariable>(v))
        return v;

    if (mappedMax.count(v))
        return mappedMax[v];

    if (mappedValue.count(v)) {
        Value * mapped = mappedValue[v];
        assert((mapped->getType() == PointerType::getUnqual(pairOfInts))
                && "Couldn't cast type while mapping, integer required");

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
    // assert(v->getType() == Type::getInt32Ty(module->getContext()));
    Instruction * inst = dyn_cast<Instruction>(v);
    // assert(inst);
    mappedFake[v] = true;
    mappedMax[v] = inst->clone();

    return mappedMax[v];
}

void Marker::visitLoadInst(LoadInst & load) {
    Value * op = load.getOperand(0);
    if (isa<GlobalVariable>(op) || isa<AllocaInst>(op)) {
        // assert(mappedKind[op] == MappedToPair);
        Value * mapped = mappedValue[op];
        // assert(mapped);

        const Type * type = Type::getInt32Ty(module->getContext());
        vector<Value *> cs;
        Value * c = ConstantInt::get(type, 0);
        cs.push_back(c);
        cs.push_back(c);

        Value * min_a = GetElementPtrInst::Create
            <vector<Value *>::iterator>(
                    mapped,
                    cs.begin(),
                    cs.end(),
                    op->getName() + "_min_a", &load);

        Value * min = new LoadInst(min_a,
                op->getName() + "_min", &load);

        cs.clear();
        cs.push_back(c);
        c = ConstantInt::get(type, 1);
        cs.push_back(c);

        Value * max_a = GetElementPtrInst::Create
            <vector<Value *>::iterator>(
                    mapped,
                    cs.begin(),
                    cs.end(),
                    op->getName() + "_max_a", &load);

        Value * max = new LoadInst(max_a,
                op->getName() + "max", &load);

        mappedKind[&load] = MappedToPair;
        mappedMin[&load] = min;
        mappedMax[&load] = max;
    } else {
        // Load from pointer. Assume range undefined
        mappedKind[&load] = MappedToPair;
        const Type * type = Type::getInt32Ty(module->getContext());
        mappedMin[&load] = ConstantInt::get(type, -10000);
        mappedMax[&load] = ConstantInt::get(type, 10000);
    }
}

void Marker::storeToField(Value * what, Value * where,
        int fieldNumber, const string & name,
        Instruction * i) {
    const Type * type = Type::getInt32Ty(module->getContext());
    vector<Value *> cs;
    cs.push_back(ConstantInt::get(type, 0));
    cs.push_back(ConstantInt::get(type, fieldNumber));
    Value * address = GetElementPtrInst::Create
        <vector<Value *>::iterator> (where,
                cs.begin(), cs.end(), where->getName() + name, i);

    new StoreInst(what, address, i);
}

Value * Marker::loadField(Value * structPointer,
        int fieldNumber, Instruction * i) {
    const Type * type = Type::getInt32Ty(module->getContext());
    vector<Value *> cs;
    cs.push_back(ConstantInt::get(type, 0));
    cs.push_back(ConstantInt::get(type, fieldNumber));
    Value * address = GetElementPtrInst::Create
        <vector<Value *>::iterator> (structPointer,
                cs.begin(), cs.end(), "", i);

    return new LoadInst(address, "", i);
}

void Marker::visitStoreInst(StoreInst & store) {
    Value * address = store.getOperand(1);
    if (GlobalVariable * var = dyn_cast<GlobalVariable>(address)) {
        if (mappedKind[var] == MappedToPair) {
            Value * target = mappedValue[var];
            Value * source = store.getOperand(0);

            storeToField(getMinValue(source, &store),
                    target, 0, "_min", &store);
            storeToField(getMaxValue(source, &store),
                    target, 1, "_max", &store);
        } else {
            // assert(var ->getType()->getElementType() ==
            //        Type::getInt1Ty(module->getContext()));
            Value * target = mappedValue[var];
            Value * source = store.getOperand(0);
            Value * sourceValue = getValueProper(source, &store);
            Value * sourceDefined = getValueDefined(source, &store);

            storeToField(sourceValue, target, 0, "_val_a", &store);
            storeToField(sourceDefined, target, 1, "_val_def_a", &store);
        }

        store.eraseFromParent();
    }
}

void Marker::visitBranchInst(BranchInst & branch) {
    if (! branch.isConditional())
        return;
    outs() << "Processing branch: " << & branch << "\n";

    if (newlyAdded.count(& branch))
        return;

    Value * cond =  branch.getCondition();

    // assert(mappedKind[cond] = MappedToValueAndFlag);
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

void Marker::visitAdd(BinaryOperator & op) {
    LLVMContext & context = module->getContext();
    // assert(op.getType() == Type::getInt32Ty(context));

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

void Marker::visitSub(BinaryOperator & op) {
    LLVMContext & context = module->getContext();
    // assert(op.getType() == Type::getInt32Ty(context));

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

void Marker::visitPHINode(PHINode & op) {
    // Merge PHI's and those that set 'bb executed' flag
    // assert(op.getNumIncomingValues() == 2);

    if (op.getType() == Type::getInt32Ty(module->getContext())) {
        mappedKind[&op] = MappedToPair;

        PHINode * min = PHINode::Create(op.getType(),
                op.getName() + "_min", &op);

        min->addIncoming(getMinValue(op.getIncomingValue(0), &op),
                op.getIncomingBlock(0));
        min->addIncoming(getMinValue(op.getIncomingValue(1), &op),
                op.getIncomingBlock(1));

        PHINode * max = PHINode::Create(op.getType(),
                op.getName() + "_max", &op);

        min->addIncoming(getMaxValue(op.getIncomingValue(0), &op),
                op.getIncomingBlock(0));
        min->addIncoming(getMaxValue(op.getIncomingValue(1), &op),
                op.getIncomingBlock(1));

        mappedMin[&op] = min;
        mappedMax[&op] = max;
        return;
    }

    mappedKind[&op] = MappedToValueAndFlag;
    mappedValueProper[&op] = &op;
    mappedValueDefined[&op] = ConstantInt::getTrue(module->getContext());
}

void Marker::visitLogical(BinaryOperator & op) {
    mappedKind[&op] = MappedToValueAndFlag;

    Value * first = op.getOperand(0);
    Value * second = op.getOperand(1);

    Value * firstDefined = getValueDefined(first, &op);
    Value * secondDefined = getValueDefined(second, &op);

    Value * defined = BinaryOperator::Create(Instruction::And,
            firstDefined, secondDefined, op.getName() + "_defined", &op);
    mappedValueDefined[&op] = defined;

    Value * firstProper = getValueProper(first, &op);
    Value * secondProper = getValueProper(second, &op);

    op.setOperand(0, firstProper);
    op.setOperand(1, secondProper);

    mappedValueProper[&op] = &op;
    mappedValueDefined[&op] = defined;
}

void Marker::visitCallInst(CallInst & call) {
    string name = call.getCalledFunction()->getName();
    Value * fake = 0;
    if (name == "updateValueForTrueBranch") {
        mappedKind[&call] = MappedToValueAndFlag; 
        // No SetCondInst so far, merging
        mappedMin[&call] = UndefValue::get(call.getType());
        mappedMax[&call] = UndefValue::get(call.getType());

        fake = BinaryOperator::Create(Instruction::Add,
                ConstantInt::get(Type::getInt32Ty(module->getContext()), 1),
                ConstantInt::get(Type::getInt32Ty(module->getContext()), 1),
                "fake", &call);
    } else if (name == "updateValueForFalseBranch") {
        // TODO: finaly decide what to do with SCI

    } else if (name == "merge_values") {
        Value * retval = new AllocaInst(pairOfInts, 0, "retval", &call);
        vector<Value *> params;
        params.push_back(retval);

        // First - function, second - num of values,
        // third - CF flag, fourth - incoming val
        params.push_back(call.getOperand(1));
        for (int i = 2; i < call.getNumOperands(); i += 2) {
            params.push_back(call.getOperand(i));
            Value * v = call.getOperand(i+1);

            params.push_back(getMinValue(v, &call));
            params.push_back(getMaxValue(v, &call));
        }

        CallInst::Create<vector<Value *>::iterator>(
                demandMerge2Function(), params.begin(),
                params.end(), "", &call);

        Value * retMin = loadField(retval, 0, &call);
        Value * retMax = loadField(retval, 1, &call);

        mappedKind[&call] = MappedToPair;
        mappedMin[&call] = retMin;
        mappedMax[&call] = retMax;

        fake = BinaryOperator::Create(BinaryOperator::Add,
                call.getOperand(1), ConstantInt::get(
                    Type::getInt32Ty(module->getContext()), 1),
                "fake", &call);
    } else if (name == "merge_out_value") {
        Value * out = call.getOperand(1);
        Value * newVal = call.getOperand(2);
        Value * firstIter = call.getOperand(3);

        Value * mappedOut = mappedValue[out];
        // assert(mappedOut);

        vector<Value *> params;
        params.push_back(mappedOut);
        params.push_back(getMinValue(newVal, &call));
        params.push_back(getMaxValue(newVal, &call));
        params.push_back(firstIter);
        CallInst::Create<vector<Value *>::iterator>(
                demandMergeOut2Function(), params.begin(),
                params.end(), "", &call);

        call.eraseFromParent();
        return;
    } else {
        // Which is presumably to be passed always
        // assert(false && "non-builtin function");
    }

    mappedKind[fake] = MappedToPair;
    mappedMin[fake] = mappedMin[&call];
    mappedMax[fake] = mappedMax[&call];

    call.replaceAllUsesWith(fake);
    call.eraseFromParent();
}





bool Marker::runOnModule(Module &M) {
    LLVMContext & context = M.getContext();
    vector<const Type *> params;
    params.push_back(Type::getInt32Ty(context));
    params.push_back(Type::getInt32Ty(context));
    FunctionType * ft = FunctionType::get(
            Type::getInt32Ty(context),
            params,
            true
            );
    M.getOrInsertFunction("max", ft);
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

            // assert(newType);

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

    // visit(M);

    for (int i = 0; i < toDeleteLater.size(); i++) {
        toDeleteLater[i]->replaceAllUsesWith(ConstantInt::getSigned(
                    Type::getInt32Ty(context), 0));
        delete toDeleteLater[i];
    }

    M.dump();

    return true;
}

 
