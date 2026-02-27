# Copyright (c) 2026 University of Salerno
# SPDX-License-Identifier: Apache-2.0

from typing import Optional


class DatasetOperationError(RuntimeError):
  operation = 'dataset'

  def __init__(self, message: str, *, graph: Optional[str] = None) -> None:
    super().__init__(message)
    self.graph = graph


class DownloadError(DatasetOperationError):
  operation = 'download'


class CleanError(DatasetOperationError):
  operation = 'clean'


class ConvertError(DatasetOperationError):
  operation = 'convert'


class TransformError(DatasetOperationError):
  operation = 'transform'


class MoveError(DatasetOperationError):
  operation = 'move'
