import numpy as np

from perturbation_fast import apply_pixel_deltas, build_checkpoint, build_pixel_deltas, save_checkpoint
from perturbation_pipeline import HybridFaceProtector, PerturbationProtector, build_face_protector
from pipeline import Detection, RegionProtector


def test_mock_perturbation_changes_pixels():
    protector = PerturbationProtector(strength=0.12, provider="mock")
    image = np.full((64, 64, 3), 180, dtype=np.uint8)
    det = Detection(label="face", confidence=0.9, bbox=(10, 10, 40, 40), sensitive=True)
    out, count = protector.blur_regions(image, [det])
    assert count == 1
    assert not np.array_equal(out[10:40, 10:40], image[10:40, 10:40])


def test_pixel_delta_changes_only_few_pixels(tmp_path):
    pixels = build_pixel_deltas(seed=7, num_pixels=8, max_channel_delta=2)
    image = np.full((32, 32, 3), 120, dtype=np.uint8)
    out = apply_pixel_deltas(image, pixels, bbox=(0, 0, 32, 32))
    diff = np.abs(out.astype(np.int16) - image.astype(np.int16))
    assert int(np.count_nonzero(diff)) <= 8 * 3


def test_learned_pixel_delta_checkpoint(tmp_path):
    weights = tmp_path / "best.pt"
    save_checkpoint(build_checkpoint(seed=99, num_pixels=6), weights)
    protector = PerturbationProtector(strength=0.08, provider="learned", weights_path=str(weights))
    assert protector.available
    image = np.full((40, 40, 3), 200, dtype=np.uint8)
    det = Detection(label="face", confidence=0.9, bbox=(5, 5, 35, 35), sensitive=True)
    out, count = protector.blur_regions(image, [det])
    assert count == 1
    assert not np.array_equal(out, image)


def test_build_face_protector_modes():
    blur = build_face_protector("blur", "gaussian", 9, "mock", "", 0.08)
    assert isinstance(blur, RegionProtector)
    none = build_face_protector("none", "gaussian", 9, "mock", "", 0.08)
    assert none is None
    hybrid = build_face_protector("hybrid", "gaussian", 9, "mock", "", 0.08)
    assert isinstance(hybrid, HybridFaceProtector)
