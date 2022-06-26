# Copyright 2021 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""operator dsl function: scatter_add"""
import akg.tvm
import akg.utils as utils
from akg.ops.array import UnsortedSegmentSum
from akg.ops.array.gpu import GatherNd
from akg.ops.math import Mul, ReduceSum

@utils.check_input_type(akg.tvm.tensor.Tensor, akg.tvm.tensor.Tensor, akg.tvm.tensor.Tensor, akg.tvm.tensor.Tensor, akg.tvm.tensor.Tensor, int,
                        bool, int, (str, type(None)))
def fused_gather_nd_reduce_sum_mul_unsorted_segment_sum(input1, input2, input3, input4, input5, axis=0, keepdims=False, num=0, target=utils.CUDA):
    item_get = GatherNd(input1, input2)
    sum_axis = ReduceSum(item_get, axis, keepdims, target=utils.CUDA)
    prod = Mul(sum_axis, input3, target=utils.CUDA)
    res1 = UnsortedSegmentSum(prod, input4, num, op_id=0)
    res2 = UnsortedSegmentSum(prod, input5, num, op_id=1)
    return res1, res2



