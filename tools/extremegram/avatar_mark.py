#!/usr/bin/env python3
"""Local experiment: encode and detect a redundant mark after avatar re-encoding.

No network access and no Telegram account changes. This is not authentication.
"""
import argparse
import hashlib
import numpy as np
from PIL import Image, ImageOps

SIZE = 640
BLOCK = 8
BITS = np.unpackbits(np.frombuffer(hashlib.sha256(b'ExtremeGram-avatar-marker-v1').digest()[:8], dtype=np.uint8))
# Distinct frequencies, both survive low-pass filtering better than pixel values.
F1, F2 = (0, 1), (1, 0)
X = np.arange(BLOCK)
C = np.cos((2 * X[None, :] + 1) * X[:, None] * np.pi / (2 * BLOCK)) * np.sqrt(2 / BLOCK)
C[0] /= np.sqrt(2)
ORDER = np.random.default_rng(0x45585452454D45).permutation((SIZE // BLOCK) ** 2)


def _prepare(image):
    return ImageOps.fit(image.convert('RGB'), (SIZE, SIZE), method=Image.Resampling.LANCZOS)


def _luminance(image):
    return np.asarray(_prepare(image).convert('YCbCr'), dtype=np.float64).copy()


def encode(image, strength=26):
    data = _luminance(image)
    y = data[:, :, 0]
    for n, index in enumerate(ORDER):
        by, bx = divmod(int(index), SIZE // BLOCK)
        patch = y[by * BLOCK:(by + 1) * BLOCK, bx * BLOCK:(bx + 1) * BLOCK]
        d = C @ (patch - 128) @ C.T
        sign = 1 if BITS[n % len(BITS)] else -1
        delta = sign * (d[F1] - d[F2])
        if delta < strength:
            adjustment = (strength - delta) / 2
            d[F1] += sign * adjustment
            d[F2] -= sign * adjustment
        patch[:] = C.T @ d @ C + 128
    result = Image.fromarray(np.uint8(np.clip(np.rint(data), 0, 255)), 'YCbCr').convert('RGB')
    return result


def detect(image):
    y = _luminance(image)[:, :, 0]
    votes = np.zeros(len(BITS), dtype=np.int32)
    counts = np.zeros(len(BITS), dtype=np.int32)
    for n, index in enumerate(ORDER):
        by, bx = divmod(int(index), SIZE // BLOCK)
        patch = y[by * BLOCK:(by + 1) * BLOCK, bx * BLOCK:(bx + 1) * BLOCK]
        d = C @ (patch - 128) @ C.T
        vote = 1 if d[F1] > d[F2] else -1
        votes[n % len(BITS)] += vote
        counts[n % len(BITS)] += 1
    decoded = votes > 0
    matches = int(np.sum(decoded == BITS))
    confidence = float(np.mean(np.abs(votes) / counts))
    # A matched signature is evidence of a mark, not proof of app identity.
    return matches, len(BITS), confidence


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    e = sub.add_parser('encode'); e.add_argument('input'); e.add_argument('output'); e.add_argument('--strength', type=float, default=26)
    d = sub.add_parser('detect'); d.add_argument('input')
    a = p.parse_args()
    if a.command == 'encode':
        if not 1 <= a.strength <= 100: p.error('strength must be between 1 and 100')
        encode(Image.open(a.input), a.strength).save(a.output)
    else:
        matches, total, confidence = detect(Image.open(a.input))
        print(f'matches={matches}/{total} vote_confidence={confidence:.3f}')

if __name__ == '__main__': main()
