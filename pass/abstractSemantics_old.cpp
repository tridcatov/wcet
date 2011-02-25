#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/SymbolTable.h"
#include "llvm/Support/InstVisitor.h"

#include <iostream>

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
    struct Marker : public ModulePass, public InstVisitor<Marker>
    {        
        Marker() : merge2_function_(0), merge_out2_function_(0)
        {
        }
        
        
#if 0
        const Type* ai_type(const Type* t)
        {
            if (t->isPrimitiveType())
            {
                if (t == Type::IntTy)
                {
                    std::vector<const Type*> types;
                    types.push_back(Type::IntTy);
                    types.push_back(Type::IntTy);
                    const Type* pair_of_ints = StructType::get(types);
                    
                    M.getSymbolTable().insert("pair_of_ints", pair_of_ints);
                    return pair_of_ints;
                }
                else if (t == Type::BoolTy)
                {
                    types.clear();
                    types.push_back(Type::BoolTy);
                    types.push_back(Type::BoolTy);
                    pair_of_bools = StructType::get(types);

                    M.getSymbolTable().insert("pair_of_bools", pair_of_bools);
                }
            }
            
        }
#endif                
        
        
        
        bool runOnModule(Module &M);
        
        Module* M_;
        
        StructType* pair_of_ints;
        StructType* pair_of_bools;

        std::map<Value*, mapping_kind_t> mapped_kind;
        std::map<Value*, Value*> mapped_value;

        std::map<Value*, Value*> mapped_value_proper;
        std::map<Value*, Value*> mapped_value_defined;

        std::map<Value*, Value*> mapped_min;
        std::map<Value*, Value*> mapped_max;
        
        std::map<Value*, bool> mapped_fake;
        
        mapping_kind_t get_mapping_kind(Value* v)
        {
            if (isa<Constant>(v)) {
                if (v->getType() == Type::IntTy) {
                    return MappedToPair;
                } 
                else if (v->getType() == Type::BoolTy) {
                    return MappedToValueAndFlag;
                }
            }
            
            return mapped_kind[v];
        }

        /// Get's value representing minimum value of 'v',
        /// suitable for use by 'IB'
        Value* get_min_value(Value* v, Instruction* IB)
        {
            if (isa<UndefValue>(v))
                return v;
            
            if (isa<Constant>(v) && !isa<GlobalVariable>(v))
                return v;
            
            if (mapped_min.count(v))
                return mapped_min[v];
            
            
            if (mapped_value.count(v))
            {            
                Value* mapped = mapped_value[v];
                
                assert(mapped->getType() == PointerType::get(pair_of_ints));
                
                Value* addr = new GetElementPtrInst(mapped,
                        
                        ConstantUInt::get(Type::UIntTy, 0),
                        ConstantUInt::get(Type::UIntTy, 0),
                        "min_a", IB);
                
                Value* min = new LoadInst(addr, "min", IB);
                
                return min;
            }
            
            // Oops, no value yet at all. Create 'fake' values by cloning the existing ones.
            assert(v->getType() == Type::IntTy);
            Instruction* inst = dyn_cast<Instruction>(v);
            assert(inst);
            mapped_fake[v] = true;
            mapped_min[v] = inst->clone();

            return mapped_min[v];
        }

        /// Get's value representing maximum value of 'v',
        /// suitable for use by 'IB'
        Value* get_max_value(Value* v, Instruction* IB)
        {
            if (isa<UndefValue>(v))
                return v;
            
            if (isa<Constant>(v) && !isa<GlobalVariable>(v))
                return v;
            
            if (mapped_max.count(v))
                return mapped_max[v];
            
            
            if (mapped_value.count(v))
            {            
                Value* mapped = mapped_value[v];
                
                assert(mapped->getType() == PointerType::get(pair_of_ints));
                
                Value* addr = new GetElementPtrInst(mapped,
                        
                        ConstantUInt::get(Type::UIntTy, 0),
                        ConstantUInt::get(Type::UIntTy, 1),
                        "max_a", IB);
                
                Value* max = new LoadInst(addr, "max", IB);
                
                return max;
            }
            
            // Oops, no value yet at all. Create 'fake' values by cloning the existing ones.
            assert(v->getType() == Type::IntTy);
            Instruction* inst = dyn_cast<Instruction>(v);
            assert(inst);
            mapped_fake[v] = true;
            mapped_max[v] = inst->clone();
            return mapped_max[v];
        }

        Value* get_value_proper(Value* v, Instruction* IB)
        {
            if (isa<Constant>(v))
                return v;

            assert(mapped_value_proper.count(v));
            return mapped_value_proper[v];
        }

        Value* get_value_defined(Value* v, Instruction* IB)
        {
            if (isa<Constant>(v))
                return ConstantBool::get(true);

            assert(mapped_value_defined.count(v));
            return mapped_value_defined[v];
        }

        void visitLoadInst(LoadInst& load)
        {
            // See what we're loading.
            Value* op = load.getOperand(0);
            if (isa<GlobalVariable>(op) || isa<AllocaInst>(op))
            {
                assert(mapped_kind[op] == MappedToPair);

                Value* mapped = mapped_value[op];
                assert(mapped);

                Value* min_a = new GetElementPtrInst(
                    mapped, 
                    ConstantUInt::get(Type::UIntTy, 0), 
                    ConstantUInt::get(Type::UIntTy, 0), 
                    op->getName() + "_min_a", &load);

                Value* min = new LoadInst(min_a, op->getName() + "_min", 
                                          &load);

                Value* max_a = new GetElementPtrInst(
                    mapped, 
                    ConstantUInt::get(Type::UIntTy, 0), 
                    ConstantUInt::get(Type::UIntTy, 1), 
                    op->getName() + "_max_a", &load);

                Value* max = new LoadInst(max_a, op->getName() + "_max", 
                                          &load);

                mapped_kind[&load] = MappedToPair;
                mapped_min[&load] = min;
                mapped_max[&load] = max;

//                load.eraseFromParent();
            }
            else
            {
                // Load from pointer, it seems. For now, don't do any analysis of pointers
                // and assume the range is undefined.
                
                //load.dump();
                //assert(false && "unsupported operand type");
                mapped_kind[&load] = MappedToPair;
                mapped_min[&load] = ConstantSInt::get(Type::IntTy, -10000);
                mapped_max[&load] = ConstantSInt::get(Type::IntTy, 10000);
            }
        }

        void storeToField(Value* what, Value* where, 
                          int field_number,
                          const std::string& name, Instruction* IB)
        {
            Value* address = new GetElementPtrInst(
                where, 
                ConstantUInt::get(Type::UIntTy, 0), 
                ConstantUInt::get(Type::UIntTy, field_number), 
                where->getName() + name, IB);

            new StoreInst(what, address, IB);
        }
        
        Value* loadField(Value* pointer_to_struct, int field_number,
                         Instruction* IB)
        {
            Value* address = new GetElementPtrInst(pointer_to_struct,
                    ConstantUInt::get(Type::UIntTy, 0), 
                    ConstantUInt::get(Type::UIntTy, field_number), "", IB);
            
            return new LoadInst(address, "", IB);
        }

        void visitStoreInst(StoreInst& store)
        {
            Value* address = store.getOperand(1);
            if (GlobalVariable* var = dyn_cast<GlobalVariable>(address))
            {
                if (mapped_kind[var] == MappedToPair)
                {
                    Value* target = mapped_value[var];
                    Value* source = store.getOperand(0);

                    storeToField(get_min_value(source, &store),
                                 target, 0, "_min", &store);
                    storeToField(get_max_value(source, &store),
                                 target, 1, "_max", &store);

                }
                else
                {
                    assert(var->getType()->getElementType() == Type::BoolTy);
                    
                    Value* target_m = mapped_value[var];
                    
                    Value* source = store.getOperand(0);
                    
                    Value* source_value = 
                        get_value_proper(source, &store);
                    Value* source_defined = get_value_defined(source, 
                                                                      &store);
                    
                    
                    storeToField(source_value, target_m, 0, 
                                 "_val_a", &store);
                    storeToField(source_defined, target_m, 1, 
                                 "_val_def_a", &store);
                }

                store.eraseFromParent();
            }
            
        }

        void visitSetCondInst(SetCondInst& setcc)
        {
            Value* first = setcc.getOperand(0);
            Value* second = setcc.getOperand(1);
            
            if (setcc.getOpcode() == Instruction::SetLT ||
                setcc.getOpcode() == Instruction::SetGT)
            {
                if (setcc.getOpcode() == Instruction::SetGT)
                {
                    std::swap(first, second);
                }
                
                Value* certainly_true = new SetCondInst(Instruction::SetLT,
                        get_max_value(setcc.getOperand(0), &setcc),
                        get_min_value(setcc.getOperand(1), &setcc),
                        "certainly_true", &setcc);
                
                Value* certainly_false = new SetCondInst(Instruction::SetGE,
                        get_min_value(setcc.getOperand(0), &setcc),
                        get_max_value(setcc.getOperand(1), &setcc),
                        "certainly_false", &setcc);
                
                Value* defined = BinaryOperator::create(Instruction::Or, 
                        certainly_true, certainly_false, "defined", 
                        &setcc);
                
                mapped_kind[&setcc] = MappedToValueAndFlag;
                mapped_value_proper[&setcc] = certainly_true;
                mapped_value_defined[&setcc] = defined;
            }            
            else 
            {
                mapped_kind[&setcc] = MappedToValueAndFlag;
                mapped_value_proper[&setcc] = ConstantBool::get(false);
                mapped_value_defined[&setcc] = ConstantBool::get(false);
            }
                
                        
#if 0            
            assert(isa<ConstantIntegral>(setcc.getOperand(1)));
            assert(dyn_cast<ConstantIntegral>(setcc.getOperand(1))->getRawValue() == 0);

            assert(mapped_kind[setcc.getOperand(0)] == MappedToPair);

            Value* certainly_true = new SetCondInst(
                Instruction::SetLT,
                get_max_value(setcc.getOperand(0), &setcc),
                setcc.getOperand(1), "certainly_true", &setcc);

            Value* certainly_false = new SetCondInst(
                Instruction::SetGE,
                get_min_value(setcc.getOperand(0), &setcc),
                setcc.getOperand(1), "certainly_false", &setcc);

            Value* defined = BinaryOperator::create(
                Instruction::Or, certainly_true, certainly_false, "defined", 
                &setcc);
                                                            

            mapped_value_proper[&setcc] = certainly_true;
            mapped_value_defined[&setcc] = defined;
#endif            

            //setcc.eraseFromParent();
        }
        
        void visitBranchInst(BranchInst& branch)
        {
            if (!branch.isConditional())
                return;
            
            cout << "Processing branch: " << &branch << "\n";
            if (newly_added.count(&branch))
                return;
            
            Value* cond = branch.getCondition();
            
            assert(mapped_kind[cond] = MappedToValueAndFlag);
            // The branch should be taken either if the condition is true,
            // or we're not sure.
            Value* not_sure = BinaryOperator::create(
                    Instruction::Xor, get_value_defined(cond, &branch),
            ConstantBool::get(true), cond->getName() + "_not_sure", &branch);
            
            Value* should_be_taken = BinaryOperator::create(
                    Instruction::Or, get_value_proper(cond, &branch),
            not_sure, cond->getName() + "_should_be_taken", &branch);
            
            Value* nv = new BranchInst(branch.getSuccessor(0), branch.getSuccessor(1),
                           should_be_taken, branch.getParent());
            
            newly_added.insert(nv);
            branch.eraseFromParent();
        }
        
        void visitAdd(BinaryOperator& op)
        {
            assert(op.getType() == Type::IntTy);
            
            Value* min = BinaryOperator::create(Instruction::Add, 
                    get_min_value(op.getOperand(0), &op),
                    get_min_value(op.getOperand(1), &op),
                    op.getName() + "_min", &op);            
            
            Value* max = BinaryOperator::create(Instruction::Add, 
                    get_max_value(op.getOperand(0), &op),
                    get_max_value(op.getOperand(1), &op),
                    op.getName() + "_max", &op);
           
            
            mapped_kind[&op] = MappedToPair;
            mapped_min[&op] = min;
            mapped_max[&op] = max;
        }

        void visitSub(BinaryOperator& op)
        {
            assert(op.getType() == Type::IntTy);
            
            
            Value* min = BinaryOperator::create(
                    Instruction::Sub, 
            get_min_value(op.getOperand(0), &op),
            get_max_value(op.getOperand(1), &op),
            op.getName() + "_min", &op);            
            
            Value* max = BinaryOperator::create(
                Instruction::Sub, 
                get_max_value(op.getOperand(0), &op),
                get_min_value(op.getOperand(1), &op),
                op.getName() + "_max", &op);
            
            mapped_kind[&op] = MappedToPair;
            mapped_min[&op] = min;
            mapped_max[&op] = max;
        }
        
        void visitShiftInst(ShiftInst& shift)
        {
            assert(shift.getOpcode() == Instruction::Shr);
            
            ConstantIntegral* c = dyn_cast<ConstantIntegral>(shift.getOperand(1));
            
            assert(c);
            
            Value* v = shift.getOperand(0);
            
            mapped_kind[&shift] = MappedToPair;
            mapped_min[&shift] = new ShiftInst(Instruction::Shr, 
                                            get_min_value(v, &shift), c, 
                                            shift.getName() + "_min", &shift);
            
            mapped_max[&shift] = new ShiftInst(Instruction::Shr, 
                                            get_max_value(v, &shift), c, 
                                            shift.getName() + "_max", &shift);
        }
        
        void visitPHINode(PHINode& op)
        {
            // The PHI nodes are two two kinds:
            // Those that merge values modified inside a basic block
            // - Those that set 'basic block executed' flag.
            
            assert(op.getNumIncomingValues() == 2);
            
            if (op.getType() == Type::IntTy)
            {
                mapped_kind[&op] = MappedToPair;
                
                PHINode* min = new PHINode(op.getType(), op.getName() + "_min", &op);
                min->addIncoming(get_min_value(op.getIncomingValue(0), &op),
                                op.getIncomingBlock(0));
                min->addIncoming(get_min_value(op.getIncomingValue(1), &op),
                                op.getIncomingBlock(1));
                
                PHINode* max = new PHINode(op.getType(), op.getName() + "_max", &op);
                max->addIncoming(get_max_value(op.getIncomingValue(0), &op),
                                op.getIncomingBlock(0));
                max->addIncoming(get_max_value(op.getIncomingValue(1), &op),
                                op.getIncomingBlock(1));
                
                mapped_min[&op] = min;
                mapped_max[&op] = max;
                
                return;
            }
            
            //assert(isa<ConstantIntegral>(op.getIncomingValue(0)));
            //assert(isa<ConstantIntegral>(op.getIncomingValue(1)));
            
            mapped_kind[&op] = MappedToValueAndFlag;
            mapped_value_proper[&op] = &op;
            mapped_value_defined[&op] = ConstantBool::get(true);
        }
        
        void visitXor(BinaryOperator& op)
        {
            visitLogical(op);
        }
        
        void visitOr(BinaryOperator& op)
        {
            visitLogical(op);
        }
        
        void visitLogical(BinaryOperator& op)
        {
            mapped_kind[&op] = MappedToValueAndFlag;
            
            Value* first = op.getOperand(0);
            Value* second = op.getOperand(1);
                                   
            Value* first_defined = get_value_defined(first, &op);
            Value* second_defined = get_value_defined(second, &op);
            
            Value* defined = BinaryOperator::create(Instruction::And,
                    first_defined, second_defined, op.getName() + "defined", &op);
            
            mapped_value_defined[&op] = defined;
            
            Value* first_proper = get_value_proper(first, &op);
            Value* second_proper = get_value_proper(second, &op);
            // In-place change op to use mapped proper value.
            op.setOperand(0, first_proper);
            op.setOperand(1, second_proper);
            
            mapped_value_proper[&op] = &op;
            mapped_value_defined[&op] = defined;
            
        }
        
        void visitAllocaInst(AllocaInst& alloca)
        {
            assert(alloca.getAllocatedType() == Type::IntTy);
            
            Value* pair = new AllocaInst(pair_of_ints, 0, alloca.getName() + "_pair", &alloca);
            
            mapped_kind[&alloca] = MappedToPair;
            mapped_value[&alloca] = pair;
        }
        
        
        void visitCallInst(CallInst& call)
        {       
            std::string name = call.getCalledFunction()->getName();
            
            Value* fake = 0;
            if (name == "update_value_for_true_branch")
            {
                mapped_kind[&call] = MappedToValueAndFlag;
            
                SetCondInst* setcc = dyn_cast<SetCondInst>(call.getOperand(1));
                if (setcc)
                {
                    mapped_min[&call] = get_min_value(setcc->getOperand(0), &call);
                    mapped_max[&call] = ConstantSInt::get(Type::IntTy, -1);
                }
                else
                {
                    mapped_min[&call] = UndefValue::get(call.getType());
                    mapped_max[&call] = UndefValue::get(call.getType());
                }
                
                fake = BinaryOperator::create(Instruction::Add, 
                                              ConstantSInt::get(Type::IntTy, 1), 
                                              ConstantSInt::get(Type::IntTy, 1), "fake", &call);
            }
            else if (name == "update_value_for_false_branch")
            {
                mapped_kind[&call] = MappedToValueAndFlag;
                
                SetCondInst* setcc = dyn_cast<SetCondInst>(call.getOperand(1));
            
                // Call a helper function to find max of current min value of
                // setcc->getOperand(0) and 0.
                vector<Value*> params;
                params.push_back(get_min_value(setcc->getOperand(0), &call));
                params.push_back(ConstantSInt::get(Type::IntTy, 0));
                
                
                mapped_min[&call] = new CallInst(max_func_, params, call.getName(), &call);
                mapped_max[&call] = get_max_value(setcc->getOperand(0), &call);
                
                fake = BinaryOperator::create(Instruction::Add, 
                                              ConstantSInt::get(Type::IntTy, 1), 
                                              ConstantSInt::get(Type::IntTy, 1), "fake", &call);
            }
            else if (name == "merge_values")
            {
                // Return value.
                Value* retval = new AllocaInst(pair_of_ints, 0, "retval", &call);
                
                std::vector<Value*> params;
                params.push_back(retval);
                
                // First operand is function, second is num of value, third is control flow
                // flag, fourth is first incoming value.
                
                params.push_back(call.getOperand(1));
                for(unsigned i = 2; i < call.getNumOperands(); i += 2)
                {
                    // Push the 'value valid' flag
                    params.push_back(call.getOperand(i));
                    Value* v = call.getOperand(i+1);
                    
                    //assert(get_mapping_kind(v) == MappedToPair);
                    
                    params.push_back(get_min_value(v, &call));
                    params.push_back(get_max_value(v, &call));
                }
                
                new CallInst(demand_merge2_function(), params, "", &call);
                
                Value* ret_min = loadField(retval, 0, &call);
                Value* ret_max = loadField(retval, 1, &call);
                
                mapped_kind[&call] = MappedToPair;
                mapped_min[&call] = ret_min;
                mapped_max[&call] = ret_max;
                                
                fake = BinaryOperator::create(BinaryOperator::Add,
                        call.getOperand(1), ConstantSInt::get(Type::IntTy, 1), "fake", &call);
                
            }
            else if (name == "merge_out_value")
            {
                Value* out = call.getOperand(1);
                Value* new_val = call.getOperand(2);
                Value* first_iter = call.getOperand(3);
                
                Value* mapped_out = mapped_value[out];
                assert(mapped_out);
                
                std::vector<Value*> params;
                params.push_back(mapped_out);
                params.push_back(get_min_value(new_val, &call));
                params.push_back(get_max_value(new_val, &call));
                // Note that there's no 'defined' flag, the 'first_iter'
                // is always defined because it's internal variable.
                params.push_back(first_iter);
                
                new CallInst(demand_merge_out2_function(), params, "", &call);
                
                // This is void return, so it's not used at all. Just remove.
                call.eraseFromParent();
                return;
            }
            else
            {
                assert(false && "non-builtin function");
            }
            
            
                
            mapped_kind[fake] = MappedToPair;
            mapped_min[fake] = mapped_min[&call];
            mapped_max[fake] = mapped_max[&call];
                
            call.replaceAllUsesWith(fake);
            call.eraseFromParent();
            
        }
            
        Function* demand_merge2_function()
        {
            if (!merge2_function_)
            {
                std::vector<const Type*> param_types;
                // Pointer to pair, to simulate structure return
                param_types.push_back(PointerType::get(pair_of_ints));
                param_types.push_back(Type::IntTy);
                llvm::FunctionType* ft = FunctionType::get(Type::VoidTy, param_types, true);
                
                merge2_function_ = M_->getOrInsertFunction("merge2", ft);
            }
            return merge2_function_;
        }
        
        Function* demand_merge_out2_function()
        {
            if (!merge_out2_function_)
            {
                std::vector<const Type*> param_types;
                param_types.push_back(PointerType::get(pair_of_ints));
                param_types.push_back(Type::IntTy);
                param_types.push_back(Type::IntTy);
                param_types.push_back(Type::BoolTy);
                llvm::FunctionType* ft = FunctionType::get(Type::VoidTy, param_types, true);
                
                merge_out2_function_ = M_->getOrInsertFunction("merge_out2", ft);
            }
            return merge_out2_function_;
        }

        
        
        void visit(Instruction &I) 
        {
            if (newly_added.count(&I))
                return;
            
            bool fake = mapped_fake[&I];
            Value* fake_min = mapped_min[&I];
            Value* fake_max = mapped_max[&I];
            
            InstVisitor<Marker>::visit(I);
            
            
            if (fake)
            {
                mapped_fake[&I] = false;
                fake_min->replaceAllUsesWith(mapped_min[&I]);
                fake_max->replaceAllUsesWith(mapped_max[&I]);
                delete fake_min;
                delete fake_max;
                
            }
        }
        using InstVisitor<Marker>::visit;
        
        
        std::set<Value*> newly_added;
        Function* merge2_function_;
        Function* merge_out2_function_;
        
        Function* max_func_;
        
        std::vector<Value*> to_delete_later;
    };
    
    RegisterOpt<Marker> X("abstract-semantics",
                          "Introducs abstract semantics");
}

bool Marker::runOnModule(Module &M)
{
    max_func_ = M.getOrInsertFunction("max", Type::IntTy, Type::IntTy, Type::IntTy, 0);
    
    
    M_ = &M;
    std::vector<const Type*> types;
    types.push_back(Type::IntTy);
    types.push_back(Type::IntTy);
    pair_of_ints = StructType::get(types);

    M.getSymbolTable().insert("pair_of_ints", pair_of_ints);

    types.clear();
    types.push_back(Type::BoolTy);
    types.push_back(Type::BoolTy);
    pair_of_bools = StructType::get(types);

    M.getSymbolTable().insert("pair_of_bools", pair_of_bools); 

    {

        std::vector<GlobalVariable*> values_to_process;

        /* Handle globals. */
        Module::global_iterator i, e;
        for(i = M.global_begin(), e = M.global_end(); i != e; ++i)
        {
            const PointerType* t = i->getType();
            const Type* el = t->getElementType();

            el->dump();

            if (isa<FunctionType>(el))
                continue;

            // Values of other types are left along, and attempts
            // to read it will produce unknown values
            if (el->isPrimitiveType() &&
                (el == Type::IntTy || el == Type::BoolTy))
                values_to_process.push_back(i);
        }


        for(unsigned i = 0; i < values_to_process.size(); ++i)
        {
            GlobalVariable* v = values_to_process[i];

            Constant* init = v->getInitializer();
            std::vector<Constant*> values;
            values.push_back(init);
            values.push_back(init);

            const StructType* new_type = 0;
            const Type* elementType = v->getType()->getElementType();
            if (elementType == Type::IntTy)
                new_type = pair_of_ints;
            else if (elementType == Type::BoolTy)
                new_type = pair_of_bools;

            assert(new_type);

            Constant* new_init = ConstantStruct::get(new_type, values);

            mapping_kind_t kind = (elementType == Type::IntTy) ? 
                MappedToPair : MappedToValueAndFlag;

            mapped_kind[v] = kind;
            
            GlobalVariable* nv = new GlobalVariable(
                new_type, 
                v->isConstant(),
                v->getLinkage(),
                new_init,
                v->getName(), v->getParent());

            mapped_value[v] = nv;
        }
    }

    

    Module::iterator i, e;
    for(i = M.begin(), e = M.end(); i != e; ++i)
    {
        if (i->isExternal())
            continue;
        
        
        
        const FunctionType* ft = i->getFunctionType();
        for(unsigned j = 0; j < ft->getNumParams(); ++j)
        {
            const Type* pt = ft->getParamType(j);
            assert(pt == Type::IntTy);
            
            Value* prev_arg = i->change_arg_type(j, PointerType::get(pair_of_ints));
            
            mapped_kind[prev_arg] = MappedToPair;
            Instruction* IB = i->front().getFirstNonPHI();            
            Function::ArgumentListType::iterator arg = i->getArgumentList().begin();
            advance(arg, j);
            newly_added.insert(mapped_min[prev_arg] = loadField(arg, 0, IB));
            newly_added.insert(mapped_max[prev_arg] = loadField(arg, 1, IB));
            
            to_delete_later.push_back(prev_arg);
        }
    }
    
    visit(M);

    for(unsigned i = 0; i < to_delete_later.size(); ++i)
    {
        to_delete_later[i]->replaceAllUsesWith(ConstantSInt::get(Type::IntTy, 0));
        delete to_delete_later[i];
    }
    
    M.dump();

#if 0
    Module::iterator i, e;
    for(i = M.begin(), e = M.end(); i != e; ++i)
    {
        i->change_arg_type(0, Type::FloatTy);
    }
#endif

    return true;
}


    
