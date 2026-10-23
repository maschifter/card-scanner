#!/usr/bin/env python3
"""Export the CDN manifest's SetCodeReader for the web.

Rebuilds the network from the manifest's PyTorch safetensors, checks it
against the manifest's ONNX, and writes BatchNorm-folded weights (conv
weights f16, like the other web models) and the parity fixture to
apps/web-example/public/models/SetCodeReader/.

Usage:
  python3 apps/web-example/tools/export-set-code-reader.py [--root <repo>]

Needs: torch, numpy, onnxruntime, safetensors, network access.
"""

import argparse
import importlib.util
import json
import tempfile
import urllib.request
from pathlib import Path

import numpy as np
import onnxruntime
import torch
import torch.nn as nn
import torch.nn.functional as F
from safetensors.numpy import save_file
from safetensors.torch import load_file

CDN = "https://mobile-data-cdn.cardnexus.com"
# The model's per-crop normalization: (x - mean) / (std + eps).
NORM_EPS = 0.02


def conv_bn_relu(cin: int, cout: int) -> nn.Sequential:
    return nn.Sequential(nn.Conv2d(cin, cout, 3, padding=1, bias=False),
                         nn.BatchNorm2d(cout), nn.ReLU())


class Trunk(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        pool, tall = nn.MaxPool2d(2, 2), nn.MaxPool2d((2, 1), (2, 1))
        self.body = nn.Sequential(
            conv_bn_relu(1, 20), pool, conv_bn_relu(20, 40), pool,
            conv_bn_relu(40, 60), conv_bn_relu(60, 60), tall,
            conv_bn_relu(60, 80), conv_bn_relu(80, 80), tall,
            conv_bn_relu(80, 100))


class SetCodeReader(nn.Module):
    """[1, 1, 96, 256] gray 0-1 -> [1, 64, 38] log-probabilities."""

    def __init__(self) -> None:
        super().__init__()
        self.trunk = Trunk()
        self.head = nn.Sequential(nn.Conv1d(100, 100, 3, padding=1), nn.ReLU(),
                                  nn.Conv1d(100, 38, 1))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        mean = x.mean(dim=(2, 3), keepdim=True)
        std = (x - mean).square().mean(dim=(2, 3), keepdim=True).sqrt()
        y = self.trunk.body((x - mean) / (std + NORM_EPS))
        y = y.amax(dim=2)  # 6 rows left: keep the strongest per column
        return F.log_softmax(self.head(y), dim=1).transpose(1, 2)


def folded_tensors(model: SetCodeReader) -> dict[str, np.ndarray]:
    """Each trunk conv with its BatchNorm folded in, named after the conv
    (trunk.body.0.weight / .bias, ...), and the head convs as trained."""
    tensors = {}
    for i, block in enumerate(model.trunk.body):
        if not isinstance(block, nn.Sequential):
            continue
        conv, bn = block[0], block[1]
        scale = bn.weight / torch.sqrt(bn.running_var + bn.eps)
        tensors[f"trunk.body.{i}.weight"] = (
            conv.weight * scale.reshape(-1, 1, 1, 1)).detach().numpy()
        tensors[f"trunk.body.{i}.bias"] = (
            bn.bias - bn.running_mean * scale).detach().numpy()
    for i in (0, 2):
        tensors[f"head.{i}.weight"] = model.head[i].weight.detach().numpy()
        tensors[f"head.{i}.bias"] = model.head[i].bias.detach().numpy()
    return {k: v.astype(np.float16) if v.ndim == 4 else v.astype(np.float32)
            for k, v in tensors.items()}


def fetch(path: str, target: Path) -> Path:
    urllib.request.urlretrieve(CDN + path, target)
    return target


def load_export_web_models(root: Path):
    path = root / "apps/web-example/tools/export-web-models.py"
    spec = importlib.util.spec_from_file_location("export_web_models", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path,
                    default=Path(__file__).resolve().parents[3])
    args = ap.parse_args()

    with urllib.request.urlopen(f"{CDN}/manifest-v2.json") as res:
        manifest = json.load(res)
    live = [v for v in manifest["models"]["set_code_reader"] if v["live"]]
    version = max(live, key=lambda v: v["version"])
    print(f"set_code_reader v{version['version']}")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        weights = fetch(version["formats"]["safetensors"]["path"],
                        tmp / "model.safetensors")
        (tmp / "SetCodeReader").mkdir()
        onnx_path = fetch(version["formats"]["onnx"]["path"],
                          tmp / "SetCodeReader/SetCodeReader.onnx")

        model = SetCodeReader()
        model.load_state_dict(load_file(weights))
        model.eval()

        x = np.random.default_rng(0).random((1, 1, 96, 256), dtype=np.float32)
        with torch.no_grad():
            got = model(torch.from_numpy(x)).numpy()
        sess = onnxruntime.InferenceSession(onnx_path,
                                            providers=["CPUExecutionProvider"])
        expected = sess.run(None, {sess.get_inputs()[0].name: x})[0]
        # The manifest's ONNX stores f16 weights; the rebuild matches its
        # f32 .pte to 1e-5.
        err = np.abs(np.exp(got) - np.exp(expected)).max()
        if err > 1e-2:
            raise SystemExit(f"rebuild differs from the ONNX by {err}")
        print(f"rebuild matches the ONNX to {err:.1e} in probability")

        out = args.root / "apps/web-example/public/models/SetCodeReader"
        out.mkdir(parents=True, exist_ok=True)
        save_file(folded_tensors(model), out / "model.safetensors",
                  metadata={"source": f"set_code_reader v{version['version']}"})
        load_export_web_models(args.root).write_fixture(
            tmp, "SetCodeReader", out / "fixture")


if __name__ == "__main__":
    main()
