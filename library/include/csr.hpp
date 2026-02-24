/*
 * Copyright (c) 2026 University of Salerno
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "matrix_market.hpp"
#include "properties.hpp"

namespace unisahpc::datasets {
namespace formats {

/**
 * @class CSR
 * @brief Compressed Sparse Row (CSR) matrix format.
 */
template<typename ValueT, typename IndexT, typename OffsetT>
class CSR {
public:
  CSR() = default;

  CSR(std::vector<OffsetT> row_offsets, std::vector<IndexT> column_indices, std::vector<ValueT> nnz_values)
      : _row_offsets(std::move(row_offsets)),
        _column_indices(std::move(column_indices)),
        _nnz_values(std::move(nnz_values)) {}

  CSR(IndexT n_rows, OffsetT n_nonzeros) {
    _row_offsets.resize(static_cast<std::size_t>(n_rows) + 1);
    _column_indices.resize(static_cast<std::size_t>(n_nonzeros));
    _nnz_values.resize(static_cast<std::size_t>(n_nonzeros));
  }

  ~CSR() = default;

  IndexT getRowOffsetsSize() const { return static_cast<IndexT>(_row_offsets.size() - 1); }
  OffsetT getNumNonzeros() const { return static_cast<OffsetT>(_column_indices.size()); }

  const std::vector<OffsetT>& getRowOffsets() const { return _row_offsets; }
  std::vector<OffsetT>& getRowOffsets() { return _row_offsets; }

  const std::vector<IndexT>& getColumnIndices() const { return _column_indices; }
  std::vector<IndexT>& getColumnIndices() { return _column_indices; }

  const std::vector<ValueT>& getValues() const { return _nnz_values; }
  std::vector<ValueT>& getValues() { return _nnz_values; }

  void setRowOffsets(const std::vector<OffsetT>& offsets) { _row_offsets = offsets; }
  void setColumnIndices(const std::vector<IndexT>& indices) { _column_indices = indices; }
  void setNnzValues(const std::vector<ValueT>& values) { _nnz_values = values; }

  CSR<ValueT, IndexT, OffsetT> invert() const;

private:
  std::vector<OffsetT> _row_offsets;
  std::vector<IndexT> _column_indices;
  std::vector<ValueT> _nnz_values;
};

template<typename ValueT, typename IndexT, typename OffsetT>
CSR<ValueT, IndexT, OffsetT> CSR<ValueT, IndexT, OffsetT>::invert() const {
  if (_row_offsets.empty()) {
    return CSR<ValueT, IndexT, OffsetT>();
  }

  const std::size_t n_rows = _row_offsets.size() - 1;
  const std::size_t nnz = _column_indices.size();

  std::vector<OffsetT> row_offsets_t(n_rows + 1, static_cast<OffsetT>(0));
  for (const auto col_idx : _column_indices) {
    const std::size_t col = static_cast<std::size_t>(col_idx);
    if (col >= n_rows) {
      throw std::runtime_error("CSR invert failed: column index out of bounds");
    }
    ++row_offsets_t[col + 1];
  }

  for (std::size_t row = 0; row < n_rows; ++row) {
    row_offsets_t[row + 1] = static_cast<OffsetT>(row_offsets_t[row + 1] + row_offsets_t[row]);
  }

  std::vector<IndexT> column_indices_t(nnz);
  std::vector<ValueT> values_t(nnz);
  std::vector<OffsetT> next_offset = row_offsets_t;

  for (std::size_t row = 0; row < n_rows; ++row) {
    const std::size_t row_begin = static_cast<std::size_t>(_row_offsets[row]);
    const std::size_t row_end = static_cast<std::size_t>(_row_offsets[row + 1]);
    if (row_end < row_begin || row_end > nnz) {
      throw std::runtime_error("CSR invert failed: invalid row offsets");
    }

    for (std::size_t idx = row_begin; idx < row_end; ++idx) {
      const std::size_t col = static_cast<std::size_t>(_column_indices[idx]);
      std::size_t dst = static_cast<std::size_t>(next_offset[col]++);
      column_indices_t[dst] = static_cast<IndexT>(row);
      values_t[dst] = _nnz_values[idx];
    }
  }

  return CSR<ValueT, IndexT, OffsetT>(std::move(row_offsets_t), std::move(column_indices_t), std::move(values_t));
}

} // namespace formats

namespace io::csr {
namespace detail {
namespace binary {
static constexpr uint64_t magic = 0x5359475243535201ULL; // "SYGCSR" + version marker
static constexpr uint8_t directed_mask = 0x1;
static constexpr uint8_t weighted_mask = 0x2;
} // namespace binary

template<typename To, typename From>
To checked_non_negative_cast(From value, const char* context) {
  static_assert(std::is_integral<To>::value, "checked_non_negative_cast requires integral target type");
  static_assert(std::is_integral<From>::value, "checked_non_negative_cast requires integral source type");

  if constexpr (std::is_signed<From>::value) {
    if (value < 0) {
      throw std::runtime_error(std::string("Negative value for ") + context);
    }
  }

  using UnsignedFrom = typename std::make_unsigned<From>::type;
  const auto unsigned_value = static_cast<std::uintmax_t>(static_cast<UnsignedFrom>(value));
  const auto max_target = static_cast<std::uintmax_t>(std::numeric_limits<To>::max());
  if (unsigned_value > max_target) {
    throw std::runtime_error(std::string("Value overflow for ") + context);
  }

  return static_cast<To>(unsigned_value);
}

inline bool is_skippable_line(const std::string& line) {
  const auto first = line.find_first_not_of(" \t\r");
  return (first == std::string::npos) || (line[first] == '%');
}

template<typename T>
void write_pod(std::ostream& oss, const T& value) {
  oss.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
}

template<typename T>
void read_pod(std::istream& iss, T& value) {
  iss.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
}

} // namespace detail

/**
 * @brief Converts a matrix in CSR format from a file to a CSR object.
 */
template<typename ValueT, typename IndexT, typename OffsetT>
unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT> fromCSR(std::istream& iss);

/**
 * @brief Reads a Matrix Market file in coordinate format and converts it to a CSR matrix.
 */
template<typename ValueT, typename IndexT, typename OffsetT>
unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT> fromMM(std::istream& iss, unisahpc::datasets::graph::Properties* properties = nullptr);

/**
 * @brief Reads a Matrix Market file and converts it to a CSR matrix.
 */
template<typename ValueT, typename IndexT, typename OffsetT>
unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT> fromMM(const std::string& filename, unisahpc::datasets::graph::Properties* properties = nullptr);

/**
 * @brief Reads a CSR matrix from a binary input stream.
 */
template<typename ValueT, typename IndexT, typename OffsetT>
unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT> fromBinary(std::istream& iss, unisahpc::datasets::graph::Properties* properties = nullptr);

/**
 * @brief Serializes a CSR matrix to a binary stream.
 */
template<typename ValueT, typename IndexT, typename OffsetT>
void toBinary(const unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT>& csr,
              std::ostream& oss,
              const unisahpc::datasets::graph::Properties& properties = unisahpc::datasets::graph::Properties());

template<typename ValueT, typename IndexT, typename OffsetT>
unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT> fromCSR(std::istream& iss) {
  return fromBinary<ValueT, IndexT, OffsetT>(iss);
}

template<typename ValueT, typename IndexT, typename OffsetT>
unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT> fromMM(std::istream& iss,
                                                                  unisahpc::datasets::graph::Properties* properties) {
  static_assert(std::is_integral<IndexT>::value, "IndexT must be an integral type");
  static_assert(std::is_integral<OffsetT>::value, "OffsetT must be an integral type");

  std::string line;
  if (!std::getline(iss, line)) {
    throw std::runtime_error("Failed to read MatrixMarket banner");
  }

  unisahpc::datasets::io::mm::Banner banner;
  banner.read(line);
  banner.template validate<ValueT, IndexT, OffsetT>();

  if (!banner.isGeneral() && !banner.isSymmetric()) {
    throw std::runtime_error("Unsupported MatrixMarket symmetry: only general and symmetric are supported");
  }
  if (banner.isComplex()) {
    throw std::runtime_error("Unsupported MatrixMarket field type: complex");
  }

  uint64_t n_rows_u64 = 0;
  uint64_t n_cols_u64 = 0;
  uint64_t declared_nnz_u64 = 0;
  bool found_dimensions = false;

  while (std::getline(iss, line)) {
    if (detail::is_skippable_line(line)) {
      continue;
    }

    std::istringstream dims(line);
    if (!(dims >> n_rows_u64 >> n_cols_u64 >> declared_nnz_u64)) {
      throw std::runtime_error("Invalid MatrixMarket size line");
    }
    found_dimensions = true;
    break;
  }

  if (!found_dimensions) {
    throw std::runtime_error("MatrixMarket size line not found");
  }

  const std::size_t n_rows = detail::checked_non_negative_cast<std::size_t>(n_rows_u64, "number of rows");
  const std::size_t n_cols = detail::checked_non_negative_cast<std::size_t>(n_cols_u64, "number of columns");
  const std::size_t declared_nnz = detail::checked_non_negative_cast<std::size_t>(declared_nnz_u64, "number of non-zeros");

  struct Entry {
    IndexT row;
    IndexT col;
    ValueT value;
  };

  std::size_t reserve_nnz = declared_nnz;
  if (banner.isSymmetric()) {
    if (reserve_nnz > (std::numeric_limits<std::size_t>::max() / 2)) {
      throw std::runtime_error("MatrixMarket file too large");
    }
    reserve_nnz *= 2;
  }

  std::vector<Entry> entries;
  entries.reserve(reserve_nnz);

  std::size_t parsed_nnz = 0;
  while (std::getline(iss, line)) {
    if (detail::is_skippable_line(line)) {
      continue;
    }

    std::istringstream entry_stream(line);
    uint64_t row_u64 = 0;
    uint64_t col_u64 = 0;
    ValueT value{};

    if (banner.isPattern()) {
      if (!(entry_stream >> row_u64 >> col_u64)) {
        throw std::runtime_error("Invalid MatrixMarket pattern entry");
      }
      value = static_cast<ValueT>(1);
    } else {
      if (!(entry_stream >> row_u64 >> col_u64 >> value)) {
        throw std::runtime_error("Invalid MatrixMarket entry");
      }
    }

    if (row_u64 == 0 || col_u64 == 0) {
      throw std::runtime_error("MatrixMarket uses 1-based indices; found zero index");
    }
    if (row_u64 > n_rows_u64 || col_u64 > n_cols_u64) {
      throw std::runtime_error("MatrixMarket index out of matrix bounds");
    }

    const IndexT row = detail::checked_non_negative_cast<IndexT>(row_u64 - 1, "row index");
    const IndexT col = detail::checked_non_negative_cast<IndexT>(col_u64 - 1, "column index");
    entries.push_back({row, col, value});

    if (banner.isSymmetric() && row != col) {
      entries.push_back({col, row, value});
    }

    ++parsed_nnz;
  }

  if (parsed_nnz != declared_nnz) {
    throw std::runtime_error("MatrixMarket non-zero count mismatch");
  }

  std::vector<OffsetT> row_offsets(n_rows + 1, static_cast<OffsetT>(0));
  for (const auto& entry : entries) {
    const std::size_t row = static_cast<std::size_t>(entry.row);
    if (row >= n_rows) {
      throw std::runtime_error("Row index overflow while building CSR");
    }
    ++row_offsets[row + 1];
  }

  for (std::size_t row = 0; row < n_rows; ++row) {
    row_offsets[row + 1] = static_cast<OffsetT>(row_offsets[row + 1] + row_offsets[row]);
  }

  std::vector<IndexT> column_indices(entries.size());
  std::vector<ValueT> values(entries.size());
  std::vector<OffsetT> next_offset = row_offsets;

  for (const auto& entry : entries) {
    const std::size_t row = static_cast<std::size_t>(entry.row);
    const std::size_t dst = static_cast<std::size_t>(next_offset[row]++);
    if (dst >= entries.size()) {
      throw std::runtime_error("Invalid destination offset while building CSR");
    }
    column_indices[dst] = entry.col;
    values[dst] = entry.value;
  }

  if (properties != nullptr) {
    properties->directed = !banner.isSymmetric();
    properties->weighted = !banner.isPattern();
  }

  (void)n_cols;

  return unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT>(
      std::move(row_offsets), std::move(column_indices), std::move(values));
}

template<typename ValueT, typename IndexT, typename OffsetT>
unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT> fromMM(const std::string& filename,
                                                                  unisahpc::datasets::graph::Properties* properties) {
  std::ifstream ifs(filename);
  if (!ifs.is_open()) {
    throw std::runtime_error("Failed to open MatrixMarket file: " + filename);
  }
  return fromMM<ValueT, IndexT, OffsetT>(ifs, properties);
}

template<typename ValueT, typename IndexT, typename OffsetT>
unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT> fromBinary(std::istream& iss,
                                                                      unisahpc::datasets::graph::Properties* properties) {
  static_assert(std::is_integral<IndexT>::value, "IndexT must be an integral type");
  static_assert(std::is_integral<OffsetT>::value, "OffsetT must be an integral type");

  uint64_t magic = 0;
  detail::read_pod(iss, magic);
  if (!iss.good()) {
    throw std::runtime_error("Failed to read binary header magic");
  }
  if (magic != detail::binary::magic) {
    throw std::runtime_error("Invalid binary CSR magic");
  }

  uint8_t header_tail[8] = {};
  iss.read(reinterpret_cast<char*>(header_tail), 8);

  uint64_t n_rows_field_u64 = 0;
  uint64_t nnz_u64 = 0;
  detail::read_pod(iss, n_rows_field_u64);
  detail::read_pod(iss, nnz_u64);

  if (!iss.good()) {
    throw std::runtime_error("Failed to read binary CSR dimensions");
  }

  const std::size_t n_rows_field = detail::checked_non_negative_cast<std::size_t>(n_rows_field_u64, "binary row count");
  const std::size_t nnz = detail::checked_non_negative_cast<std::size_t>(nnz_u64, "binary nnz count");
  if (nnz > static_cast<std::size_t>(std::numeric_limits<OffsetT>::max())) {
    throw std::runtime_error("Binary CSR nnz does not fit in target OffsetT");
  }

  // CLUTRA writes num_rows as row_ptr size, while this project historically wrote matrix rows.
  // Infer the payload layout from remaining bytes whenever the stream is seekable.
  std::size_t row_ptr_size = 0;
  const std::streampos payload_begin = iss.tellg();
  if (payload_begin != std::streampos(-1)) {
    iss.seekg(0, std::ios::end);
    const std::streampos payload_end = iss.tellg();
    iss.seekg(payload_begin);

    if (payload_end != std::streampos(-1) && payload_end >= payload_begin) {
      const auto remaining = static_cast<std::uintmax_t>(payload_end - payload_begin);
      const auto edge_bytes = static_cast<std::uintmax_t>(nnz) * static_cast<std::uintmax_t>(sizeof(IndexT) + sizeof(ValueT));
      if (remaining >= edge_bytes) {
        const auto row_bytes = remaining - edge_bytes;
        if ((row_bytes % sizeof(OffsetT)) == 0) {
          const std::size_t candidate_row_ptr_size = static_cast<std::size_t>(row_bytes / sizeof(OffsetT));
          if (candidate_row_ptr_size == n_rows_field || candidate_row_ptr_size == (n_rows_field + 1)) {
            row_ptr_size = candidate_row_ptr_size;
          }
        }
      }
    }
  }

  // Fallback heuristic for non-seekable streams.
  if (row_ptr_size == 0) {
    const bool looks_like_clutra_header = (header_tail[0] == 1);
    if (looks_like_clutra_header) {
      row_ptr_size = n_rows_field;
    } else {
      if (n_rows_field == std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Invalid binary CSR row count");
      }
      row_ptr_size = n_rows_field + 1;
    }
  }

  if (row_ptr_size == 0) {
    throw std::runtime_error("Invalid binary CSR row pointer size");
  }
  if (static_cast<std::uintmax_t>(row_ptr_size - 1) > static_cast<std::uintmax_t>(std::numeric_limits<IndexT>::max())) {
    throw std::runtime_error("Binary CSR row pointer size does not fit in target IndexT");
  }

  std::vector<OffsetT> row_offsets(row_ptr_size);
  std::vector<IndexT> column_indices(nnz);
  std::vector<ValueT> values(nnz);

  if (!row_offsets.empty()) {
    iss.read(reinterpret_cast<char*>(row_offsets.data()), static_cast<std::streamsize>(row_ptr_size * sizeof(OffsetT)));
  }
  if (!column_indices.empty()) {
    iss.read(reinterpret_cast<char*>(column_indices.data()), static_cast<std::streamsize>(nnz * sizeof(IndexT)));
  }
  if (!values.empty()) {
    iss.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(nnz * sizeof(ValueT)));
  }

  if (!iss.good()) {
    throw std::runtime_error("Failed to read binary CSR payload");
  }

  if (row_offsets.empty()) {
    throw std::runtime_error("Invalid binary CSR payload: missing row offsets");
  }
  if (row_offsets[0] != static_cast<OffsetT>(0)) {
    throw std::runtime_error("Invalid binary CSR payload: row_offsets[0] must be 0");
  }

  const std::size_t n_rows = row_ptr_size - 1;
  for (std::size_t row = 0; row < n_rows; ++row) {
    const OffsetT curr = row_offsets[row];
    const OffsetT next = row_offsets[row + 1];

    if constexpr (std::is_signed<OffsetT>::value) {
      if (curr < 0 || next < 0) {
        throw std::runtime_error("Invalid binary CSR payload: negative row offset");
      }
    }

    if (next < curr) {
      throw std::runtime_error("Invalid binary CSR payload: non-monotonic row offsets");
    }
    if (static_cast<std::size_t>(next) > nnz) {
      throw std::runtime_error("Invalid binary CSR payload: row offset exceeds nnz");
    }
  }

  if (static_cast<std::size_t>(row_offsets.back()) != nnz) {
    throw std::runtime_error("Invalid binary CSR payload: row offsets and nnz mismatch");
  }

  const uint8_t flags = (row_ptr_size == n_rows_field) ? header_tail[1] : header_tail[0];
  if (properties != nullptr) {
    properties->directed = (flags & detail::binary::directed_mask) != 0;
    properties->weighted = (flags & detail::binary::weighted_mask) != 0;
  }

  return unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT>(
      std::move(row_offsets), std::move(column_indices), std::move(values));
}

template<typename ValueT, typename IndexT, typename OffsetT>
void toBinary(const unisahpc::datasets::formats::CSR<ValueT, IndexT, OffsetT>& csr,
              std::ostream& oss,
              const unisahpc::datasets::graph::Properties& properties) {
  static_assert(std::is_integral<IndexT>::value, "IndexT must be an integral type");
  static_assert(std::is_integral<OffsetT>::value, "OffsetT must be an integral type");

  const auto& row_offsets = csr.getRowOffsets();
  const auto& column_indices = csr.getColumnIndices();
  const auto& values = csr.getValues();

  const std::size_t n_rows = static_cast<std::size_t>(csr.getRowOffsetsSize());
  const std::size_t nnz = static_cast<std::size_t>(csr.getNumNonzeros());

  if (row_offsets.size() != (n_rows + 1)) {
    throw std::runtime_error("Invalid CSR row offsets size");
  }
  if (column_indices.size() != nnz || values.size() != nnz) {
    throw std::runtime_error("Invalid CSR column/value size");
  }
  if (row_offsets.empty()) {
    throw std::runtime_error("Invalid CSR row offsets content");
  }
  if (row_offsets[0] != static_cast<OffsetT>(0)) {
    throw std::runtime_error("Invalid CSR row offsets content");
  }
  for (std::size_t row = 0; row < n_rows; ++row) {
    const OffsetT curr = row_offsets[row];
    const OffsetT next = row_offsets[row + 1];

    if constexpr (std::is_signed<OffsetT>::value) {
      if (curr < 0 || next < 0) {
        throw std::runtime_error("Invalid CSR row offsets content");
      }
    }

    if (next < curr) {
      throw std::runtime_error("Invalid CSR row offsets content");
    }
  }
  if (static_cast<std::size_t>(row_offsets.back()) != nnz) {
    throw std::runtime_error("Invalid CSR row offsets content");
  }

  if (!oss.good()) {
    throw std::runtime_error("Output stream is not writable");
  }

  detail::write_pod(oss, detail::binary::magic);

  uint8_t flags = 0;
  if (properties.directed) {
    flags |= detail::binary::directed_mask;
  }
  if (properties.weighted) {
    flags |= detail::binary::weighted_mask;
  }

  // CLUTRA-compatible header tail: version (1), flags, reserved16, reserved32.
  const uint8_t version = 1;
  const uint16_t reserved16 = 0;
  const uint32_t reserved32 = 0;
  detail::write_pod(oss, version);
  detail::write_pod(oss, flags);
  detail::write_pod(oss, reserved16);
  detail::write_pod(oss, reserved32);

  // CLUTRA-compatible semantics: store row pointer size, not matrix row count.
  const uint64_t n_rows_u64 = detail::checked_non_negative_cast<uint64_t>(row_offsets.size(), "row pointer size");
  const uint64_t nnz_u64 = detail::checked_non_negative_cast<uint64_t>(nnz, "nnz count");
  detail::write_pod(oss, n_rows_u64);
  detail::write_pod(oss, nnz_u64);

  if (!row_offsets.empty()) {
    oss.write(reinterpret_cast<const char*>(row_offsets.data()), static_cast<std::streamsize>(row_offsets.size() * sizeof(OffsetT)));
  }
  if (!column_indices.empty()) {
    oss.write(reinterpret_cast<const char*>(column_indices.data()), static_cast<std::streamsize>(column_indices.size() * sizeof(IndexT)));
  }
  if (!values.empty()) {
    oss.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(ValueT)));
  }

  if (!oss.good()) {
    throw std::runtime_error("Failed while writing binary CSR payload");
  }
}

} // namespace io::csr
} // namespace unisahpc::datasets
