/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <igl/Shader.h>

namespace igl::tests {

// `FunctionConstantValues` is a backend-agnostic value type, so these tests do not need a
// device fixture — plain TEST is fine.

TEST(FunctionConstantValuesTest, DefaultIsEmpty) {
  FunctionConstantValues fcv;
  EXPECT_TRUE(fcv.getConstantValues().empty());
  EXPECT_TRUE(fcv.getData().empty());
}

TEST(FunctionConstantValuesTest, SetSingleConstant) {
  FunctionConstantValues fcv;
  float value = 1.5f;
  fcv.setConstantValue(0, ConstantValueType::Float1, &value);

  const auto& values = fcv.getConstantValues();
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0].type, ConstantValueType::Float1);
  EXPECT_EQ(values[0].offset, 0u);

  ASSERT_EQ(fcv.getData().size(), sizeof(float));
  float read = 0.f;
  std::memcpy(&read, fcv.getData().data() + values[0].offset, sizeof(float));
  EXPECT_EQ(read, 1.5f);
}

TEST(FunctionConstantValuesTest, SparseLayoutLeavesInvalidGapSlots) {
  FunctionConstantValues fcv;
  int32_t a = 7;
  int32_t b = 99;
  fcv.setConstantValue(0, ConstantValueType::Int1, &a);
  fcv.setConstantValue(3, ConstantValueType::Int1, &b);

  const auto& values = fcv.getConstantValues();
  ASSERT_EQ(values.size(), 4u); // sized to max binding + 1

  EXPECT_EQ(values[0].type, ConstantValueType::Int1);
  EXPECT_EQ(values[1].type, ConstantValueType::Invalid);
  EXPECT_EQ(values[2].type, ConstantValueType::Invalid);
  EXPECT_EQ(values[3].type, ConstantValueType::Int1);
}

TEST(FunctionConstantValuesTest, OverwriteSameTypeWritesInPlace) {
  FunctionConstantValues fcv;
  float v1 = 1.f;
  float v2 = 2.f;
  fcv.setConstantValue(0, ConstantValueType::Float1, &v1);
  const auto sizeAfterFirst = fcv.getData().size();

  fcv.setConstantValue(0, ConstantValueType::Float1, &v2);

  // Same-size overwrite must reuse the slot — no growth in `data_`, no gap created.
  EXPECT_EQ(fcv.getData().size(), sizeAfterFirst);
  EXPECT_EQ(fcv.getConstantValues()[0].offset, 0u);

  float read = 0.f;
  std::memcpy(&read, fcv.getData().data(), sizeof(float));
  EXPECT_EQ(read, 2.f);
}

TEST(FunctionConstantValuesTest, OverwriteDifferentSizeAppends) {
  FunctionConstantValues fcv;
  float vFloat1 = 1.f;
  float vFloat4[4] = {1.f, 2.f, 3.f, 4.f};
  fcv.setConstantValue(0, ConstantValueType::Float1, &vFloat1);
  const auto firstOffset = fcv.getConstantValues()[0].offset;

  fcv.setConstantValue(0, ConstantValueType::Float4, &vFloat4);

  // Different size → must append; offset advances and `data_` grows past the original entry.
  EXPECT_EQ(fcv.getConstantValues()[0].type, ConstantValueType::Float4);
  EXPECT_GT(fcv.getConstantValues()[0].offset, firstOffset);
  EXPECT_GE(fcv.getData().size(), sizeof(float) + 4 * sizeof(float));
}

TEST(FunctionConstantValuesTest, EqualityIdenticalContent) {
  FunctionConstantValues a;
  FunctionConstantValues b;
  float v = 3.14f;
  a.setConstantValue(0, ConstantValueType::Float1, &v);
  b.setConstantValue(0, ConstantValueType::Float1, &v);
  EXPECT_EQ(a, b);
}

TEST(FunctionConstantValuesTest, EqualityIsOrderInsensitive) {
  // Sparse storage canonicalizes layout: the order in which `setConstantValue` is called
  // does not affect equality as long as the final logical content matches.
  FunctionConstantValues a;
  FunctionConstantValues b;
  float v0 = 1.f;
  int32_t v3 = 42;

  a.setConstantValue(0, ConstantValueType::Float1, &v0);
  a.setConstantValue(3, ConstantValueType::Int1, &v3);

  b.setConstantValue(3, ConstantValueType::Int1, &v3);
  b.setConstantValue(0, ConstantValueType::Float1, &v0);

  EXPECT_EQ(a, b);
}

TEST(FunctionConstantValuesTest, EqualityIgnoresOrphanGapBytes) {
  // Regression test for the gap-byte equality bug: an FCV whose data buffer contains
  // orphan bytes from a different-size overwrite must still compare equal to a freshly
  // constructed FCV with the same logical content.
  FunctionConstantValues a;
  FunctionConstantValues b;
  float vFloat1 = 7.f;
  float vFloat4[4] = {1.f, 2.f, 3.f, 4.f};

  // a: directly write Float4 → no gap.
  a.setConstantValue(0, ConstantValueType::Float4, &vFloat4);

  // b: write Float1 (4 bytes) first, then overwrite with Float4 (16 bytes) — leaves
  // 4 dead bytes in `data_` from the abandoned Float1 slot.
  b.setConstantValue(0, ConstantValueType::Float1, &vFloat1);
  b.setConstantValue(0, ConstantValueType::Float4, &vFloat4);

  // Sanity: the underlying buffers really are different sizes (a: 16, b: 20).
  EXPECT_NE(a.getData().size(), b.getData().size());

  // But logical equality must hold.
  EXPECT_EQ(a, b);
}

TEST(FunctionConstantValuesTest, EqualityDifferentValues) {
  FunctionConstantValues a;
  FunctionConstantValues b;
  float v1 = 1.f;
  float v2 = 2.f;
  a.setConstantValue(0, ConstantValueType::Float1, &v1);
  b.setConstantValue(0, ConstantValueType::Float1, &v2);
  EXPECT_NE(a, b);
}

TEST(FunctionConstantValuesTest, EqualityDifferentTypesAtSameIndex) {
  FunctionConstantValues a;
  FunctionConstantValues b;
  uint32_t v = 42;
  a.setConstantValue(0, ConstantValueType::Int1, &v);
  b.setConstantValue(0, ConstantValueType::Float1, &v);
  EXPECT_NE(a, b);
}

TEST(FunctionConstantValuesTest, EqualityDifferentBindings) {
  FunctionConstantValues a;
  FunctionConstantValues b;
  float v = 1.f;
  a.setConstantValue(0, ConstantValueType::Float1, &v);
  b.setConstantValue(1, ConstantValueType::Float1, &v);
  EXPECT_NE(a, b);
}

TEST(FunctionConstantValuesTest, GetConstantValueSizeAllTypes) {
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Invalid), 0u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Float1), 4u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Float2), 8u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Float3), 12u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Float4), 16u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Boolean1), 4u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Boolean2), 8u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Boolean3), 12u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Boolean4), 16u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Int1), 4u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Int2), 8u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Int3), 12u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Int4), 16u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Mat2x2), 16u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Mat3x3), 36u);
  EXPECT_EQ(getConstantValueSize(ConstantValueType::Mat4x4), 64u);
}

} // namespace igl::tests
