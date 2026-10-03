// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// HDF5 array store (src/io/array/): round trips of the core API and of the std, Eigen and
// CHOLMOD adapters, error codes, attributes, compression and concurrent use.

#include "testSupport.hpp"

#include <io/array/eigenArrays.hpp>
#include <io/array/stdArrays.hpp>
#ifdef ANAFINEN_HAS_CHOLMOD
#include <io/array/cholmodArrays.hpp>
#endif
#include <io/core/pathUtf8.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace anaf::IO::ARRAY;
using Code = ArrayError::Code;
namespace fs = std::filesystem;

namespace {

  fs::path workDir() {
    static const fs::path dir = [] {
      auto path = fs::temp_directory_path() / std::format("anaf_array_tests_{}", std::chrono::steady_clock::now().time_since_epoch().count());
      fs::create_directories(path);
      return path;
    }();
    return dir;
  }

  ArrayFile createFile(const std::string& name) {
    auto file = ArrayFile::create(workDir() / name);
    if (!file) std::printf("      create failed: %s\n", file.error().message.c_str());
    REQUIRE(file.has_value());
    return std::move(*file);
  }

  ArrayFile reopen(ArrayFile& file, const ArrayFile::Access access = ArrayFile::Access::ReadOnly) {
    const fs::path path = file.path();
    REQUIRE(file.close().has_value());
    auto opened = ArrayFile::open(path, access);
    if (!opened) std::printf("      open failed: %s\n", opened.error().message.c_str());
    REQUIRE(opened.has_value());
    return std::move(*opened);
  }

  template <class T>
  void requireOk(const Result<T>& result, const std::string_view label) {
    if (!result) std::printf("      [%.*s] %s\n", static_cast<int>(label.size()), label.data(), result.error().message.c_str());
    REQUIRE(result.has_value());
  }

  template <class T>
  bool failsWith(const Result<T>& result, const Code code) {
    return !result && result.error().code == code;
  }

  // Value of a result that must succeed; reports the error message otherwise.
  template <class T>
  T take(Result<T> result, const std::string_view label = "result") {
    requireOk(result, label);
    return std::move(*result);
  }

  template <Scalar T>
  T sample(const std::size_t i) {
    if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
      using Part = typename T::value_type;
      return T(static_cast<Part>(i) * Part(0.5) - 3, Part(1) / static_cast<Part>(i + 1));
    }
    else if constexpr (std::is_floating_point_v<T>) return static_cast<T>(i) / T(3) - T(7.25);
    else if constexpr (std::is_signed_v<T>) return static_cast<T>(i * 1000003) - T(500);
    else return static_cast<T>(i) * T(0x100000001ULL);
  }

  template <Scalar T>
  void roundTripDense(ArrayFile& file, const std::string& name, const std::vector<std::uint64_t>& shape) {
    std::uint64_t count = 1;
    for (const auto extent : shape) count *= extent;
    std::vector<T> values(count);
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = sample<T>(i);
    requireOk(file.writeDense<T>(name, values, shape), name);

    auto info = file.info(name);
    requireOk(info, name);
    CHECK(info->kind == ObjectKind::Dense);
    CHECK(info->scalar == ScalarTraits<T>::type);
    CHECK(info->shape == shape);

    auto read = file.readDense<T>(name);
    requireOk(read, name);
    CHECK_MSG(read->shape == shape, name);
    CHECK_MSG(read->values == values, name);
  }

} // namespace end

TEST(denseRoundTripEveryScalarTypeAndRank) {
  auto file = createFile("dense.h5");
  roundTripDense<double>(file, "f64/scalar", {});
  roundTripDense<double>(file, "f64/vector", {17});
  roundTripDense<double>(file, "f64/matrix", {4, 5});
  roundTripDense<double>(file, "f64/tensor", {2, 3, 4});
  roundTripDense<double>(file, "f64/empty", {0, 3});
  roundTripDense<float>(file, "f32/matrix", {3, 2});
  roundTripDense<std::int32_t>(file, "i32/vector", {9});
  roundTripDense<std::int64_t>(file, "i64/tensor", {2, 2, 2, 2});
  roundTripDense<std::uint64_t>(file, "u64/vector", {5});
  roundTripDense<std::complex<float>>(file, "c64/matrix", {2, 3});
  roundTripDense<std::complex<double>>(file, "c128/tensor", {3, 1, 2});

  // The same content after closing and reopening the file.
  auto reopened = reopen(file);
  auto tensor = reopened.readDense<double>("f64/tensor");
  requireOk(tensor, "reopened tensor");
  CHECK(tensor->shape == (std::vector<std::uint64_t>{2, 3, 4}));
  CHECK(tensor->values[23] == sample<double>(23));
  auto complex = reopened.readDense<std::complex<double>>("c128/tensor");
  requireOk(complex, "reopened complex");
  CHECK(complex->values[5] == sample<std::complex<double>>(5));
}

TEST(readsWidenButNeverNarrow) {
  auto file = createFile("widen.h5");
  const std::vector<float> floats = {1.5f, -2.25f};
  const std::vector<std::int32_t> ints = {7, -9};
  const std::vector<std::complex<float>> complexes = {{1.0f, 2.0f}};
  requireOk(write(file, "floats", floats), "floats");
  requireOk(write(file, "ints", ints), "ints");
  requireOk(write(file, "complexes", complexes), "complexes");

  auto asDouble = readVector<double>(file, "floats");
  requireOk(asDouble, "float -> double");
  CHECK(*asDouble == (std::vector<double>{1.5, -2.25}));
  auto asInt64 = readVector<std::int64_t>(file, "ints");
  requireOk(asInt64, "int32 -> int64");
  CHECK(*asInt64 == (std::vector<std::int64_t>{7, -9}));
  auto asComplex = readVector<std::complex<double>>(file, "complexes");
  requireOk(asComplex, "complex64 -> complex128");
  CHECK((*asComplex)[0] == std::complex<double>(1.0, 2.0));

  CHECK(failsWith(readVector<float>(file, "ints"), Code::TypeMismatch));
  CHECK(failsWith(readVector<std::int32_t>(file, "floats"), Code::TypeMismatch));
  CHECK(failsWith(readVector<double>(file, "complexes"), Code::TypeMismatch));
}

TEST(structureGroupsListExistsRemove) {
  auto file = createFile("structure.h5");
  const std::vector<double> values = {1.0, 2.0, 3.0};
  requireOk(write(file, "results/step_002/u", values), "nested write");
  requireOk(write(file, "results/step_001/u", values), "nested write");
  requireOk(file.createGroup("model/materials"), "createGroup");
  requireOk(file.createGroup("model/materials"), "createGroup twice");

  CHECK(file.exists("/"));
  CHECK(file.exists("results/step_001/u"));
  CHECK(file.exists("/results/step_001/u"));
  CHECK(!file.exists("results/step_003/u"));
  CHECK(!file.exists("missing/deeper/path"));

  auto root = file.list();
  requireOk(root, "list /");
  CHECK(*root == (std::vector<std::string>{"model", "results"}));
  auto steps = file.list("results");
  requireOk(steps, "list results");
  CHECK(*steps == (std::vector<std::string>{"step_001", "step_002"}));
  CHECK(failsWith(file.list("results/step_001/u"), Code::KindMismatch));
  CHECK(failsWith(file.list("nothing"), Code::NotFound));

  auto groupInfo = file.info("model");
  requireOk(groupInfo, "group info");
  CHECK(groupInfo->kind == ObjectKind::Group);
  CHECK(!groupInfo->scalar.has_value());

  CHECK(failsWith(file.createGroup("results/step_001/u"), Code::AlreadyExists));
  CHECK(failsWith(write(file, "results/step_001/u", values, {.overwrite = false}), Code::AlreadyExists));
  const std::vector<double> replacement = {9.0};
  requireOk(write(file, "results/step_001/u", replacement), "overwrite");
  CHECK(take(readVector<double>(file, "results/step_001/u")) == replacement);

  requireOk(file.remove("results/step_002"), "remove");
  CHECK(!file.exists("results/step_002/u"));
  CHECK(failsWith(file.remove("results/step_002"), Code::NotFound));
  CHECK(failsWith(file.remove("/"), Code::InvalidData));

  for (const auto* invalid : {"a//b", "../x", "a/./b", "/"}) {
    CHECK_MSG(failsWith(write(file, invalid, values), Code::InvalidData), invalid);
  }
}

TEST(attributesOfEveryKind) {
  auto file = createFile("attributes.h5");
  const std::vector<double> values = {1.0};
  requireOk(write(file, "u", values), "u");
  requireOk(file.setAttribute("/", "title", std::string("Köprü kafes sistemi")), "root string");
  requireOk(file.setAttribute("u", "step", std::int64_t{42}), "int");
  requireOk(file.setAttribute("u", "time", 0.125), "double");
  requireOk(file.setAttribute("u", "dims", std::vector<std::int64_t>{3, -1, 7}), "int vector");
  requireOk(file.setAttribute("u", "scale", std::vector<double>{0.5, 2.0}), "double vector");
  requireOk(file.setAttribute("u", "empty", std::vector<double>{}), "empty vector");
  requireOk(file.setAttribute("u", "step", std::int64_t{43}), "replace");

  auto reopened = reopen(file);
  CHECK(std::get<std::string>(take(reopened.attribute("/", "title"))) == "Köprü kafes sistemi");
  CHECK(std::get<std::int64_t>(take(reopened.attribute("u", "step"))) == 43);
  CHECK(std::get<double>(take(reopened.attribute("u", "time"))) == 0.125);
  CHECK(std::get<std::vector<std::int64_t>>(take(reopened.attribute("u", "dims"))) == (std::vector<std::int64_t>{3, -1, 7}));
  CHECK(std::get<std::vector<double>>(take(reopened.attribute("u", "scale"))) == (std::vector<double>{0.5, 2.0}));
  CHECK(std::get<std::vector<double>>(take(reopened.attribute("u", "empty"))).empty());
  CHECK(failsWith(reopened.attribute("u", "missing"), Code::NotFound));
  CHECK(failsWith(reopened.attribute("missing", "step"), Code::NotFound));
  CHECK(failsWith(reopened.setAttribute("u", "step", std::int64_t{1}), Code::ReadOnly));
}

TEST(errorCodes) {
  CHECK(failsWith(ArrayFile::open(workDir() / "does_not_exist.h5"), Code::FileNotFound));
  const fs::path text = workDir() / "not_hdf5.h5";
  std::ofstream(text) << "plain text, not HDF5\n";
  CHECK(failsWith(ArrayFile::open(text), Code::InvalidFile));

  auto file = createFile("errors.h5");
  const std::vector<double> values = {1.0, 2.0};
  requireOk(write(file, "dense", values), "dense");
  requireOk(file.createGroup("group"), "group");
  CHECK(failsWith(file.readDense<double>("group"), Code::KindMismatch));
  CHECK(failsWith(file.readSparse<double>("dense"), Code::KindMismatch));
  CHECK(failsWith(file.readDense<double>("missing"), Code::NotFound));
  CHECK(failsWith(readRows<double>(file, "dense"), Code::ShapeMismatch));
  const std::uint64_t wrongShape[] = {3};
  CHECK(failsWith(file.writeDense<double>("wrong", values, wrongShape), Code::ShapeMismatch));
  CHECK(failsWith(write(file, "bad", values, {.deflateLevel = 10}), Code::InvalidData));

  auto readOnly = reopen(file);
  CHECK(failsWith(write(readOnly, "new", values), Code::ReadOnly));
  CHECK(failsWith(readOnly.createGroup("new"), Code::ReadOnly));

  ArrayFile moved = std::move(readOnly);
  CHECK(!readOnly.isOpen()); // NOLINT(bugprone-use-after-move): the moved-from state is under test
  CHECK(failsWith(readOnly.info("dense"), Code::InvalidData));
  CHECK(moved.info("dense").has_value());

  auto writable = reopen(moved, ArrayFile::Access::ReadWrite);
  requireOk(write(writable, "added", values), "write after ReadWrite open");
}

TEST(deflateCompressionShrinksTheFile) {
  std::vector<double> values(200000);
  for (std::size_t i = 0; i < values.size(); ++i) values[i] = static_cast<double>(i % 50);
  const std::uint64_t shape[] = {400, 500};
  {
    auto plain = createFile("plain.h5");
    requireOk(plain.writeDense<double>("a", values, shape), "plain");
    auto packed = createFile("packed.h5");
    requireOk(packed.writeDense<double>("a", values, shape, {.deflateLevel = 6}), "packed");
    auto read = packed.readDense<double>("a");
    requireOk(read, "packed read");
    CHECK(read->values == values);
  }
  const auto plainSize = fs::file_size(workDir() / "plain.h5");
  const auto packedSize = fs::file_size(workDir() / "packed.h5");
  CHECK_MSG(packedSize * 10 < plainSize, std::format("plain {} bytes, deflate {} bytes", plainSize, packedSize));
}

TEST(sparseCoreRoundTripAndValidation) {
  auto file = createFile("sparse.h5");
  // 3x4 matrix [[1 0 0 2] [0 0 3 0] [4 0 5 0]] in CSC
  const std::vector<double> values = {1, 4, 3, 5, 2};
  const std::vector<std::int64_t> rows = {0, 2, 1, 2, 0};
  const std::vector<std::int64_t> pointers = {0, 2, 2, 4, 5};
  const CompressedView<double> csc{.rows = 3, .cols = 4, .layout = SparseLayout::Csc, .values = values, .indices = rows, .pointers = pointers};
  requireOk(file.writeSparse<double>("K", csc), "write csc");

  auto info = file.info("K");
  requireOk(info, "info");
  CHECK(info->kind == ObjectKind::Sparse);
  CHECK(info->layout == SparseLayout::Csc);
  CHECK(info->shape == (std::vector<std::uint64_t>{3, 4}));
  CHECK(info->nonZeros == 5);
  CHECK(std::get<std::string>(take(file.attribute("K", "encoding-type"))) == "csc_matrix");

  auto read = file.readSparse<double>("K");
  requireOk(read, "read csc");
  CHECK(read->values == values);
  CHECK(read->indices == rows);
  CHECK(read->pointers == pointers);

  const std::vector<std::int64_t> badPointers = {0, 2, 1, 4, 5};
  const std::vector<std::int64_t> badRows = {0, 2, 1, 3, 0};
  auto bad = csc;
  bad.pointers = badPointers;
  CHECK(failsWith(file.writeSparse<double>("bad", bad), Code::InvalidData));
  bad = csc;
  bad.indices = badRows;
  CHECK(failsWith(file.writeSparse<double>("bad", bad), Code::InvalidData));
  CHECK(!file.exists("bad"));

  // A hand-made group with inconsistent arrays is rejected on read.
  requireOk(file.createGroup("broken"), "broken group");
  requireOk(file.setAttribute("broken", "encoding-type", std::string("csr_matrix")), "broken attribute");
  requireOk(file.setAttribute("broken", "shape", std::vector<std::int64_t>{2, 2}), "broken shape");
  requireOk(write(file, "broken/data", std::vector<double>{1.0, 2.0}), "broken data");
  requireOk(write(file, "broken/indices", std::vector<std::int64_t>{0, 5}), "broken indices");
  requireOk(write(file, "broken/indptr", std::vector<std::int32_t>{0, 1, 2}), "broken indptr");
  CHECK(failsWith(file.readSparse<double>("broken"), Code::InvalidData));
}

TEST(stdContainersAndMdspan) {
  auto file = createFile("std.h5");
  const std::array<std::int64_t, 4> fixed = {1, 2, 3, 4};
  requireOk(write(file, "array", fixed), "std::array");
  CHECK(take(readVector<std::int64_t>(file, "array")) == (std::vector<std::int64_t>{1, 2, 3, 4}));

  const std::vector<std::vector<double>> rows = {{1, 2, 3}, {4, 5, 6}};
  requireOk(write(file, "rows", rows), "nested vector");
  CHECK(file.info("rows")->shape == (std::vector<std::uint64_t>{2, 3}));
  CHECK(take(readRows<double>(file, "rows")) == rows);
  CHECK(failsWith(write(file, "ragged", std::vector<std::vector<double>>{{1, 2}, {3}}), Code::ShapeMismatch));
  CHECK(failsWith(readVector<double>(file, "rows"), Code::ShapeMismatch));

#ifdef __cpp_lib_mdspan
  std::vector<double> storage(24);
  for (std::size_t i = 0; i < storage.size(); ++i) storage[i] = static_cast<double>(i);
  const std::mdspan right(storage.data(), 2, 3, 4);
  requireOk(write(file, "tensor/right", right), "layout_right");
  const std::mdspan<const double, std::dextents<std::size_t, 3>, std::layout_left> left(storage.data(), 2, 3, 4);
  requireOk(write(file, "tensor/left", left), "layout_left");

  auto readRight = file.readDense<double>("tensor/right");
  requireOk(readRight, "read right");
  CHECK(readRight->values == storage);
  auto readLeft = file.readDense<double>("tensor/left");
  requireOk(readLeft, "read left");
  auto view = asMdspan<3>(*readLeft);
  requireOk(view, "asMdspan");
  bool same = true;
  for (std::size_t i = 0; i < 2; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      for (std::size_t k = 0; k < 4; ++k) same = same && (*view)[i, j, k] == left[i, j, k];
  CHECK(same);
  CHECK(failsWith(asMdspan<2>(*readLeft), Code::ShapeMismatch));
#else
  std::printf("      std::mdspan not available in this standard library, mdspan checks skipped\n");
#endif
}

TEST(eigenDenseRoundTrip) {
  auto file = createFile("eigen_dense.h5");
  Eigen::MatrixXd colMajor(3, 4);
  for (Eigen::Index r = 0; r < 3; ++r)
    for (Eigen::Index c = 0; c < 4; ++c) colMajor(r, c) = static_cast<double>(10 * r + c);
  requireOk(write(file, "colMajor", colMajor), "MatrixXd");
  // Row-major on disk: the second stored value is (0, 1), not (1, 0).
  auto raw = file.readDense<double>("colMajor");
  requireOk(raw, "raw");
  CHECK(raw->shape == (std::vector<std::uint64_t>{3, 4}));
  CHECK(raw->values[1] == 1.0);
  CHECK(raw->values[4] == 10.0);
  CHECK(take(readEigen<Eigen::MatrixXd>(file, "colMajor")) == colMajor);

  const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> rowMajor = colMajor;
  requireOk(write(file, "rowMajor", rowMajor), "RowMajor");
  CHECK(file.readDense<double>("rowMajor")->values == raw->values);
  CHECK((take(readEigen<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(file, "colMajor"))) == rowMajor);

  requireOk(write(file, "block", colMajor.block(1, 1, 2, 2)), "block expression");
  CHECK(take(readEigen<Eigen::MatrixXd>(file, "block")) == colMajor.block(1, 1, 2, 2));

  const Eigen::VectorXd vector = Eigen::VectorXd::LinSpaced(6, -1.0, 1.0);
  requireOk(write(file, "vector", vector), "VectorXd");
  CHECK(file.info("vector")->shape == (std::vector<std::uint64_t>{6}));
  CHECK(take(readEigen<Eigen::VectorXd>(file, "vector")) == vector);
  CHECK(take(readEigen<Eigen::RowVectorXd>(file, "vector")) == vector.transpose());
  requireOk(write(file, "column", colMajor.col(2)), "column expression");
  CHECK(take(readEigen<Eigen::VectorXd>(file, "column")) == colMajor.col(2));
  requireOk(write(file, "row", colMajor.row(1)), "strided row expression");
  CHECK(take(readEigen<Eigen::RowVectorXd>(file, "row")) == colMajor.row(1));

  const Eigen::Matrix3d fixed = Eigen::Matrix3d::Identity() * 2.0;
  requireOk(write(file, "fixed", fixed), "Matrix3d");
  CHECK(take(readEigen<Eigen::Matrix3d>(file, "fixed")) == fixed);
  CHECK(failsWith(readEigen<Eigen::Matrix3d>(file, "colMajor"), Code::ShapeMismatch));
  CHECK(failsWith(readEigen<Eigen::MatrixXd>(file, "vector"), Code::ShapeMismatch));

  const Eigen::ArrayXXf array = Eigen::ArrayXXf::Constant(2, 3, 1.5f);
  requireOk(write(file, "array", array), "ArrayXXf");
  CHECK((take(readEigen<Eigen::ArrayXXf>(file, "array")) == array).all());
  CHECK((take(readEigen<Eigen::MatrixXd>(file, "array"))).isApprox(array.cast<double>().matrix()));

  Eigen::MatrixXcd complex(2, 2);
  complex << std::complex<double>(1, 2), std::complex<double>(3, -4), std::complex<double>(0, 1), std::complex<double>(-5, 0);
  requireOk(write(file, "complex", complex), "MatrixXcd");
  CHECK(take(readEigen<Eigen::MatrixXcd>(file, "complex")) == complex);
}

TEST(eigenSparseRoundTrip) {
  auto file = createFile("eigen_sparse.h5");
  Eigen::SparseMatrix<double> matrix(5, 4);
  std::vector<Eigen::Triplet<double>> triplets = {{0, 0, 4.0}, {1, 0, -1.0}, {4, 1, 2.5}, {2, 2, 7.0}, {0, 3, 1e-12}, {3, 3, -6.0}};
  matrix.setFromTriplets(triplets.begin(), triplets.end());
  requireOk(write(file, "K", matrix), "ColMajor");
  CHECK(file.info("K")->layout == SparseLayout::Csc);
  const Eigen::SparseMatrix<double> read = take(readEigen<Eigen::SparseMatrix<double>>(file, "K"));
  CHECK(Eigen::MatrixXd(read) == Eigen::MatrixXd(matrix));

  const Eigen::SparseMatrix<double, Eigen::RowMajor, std::int64_t> rowMajor = matrix;
  requireOk(write(file, "Kcsr", rowMajor), "RowMajor");
  CHECK(file.info("Kcsr")->layout == SparseLayout::Csr);
  // Either layout reads into either Eigen storage order.
  CHECK(Eigen::MatrixXd(take(readEigen<Eigen::SparseMatrix<double>>(file, "Kcsr"))) == Eigen::MatrixXd(matrix));
  CHECK(Eigen::MatrixXd(take(readEigen<Eigen::SparseMatrix<double, Eigen::RowMajor>>(file, "K"))) == Eigen::MatrixXd(matrix));

  Eigen::SparseMatrix<double> uncompressed(3, 3);
  uncompressed.reserve(Eigen::VectorXi::Constant(3, 2));
  uncompressed.insert(0, 0) = 1.0;
  uncompressed.insert(2, 1) = 2.0;
  uncompressed.insert(1, 2) = 3.0;
  CHECK(!uncompressed.isCompressed());
  requireOk(write(file, "uncompressed", uncompressed), "uncompressed");
  CHECK(file.info("uncompressed")->nonZeros == 3);
  CHECK(Eigen::MatrixXd(take(readEigen<Eigen::SparseMatrix<double>>(file, "uncompressed"))) == Eigen::MatrixXd(uncompressed));

  // Unsorted row indices (and a duplicate entry) go through the triplet path.
  const std::vector<double> values = {1.0, 2.0, 3.0, 4.0};
  const std::vector<std::int64_t> rows = {2, 0, 2, 1};
  const std::vector<std::int64_t> pointers = {0, 3, 4};
  requireOk(file.writeSparse<double>("unsorted", {.rows = 3, .cols = 2, .values = values, .indices = rows, .pointers = pointers}), "unsorted");
  const Eigen::MatrixXd dense = Eigen::MatrixXd(take(readEigen<Eigen::SparseMatrix<double>>(file, "unsorted")));
  CHECK(dense(2, 0) == 4.0);
  CHECK(dense(0, 0) == 2.0);
  CHECK(dense(1, 1) == 4.0);

  Eigen::SparseMatrix<std::complex<double>> complex(2, 2);
  complex.insert(1, 0) = std::complex<double>(0.5, -0.5);
  complex.makeCompressed();
  requireOk(write(file, "complex", complex), "complex sparse");
  CHECK((take(readEigen<Eigen::SparseMatrix<std::complex<double>>>(file, "complex"))).coeff(1, 0) == std::complex<double>(0.5, -0.5));

  CHECK(failsWith(readEigen<Eigen::SparseMatrix<double>>(file, "missing"), Code::NotFound));
}

#ifdef ANAFINEN_HAS_CHOLMOD
TEST(cholmodRoundTrip) {
  cholmod_common common;
  cholmod_start(&common);
  auto file = createFile("cholmod.h5");

  // Symmetric 3x3 [[4 1 0] [1 3 0] [0 0 2]], upper triangle stored (stype = 1).
  cholmod_sparse* upper = cholmod_allocate_sparse(3, 3, 4, 1, 1, 1, CHOLMOD_REAL, &common);
  REQUIRE(upper != nullptr);
  const int p[] = {0, 1, 3, 4};
  const int i[] = {0, 0, 1, 2};
  const double x[] = {4.0, 1.0, 3.0, 2.0};
  std::copy(std::begin(p), std::end(p), static_cast<int*>(upper->p));
  std::copy(std::begin(i), std::end(i), static_cast<int*>(upper->i));
  std::copy(std::begin(x), std::end(x), static_cast<double*>(upper->x));
  requireOk(write(file, "K", *upper), "cholmod_sparse");
  CHECK(std::get<std::int64_t>(take(file.attribute("K", "cholmod-stype"))) == 1);

  auto read = readCholmodSparse(file, "K", common);
  requireOk(read, "readCholmodSparse");
  cholmod_sparse* back = *read;
  CHECK(back->stype == 1);
  CHECK(back->nrow == 3 && back->ncol == 3);
  CHECK(std::equal(std::begin(x), std::end(x), static_cast<const double*>(back->x)));

  // A csr_matrix written from Eigen reads back as the same CSC matrix.
  Eigen::SparseMatrix<double, Eigen::RowMajor> eigenRows(2, 3);
  eigenRows.insert(0, 2) = 5.0;
  eigenRows.insert(1, 0) = 6.0;
  eigenRows.makeCompressed();
  requireOk(write(file, "csr", eigenRows), "Eigen csr");
  auto fromCsr = readCholmodSparse(file, "csr", common);
  requireOk(fromCsr, "csr -> cholmod");
  cholmod_dense* fromCsrDense = cholmod_sparse_to_dense(*fromCsr, &common);
  const auto* d = static_cast<const double*>(fromCsrDense->x);
  CHECK((*fromCsr)->nrow == 2 && (*fromCsr)->ncol == 3);
  CHECK(d[2 * 2 + 0] == 5.0); // (0, 2), column-major with leading dimension 2
  CHECK(d[0 * 2 + 1] == 6.0); // (1, 0)

  // Dense: column-major with leading dimension d on both sides, row-major in the file.
  cholmod_dense* dense = cholmod_allocate_dense(2, 3, 4, CHOLMOD_REAL, &common);
  auto* dx = static_cast<double*>(dense->x);
  for (std::size_t c = 0; c < 3; ++c)
    for (std::size_t r = 0; r < 2; ++r) dx[c * 4 + r] = static_cast<double>(10 * r + c);
  requireOk(write(file, "F", *dense), "cholmod_dense");
  CHECK(file.readDense<double>("F")->values == (std::vector<double>{0, 1, 2, 10, 11, 12}));
  auto denseBack = readCholmodDense(file, "F", common);
  requireOk(denseBack, "readCholmodDense");
  CHECK(static_cast<const double*>((*denseBack)->x)[1 * 2 + 1] == 11.0);

  cholmod_free_dense(&*denseBack, &common);
  cholmod_free_dense(&dense, &common);
  cholmod_free_dense(&fromCsrDense, &common);
  cholmod_free_sparse(&*fromCsr, &common);
  cholmod_free_sparse(&back, &common);
  cholmod_free_sparse(&upper, &common);
  cholmod_finish(&common);
}
#endif

TEST(utf8FileNames) {
  const fs::path path = workDir() / anaf::IO::pathFromUtf8("matris_ğüşıöç_Ж.h5");
  {
    auto file = ArrayFile::create(path);
    requireOk(file, "create UTF-8 name");
    requireOk(write(*file, "v", std::vector<double>{3.0}), "write");
  }
  CHECK(fs::exists(path));
  auto file = ArrayFile::open(path);
  requireOk(file, "open UTF-8 name");
  CHECK(take(readVector<double>(*file, "v")) == (std::vector<double>{3.0}));
}

TEST(concurrentFilesFromSeveralThreads) {
  constexpr int threadCount = 4;
  std::vector<int> failures(threadCount, 0);
  {
    std::vector<std::jthread> threads;
    for (int t = 0; t < threadCount; ++t) {
      threads.emplace_back([t, &failures] {
        auto file = ArrayFile::create(workDir() / std::format("thread_{}.h5", t));
        if (!file) {
          ++failures[static_cast<std::size_t>(t)];
          return;
        }
        for (int step = 0; step < 50; ++step) {
          std::vector<double> values(100, static_cast<double>(t * 1000 + step));
          const auto name = std::format("step_{:03}", step);
          if (!write(*file, name, values) || readVector<double>(*file, name) != values) ++failures[static_cast<std::size_t>(t)];
        }
      });
    }
  }
  for (int t = 0; t < threadCount; ++t) CHECK_MSG(failures[static_cast<std::size_t>(t)] == 0, std::format("thread {}", t));
}

int main(int argc, char** argv) {
  const int status = anaf::TESTING::runAll(argc > 1 ? argv[1] : "");
  std::error_code ignored;
  fs::remove_all(workDir(), ignored);
  return status;
}
