/*
 * Copyright (c) 2026 University of Salerno
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "datasets.hpp"

namespace {

void printUsage(const char* prog) {
  std::cerr << "Usage: " << prog << " <input.mtx> <output.bin> [--undirected|-u]" << std::endl;
}

template<typename ValueT, typename IndexT, typename OffsetT>
unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT>
symmetrize(const unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT>& input) {
  const std::size_t n_rows = static_cast<std::size_t>(input.getRowOffsetsSize());
  const auto& row_offsets = input.getRowOffsets();
  const auto& col_indices = input.getColumnIndices();
  const auto& values = input.getValues();

  using Edge = std::tuple<IndexT, IndexT, ValueT>;
  std::vector<Edge> edges;
  edges.reserve(static_cast<std::size_t>(input.getNumNonzeros()) * 2);

  for (std::size_t row = 0; row < n_rows; ++row) {
    const std::size_t begin = static_cast<std::size_t>(row_offsets[row]);
    const std::size_t end = static_cast<std::size_t>(row_offsets[row + 1]);
    for (std::size_t idx = begin; idx < end; ++idx) {
      const IndexT src = static_cast<IndexT>(row);
      const IndexT dst = col_indices[idx];
      const ValueT val = values[idx];

      edges.emplace_back(src, dst, val);
      if (src != dst) {
        edges.emplace_back(dst, src, val);
      }
    }
  }

  std::sort(edges.begin(), edges.end(), [](const Edge& lhs, const Edge& rhs) {
    if (std::get<0>(lhs) != std::get<0>(rhs)) {
      return std::get<0>(lhs) < std::get<0>(rhs);
    }
    return std::get<1>(lhs) < std::get<1>(rhs);
  });

  edges.erase(std::unique(edges.begin(), edges.end(), [](const Edge& lhs, const Edge& rhs) {
                return std::get<0>(lhs) == std::get<0>(rhs) && std::get<1>(lhs) == std::get<1>(rhs);
              }),
              edges.end());

  std::vector<OffsetT> out_row_offsets(n_rows + 1, static_cast<OffsetT>(0));
  for (const auto& edge : edges) {
    const std::size_t row = static_cast<std::size_t>(std::get<0>(edge));
    ++out_row_offsets[row + 1];
  }

  for (std::size_t row = 0; row < n_rows; ++row) {
    out_row_offsets[row + 1] = static_cast<OffsetT>(out_row_offsets[row] + out_row_offsets[row + 1]);
  }

  std::vector<IndexT> out_col_indices(edges.size());
  std::vector<ValueT> out_values(edges.size());
  std::vector<OffsetT> next_offset = out_row_offsets;

  for (const auto& edge : edges) {
    const std::size_t row = static_cast<std::size_t>(std::get<0>(edge));
    const std::size_t dst = static_cast<std::size_t>(next_offset[row]++);
    out_col_indices[dst] = std::get<1>(edge);
    out_values[dst] = std::get<2>(edge);
  }

  return unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT>(
      std::move(out_row_offsets), std::move(out_col_indices), std::move(out_values));
}

template<typename ValueT, typename IndexT, typename OffsetT>
int convertMatrixMarket(const std::string& input_path, const std::string& output_path, bool force_undirected) {
  std::ifstream in_file(input_path);
  if (!in_file.is_open()) {
    std::cerr << "Error: could not open file " << input_path << std::endl;
    return 1;
  }

  unisahpc::datasets::graph::Properties properties;
  auto csr = unisahpc::datasets::io::csr::fromMM<ValueT, IndexT, OffsetT>(in_file, &properties);

  if (force_undirected) {
    csr = symmetrize(csr);
    properties.directed = false;
  }

  std::ofstream out_file(output_path, std::ios::binary);
  if (!out_file.is_open()) {
    std::cerr << "Error: could not open output file " << output_path << std::endl;
    return 1;
  }

  unisahpc::datasets::io::csr::toBinary(csr, out_file, properties);
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
  bool force_undirected = false;

  for (int arg_idx = 3; arg_idx < argc; ++arg_idx) {
    const std::string arg = argv[arg_idx];
    if (arg == "-u" || arg == "--undirected") {
      force_undirected = true;
      continue;
    }

    std::cerr << "Error: unknown option '" << arg << "'" << std::endl;
    printUsage(argv[0]);
    return 1;
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
      return convertMatrixMarket<uint32_t, uint32_t, uint32_t>(input_path, output_path, force_undirected);
    }
    if (banner.isReal() || banner.isPattern()) {
      return convertMatrixMarket<float, uint32_t, uint32_t>(input_path, output_path, force_undirected);
    }

    std::cerr << "Error: unsupported field type" << std::endl;
    return 1;
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << std::endl;
    return 1;
  }
}
