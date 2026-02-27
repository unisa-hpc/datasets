# Copyright (c) 2025 University of Salerno
# SPDX-License-Identifier: Apache-2.0

import os
from _utils.errors import CleanError

def clean_graph(folder, only_installation: bool = False):
  try:
    for root, _, files in os.walk(folder):
      if only_installation:
        for file in files:
          if file.endswith('.tar.gz'):
            os.remove(os.path.join(root, file))
      else:
        for file in files:
          if not file.endswith('.yaml') and not file.endswith('.py'):
            os.remove(os.path.join(root, file))
  except OSError as exc:
    raise CleanError(f'Failed to clean "{folder}": {exc}') from exc
