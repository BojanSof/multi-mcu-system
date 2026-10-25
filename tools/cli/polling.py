"""Bounded completion polling; passive watches/reconnects use their own cadence."""

import time


class CompletionBackoff:
    """Fresh instance per operation/chunk; never sleep past its deadline."""

    _DELAYS = (0.005, 0.010, 0.020, 0.050)

    def __init__(self):
        self._index = 0

    def wait(self, deadline):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        time.sleep(min(self._DELAYS[self._index], remaining))
        self._index = min(self._index + 1, len(self._DELAYS) - 1)
