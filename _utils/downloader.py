# Copyright (c) 2025 University of Salerno
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import tarfile
import zipfile

import gdown
import requests


def check_disk_space(directory, required_space):
  """Check if there's enough space on the drive."""
  total, used, free = shutil.disk_usage(directory)
  return free >= required_space


def _select_member_by_name(member_names, target_filename):
  # Exact filename match first.
  for member_name in member_names:
    if os.path.basename(member_name) == target_filename:
      return member_name

  # Case-insensitive match as fallback.
  target_lower = target_filename.lower()
  for member_name in member_names:
    if os.path.basename(member_name).lower() == target_lower:
      return member_name

  # If archive has exactly one .mtx file, use it and rename on output.
  mtx_members = [m for m in member_names if os.path.basename(m).lower().endswith('.mtx')]
  if len(mtx_members) == 1:
    return mtx_members[0]

  return None


def _confirm_download(target_path: str, always_yes: bool, always_no: bool) -> bool:
  if not os.path.exists(target_path):
    return True
  if always_yes:
    return True
  if always_no:
    return False
  while True:
    answer = input(f'{target_path} already exists. Download again and overwrite? [y/n]: ').strip().lower()
    if answer in ('y', 'yes'):
      return True
    if answer in ('n', 'no'):
      return False
    print('Please answer with y or n.')


def _extract_target_from_zip(archive_path, extract_to, target_filename):
  with zipfile.ZipFile(archive_path, 'r') as zip_ref:
    member_names = [m for m in zip_ref.namelist() if not m.endswith('/')]
    selected_member = _select_member_by_name(member_names, target_filename)
    if selected_member is None:
      raise FileNotFoundError(f"Could not find '{target_filename}' in archive")

    output_path = os.path.join(extract_to, target_filename)
    with zip_ref.open(selected_member) as source, open(output_path, 'wb') as target:
      target.write(source.read())


def _extract_target_from_tar(archive_path, extract_to, target_filename):
  with tarfile.open(archive_path, 'r:gz') as tar_ref:
    member_names = [m.name for m in tar_ref.getmembers() if m.isfile()]
    selected_member_name = _select_member_by_name(member_names, target_filename)
    if selected_member_name is None:
      raise FileNotFoundError(f"Could not find '{target_filename}' in archive")

    selected_member = tar_ref.getmember(selected_member_name)
    source = tar_ref.extractfile(selected_member)
    if source is None:
      raise FileNotFoundError(f"Could not extract '{selected_member_name}' from archive")

    output_path = os.path.join(extract_to, target_filename)
    with source, open(output_path, 'wb') as target:
      target.write(source.read())


def download_and_extract(name, url, download_dir='.', extract_to='.', always_yes: bool = False, always_no: bool = False):
  try:
    # Ensure download directory exists.
    os.makedirs(download_dir, exist_ok=True)
    os.makedirs(extract_to, exist_ok=True)
    target_filename = f'{name}.mtx'
    target_path = os.path.join(extract_to, target_filename)

    if not _confirm_download(target_path, always_yes, always_no):
      print(f'Skipped download for {name}')
      return

    # Step 1: Download the file.
    response = requests.get(url, stream=True)
    response.raise_for_status()

    total_size = int(response.headers.get('content-length', 0))
    if not check_disk_space(download_dir, total_size):
      raise OSError('Not enough space in the specified download directory.')

    temp_file_path = os.path.join(download_dir, f'{name}_temp')
    gdown.download(url, temp_file_path, quiet=False)

    # Step 2: Determine the file type.
    with open(temp_file_path, 'rb') as file:
      magic_bytes = file.read(262)
      if magic_bytes.startswith(b'PK'):
        file_extension = '.zip'
      elif magic_bytes.startswith(b'\x1f\x8b\x08'):
        file_extension = '.tar.gz'
      else:
        raise ValueError('Unsupported file type')

    # Step 3: Rename the temporary file with the correct extension.
    file_path = os.path.join(download_dir, f'{name}{file_extension}')
    os.rename(temp_file_path, file_path)

    # Step 4: Extract only the desired MatrixMarket file.

    if file_extension == '.zip':
      _extract_target_from_zip(file_path, extract_to, target_filename)
    else:
      _extract_target_from_tar(file_path, extract_to, target_filename)

    # Step 5: Clean up the downloaded archive file.
    os.remove(file_path)

  except requests.exceptions.RequestException as e:
    print(f'An error occurred while downloading the file: {e}')
  except (zipfile.BadZipFile, tarfile.TarError) as e:
    print(f'An error occurred while extracting the file: {e}')
  except (ValueError, FileNotFoundError) as e:
    print(e)
  except OSError as e:
    print(f'Error with disk space or directory: {e}')
