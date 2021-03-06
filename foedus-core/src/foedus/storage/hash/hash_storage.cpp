/*
 * Copyright (c) 2014-2015, Hewlett-Packard Development Company, LP.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details. You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * HP designates this particular file as subject to the "Classpath" exception
 * as provided by HP in the LICENSE.txt file that accompanied this code.
 */
#include "foedus/storage/hash/hash_storage.hpp"

#include <glog/logging.h>

#include <iostream>
#include <string>

#include "foedus/engine.hpp"
#include "foedus/log/thread_log_buffer.hpp"
#include "foedus/storage/storage_manager.hpp"
#include "foedus/storage/hash/hash_combo.hpp"
#include "foedus/storage/hash/hash_log_types.hpp"
#include "foedus/storage/hash/hash_storage_pimpl.hpp"
#include "foedus/thread/thread.hpp"

namespace foedus {
namespace storage {
namespace hash {

HashStorage::HashStorage() : Storage<HashStorageControlBlock>() {}
HashStorage::HashStorage(Engine* engine, HashStorageControlBlock* control_block)
  : Storage<HashStorageControlBlock>(engine, control_block) {
  ASSERT_ND(get_type() == kHashStorage || !exists());
}
HashStorage::HashStorage(Engine* engine, StorageControlBlock* control_block)
  : Storage<HashStorageControlBlock>(engine, control_block) {
  ASSERT_ND(get_type() == kHashStorage || !exists());
}
HashStorage::HashStorage(Engine* engine, StorageId id)
  : Storage<HashStorageControlBlock>(engine, id) {}
HashStorage::HashStorage(Engine* engine, const StorageName& name)
  : Storage<HashStorageControlBlock>(engine, name) {}
HashStorage::HashStorage(const HashStorage& other)
  : Storage<HashStorageControlBlock>(other.engine_, other.control_block_) {
}
HashStorage& HashStorage::operator=(const HashStorage& other) {
  engine_ = other.engine_;
  control_block_ = other.control_block_;
  return *this;
}

uint8_t HashStorage::get_levels() const { return control_block_->levels_; }
HashBin HashStorage::get_bin_count() const { return control_block_->bin_count_; }
uint8_t HashStorage::get_bin_bits() const { return control_block_->meta_.bin_bits_; }
uint8_t HashStorage::get_bin_shifts() const { return control_block_->meta_.get_bin_shifts(); }
uint16_t HashStorage::get_root_children() const { return control_block_->get_root_children(); }

ErrorStack  HashStorage::create(const Metadata &metadata) {
  HashStoragePimpl pimpl(this);
  return pimpl.create(static_cast<const HashMetadata&>(metadata));
}
ErrorStack HashStorage::load(const StorageControlBlock& snapshot_block) {
  HashStoragePimpl pimpl(this);
  return pimpl.load(snapshot_block);
}
ErrorStack  HashStorage::drop() {
  HashStoragePimpl pimpl(this);
  return pimpl.drop();
}

const HashMetadata* HashStorage::get_hash_metadata() const  { return &control_block_->meta_; }

ErrorCode HashStorage::get_record(
  thread::Thread* context,
  const void* key,
  uint16_t key_length,
  const HashCombo& combo,
  void* payload,
  uint16_t* payload_capacity,
  bool read_only) {
  HashStoragePimpl pimpl(this);
  return pimpl.get_record(
    context,
    key,
    key_length,
    combo,
    payload,
    payload_capacity,
    read_only);
}

ErrorCode HashStorage::get_record_part(
  thread::Thread* context,
  const void* key,
  uint16_t key_length,
  const HashCombo& combo,
  void* payload,
  uint16_t payload_offset,
  uint16_t payload_count,
  bool read_only) {
  HashStoragePimpl pimpl(this);
  return pimpl.get_record_part(
    context,
    key,
    key_length,
    combo,
    payload,
    payload_offset,
    payload_count,
    read_only);
}

template <typename PAYLOAD>
ErrorCode HashStorage::get_record_primitive(
  thread::Thread* context,
  const void* key,
  uint16_t key_length,
  const HashCombo& combo,
  PAYLOAD* payload,
  uint16_t payload_offset,
  bool read_only) {
  HashStoragePimpl pimpl(this);
  return pimpl.get_record_primitive(
    context,
    key,
    key_length,
    combo,
    payload,
    payload_offset,
    read_only);
}

ErrorCode HashStorage::insert_record(
  thread::Thread* context,
  const void* key,
  uint16_t key_length,
  const HashCombo& combo,
  const void* payload,
  uint16_t payload_count,
  uint16_t physical_payload_hint) {
  HashStoragePimpl pimpl(this);
  return pimpl.insert_record(
    context,
    key,
    key_length,
    combo,
    payload,
    payload_count,
    physical_payload_hint);
}

ErrorCode HashStorage::upsert_record(
  thread::Thread* context,
  const void* key,
  uint16_t key_length,
  const HashCombo& combo,
  const void* payload,
  uint16_t payload_count,
  uint16_t physical_payload_hint) {
  HashStoragePimpl pimpl(this);
  return pimpl.upsert_record(
    context,
    key,
    key_length,
    combo,
    payload,
    payload_count,
    physical_payload_hint);
}


ErrorCode HashStorage::delete_record(
  thread::Thread* context,
  const void* key,
  uint16_t key_length,
  const HashCombo& combo) {
  HashStoragePimpl pimpl(this);
  return pimpl.delete_record(
    context,
    key,
    key_length,
    combo);
}

ErrorCode HashStorage::overwrite_record(
  thread::Thread* context,
  const void* key,
  uint16_t key_length,
  const HashCombo& combo,
  const void* payload,
  uint16_t payload_offset,
  uint16_t payload_count) {
  HashStoragePimpl pimpl(this);
  return pimpl.overwrite_record(
    context,
    key,
    key_length,
    combo,
    payload,
    payload_offset,
    payload_count);
}

template <typename PAYLOAD>
ErrorCode HashStorage::overwrite_record_primitive(
  thread::Thread* context,
  const void* key,
  uint16_t key_length,
  const HashCombo& combo,
  PAYLOAD payload,
  uint16_t payload_offset) {
  HashStoragePimpl pimpl(this);
  return pimpl.overwrite_record_primitive(
    context,
    key,
    key_length,
    combo,
    payload,
    payload_offset);
}

template <typename PAYLOAD>
ErrorCode HashStorage::increment_record(
  thread::Thread* context,
  const void* key,
  uint16_t key_length,
  const HashCombo& combo,
  PAYLOAD* value,
  uint16_t payload_offset) {
  HashStoragePimpl pimpl(this);
  return pimpl.increment_record(
    context,
    key,
    key_length,
    combo,
    value,
    payload_offset);
}

std::ostream& operator<<(std::ostream& o, const HashStorage& v) {
  o << "<HashStorage>"
    << "<id>" << v.get_id() << "</id>"
    << "<name>" << v.get_name() << "</name>"
    << "<bin_bits>" << static_cast<int>(v.control_block_->meta_.bin_bits_) << "</bin_bits>"
    << "</HashStorage>";
  return o;
}

xct::TrackMovedRecordResult HashStorage::track_moved_record(
  xct::RwLockableXctId* old_address,
  xct::WriteXctAccess* write_set) {
  HashStoragePimpl pimpl(this);
  return pimpl.track_moved_record(old_address, write_set);
}

ErrorStack HashStorage::verify_single_thread(Engine* engine) {
  HashStoragePimpl pimpl(this);
  return pimpl.verify_single_thread(engine);
}

ErrorStack HashStorage::verify_single_thread(thread::Thread* context) {
  HashStoragePimpl pimpl(this);
  return pimpl.verify_single_thread(context);
}

ErrorStack HashStorage::hcc_reset_all_temperature_stat() {
  HashStoragePimpl pimpl(this);
  return pimpl.hcc_reset_all_temperature_stat();
}


ErrorStack HashStorage::debugout_single_thread(
  Engine* engine,
  bool volatile_only,
  bool intermediate_only,
  uint32_t max_pages) {
  HashStoragePimpl pimpl(this);
  return pimpl.debugout_single_thread(engine, volatile_only, intermediate_only, max_pages);
}

// Explicit instantiations for each payload type
// @cond DOXYGEN_IGNORE
#define EXPIN_2(x) template ErrorCode HashStorage::get_record_primitive< x > \
  (thread::Thread* context, \
    const void* key, \
    uint16_t key_length, \
    const HashCombo& combo, \
    x* payload, \
    uint16_t payload_offset, \
    bool read_only)
INSTANTIATE_ALL_NUMERIC_TYPES(EXPIN_2);

#define EXPIN_3(x) template ErrorCode HashStorage::overwrite_record_primitive< x > \
  (thread::Thread* context, \
    const void* key, \
    uint16_t key_length, \
    const HashCombo& combo, \
    x payload, \
    uint16_t payload_offset)
INSTANTIATE_ALL_NUMERIC_TYPES(EXPIN_3);

#define EXPIN_5(x) template ErrorCode HashStorage::increment_record< x > \
  (thread::Thread* context, \
    const void* key, \
    uint16_t key_length, \
    const HashCombo& combo, \
    x* value, \
    uint16_t payload_offset)
INSTANTIATE_ALL_NUMERIC_TYPES(EXPIN_5);
// @endcond


}  // namespace hash
}  // namespace storage
}  // namespace foedus
