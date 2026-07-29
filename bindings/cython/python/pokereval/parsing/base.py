from abc import ABC, abstractmethod
from typing import List, Optional
from .dtos import ParserResult

class HandHistoryParser(ABC):
    """Abstract base class for hand history parsers."""

    @abstractmethod
    def parse_content(self, content: str) -> ParserResult:
        """Parses the raw content of a hand history file or summary file."""
        pass

    @abstractmethod
    def parse_file(self, filepath: str) -> ParserResult:
        """Reads and parses a file."""
        pass
