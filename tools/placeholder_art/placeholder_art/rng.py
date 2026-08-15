# SPDX-License-Identifier: MIT
"""Deterministic pseudo-random numbers.

`random.Random` would also be reproducible, but rolling a tiny xorshift here
removes any dependence on the CPython version's generator and makes the seed
derivation explicit and auditable.

Seeds are always derived from strings via :func:`seed_of`, never from object
identity, iteration order, or the clock. That is what lets a rebuild produce
byte-identical output.
"""

from __future__ import annotations

import hashlib
from typing import Sequence, TypeVar

_MASK64 = (1 << 64) - 1

T = TypeVar("T")


def seed_of(*parts: object) -> int:
    """Return a stable 64-bit seed derived from ``parts``.

    Parts are stringified and joined with ``|``. The digest is BLAKE2b, so the
    value is stable across Python versions, platforms, and process restarts
    (unlike :func:`hash`, which is randomised per process for ``str``).
    """
    material = "|".join(str(part) for part in parts).encode("utf-8")
    return int.from_bytes(hashlib.blake2b(material, digest_size=8).digest(), "big")


class Rng:
    """A xorshift64* generator seeded from :func:`seed_of`."""

    __slots__ = ("_state",)

    def __init__(self, seed: int) -> None:
        state = (seed ^ 0x9E3779B97F4A7C15) & _MASK64
        self._state = state if state else 0x2545F4914F6CDD1D

    def next_u64(self) -> int:
        state = self._state
        state ^= (state << 13) & _MASK64
        state ^= state >> 7
        state ^= (state << 17) & _MASK64
        self._state = state
        return (state * 0x2545F4914F6CDD1D) & _MASK64

    def random(self) -> float:
        """Return a float in ``[0, 1)``."""
        return (self.next_u64() >> 11) * (1.0 / (1 << 53))

    def uniform(self, low: float, high: float) -> float:
        return low + (high - low) * self.random()

    def randint(self, low: int, high: int) -> int:
        """Return an integer in ``[low, high]``."""
        if high <= low:
            return low
        return low + int(self.random() * (high - low + 1))

    def chance(self, probability: float) -> bool:
        return self.random() < probability

    def choice(self, items: Sequence[T]) -> T:
        return items[self.randint(0, len(items) - 1)]
