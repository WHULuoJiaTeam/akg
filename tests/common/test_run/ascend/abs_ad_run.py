# Copyright 2019-2021 Huawei Technologies Co., Ltd
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

import numpy as np
from tests.common.tensorio import compare_tensor
from akg.topi.util import get_const_tuple
from akg.ops.math.ascend import AbsAd
from akg.utils import kernel_exec as utils
from tests.common.base import get_rtol_atol

def abs_ad_run(shape, dtype, attrs):
    if 'tuning' in attrs.keys():
        t = attrs.get("tuning", False)
        kernel_name = attrs.get("kernel_name", False)
        mod = utils.op_build_test(AbsAd, [shape, shape], [dtype, dtype], kernel_name=kernel_name, attrs=attrs, tuning=t)
        if t:
            expect, head_np, input_np, output = gen_data(dtype, shape)
            return mod, expect, (head_np, input_np, output)
        else:
            return mod
    else:
        mod = utils.op_build_test(AbsAd, [shape, shape], [dtype, dtype], kernel_name='abs_ad', attrs=attrs)
        expect, head_np, input_np, output = gen_data(dtype, shape)
        output = utils.mod_launch(mod, (head_np, input_np, output), expect=expect)
        rtol, atol = get_rtol_atol("abs_ad", dtype)
        return (head_np, input_np), output, expect, compare_tensor(output, expect, rtol=rtol, atol=atol)


def gen_data(dtype, shape):
    input_np = np.random.uniform(low=-1.0, high=1.0, size=get_const_tuple(shape)).astype(dtype)
    head_np = np.random.uniform(low=-1.0, high=1.0, size=shape).astype(dtype)
    expect = ((input_np >= 0).astype(dtype)) - ((input_np < 0).astype(dtype))
    expect = expect * head_np
    output = np.full(expect.shape, np.nan, dtype)
    return expect, head_np, input_np, output
