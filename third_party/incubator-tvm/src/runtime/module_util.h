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
 * \file module_util.h
 * \brief Helper utilities for module building
 */

/*
 * 2021.11.01
 *   Add AKG backend interface.
 */

#ifndef TVM_RUNTIME_MODULE_UTIL_H_
#define TVM_RUNTIME_MODULE_UTIL_H_

#include <tvm/runtime/module.h>
#include <tvm/runtime/c_runtime_api.h>
#include <tvm/runtime/c_backend_api.h>
#include <memory>
#include <vector>

extern "C" {
// Function signature for generated packed function in shared library
typedef int (*BackendPackedCFunc)(void* args,
                                  int* type_codes,
                                  int num_args);
}  // extern "C"

namespace air {
namespace runtime {
/*!
 * \brief Wrap a BackendPackedCFunc to packed function.
 * \param faddr The function address
 * \param mptr The module pointer node.
 */
PackedFunc WrapPackedFunc(BackendPackedCFunc faddr, const ObjectPtr<Object>& mptr);
/*!
 * \brief Load and append module blob to module list
 * \param mblob The module blob.
 * \param module_list The module list to append to
 */
void ImportModuleBlob(const char* mblob, std::vector<Module>* module_list);

/*!
 * \brief Utility to initialize conext function symbols during startup
 * \param flookup A symbol lookup function.
 * \tparam FLookup a function of signature string->void*
 */
template<typename FLookup>
void InitContextFunctions(FLookup flookup) {
  #define TVM_INIT_CONTEXT_FUNC(FuncName)                     \
    if (auto *fp = reinterpret_cast<decltype(&FuncName)*>     \
      (flookup("__" #FuncName))) {                            \
      *fp = FuncName;                                         \
    }
  // Initialize the functions
  TVM_INIT_CONTEXT_FUNC(TVMFuncCall);
  TVM_INIT_CONTEXT_FUNC(TVMAPISetLastError);
  TVM_INIT_CONTEXT_FUNC(TVMBackendGetFuncFromEnv);
  TVM_INIT_CONTEXT_FUNC(TVMBackendAllocWorkspace);
  TVM_INIT_CONTEXT_FUNC(TVMBackendFreeWorkspace);
  TVM_INIT_CONTEXT_FUNC(TVMBackendParallelLaunch);
  TVM_INIT_CONTEXT_FUNC(TVMBackendParallelBarrier);

  #undef TVM_INIT_CONTEXT_FUNC
}
}  // namespace runtime
}  // namespace air
#endif   // TVM_RUNTIME_MODULE_UTIL_H_
