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
#ifndef FOEDUS_STORAGE_COMPOSER_HPP_
#define FOEDUS_STORAGE_COMPOSER_HPP_

#include <iosfwd>
#include <string>

#include "foedus/compiler.hpp"
#include "foedus/epoch.hpp"
#include "foedus/error_stack.hpp"
#include "foedus/fwd.hpp"
#include "foedus/cache/fwd.hpp"
#include "foedus/memory/aligned_memory.hpp"
#include "foedus/snapshot/fwd.hpp"
#include "foedus/snapshot/snapshot.hpp"
#include "foedus/snapshot/snapshot_id.hpp"
#include "foedus/storage/fwd.hpp"
#include "foedus/storage/storage_id.hpp"
#include "foedus/thread/thread_id.hpp"

namespace foedus {
namespace storage {
/**
 * @brief Represents a logic to compose a new version of data pages for one storage.
 * @ingroup STORAGE SNAPSHOT
 * @details
 * @section COMPOSER_OVERVIEW Overview
 * This object is one of the liaisons between \ref SNAPSHOT module and \ref STORAGE module.
 * It receives previous snapshot files and pre-sorted log entries from snapshot module,
 * then applies a storage-specific implementation to convert them into a new version of data pages.
 * Every interface is batched and completely separated from the normal transactional processing
 * part. In fact, this object is not part of foedus::storage::Storage at all.
 *
 * @section COMPOSER_SCOPE Composer's scope
 * One composer object is in charge of data pages that meet \b all of following criteria:
 *  \li In one storage
 *  \li In one partition (in one NUMA node)
 *  \li In one snapshot
 *
 * None of these responsibilities is overlapping, so the job of composer is totally independent
 * from other composers \b except the root page of the storage.
 *
 * @section COMPOSER_INPUTS Inputs
 * Every composer receives the following when constructed.
 *  \li Corresponding Partitioner object that tells what pages this composer is responsible for.
 *  \li Pre-allocated and reused working memory (assured to be on the same NUMA node).
 *  \li Pre-sorted stream(s) of log entries (foedus::snapshot::SortedBuffer).
 *  \li Snapshot writer to allocate pages and write them out to a snapshot file.
 *  \li Most recent snapshot files.
 *
 * @section COMPOSER_OUTPUTS Outputs
 * Composers emit the following data when it's done.
 *  \li Composed data pages, which are written to the snapshot file by the snapshot writer.
 *  \li For each storage and for each second-level page that is pointed from the root page,
 * the snapshot pointer and relevant pointer information (eg key range).
 * We call this information as \e root-info and store them in a tentative page.
 * This is required to construct the root page at the end of snapshotting.
 *
 * @section COMPOSER_INSTALL Installing Composed Pages
 * At the end of snapshotting, composers install pointers to the snapshot pages they composed.
 * These are written to the snapshot pointer part of DualPagePointer so that transactions
 * can start using the snapshot pages.
 * Composers also drop volatile pointers if possible, reducing pressures to volatile page pool.
 * This volatile-drop is carefully done after pausing all transactions because we have to make sure
 * no transactions are newly installing a volatile page while we are dropping its parent.
 *
 * @par Shared memory, No virtual methods
 * Like Partitioner, no virtual methods allowed. We just do switch-case.
 */
class Composer CXX11_FINAL {
 public:
  Composer(Engine *engine, StorageId storage_id);

  Engine*   get_engine() { return engine_; }
  StorageId get_storage_id() const { return storage_id_; }
  StorageType get_storage_type() const { return storage_type_; }

  /** Arguments for compose() */
  struct ComposeArguments {
    /** Writes out composed pages. */
    snapshot::SnapshotWriter*         snapshot_writer_;
    /** To read existing snapshots. */
    cache::SnapshotFileSet*           previous_snapshot_files_;
    /** Sorted runs. */
    snapshot::SortedBuffer* const*    log_streams_;
    /** Number of sorted runs. */
    uint32_t                          log_streams_count_;
    /** Working memory to be used in this method. Automatically expand if needed. */
    memory::AlignedMemory*            work_memory_;
    /**
     * All log entries in this inputs are assured to be after this epoch.
     * Also, it is assured to be within 2^16 from this epoch.
     */
    Epoch                             base_epoch_;
    /**
     * [OUT] Returns pointers and related information that is required
     * to construct the root page. The data format depends on the composer. In all implementations,
     * the information must fit in one page (should be, otherwise we can't have a root page)
     */
    Page*                             root_info_page_;
  };
  /**
   * @brief Construct snapshot pages from sorted run files of one storage.
   */
  ErrorStack  compose(const ComposeArguments& args);

  /** Arguments for construct_root() */
  struct ConstructRootArguments {
    /** Writes out composed pages. */
    snapshot::SnapshotWriter*         snapshot_writer_;
    /** To read existing snapshots. */
    cache::SnapshotFileSet*           previous_snapshot_files_;
    /** Root info pages output by compose() */
    const Page* const*                root_info_pages_;
    /** Number of root info pages. */
    uint32_t                          root_info_pages_count_;
    /** All pre-allocated resouces to help run construct_root(), such as memory buffers. */
    snapshot::LogGleanerResource*     gleaner_resource_;
    /** [OUT] Returns pointer to new root snapshot page/ */
    SnapshotPagePointer*              new_root_page_pointer_;
  };

  /**
   * @brief Construct root page(s) for one storage based on the ouputs of compose().
   * @details
   * When all reducers complete, the gleaner invokes this method to construct new root
   * page(s) for the storage. This
   */
  ErrorStack  construct_root(const ConstructRootArguments& args);

  /** Arguments for drop_volatiles() */
  struct DropVolatilesArguments {
    /** The new snapshot. All newly created snapshot pages are of this snapshot */
    snapshot::Snapshot            snapshot_;
    /** if partitioned_drop_ is true, the partition this thread should drop volatile pages from */
    uint16_t                      my_partition_;
    /** if true, one thread for each partition will invoke drop_volatiles() */
    bool                          partitioned_drop_;
    /**
     * Caches dropped pages to avoid returning every single page.
     * This is an array of PagePoolOffsetChunk whose index is node ID.
     * For each dropped page, we add it to this chunk and batch-return them to the
     * volatile pool when it becomes full or after processing all storages.
     */
    memory::PagePoolOffsetChunk*  dropped_chunks_;
    /** [OUT] Number of volatile pages that were dropped */
    uint64_t*                     dropped_count_;

    /**
     * Returns (might cache) the given pointer to volatile pool.
     */
    void drop(Engine* engine, VolatilePagePointer pointer) const;
  };
  /** Retrun value of drop_volatiles() */
  struct DropResult {
    explicit DropResult(const DropVolatilesArguments& args) {
      max_observed_ = args.snapshot_.valid_until_epoch_;  // min value to make store_max easier.
      dropped_all_ = true;  // "so far". zero-inspected, thus zero-failure.
    }
    inline void combine(const DropResult& other) {
      max_observed_.store_max(other.max_observed_);
      dropped_all_ &= other.dropped_all_;
    }

    inline void on_rec_observed(Epoch epoch) {
      if (epoch > max_observed_) {
        max_observed_ = epoch;
        dropped_all_ = false;
      }
    }

    friend std::ostream&    operator<<(std::ostream& o, const DropResult& v);

    /**
     * the largest Epoch it observed recursively. The page is dropped only if the return value
     * is ==args.snapshot_.valid_until_epoch_. If some record under this contains larger (newer)
     * epoch, it returns that epoch. For ease of store_max, the returned epoch
     * is adjusted to args.snapshot_.valid_until_epoch_ if it's smaller than that.
     * Note that not all volatile pages might be dropped even if this is equal to
     * snapshot_.valid_until_epoch_ (eg no new modifications, but keep-volatile policy
     * told us to keep the volatile page).
     * Use dropped_all_ for that purpose.
     */
    Epoch max_observed_;
    /** Whether all volatile pages under the page was dropped. */
    bool  dropped_all_;
    bool  padding_[3];
  };

  /**
   * @brief Drops volatile pages that have not been modified since the snapshotted epoch.
   * @details
   * This is called after pausing transaction executions, so this method does not worry about
   * concurrent reads/writes while running this. Otherwise this method becomes
   * very complex and/or expensive. It's just milliseconds for each several minutes, so should
   * be fine to pause transactions.
   * Also, this method is best-effort in many aspects. It might not drop some volatile pages
   * that were not logically modified. In long run, it will be done at next snapshot,
   * so it's okay to be opportunistic.
   */
  DropResult drop_volatiles(const DropVolatilesArguments& args);

  /**
   * This is additionally called when no partitions observed any new modifications.
   * Only in this case, we can drop the root volatile page. Further, we can also drop
   * all the descendent volatile pages safely in this case.
   * Remember these methods are called within xct pausing.
   */
  void drop_root_volatile(const DropVolatilesArguments& args);

  friend std::ostream&    operator<<(std::ostream& o, const Composer& v);

 private:
  Engine* const                       engine_;
  const StorageId                     storage_id_;
  const StorageType                   storage_type_;
};

}  // namespace storage
}  // namespace foedus
#endif  // FOEDUS_STORAGE_COMPOSER_HPP_
