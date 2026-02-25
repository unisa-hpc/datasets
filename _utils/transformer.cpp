/*
 * Copyright (c) 2026 University of Salerno
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "datasets.hpp"

namespace {

void printUsage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " <input.mtx> <output.mtx> [--operation|-o <sort|symmetrize|both>]..."
            << std::endl;
}

enum class TransformOperation {
  sort,
  symmetrize,
};

bool parseOperation(const std::string& value, std::vector<TransformOperation>& operations) {
  if (value == "sort") {
    operations.push_back(TransformOperation::sort);
    return true;
  }
  if (value == "symmetrize") {
    operations.push_back(TransformOperation::symmetrize);
    return true;
  }
  if (value == "both") {
    operations.push_back(TransformOperation::symmetrize);
    operations.push_back(TransformOperation::sort);
    return true;
  }
  return false;
}

template<typename ValueT, typename IndexT>
struct DirectedEdge {
  IndexT src;
  IndexT dst;
  ValueT value;
};

template<typename ValueT, typename IndexT, typename OffsetT>
int transformMatrixMarket(const std::string& input_path,
                          const std::string& output_path,
                          const std::vector<TransformOperation>& operations,
                          bool pattern_output,
                          const char* field_name) {
  std::ifstream in_file(input_path);
  if (!in_file.is_open()) {
    std::cerr << "Error: could not open file " << input_path << std::endl;
    return 1;
  }

  auto csr = unisahpc::datasets::io::csr::fromMM<ValueT, IndexT, OffsetT>(in_file);
  const auto& row_offsets = csr.getRowOffsets();
  const auto& col_indices = csr.getColumnIndices();
  const auto& values = csr.getValues();

  const std::size_t n_rows = static_cast<std::size_t>(csr.getRowOffsetsSize());

  std::ofstream out_file(output_path);
  if (!out_file.is_open()) {
    std::cerr << "Error: could not open output file " << output_path << std::endl;
    return 1;
  }

  if constexpr (std::is_floating_point<ValueT>::value) {
    out_file << std::setprecision(std::numeric_limits<ValueT>::max_digits10);
  }

  std::size_t max_node = (n_rows > 0) ? (n_rows - 1) : 0;
  std::vector<DirectedEdge<ValueT, IndexT>> edges;
  edges.reserve(static_cast<std::size_t>(csr.getNumNonzeros()));
  for (std::size_t row = 0; row < n_rows; ++row) {
    const std::size_t begin = static_cast<std::size_t>(row_offsets[row]);
    const std::size_t end = static_cast<std::size_t>(row_offsets[row + 1]);
    for (std::size_t idx = begin; idx < end; ++idx) {
      const IndexT src = static_cast<IndexT>(row);
      const IndexT dst = col_indices[idx];
      edges.push_back({src, dst, values[idx]});

      const std::size_t src_idx = static_cast<std::size_t>(src);
      const std::size_t dst_idx = static_cast<std::size_t>(dst);
      if (src_idx > max_node) {
        max_node = src_idx;
      }
      if (dst_idx > max_node) {
        max_node = dst_idx;
      }
    }
  }

  bool symmetric_output = false;
  for (const auto operation : operations) {
    if (operation == TransformOperation::sort) {
      std::sort(edges.begin(), edges.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.src != rhs.src) {
          return lhs.src < rhs.src;
        }
        return lhs.dst < rhs.dst;
      });
      continue;
    }

    if (operation == TransformOperation::symmetrize) {
      std::vector<DirectedEdge<ValueT, IndexT>> sym_edges;
      sym_edges.reserve(edges.size());

      std::unordered_set<std::uint64_t> seen;
      seen.reserve(edges.size() * 2);

      for (const auto& edge : edges) {
        const IndexT u = std::min(edge.src, edge.dst);
        const IndexT v = std::max(edge.src, edge.dst);
        const std::uint64_t key = (static_cast<std::uint64_t>(u) << 32) | static_cast<std::uint64_t>(v);
        if (seen.insert(key).second) {
          sym_edges.push_back({u, v, edge.value});
        }
      }

      edges = std::move(sym_edges);
      symmetric_output = true;
    }
  }

  const std::size_t n_nodes = (n_rows == 0 && edges.empty()) ? 0 : (max_node + 1);
  out_file << "%%MatrixMarket matrix coordinate " << field_name << " "
           << (symmetric_output ? "symmetric" : "general") << "\n";

  out_file << "% transformed with";
  for (const auto operation : operations) {
    if (operation == TransformOperation::sort) {
      out_file << " sort";
    } else if (operation == TransformOperation::symmetrize) {
      out_file << " symmetrize";
    }
  }
  out_file << "\n";
  out_file << n_nodes << " " << n_nodes << " " << edges.size() << "\n";

  for (const auto& edge : edges) {
    out_file << static_cast<std::uint64_t>(edge.src) + 1 << " " << static_cast<std::uint64_t>(edge.dst) + 1;
    if (!pattern_output) {
      out_file << " " << edge.value;
    }
    out_file << "\n";
  }

  return 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    printUsage(argv[0]);
    return 1;
  }

  const std::string input_path = argv[1];
  const std::string output_path = argv[2];
  std::vector<TransformOperation> operations;

  for (int arg_idx = 3; arg_idx < argc; ++arg_idx) {
    const std::string arg = argv[arg_idx];
    if (arg == "-o" || arg == "--operation") {
      if ((arg_idx + 1) >= argc) {
        std::cerr << "Error: missing value for " << arg << std::endl;
        printUsage(argv[0]);
        return 1;
      }
      const std::string value = argv[++arg_idx];
      if (!parseOperation(value, operations)) {
        std::cerr << "Error: invalid operation '" << value << "'" << std::endl;
        printUsage(argv[0]);
        return 1;
      }
      continue;
    }

    std::cerr << "Error: unknown option '" << arg << "'" << std::endl;
    printUsage(argv[0]);
    return 1;
  }
  if (operations.empty()) {
    operations.push_back(TransformOperation::symmetrize);
    operations.push_back(TransformOperation::sort);
  }

  std::ifstream in_file(input_path);
  if (!in_file.is_open()) {
    std::cerr << "Error: could not open file " << input_path << std::endl;
    return 1;
  }

  std::string line;
  if (!std::getline(in_file, line)) {
    std::cerr << "Error: empty input file " << input_path << std::endl;
    return 1;
  }

  unisahpc::datasets::io::mm::Banner banner;
  try {
    banner.read(line);

    if (banner.isInteger()) {
      return transformMatrixMarket<std::uint32_t, std::uint32_t, std::uint32_t>(
          input_path, output_path, operations, false, "integer");
    }
    if (banner.isReal()) {
      return transformMatrixMarket<float, std::uint32_t, std::uint32_t>(
          input_path, output_path, operations, false, "real");
    }
    if (banner.isPattern()) {
      return transformMatrixMarket<float, std::uint32_t, std::uint32_t>(
          input_path, output_path, operations, true, "pattern");
    }

    std::cerr << "Error: unsupported field type" << std::endl;
    return 1;
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << std::endl;
    return 1;
  }
}
