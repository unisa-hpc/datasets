# Copyright (c) 2025 University of Salerno
# SPDX-License-Identifier: Apache-2.0

import os
import glob
import subprocess

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
LIBRARY_DIR = os.path.join(REPO_ROOT, 'library')
CONVERTER_DIR = os.path.join(REPO_ROOT, '_converter')
CONVERTER_SOURCE = os.path.join(os.path.dirname(__file__), 'converter.cpp')
DEFAULT_CONVERTER_PATH = os.path.join(CONVERTER_DIR, 'converter')


def ensure_converter(converter_path: str = DEFAULT_CONVERTER_PATH) -> str:
  if os.path.exists(converter_path):
    if not os.access(converter_path, os.X_OK):
      raise PermissionError(f'Converter exists but is not executable: {converter_path}')
    return converter_path

  if os.path.abspath(converter_path) != os.path.abspath(DEFAULT_CONVERTER_PATH):
    raise FileNotFoundError(f'Converter not found at: {converter_path}')

  compiler = 'g++'
  if not os.path.exists(CONVERTER_SOURCE):
    raise FileNotFoundError(f'Converter source not found at: {CONVERTER_SOURCE}')

  os.makedirs(CONVERTER_DIR, exist_ok=True)
  print(f'Converter not found at {converter_path}. Building it with {compiler}...')

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
  converter_path = ensure_converter(converter_path)
  if not (lst := glob.glob(f'{folder}/*.mtx')):
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
