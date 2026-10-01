#!/usr/bin/env python3
"""Export the deployed model bundle into web-ready safetensors for TIE.

Takes the deployed-models bundle (see the bundle README) and writes one
directory per model under --out:

- CardSegmentationModel, SetSymbolDetectionModel: the ultralytics state dict
  with training-only tensors stripped (one2many towers, semseg aux head,
  num_batches_tracked) and 4D conv weights cast to f16. Loads into
  @tie/zoo/yolo26, which folds BN at load time.
- CardRecognitionModel, SetSymbolRecognitionModel, ColorBarModel: weights
  lifted from the ONNX graph, where BatchNorm is already folded into conv
  weights. ColorBarModel's 1D BatchNorms are folded forward into the next
  Linear here. Conv weights f16, everything else f32.

Also writes deterministic parity fixtures (input.bin/output.bin f32 +
manifest.json) per model by running the ONNX export, so the browser ports
can be checked against ground truth.

Usage:
  python3 apps/web-example/tools/export-web-models.py --bundle <deployed-models-dir> \
      --out apps/web-example/public/models

Needs: numpy, onnx, onnxruntime, safetensors.
"""

import argparse
import json
import struct
from pathlib import Path

import numpy as np
import onnx
import onnxruntime
from safetensors import safe_open
from safetensors.numpy import save_file

YOLO_MODELS = ["CardSegmentationModel", "SetSymbolDetectionModel"]
ONNX_MODELS = {
    "CardRecognitionModel": (3, 224, 224),
    "SetSymbolRecognitionModel": (3, 96, 96),
    "ColorBarModel": (3, 100, 100),
}
YOLO_STRIP = ("model.23.cv2.", "model.23.cv3.", "model.23.cv4.", "model.23.proto.semseg.")


def export_yolo(src: Path, out_dir: Path) -> None:
    tensors, meta = {}, {}
    with safe_open(src, framework="numpy") as f:
        meta = dict(f.metadata() or {})
        for key in f.keys():
            if key.startswith(YOLO_STRIP) or key.endswith("num_batches_tracked"):
                continue
            t = f.get_tensor(key).astype(np.float32)
            tensors[key] = t.astype(np.float16) if (key.endswith(".weight") and t.ndim == 4) else t
    out_dir.mkdir(parents=True, exist_ok=True)
    save_file(tensors, out_dir / "model.safetensors", metadata=meta)
    print(f"{out_dir.name}: {len(tensors)} tensors, "
          f"{(out_dir / 'model.safetensors').stat().st_size / 1e6:.1f} MB")


def export_onnx_model(src: Path, out_dir: Path) -> None:
    g = onnx.load(src).graph
    inits = {i.name: onnx.numpy_helper.to_array(i).astype(np.float32) for i in g.initializer}
    tensors: dict[str, np.ndarray] = {}
    pending_bn: dict[str, tuple[np.ndarray, np.ndarray]] = {}
    for n in g.node:
        if n.op_type == "BatchNormalization":
            w, b, mean, var = (inits[i] for i in n.input[1:5])
            eps = next(a.f for a in n.attribute if a.name == "epsilon")
            a = w / np.sqrt(var + eps)
            pending_bn[n.output[0]] = (a, b - mean * a)
            continue
        if n.op_type not in ("Conv", "Gemm"):
            continue
        w = inits[n.input[1]]
        b = inits[n.input[2]] if len(n.input) > 2 else np.zeros(w.shape[0], np.float32)
        if n.op_type == "Gemm" and n.input[0] in pending_bn:
            # y = W(a*x + s) + b  ==  (W*diag(a)) x + (W s + b)
            a, s = pending_bn.pop(n.input[0])
            b = w @ s + b
            w = w * a[None, :]
        name = n.input[1].removesuffix(".weight")
        tensors[f"{name}.weight"] = w.astype(np.float16) if w.ndim == 4 else w
        tensors[f"{name}.bias"] = b
    if pending_bn:
        raise RuntimeError(f"{src.name}: unconsumed BatchNormalization folds {list(pending_bn)}")
    out_dir.mkdir(parents=True, exist_ok=True)
    save_file(tensors, out_dir / "model.safetensors", metadata={"source": src.name})
    print(f"{out_dir.name}: {len(tensors)} tensors, "
          f"{(out_dir / 'model.safetensors').stat().st_size / 1e6:.1f} MB")


def write_fixture(bundle: Path, name: str, out_dir: Path) -> None:
    """Run the ONNX export on a deterministic input and store input/output."""
    onnx_path = bundle / name / f"{name}.onnx"
    sess = onnxruntime.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    inp = sess.get_inputs()[0]
    shape = [1] + [int(d) for d in inp.shape[1:]]
    rng = np.random.default_rng(1234)
    # Image-like range keeps activations in the regime the nets were trained on.
    x = rng.random(shape, dtype=np.float32)
    outs = sess.run(None, {inp.name: x})
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "input.bin").write_bytes(x.tobytes())
    manifest = {"input": {"name": inp.name, "shape": shape}, "outputs": []}
    for meta, arr in zip(sess.get_outputs(), outs):
        fn = f"output.{meta.name}.bin"
        (out_dir / fn).write_bytes(np.ascontiguousarray(arr, np.float32).tobytes())
        manifest["outputs"].append({"name": meta.name, "shape": list(arr.shape), "file": fn})
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"fixture {name}: outputs {[o['shape'] for o in manifest['outputs']]}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bundle", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    for name in YOLO_MODELS:
        export_yolo(args.bundle / name / f"{name}.safetensors", args.out / name)
    for name in ONNX_MODELS:
        export_onnx_model(args.bundle / name / f"{name}.onnx", args.out / name)
    mapping = args.bundle / "CardSegmentationModel" / "gameClassMapping.json"
    (args.out / "CardSegmentationModel").mkdir(parents=True, exist_ok=True)
    (args.out / "CardSegmentationModel" / "gameClassMapping.json").write_text(mapping.read_text())
    for name in [*YOLO_MODELS, *ONNX_MODELS]:
        write_fixture(args.bundle, name, args.out / name / "fixture")


if __name__ == "__main__":
    main()
