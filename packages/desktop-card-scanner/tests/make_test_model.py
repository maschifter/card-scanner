#!/usr/bin/env python3
"""Regenerates tests/two_output.onnx, the fixture test_onnx_session.cpp asserts against.

Committed output, so a normal build needs neither python nor onnx. Run this by
hand only if the fixture needs to change:

    python3 tests/make_test_model.py

Two outputs of differing shape on purpose: the real segmentation model returns
`output0` and `proto` with different ranks, and a backend that only ever moved
one tensor of one shape would pass a single-output test and still be wrong.
"""

import onnx
from onnx import TensorProto, helper, numpy_helper
import numpy as np

OUT = "tests/two_output.onnx"

# scaled = input * 2   (shape preserved: 1x3x2x2)
# flat   = reshape(input, [1, 12])
scale = numpy_helper.from_array(np.array([2.0], dtype=np.float32), name="scale")
shape = numpy_helper.from_array(np.array([1, 12], dtype=np.int64), name="flat_shape")

graph = helper.make_graph(
    nodes=[
        helper.make_node("Mul", ["input", "scale"], ["scaled"]),
        helper.make_node("Reshape", ["input", "flat_shape"], ["flat"]),
    ],
    name="two_output",
    inputs=[helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 2, 2])],
    outputs=[
        helper.make_tensor_value_info("scaled", TensorProto.FLOAT, [1, 3, 2, 2]),
        helper.make_tensor_value_info("flat", TensorProto.FLOAT, [1, 12]),
    ],
    initializer=[scale, shape],
)

model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
model.ir_version = 9  # ONNX Runtime 1.23 rejects newer IR versions
onnx.checker.check_model(model)
onnx.save(model, OUT)
print(f"wrote {OUT}")
