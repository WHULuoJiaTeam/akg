/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file make_api.cc Build API function.
 */

/*
 * 2019.12.30 - Define new function to push buffer node from api_args to args_real.
 * 2021.11.01 - Remove redundant asserter.
 */

#include <tvm/ir_pass.h>
#include <tvm/ir.h>
#include <tvm/ir_visitor.h>
#include <tvm/ir_mutator.h>
#include <tvm/buffer.h>
#include <tvm/runtime/device_api.h>
#include <vector>
#include <utility>
#include <unordered_set>

#include "ir_util.h"
#include "arg_binder.h"

namespace air {
namespace ir {

inline Stmt MakeAssertEQ(Expr lhs, Expr rhs, std::string msg) {
  return AssertStmt::make(lhs == rhs, msg, Evaluate::make(0));
}

Array<Var> Param (const Array<NodeRef> &api_args, Array<Var> args_real) {
  for (const auto& arg : api_args) {
    if (const auto v = arg.as<BufferNode>()) {
      args_real.push_back(v->data);
    } else if (arg.as<Variable>()) {
      args_real.push_back(air::Downcast<Var>(arg));
    } else {
      LOG(FATAL) << "Unknown arg " << arg;
    }
  }
  return args_real;
}

LoweredFunc MakeAPI(Stmt body,
                    const std::string &name,
                    Array<NodeRef> api_args,
                    int num_unpacked_args,
                    bool is_restricted) {
  const Stmt nop = Evaluate::make(0);
  int num_args = static_cast<int>(api_args.size());
  Array<Var> args_real;
  args_real = Param (api_args, args_real);
  CHECK_LE(num_unpacked_args, num_args);
  int num_packed_args = num_args - num_unpacked_args;
  // Data field definitions
  // The packed fields
  Var v_packed_args("args", Handle());
  Var v_packed_arg_type_ids("arg_type_ids", Handle());
  Var v_num_packed_args("num_args", Int(32));
  // The arguments of the function.
  Array<Var> args;
  // The device context
  Var device_type("dev_type"), device_id("dev_id");
  // seq_init gives sequence of initialization
  // seq_check gives sequence of later checks after iniit
  std::vector<Stmt> seq_init, seq_check;
  std::unordered_map<const Variable*, Expr> vmap;
  ArgBinder binder(&vmap);
  // ---------------------------
  // local function defintiions
  // load i-th argument as type t
  auto f_arg_value = [&](Type t, int i) {
    Array<Expr> call_args{v_packed_args,
                          IntImm::make(Int(32), i),
                          IntImm::make(Int(32), intrinsic::kTVMValueContent)};
    // load 64 bit version
    Type api_type = APIType(t);
    Expr res = Call::make(
        api_type, intrinsic::tvm_struct_get, call_args,
        Call::PureIntrinsic);
    // cast to the target version.
    if (api_type != t) {
      res = Cast::make(t, res);
    }
    return res;
  };
  // get declaration of argument i
  auto f_arg_decl = [&](int i) {
    std::ostringstream os;
    os << "arg" << i;
    const Variable* v = api_args[i].as<Variable>();
    return Var(os.str(), v ? v->type: Handle());
  };
  // ---------------------------
  // start of logics
  // add signiture for packed arguments.
  if (num_packed_args != 0) {
    args.push_back(v_packed_args);
    args.push_back(v_packed_arg_type_ids);
    args.push_back(v_num_packed_args);
    std::ostringstream os;

    os << name << ": num_args should be " << num_packed_args;
    seq_init.emplace_back(
        MakeAssertEQ(v_num_packed_args, num_packed_args, os.str()));
  }

  // Save the input variables and buffers that will be bound later.
  std::vector<std::pair<Var, Var> > var_defs;
  std::vector<std::pair<Buffer, Var> > buf_defs;
  for (int i = 0; i < static_cast<int>(api_args.size()); ++i) {
    Var v_arg = f_arg_decl(i);
    if (i < num_packed_args) {
      // Value loads
      seq_init.emplace_back(LetStmt::make(
          v_arg, f_arg_value(v_arg.type(), i), nop));
      // type code checks
      Var tcode(v_arg->name_hint + ".code", Int(32));
      seq_init.emplace_back(LetStmt::make(
          tcode, Load::make(
              Int(32), v_packed_arg_type_ids, IntImm::make(Int(32), i), const_true(1)),
          nop));
      Type t = v_arg.type();
      if (t.is_handle()) {
        std::ostringstream msg;
        msg << name << ": Expect arg[" << i << "] to be pointer";
        seq_check.emplace_back(
            AssertStmt::make(tcode == kHandle ||
                             tcode == kNDArrayContainer ||
                             tcode == kArrayHandle ||
                             tcode == kNull, msg.str(), nop));
      } else if (t.is_int() || t.is_uint()) {
        std::ostringstream msg;
        msg << name << ": Expect arg[" << i << "] to be int";
        seq_check.emplace_back(AssertStmt::make(tcode == kDLInt, msg.str(), nop));
      } else {
        CHECK(t.is_float());
        std::ostringstream msg;
        msg << name << ": Expect arg[" << i << "] to be float";
        seq_check.emplace_back(
            AssertStmt::make(tcode == kDLFloat, msg.str(), nop));
      }
    } else {
      args.push_back(v_arg);
    }
    // add checks for functions.
    if (api_args[i].as<Variable>()) {
      var_defs.emplace_back(std::make_pair(Downcast<Var>(api_args[i]), v_arg));
    } else {
      // Buffer checks
      CHECK(api_args[i].as<BufferNode>())
          << "api_args can only be Buffer or Var";
      buf_defs.emplace_back(std::make_pair(Downcast<Buffer>(api_args[i]), v_arg));
    }
  }

  // Arg definitions are defined before buffer binding to avoid the use before
  // def errors.
  //
  // For example, for auto broadcasting, checks are required to guarantee that
  // either 0 or the original stride will be correctly used. Checks here have
  // to use the args that may have no let bining yet. Therefore, hoisting let
  // binding for args before buffer declaration is needed.
  for (const auto& arg : var_defs) {
    binder.Bind(arg.first, arg.second, arg.second->name_hint, true);
  }

  for (const auto& buf_arg : buf_defs) {
    binder.BindDLTensor(buf_arg.first, device_type, device_id,
                        buf_arg.second, buf_arg.second->name_hint);
  }

  NodePtr<LoweredFuncNode> n = make_node<LoweredFuncNode>();
  n->name = name;
  n->args = args;
  n->args_real = args_real;
  n->handle_data_type = binder.def_handle_dtype();
  n->is_packed_func = num_unpacked_args == 0;
  n->is_restricted = is_restricted;
  body = AttrStmt::make(
      make_zero(Int(32)), attr::compute_scope,
      StringImm::make(name + "_compute_"), body);
  // Set device context
  if (vmap.count(device_id.get())) {
    Expr node = StringImm::make("default");
    CHECK(vmap.count(device_type.get()));
    seq_check.push_back(AttrStmt::make(
        node, attr::device_context_id, device_id, nop));
    seq_check.push_back(AttrStmt::make(
        node, attr::device_context_type, device_type, nop));
    Stmt set_device = IfThenElse::make(
        device_type != kDLCPU, Evaluate::make(Call::make(
            Int(32), intrinsic::tvm_call_packed,
            {StringImm::make(runtime::symbol::tvm_set_device),
             device_type, device_id}, Call::Intrinsic)));
    body = Block::make(set_device, body);
  }
  n->body = MergeNest(
      {seq_init, binder.init_nest(), seq_check}, body);
  LoweredFunc f(n);
  Array<Var> undefined = UndefinedVars(f->body, f->args);
  if (undefined.size() != 0) {
    std::ostringstream os;
    for (Var v : undefined) {
      os << " \'" << v->name_hint << "\' ";
    }
    os << " does not appear in api_args";
    LOG(FATAL) << "Not all Vars are passed in api_args: " << os.str();
  }
  return f;
}

class DeviceTypeBinder: public IRMutator {
 public:
  explicit DeviceTypeBinder(int device_type)
      : device_type_(device_type) {}

  Stmt Mutate_(const AttrStmt* op, const Stmt& s) final {
    if (op->attr_key == attr::device_context_type) {
      if (const Variable* var = op->value.as<Variable>()) {
        var_ = var;
        Expr value = make_const(op->value.type(), device_type_);
        Stmt body = IRMutator::Mutate_(op, s);
        var_ = nullptr;
        std::ostringstream os;
        os << "device_type need to be " << device_type_;
        return AssertStmt::make(op->value == value, os.str(), body);
      }
    }
    return IRMutator::Mutate_(op, s);
  }

  Stmt Mutate_(const IfThenElse* op, const Stmt& s) final {
    // eager simplify if guard.
    Stmt res = IRMutator::Mutate_(op, s);
    op = res.as<IfThenElse>();
    if (is_zero(op->condition)) {
      if (op->else_case.defined()) return op->else_case;
      return Evaluate::make(0);
    }
    if (is_one(op->condition)) {
      return op->then_case;
    }
    return res;
  }

  Expr Mutate_(const NE* op, const Expr& e) final {
    // eager check NE for device check
    Expr res = IRMutator::Mutate_(op, e);
    op = res.as<NE>();
    if (ir::Equal(op->a, op->b)) {
      return make_const(op->type, false);
    }
    return res;
  }

  Expr Mutate_(const Variable* op, const Expr& e) final {
    if (op == var_) {
      return make_const(op->type, device_type_);
    } else {
      return e;
    }
  }

 public:
  const Variable* var_{nullptr};
  int device_type_;
};

LoweredFunc BindDeviceType(LoweredFunc f,
                           int device_type) {
  auto n = make_node<LoweredFuncNode>(*f.operator->());
  n->body = DeviceTypeBinder(device_type).Mutate(n->body);
  return LoweredFunc(n);
}

}  // namespace ir
}  // namespace air
