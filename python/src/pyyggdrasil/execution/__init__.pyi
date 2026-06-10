from __future__ import annotations

from types import TracebackType
from typing import Optional, Type

class ExecutionContext:
    def __init__(self, num_threads: int) -> None: ...
    @property
    def num_threads(self) -> int: ...
    @staticmethod
    def max_num_threads() -> int: ...
    def __enter__(self) -> ExecutionContext: ...
    def __exit__(
        self,
        exc_type: Optional[Type[BaseException]],
        exc_value: Optional[BaseException],
        traceback: Optional[TracebackType],
    ) -> bool: ...
