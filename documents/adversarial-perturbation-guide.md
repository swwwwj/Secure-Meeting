# Phase E: Adversarial Perturbation (dev branch)

Branch: `adversarial-perturbation-dev` (from `main`)

## Privacy modes

| Mode | Behavior |
|------|----------|
| `none` | Detect only, no visual protection |
| `blur` | Gaussian/mosaic blur on non-whitelist faces (default) |
| `perturbation` | Learned or mock adversarial noise on protected face regions |
| `hybrid` | Perturbation + light blur |

Configured in `ai_service/config/*.json` as `default_privacy_protect_mode`. The client can override per frame via `privacy_protect_mode` in `process_frame`.

## Training

```bash
cd ai_service
pip install torch
python -m training.train_adversarial_perturbation
```

Optional face/selfie images under `ai_service/training/perturbation_samples/`.

## Meeting client

On the join page, enable **「启用对抗性扰动（实验，替代模糊）」**. Requires AI service running; works with ArcFace whitelist logic.

## Rollback

Set `default_privacy_protect_mode` to `blur` or disable the checkbox in the client.
