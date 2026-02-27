# Copyright (c) 2026 University of Salerno
# SPDX-License-Identifier: Apache-2.0

import os
import shutil

from _utils.errors import MoveError


def _prompt_overwrite(path: str) -> bool:
  while True:
    answer = input(f'{path} already exists. Overwrite? [y/n]: ').strip().lower()
    if answer in ('y', 'yes'):
      return True
    if answer in ('n', 'no'):
      return False
    print('Please answer with y or n.')


def move_graph_files(name: str, source_root='.', destination_root='.'):
  source_dir = os.path.join(source_root, name)
  destination_dir = os.path.join(destination_root, name)

  candidates = [
      os.path.join(source_dir, f'{name}.mtx'),
      os.path.join(source_dir, f'{name}.bin'),
  ]
  existing_candidates = [path for path in candidates if os.path.isfile(path)]

  if not existing_candidates:
    print(f'Warning: no .mtx or .bin file found for "{name}" in {source_dir}')
    return

  try:
    os.makedirs(destination_dir, exist_ok=True)
  except OSError as exc:
    raise MoveError(f'Failed to create destination directory "{destination_dir}": {exc}', graph=name) from exc

  for src_path in existing_candidates:
    filename = os.path.basename(src_path)
    dst_path = os.path.join(destination_dir, filename)

    try:
      if os.path.exists(dst_path):
        if not _prompt_overwrite(dst_path):
          print(f'Skipped {src_path}')
          continue
        os.remove(dst_path)

      shutil.move(src_path, dst_path)
      print(f'Moved {src_path} -> {dst_path}')
    except OSError as exc:
      raise MoveError(f'Failed to move "{src_path}" to "{dst_path}": {exc}', graph=name) from exc
