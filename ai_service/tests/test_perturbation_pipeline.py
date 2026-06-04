import numpy as np

from perturbation_pipeline import HybridFaceProtector, PerturbationProtector, build_face_protector
from pipeline import Detection, RegionProtector


def test_mock_perturbation_changes_pixels():
    protector = PerturbationProtector(strength=0.12, provider="mock")
    image = np.full((64, 64, 3), 180, dtype=np.uint8)
    det = Detection(label="face", confidence=0.9, bbox=(10, 10, 40, 40), sensitive=True)
    out, count = protector.blur_regions(image, [det])
    assert count == 1
    assert not np.array_equal(out[10:40, 10:40], image[10:40, 10:40])


def test_build_face_protector_modes():
    blur = build_face_protector("blur", "gaussian", 9, "mock", "", 0.08)
    assert isinstance(blur, RegionProtector)
    none = build_face_protector("none", "gaussian", 9, "mock", "", 0.08)
    assert none is None
    hybrid = build_face_protector("hybrid", "gaussian", 9, "mock", "", 0.08)
    assert isinstance(hybrid, HybridFaceProtector)
