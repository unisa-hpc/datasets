# Copyright (c) 2025 University of Salerno
# SPDX-License-Identifier: Apache-2.0

import os
import glob
import subprocess

from _utils.errors import ConvertError

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
LIBRARY_DIR = os.path.join(REPO_ROOT, 'library')
CONVERTER_DIR = os.path.join(REPO_ROOT, '_tools')
CONVERTER_SOURCE = os.path.join(CONVERTER_DIR, 'converter.cpp')
DEFAULT_CONVERTER_PATH = os.path.join(CONVERTER_DIR, 'converter.out')


def _converter_dependencies():
  deps = [CONVERTER_SOURCE]
  deps.extend(glob.glob(os.path.join(LIBRARY_DIR, 'include', '*.hpp')))
  return deps


def _needs_rebuild(converter_path: str) -> bool:
  if not os.path.exists(converter_path):
    return True
  converter_mtime = os.path.getmtime(converter_path)
  for dep in _converter_dependencies():
    if os.path.exists(dep) and os.path.getmtime(dep) > converter_mtime:
      return True
  return False


def ensure_converter(converter_path: str = DEFAULT_CONVERTER_PATH, force_rebuild: bool = False) -> str:
  if os.path.abspath(converter_path) != os.path.abspath(DEFAULT_CONVERTER_PATH):
    if force_rebuild:
      raise ValueError('--update can only be used with the default converter path')
    if os.path.exists(converter_path):
      if not os.access(converter_path, os.X_OK):
        raise PermissionError(f'Converter exists but is not executable: {converter_path}')
      return converter_path
    raise FileNotFoundError(f'Converter not found at: {converter_path}')

  compiler = 'g++'
  if not os.path.exists(CONVERTER_SOURCE):
    raise FileNotFoundError(f'Converter source not found at: {CONVERTER_SOURCE}')

  if (not force_rebuild
      and os.path.exists(converter_path)
      and os.access(converter_path, os.X_OK)
      and not _needs_rebuild(converter_path)):
    return converter_path

  os.makedirs(CONVERTER_DIR, exist_ok=True)
  print(f'Building converter at {converter_path} with {compiler}...')

  compile_cmd = [
      compiler,
      '-O3',
      '-std=c++17',
      '-I', os.path.join(LIBRARY_DIR, 'include'),
      CONVERTER_SOURCE,
      '-o', converter_path,
  ]

  try:
    subprocess.run(compile_cmd, check=True)
  except FileNotFoundError as exc:
    raise RuntimeError('g++ compiler not found. Please install g++ to build the converter.') from exc

  os.chmod(converter_path, 0o755)
  return converter_path


def convert_graph(converter_path, folder, undirected: bool, always: bool = False):
  graph_name = os.path.basename(os.path.normpath(folder))
  try:
    converter_path = ensure_converter(converter_path)
    if not (lst := glob.glob(f'{folder}/*.mtx')):
      print(f'Warning: no .mtx file found in {folder}')
      return
    mtx = lst[0]
    basename = os.path.basename(mtx).split('.')[0]
    bin_file = f'{folder}/{basename}.bin'

    args = [converter_path, mtx, bin_file]
    if undirected:
      args.append('-u')
    
    if os.path.exists(bin_file) and not always:
      # ask user if they want to convert again
      if input(f'{basename} already converted. Do you want to convert again? [y/n]: ').lower() != 'y':
        return
    
    print(f'Converting {basename}')
    subprocess.run(args, check=True)
  except subprocess.CalledProcessError as exc:
    raise ConvertError(f'Converter process failed with exit code {exc.returncode}', graph=graph_name) from exc
  except (FileNotFoundError, PermissionError, OSError, RuntimeError, ValueError) as exc:
    raise ConvertError(str(exc), graph=graph_name) from exc
