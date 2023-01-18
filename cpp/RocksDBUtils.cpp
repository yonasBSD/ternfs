// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include <rocksdb/env.h>
#include <rocksdb/merge_operator.h>
#include <rocksdb/slice.h>

#include "RocksDBUtils.hpp"

namespace { // anonymous namespace

using ROCKSDB_NAMESPACE::AssociativeMergeOperator;
using ROCKSDB_NAMESPACE::Logger;
using ROCKSDB_NAMESPACE::Slice;

// A 'model' merge operator with int64 addition semantics
// Implemented as an AssociativeMergeOperator for simplicity and example.
class Int64AddOperator : public AssociativeMergeOperator {
 public:
  bool Merge(const Slice& /*key*/, const Slice* existing_value,
             const Slice& value, std::string* new_value,
             Logger* logger) const override {
    int64_t orig_value = 0;
    if (existing_value) {
      orig_value = ExternalValue<I64Value>::FromSlice(*existing_value)().i64();
    }
    int64_t operand = ExternalValue<I64Value>::FromSlice(value)().i64();

    ALWAYS_ASSERT(new_value);
    new_value->resize(I64Value::MAX_SIZE);
    ExternalValue<I64Value>(*new_value)().setI64(orig_value + operand);

    return true;
  }

  static const char* kClassName() { return "Int64AddOperator"; }
  static const char* kNickName() { return "int64add"; }
  const char* Name() const override { return kClassName(); }
  const char* NickName() const override { return kNickName(); }
};

}  // anonymous namespace

std::shared_ptr<rocksdb::MergeOperator> CreateInt64AddOperator() {
  return std::make_shared<Int64AddOperator>();
}
