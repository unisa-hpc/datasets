# Copyright (c) 2026 University of Salerno
# SPDX-License-Identifier: Apache-2.0

import glob
import os
import subprocess

from _utils.errors import TransformError

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
LIBRARY_DIR = os.path.join(REPO_ROOT, 'library')
CONVERTER_DIR = os.path.join(REPO_ROOT, '_tools')
TRANSFORMER_SOURCE = os.path.join(CONVERTER_DIR, 'transformer.cpp')
DEFAULT_TRANSFORMER_PATH = os.path.join(CONVERTER_DIR, 'transformer.out')


def _transformer_dependencies():
  deps = [TRANSFORMER_SOURCE]
  deps.extend(glob.glob(os.path.join(LIBRARY_DIR, 'include', '*.hpp')))
  return deps


def _needs_rebuild(transformer_path: str) -> bool:
  if not os.path.exists(transformer_path):
    return True
  transformer_mtime = os.path.getmtime(transformer_path)
  for dep in _transformer_dependencies():
    if os.path.exists(dep) and os.path.getmtime(dep) > transformer_mtime:
      return True
  return False


def ensure_transformer(transformer_path: str = DEFAULT_TRANSFORMER_PATH, force_rebuild: bool = False) -> str:
  if os.path.abspath(transformer_path) != os.path.abspath(DEFAULT_TRANSFORMER_PATH):
    if force_rebuild:
      raise ValueError('--update can only be used with the default transformer path')
    if os.path.exists(transformer_path):
      if not os.access(transformer_path, os.X_OK):
        raise PermissionError(f'Transformer exists but is not executable: {transformer_path}')
      return transformer_path
    raise FileNotFoundError(f'Transformer not found at: {transformer_path}')

  compiler = 'g++'
  if not os.path.exists(TRANSFORMER_SOURCE):
    raise FileNotFoundError(f'Transformer source not found at: {TRANSFORMER_SOURCE}')

  if (not force_rebuild
      and os.path.exists(transformer_path)
      and os.access(transformer_path, os.X_OK)
      and not _needs_rebuild(transformer_path)):
    return transformer_path

  os.makedirs(CONVERTER_DIR, exist_ok=True)
  print(f'Building transformer at {transformer_path} with {compiler}...')

  compile_cmd = [
      compiler,
      '-O3',
      '-std=c++17',
      '-I', os.path.join(LIBRARY_DIR, 'include'),
      TRANSFORMER_SOURCE,
      '-o', transformer_path,
  ]

  try:
    subprocess.run(compile_cmd, check=True)
  except FileNotFoundError as exc:
    raise RuntimeError('g++ compiler not found. Please install g++ to build the transformer.') from exc

  os.chmod(transformer_path, 0o755)
  return transformer_path


def _normalize_transformations(transformations):
  if transformations is None:
    transformations = ['symmetrize', 'sort']
  if isinstance(transformations, str):
    transformations = [transformations]

  normalized = []
  for transformation in transformations:
    if transformation == 'both':
      normalized.extend(['symmetrize', 'sort'])
    else:
      normalized.append(transformation)
  if not normalized:
    raise ValueError('At least one transformation must be provided')
  return normalized


def transform_graph(transformer_path, folder, transformations=None, always: bool = False):
  graph_name = os.path.basename(os.path.normpath(folder))
  try:
    transformer_path = ensure_transformer(transformer_path)
    transformations = _normalize_transformations(transformations)

    candidates = sorted(
        path for path in glob.glob(f'{folder}/*.mtx')
        if not path.endswith('.transformed.mtx'))
    if not candidates:
      print(f'Warning: no .mtx file found in {folder}')
      return

    mtx = candidates[0]
    basename = os.path.splitext(os.path.basename(mtx))[0]
    transformed_file = f'{folder}/{basename}.transformed.mtx'

    if os.path.exists(transformed_file) and not always:
      if input(f'{os.path.basename(transformed_file)} already exists. Do you want to transform again? [y/n]: ').lower() != 'y':
        return

    print(f'Transforming {basename} ({", ".join(transformations)})')
    args = [transformer_path, mtx, transformed_file]
    for transformation in transformations:
      args.extend(['--operation', transformation])
    subprocess.run(args, check=True)
  except subprocess.CalledProcessError as exc:
    raise TransformError(f'Transformer process failed with exit code {exc.returncode}', graph=graph_name) from exc
  except (FileNotFoundError, PermissionError, OSError, RuntimeError, ValueError) as exc:
    raise TransformError(str(exc), graph=graph_name) from exc
