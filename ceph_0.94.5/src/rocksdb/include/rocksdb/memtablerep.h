// Copyright (c) 2013, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//
// This file contains the interface that must be implemented by any collection
// to be used as the backing store for a MemTable. Such a collection must
// satisfy the following properties:
//  (1) It does not store duplicate items.
//  (2) It uses MemTableRep::KeyComparator to compare items for iteration and
//     equality.
//  (3) It can be accessed concurrently by multiple readers and can support
//     during reads. However, it needn't support multiple concurrent writes.
//  (4) Items are never deleted.
// The liberal use of assertions is encouraged to enforce (1).
//
// The factory will be passed an Arena object when a new MemTableRep is
// requested. The API for this object is in rocksdb/arena.h.
//
// Users can implement their own memtable representations. We include three
// types built in:
//  - SkipListRep: This is the default; it is backed by a skip list.
//  - HashSkipListRep: The memtable rep that is best used for keys that are
//  structured like "prefix:suffix" where iteration within a prefix is
//  common and iteration across different prefixes is rare. It is backed by
//  a hash map where each bucket is a skip list.
//  - VectorRep: This is backed by an unordered std::vector. On iteration, the
// vector is sorted. It is intelligent about sorting; once the MarkReadOnly()
// has been called, the vector will only be sorted once. It is optimized for
// random-write-heavy workloads.
//
// The last four implementations are designed for situations in which
// iteration over the entire collection is rare since doing so requires all the
// keys to be copied into a sorted data structure.

#pragma once

#include <memory>
#include <stdint.h>

namespace rocksdb {

class Arena;
class LookupKey;
class Slice;
class SliceTransform;

typedef void* KeyHandle;

class MemTableRep {
 public:
  // KeyComparator provides a means to compare keys, which are internal keys
  // concatenated with values.
  class KeyComparator {
   public:
    // Compare a and b. Return a negative value if a is less than b, 0 if they
    // are equal, and a positive value if a is greater than b
    virtual int operator()(const char* prefix_len_key1,
                           const char* prefix_len_key2) const = 0;

    virtual int operator()(const char* prefix_len_key,
                           const Slice& key) const = 0;

    virtual ~KeyComparator() { }
  };

  explicit MemTableRep(Arena* arena) : arena_(arena) {}

  // Allocate a buf of len size for storing key. The idea is that a specific
  // memtable representation knows its underlying data structure better. By
  // allowing it to allocate memory, it can possibly put correlated stuff
  // in consecutive memory area to make processor prefetching more efficient.
  virtual KeyHandle Allocate(const size_t len, char** buf);

  // Insert key into the collection. (The caller will pack key and value into a
  // single buffer and pass that in as the parameter to Insert).
  // REQUIRES: nothing that compares equal to key is currently in the
  // collection.
  virtual void Insert(KeyHandle handle) = 0;

  // Returns true iff an entry that compares equal to key is in the collection.
  virtual bool Contains(const char* key) const = 0;

  // Notify this table rep that it will no longer be added to. By default, does
  // nothing.
  virtual void MarkReadOnly() { }

  // Look up key from the mem table, since the first key in the mem table whose
  // user_key matches the one given k, call the function callback_func(), with
  // callback_args directly forwarded as the first parameter, and the mem table
  // key as the second parameter. If the return value is false, then terminates.
  // Otherwise, go through the next key.
  // It's safe for Get() to terminate after having finished all the potential
  // key for the k.user_key(), or not.
  //
  // Default:
  // Get() function with a default value of dynamically construct an iterator,
  // seek and call the call back function.
  virtual void Get(const LookupKey& k, void* callback_args,
                   bool (*callback_func)(void* arg, const char* entry));

  // Report an approximation of how much memory has been used other than memory
  // that was allocated through the arena.
  virtual size_t ApproximateMemoryUsage() = 0;

  virtual ~MemTableRep() { }

  // Iteration over the contents of a skip collection
  class Iterator {
   public:
    // Initialize an iterator over the specified collection.
    // The returned iterator is not valid.
    // explicit Iterator(const MemTableRep* collection);
    virtual ~Iterator() {}

    // Returns true iff the iterator is positioned at a valid node.
    virtual bool Valid() const = 0;

    // Returns the key at the current position.
    // REQUIRES: Valid()
    virtual const char* key() const = 0;

    // Advances to the next position.
    // REQUIRES: Valid()
    virtual void Next() = 0;

    // Advances to the previous position.
    // REQUIRES: Valid()
    virtual void Prev() = 0;

    // Advance to the first entry with a key >= target
    virtual void Seek(const Slice& internal_key, const char* memtable_key) = 0;

    // Position at the first entry in collection.
    // Final state of iterator is Valid() iff collection is not empty.
    virtual void SeekToFirst() = 0;

    // Position at the last entry in collection.
    // Final state of iterator is Valid() iff collection is not empty.
    virtual void SeekToLast() = 0;
  };

  // Return an iterator over the keys in this representation.
  virtual Iterator* GetIterator() = 0;

  // Return an iterator over at least the keys with the specified user key. The
  // iterator may also allow access to other keys, but doesn't have to. Default:
  // GetIterator().
  virtual Iterator* GetIterator(const Slice& user_key) { return GetIterator(); }

  // Return an iterator that has a special Seek semantics. The result of
  // a Seek might only include keys with the same prefix as the target key.
  virtual Iterator* GetDynamicPrefixIterator() { return GetIterator(); }

  // Return true if the current MemTableRep supports merge operator.
  // Default: true
  virtual bool IsMergeOperatorSupported() const { return true; }

  // Return true if the current MemTableRep supports snapshot
  // Default: true
  virtual bool IsSnapshotSupported() const { return true; }

 protected:
  // When *key is an internal key concatenated with the value, returns the
  // user key.
  virtual Slice UserKey(const char* key) const;

  Arena* arena_;
};

// This is the base class for all factories that are used by RocksDB to create
// new MemTableRep objects
class MemTableRepFactory {
 public:
  virtual ~MemTableRepFactory() {}
  virtual MemTableRep* CreateMemTableRep(const MemTableRep::KeyComparator&,
      Arena*, const SliceTransform*) = 0;
  virtual const char* Name() const = 0;
};

// This uses a skip list to store keys. It is the default.
class SkipListFactory : public MemTableRepFactory {
 public:
  virtual MemTableRep* CreateMemTableRep(const MemTableRep::KeyComparator&,
                                         Arena*,
                                         const SliceTransform*) override;
  virtual const char* Name() const override { return "SkipListFactory"; }
};

#ifndef ROCKSDB_LITE
// This creates MemTableReps that are backed by an std::vector. On iteration,
// the vector is sorted. This is useful for workloads where iteration is very
// rare and writes are generally not issued after reads begin.
//
// Parameters:
//   count: Passed to the constructor of the underlying std::vector of each
//     VectorRep. On initialization, the underlying array will be at least count
//     bytes reserved for usage.
class VectorRepFactory : public MemTableRepFactory {
  const size_t count_;

 public:
  explicit VectorRepFactory(size_t count = 0) : count_(count) { }
  virtual MemTableRep* CreateMemTableRep(
      const MemTableRep::KeyComparator&, Arena*,
      const SliceTransform*) override;
  virtual const char* Name() const override {
    return "VectorRepFactory";
  }
};

// This class contains a fixed array of buckets, each
// pointing to a skiplist (null if the bucket is empty).
// bucket_count: number of fixed array buckets
// skiplist_height: the max height of the skiplist
// skiplist_branching_factor: probabilistic size ratio between adjacent
//                            link lists in the skiplist
extern MemTableRepFactory* NewHashSkipListRepFactory(
    size_t bucket_count = 1000000, int32_t skiplist_height = 4,
    int32_t skiplist_branching_factor = 4
);

// The factory is to create memtables with a hashed linked list:
// it contains a fixed array of buckets, each pointing to a sorted single
// linked list (null if the bucket is empty).
// @bucket_count: number of fixed array buckets
// @huge_page_tlb_size: if <=0, allocate the hash table bytes from malloc.
//                      Otherwise from huge page TLB. The user needs to reserve
//                      huge pages for it to be allocated, like:
//                          sysctl -w vm.nr_hugepages=20
//                      See linux doc Documentation/vm/hugetlbpage.txt
extern MemTableRepFactory* NewHashLinkListRepFactory(
    size_t bucket_count = 50000, size_t huge_page_tlb_size = 0);

// This factory creates a cuckoo-hashing based mem-table representation.
// Cuckoo-hash is a closed-hash strategy, in which all key/value pairs
// are stored in the bucket array itself intead of in some data structures
// external to the bucket array.  In addition, each key in cuckoo hash
// has a constant number of possible buckets in the bucket array.  These
// two properties together makes cuckoo hash more memory efficient and
// a constant worst-case read time.  Cuckoo hash is best suitable for
// point-lookup workload.
//
// When inserting a key / value, it first checks whether one of its possible
// buckets is empty.  If so, the key / value will be inserted to that vacant
// bucket.  Otherwise, one of the keys originally stored in one of these
// possible buckets will be "kicked out" and move to one of its possible
// buckets (and possibly kicks out another victim.)  In the current
// implementation, such "kick-out" path is bounded.  If it cannot find a
// "kick-out" path for a specific key, this key will be stored in a backup
// structure, and the current memtable to be forced to immutable.
//
// Note that currently this mem-table representation does not support
// snapshot (i.e., it only queries latest state) and iterators.  In addition,
// MultiGet operation might also lose its atomicity due to the lack of
// snapshot support.
//
// Parameters:
//   write_buffer_size: the write buffer size in bytes.
//   average_data_size: the average size of key + value in bytes.  This value
//     together with write_buffer_size will be used to compute the number
//     of buckets.
//   hash_function_count: the number of hash functions that will be used by
//     the cuckoo-hash.  The number also equals to the number of possible
//     buckets each key will have.
extern MemTableRepFactory* NewHashCuckooRepFactory(
    size_t write_buffer_size, size_t average_data_size = 64,
    unsigned int hash_function_count = 4);
#endif  // ROCKSDB_LITE
}  // namespace rocksdb
