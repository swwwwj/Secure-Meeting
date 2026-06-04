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

## Training (CPU，无需独显)

训练脚本是轻量导出流程：**不跑神经网络**，只生成 `pixel_delta_v1` 权重（每张人脸 ROI 改少量像素，视觉上几乎不变）。

```bash
cd ai_service
python -m training.train_adversarial_perturbation
```

可选：把任意图片放进 `training/perturbation_samples/`（仅用于统计样本数，不参与反向传播）。

```bash
python -m training.train_adversarial_perturbation --data ./training/perturbation_samples --epochs 5
```

权重类型：`checkpoint_type: pixel_delta_v1`。推理时设置 `perturbation_provider: "learned"` 即可加载。

## Meeting client

On the join page, enable **「启用对抗性扰动（实验，替代模糊）」**. Requires AI service running; works with ArcFace whitelist logic.

## Rollback

Set `default_privacy_protect_mode` to `blur` or disable the checkbox in the client.
