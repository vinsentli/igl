/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/Shader.h>
#include <map>

namespace igl {

struct FunctionConstantValues::Impl {
 public:
  struct Entry {
    ConstantValueType type;
    std::vector<uint8_t> data;
  };

  void setConstantValue(uint8_t index, ConstantValueType type, void* value) {
    IGL_DEBUG_ASSERT(type != ConstantValueType::Invalid);
    IGL_DEBUG_ASSERT(value);
    auto dataSize = getContantValueSize(type);
    IGL_DEBUG_ASSERT(dataSize);

    auto& entry = values_[index];
    entry.type = type;
    entry.data.resize(dataSize);
    memcpy(entry.data.data(), value, dataSize);
  }

  const std::map<uint8_t, Entry>& getConstantValues() const {
    return values_;
  }

  bool operator==(const Impl& other) const {
    return false;
  }

  bool operator!=(const Impl& other) const {
    return !(*this == other);
  }

  size_t getContantValueSize(ConstantValueType type) {
    switch (type) {
    case ConstantValueType::Invalid:
      return 0;
    case ConstantValueType::Float:
      return 4;
    case ConstantValueType::Float2:
      return 8;
    case ConstantValueType::Float3:
      return 12;
    case ConstantValueType::Float4:
      return 16;
    case ConstantValueType::Boolean:
      return 4;
    case ConstantValueType::Boolean2:
      return 8;
    case ConstantValueType::Boolean3:
      return 12;
    case ConstantValueType::Boolean4:
      return 16;
    case ConstantValueType::Int:
      return 4;
    case ConstantValueType::Int2:
      return 8;
    case ConstantValueType::Int3:
      return 12;
    case ConstantValueType::Int4:
      return 16;
    case ConstantValueType::Mat2x2:
      return 16;
    case ConstantValueType::Mat3x3:
      return 36;
    case ConstantValueType::Mat4x4:
      return 64;
    }
    return 0;
  }

 private:
  std::map<uint8_t, Entry> values_;
};

} // namespace igl
