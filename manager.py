#!/usr/bin/env python
from dataclasses import dataclass
from typing import Callable, Dict, List
import argparse
import glob
import os
import sys
import traceback
import _utils as utl

import yaml


@dataclass
class OperationFailure:
  operation: str
  graph: str
  message: str


def get_available_graphs() -> Dict[str, Dict]:
  graphs = {}
  for f in glob.glob('*'):
    # if f is folder, then it is a graph
    if os.path.isdir(f) and not f.startswith('_') and not f.startswith('.'):
      # check if there is a info.yaml file inside
      path = f + '/info.yaml'
      if os.path.isfile(path):
        # read yaml field and append to graphs
        with open(path, 'r') as file:
          info = yaml.safe_load(file)
          info['folder'] = os.path.abspath(f)
          name = info['name']
          graphs[name] = info
        
  return graphs


def list_command(args: argparse.Namespace, graphs: Dict[str, Dict]):
  if not args.filter:
    args.filter = []
  utl.list_items(graphs, order_by=args.order_by, desc=args.desc, filters=args.filter)

def _selected_graphs(args: argparse.Namespace, graphs: Dict[str, Dict]) -> List[str]:
  if args.all:
    return list(graphs.keys())
  if isinstance(args.graph, str):
    return [args.graph]
  return args.graph


def _print_error(operation: str, graph: str, message: str):
  print(f'ERROR [{operation}] [{graph}]: {message}', file=sys.stderr)


def _run_graph_operation(
    args: argparse.Namespace,
    graphs: Dict[str, Dict],
    operation: str,
    graph_names: List[str],
    runner: Callable[[str, Dict], None],
):
  failures = []
  total = len(graph_names)
  for name in graph_names:
    info = graphs.get(name)
    if info is None:
      message = f'Graph "{name}" not found'
      _print_error(operation, name, message)
      failures.append(OperationFailure(operation=operation, graph=name, message=message))
      continue

    try:
      runner(name, info)
    except Exception as exc:
      error_operation = operation
      error_graph = name
      if isinstance(exc, utl.DatasetOperationError):
        error_operation = exc.operation
        if exc.graph:
          error_graph = exc.graph

      _print_error(error_operation, error_graph, str(exc))
      if args.debug:
        traceback.print_exc(file=sys.stderr)
      failures.append(
          OperationFailure(
              operation=error_operation,
              graph=error_graph,
              message=str(exc),
          )
      )

  if failures:
    print(f'ERROR [{operation}]: {len(failures)} of {total} operation(s) failed.', file=sys.stderr)
    raise SystemExit(1)


def download_command(args: argparse.Namespace, graphs: Dict[str, Dict]):
  to_download = _selected_graphs(args, graphs)

  def _download(name: str, info: Dict):
    folder = info['folder']
    if args.destination:
      folder = os.path.join(args.destination, name)

    utl.download_and_extract(
        name,
        info['url'],
        folder,
        folder,
        always_yes=args.yes,
        always_no=args.no,
    )

  _run_graph_operation(args, graphs, 'download', to_download, _download)


def clean_command(args: argparse.Namespace, graphs: Dict[str, Dict]):
  to_clean = _selected_graphs(args, graphs)

  def _clean(_: str, info: Dict):
    utl.clean_graph(info['folder'], args.only_installation)

  _run_graph_operation(args, graphs, 'clean', to_clean, _clean)


def convert_command(args: argparse.Namespace, graphs: Dict[str, Dict]):
  try:
    converter_path = utl.ensure_converter(args.converter_path, force_rebuild=args.update)
  except Exception as exc:
    raise utl.ConvertError(str(exc)) from exc

  to_convert = _selected_graphs(args, graphs)

  def _convert(name: str, info: Dict):
    folder = info['folder']
    if args.destination:
      folder = os.path.join(args.destination, name)
    utl.convert_graph(converter_path, folder, args.undirected, args.always)

  _run_graph_operation(args, graphs, 'convert', to_convert, _convert)


def _requests_binary_output(transformations) -> bool:
  if transformations is None:
    return False
  if isinstance(transformations, str):
    transformations = [transformations]
  return 'binary' in transformations


def transform_command(args: argparse.Namespace, graphs: Dict[str, Dict]):
  transformations = args.transformations

  try:
    transformer_path = utl.ensure_transformer(args.transformer_path, force_rebuild=args.update)
    converter_path = None
    if _requests_binary_output(transformations):
      converter_path = utl.ensure_converter(args.converter_path, force_rebuild=args.update)
  except Exception as exc:
    raise utl.TransformError(str(exc)) from exc

  to_transform = _selected_graphs(args, graphs)

  def _transform(name: str, info: Dict):
    folder = info['folder']
    if args.destination:
      folder = os.path.join(args.destination, name)
    utl.transform_graph(
        transformer_path,
        folder,
        transformations,
        args.always,
        converter_path=converter_path,
        keep_transformed=not args.discard_transformed,
    )

  _run_graph_operation(args, graphs, 'transform', to_transform, _transform)


def move_command(args: argparse.Namespace, graphs: Dict[str, Dict]):
  to_move = _selected_graphs(args, graphs)

  def _move(name: str, _: Dict):
    utl.move_graph_files(name, source_root=args.source, destination_root=args.destination)

  _run_graph_operation(args, graphs, 'move', to_move, _move)


def info_command(args: argparse.Namespace, graphs: Dict[str, Dict]):
  if args.graph not in graphs:
    raise ValueError(f'Graph "{args.graph}" not found')
  info = graphs[args.graph]
  if args.json:
    print(info)
  else:
    utl.print_graph(info)


def main() -> int:
  graphs = get_available_graphs()
  
  parser = argparse.ArgumentParser(description='Graph repo manager')
  parser.add_argument('--debug', action='store_true', help='Show traceback details when an error occurs')
  # define commands 
  subparsers = parser.add_subparsers(dest='command', required=True)
  
  # list command
  list_parser = subparsers.add_parser('list', help='List available graphs', description='List available graphs')
  list_parser.set_defaults(func=list_command)
  list_parser.add_argument('--order-by', choices=['name', 'date', 'nodes', 'edges'], default='name', help='Order by field')
  list_parser.add_argument('--desc', action='store_true', help='Order in decreasing order')
  list_parser.add_argument('-f', '--filter', nargs='*', help='Filter graphs by parameter. Format: field[=,<,>,<=,>=]value', metavar='FIELD[=,<,>,<=,>=]VALUE')
  
  # add download command
  download_parser = subparsers.add_parser('download', help='Download a graph', description='Download one or more graph')
  # set a list of graphs as argument of the download command
  download_parser.set_defaults(func=download_command)
  download_parser.add_argument('-a', '--all', action='store_true', help='Download all graphs')
  download_parser.add_argument('graph', nargs='*', help='graph(s) to download', metavar='GRAPH')
  download_parser.add_argument('-d', '--destination', help='Destination folder')
  download_behavior_group = download_parser.add_mutually_exclusive_group()
  download_behavior_group.add_argument('-y', '--yes', action='store_true', help='Always overwrite existing downloaded files')
  download_behavior_group.add_argument('-N', '--no', action='store_true', help='Never overwrite existing downloaded files')

  # add info command
  info_parser = subparsers.add_parser('info', help='Get info of a graph', description='Get info of a graph')
  info_parser.set_defaults(func=info_command)
  # set a list of graphs as argument of the info command
  info_parser.add_argument('graph', help='graph to analyze', metavar='GRAPH')
  info_parser.add_argument('--json', action='store_true', help='Print info in json format')
  
  # add clean command
  clean_parser = subparsers.add_parser('clean', help='Clean downloaded graphs', description='Clean one or more downloaded graphs')
  clean_parser.set_defaults(func=clean_command)
  clean_parser.add_argument('-a', '--all', action='store_true', help='Clean all graphs')
  clean_parser.add_argument('--only-installation', action='store_true', help='Clean only installation files')
  clean_parser.add_argument('graph', nargs='*', help='Graph(s) to clean', metavar='GRAPH')
  
  # add convert command
  convert_parser = subparsers.add_parser('convert', help='Convert a graph', description='Convert a graph to binary format')
  convert_parser.set_defaults(func=convert_command)
  convert_parser.add_argument('--converter-path', default=utl.DEFAULT_CONVERTER_PATH, help=f'Path to converter executable (default: {utl.DEFAULT_CONVERTER_PATH})')
  convert_parser.add_argument('-a', '--all', action='store_true', help='Download all graphs')
  convert_parser.add_argument('-u', '--undirected', action='store_true', help='Convert to undirected graph')
  convert_parser.add_argument('--update', action='store_true', help='Force rebuilding the default converter before conversion')
  convert_parser.add_argument('-y', '--always', action='store_true', help='Always convert the graph')
  convert_parser.add_argument('-d', '--destination', help='Destination folder')
  convert_parser.add_argument('graph', nargs='*', help='graph(s) to download', metavar='GRAPH')

  # add transform command
  transform_parser = subparsers.add_parser('transform', help='Transform a graph', description='Transform a graph Matrix Market file with a transformation pipeline')
  transform_parser.set_defaults(func=transform_command)
  transform_parser.add_argument('--transformer-path', default=utl.DEFAULT_TRANSFORMER_PATH, help=f'Path to transformer executable (default: {utl.DEFAULT_TRANSFORMER_PATH})')
  transform_parser.add_argument('--converter-path', default=utl.DEFAULT_CONVERTER_PATH, help=f'Path to converter executable used by the binary stage (default: {utl.DEFAULT_CONVERTER_PATH})')
  transform_parser.add_argument('-t', '--transformations', nargs='+', default=['symmetrize', 'sort'], metavar='TRANSFORM', help='Ordered transformation pipeline (available: symmetrize sort binary; default: symmetrize sort)')
  transform_parser.add_argument('-a', '--all', action='store_true', help='Transform all graphs')
  transform_parser.add_argument('--update', action='store_true', help='Force rebuilding the default transformer and, if needed, converter before transformation')
  transform_parser.add_argument('-y', '--always', action='store_true', help='Always transform the graph')
  transform_parser.add_argument('--discard-transformed', action='store_true', help='Discard the intermediate .transformed.mtx after successful binary conversion')
  transform_parser.add_argument('-d', '--destination', help='Destination folder')
  transform_parser.add_argument('graph', nargs='*', help='graph(s) to transform', metavar='GRAPH')

  # add move command
  move_parser = subparsers.add_parser('move', help='Move downloaded graph files', description='Move .mtx/.bin files for one or more graphs')
  move_parser.set_defaults(func=move_command)
  move_parser.add_argument('-a', '--all', action='store_true', help='Move all graphs')
  move_parser.add_argument('-s', '--source', default='.', help='Source root folder (default: current working directory)')
  move_parser.add_argument('-d', '--destination', required=True, help='Destination root folder')
  move_parser.add_argument('graph', nargs='*', help='graph(s) to move', metavar='GRAPH')
  
  # parse arguments
  args = parser.parse_args()
  try:
    args.func(args, graphs)
    return 0
  except SystemExit:
    raise
  except utl.DatasetOperationError as exc:
    graph = exc.graph if exc.graph else 'global'
    _print_error(exc.operation, graph, str(exc))
    if args.debug:
      traceback.print_exc(file=sys.stderr)
    return 1
  except ValueError as exc:
    print(f'ERROR: {exc}', file=sys.stderr)
    if args.debug:
      traceback.print_exc(file=sys.stderr)
    return 1
  

if __name__ == '__main__':
  debug_mode = '--debug' in sys.argv
  try:
    raise SystemExit(main())
  except SystemExit:
    raise
  except Exception as exc:
    if debug_mode:
      traceback.print_exc(file=sys.stderr)
    else:
      print(f'ERROR: unexpected error: {exc}', file=sys.stderr)
      print('Re-run with --debug for traceback details.', file=sys.stderr)
    raise SystemExit(1)
  
