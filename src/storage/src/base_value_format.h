//  Copyright (c) 2024-present, OpenAtom Foundation, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#ifndef SRC_BASE_VALUE_FORMAT_H_
#define SRC_BASE_VALUE_FORMAT_H_

#include <string>

#include "rocksdb/env.h"
#include "rocksdb/slice.h"

#include "pstd/env.h"
#include "src/coding.h"
#include "src/mutex.h"

namespace storage {

using Status = rocksdb::Status;
using Slice = rocksdb::Slice;

enum DataType : uint8_t { kStrings = 0, kHashes = 1, kSets = 2, kLists = 3, kZSets = 4, kNones = 5, kAll = 6 };

static const char* DataTypeStrings[] = {"string", "hash", "set", "list", "zset", "none", "all"};

static const char DataTypeTag[] = {'k', 'h', 's', 'l', 'z', 'n', 'a'};

const char* DataTypeToString(DataType type);

const char DataTypeToTag(DataType type);

class InternalValue {
 public:
  explicit InternalValue(DataType type, const Slice& user_value) : type_(type), user_value_(user_value) {
    ctime_ = pstd::NowMicros() / 1e6;
  }

  virtual ~InternalValue() {
    if (start_ != space_) {
      delete[] start_;
    }
  }
  void SetEtime(uint64_t etime = 0) { etime_ = etime; }
  void setCtime(uint64_t ctime) { ctime_ = ctime; }
  Status SetRelativeTimestamp(uint64_t ttl) {
    int64_t unix_time;
    rocksdb::Env::Default()->GetCurrentTime(&unix_time);
    etime_ = uint64_t(unix_time) + ttl;
    if (etime_ != uint64_t(unix_time) + ttl) {
      return rocksdb::Status::InvalidArgument("invalid expire time");
    }
    return rocksdb::Status::OK();
  }
  void SetVersion(uint64_t version = 0) { version_ = version; }

  char* ReAllocIfNeeded(size_t needed) {
    char* dst;
    if (needed <= sizeof(space_)) {
      dst = space_;
    } else {
      dst = new char[needed];
      if (start_ != space_) {
        delete[] start_;
      }
    }
    start_ = dst;
    return dst;
  }

  virtual Slice Encode() = 0;

 protected:
  char space_[200];
  char* start_ = nullptr;
  DataType type_ = DataType::kNones;
  Slice user_value_;
  uint64_t version_ = 0;
  uint64_t etime_ = 0;
  uint64_t ctime_ = 0;
  char reserve_[16] = {0};
};

class ParsedInternalValue {
 public:
  // Use this constructor after rocksdb::DB::Get(), since we use this in
  // the implement of user interfaces and may need to modify the
  // original value suffix, so the value_ must point to the string
  explicit ParsedInternalValue(std::string* value) : value_(value) {}

  // Use this constructor in rocksdb::CompactionFilter::Filter(),
  // since we use this in Compaction process, all we need to do is parsing
  // the rocksdb::Slice, so don't need to modify the original value, value_ can be
  // set to nullptr
  explicit ParsedInternalValue(const Slice& value) {}

  virtual ~ParsedInternalValue() = default;

  rocksdb::Slice UserValue() { return user_value_; }

  uint64_t Version() { return version_; }

  void SetVersion(uint64_t version) {
    version_ = version;
    SetVersionToValue();
  }

  uint64_t Etime() { return etime_; }

  void SetEtime(uint64_t etime) {
    etime_ = etime;
    SetEtimeToValue();
  }

  void SetCtime(uint64_t ctime) {
    ctime_ = ctime;
    SetCtimeToValue();
  }

  void SetRelativeTimestamp(uint64_t ttl) {
    int64_t unix_time;
    rocksdb::Env::Default()->GetCurrentTime(&unix_time);
    etime_ = static_cast<uint64_t>(unix_time) + ttl;
    SetEtimeToValue();
  }

  bool IsPermanentSurvival() { return etime_ == 0; }

  bool IsStale() {
    if (etime_ == 0) {
      return false;
    }
    int64_t unix_time;
    rocksdb::Env::Default()->GetCurrentTime(&unix_time);
    return etime_ < unix_time;
  }

  virtual bool IsValid() { return !IsStale(); }

  virtual void StripSuffix() = 0;

 protected:
  virtual void SetVersionToValue() = 0;
  virtual void SetEtimeToValue() = 0;
  virtual void SetCtimeToValue() = 0;
  std::string* value_ = nullptr;
  DataType type_ = DataType::kNones;
  Slice user_value_;
  uint64_t version_ = 0;
  uint64_t ctime_ = 0;
  uint64_t etime_ = 0;
  char reserve_[16] = {0};  // unused
};

}  //  namespace storage
#endif  // SRC_BASE_VALUE_FORMAT_H_
