"""
Optional legacy PyTorch generator (Phase E). Default training exports pixel_delta_v1 instead.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    import torch.nn as nn

try:
    import torch
    import torch.nn as nn
except Exception:  # pragma: no cover - optional in CI without torch
    torch = None  # type: ignore
    nn = None  # type: ignore


def torch_available() -> bool:
    return torch is not None and nn is not None


if torch_available():

    class PerturbationGenerator(nn.Module):
        """Maps normalized RGB patches to bounded residual noise."""

        def __init__(self, epsilon: float = 0.08) -> None:
            super().__init__()
            self.epsilon = float(epsilon)
            self.encoder = nn.Sequential(
                nn.Conv2d(3, 32, kernel_size=3, padding=1),
                nn.ReLU(inplace=True),
                nn.Conv2d(32, 32, kernel_size=3, padding=1),
                nn.ReLU(inplace=True),
                nn.Conv2d(32, 3, kernel_size=3, padding=1),
            )

        def forward(self, x: "torch.Tensor") -> "torch.Tensor":
            delta = torch.tanh(self.encoder(x)) * self.epsilon
            return torch.clamp(x + delta, 0.0, 1.0)

else:

    class PerturbationGenerator:  # type: ignore[no-redef]
        def __init__(self, epsilon: float = 0.08) -> None:
            raise RuntimeError("PyTorch is required for PerturbationGenerator")
