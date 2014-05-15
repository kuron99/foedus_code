/*
 * Copyright (c) 2014, Hewlett-Packard Development Company, LP.
 * The license and distribution terms for this file are placed in LICENSE.txt.
 */
#include <foedus/test_common.hpp>
#include <foedus/engine_options.hpp>
#include <foedus/engine.hpp>
#include <foedus/epoch.hpp>
#include <foedus/assorted/uniform_random.hpp>
#include <foedus/storage/array/array_storage.hpp>
#include <foedus/storage/storage_manager.hpp>
#include <foedus/thread/rendezvous_impl.hpp>
#include <foedus/thread/thread_pool.hpp>
#include <foedus/thread/thread.hpp>
#include <foedus/xct/xct_manager.hpp>
#include <foedus/xct/xct.hpp>
#include <foedus/xct/xct_access.hpp>
#include <gtest/gtest.h>
#include <stdint.h>
#include <iostream>
#include <vector>
/**
 * @file tpch_array_tpcb.cpp
 * A minimal TPC-B on array storage.
 * This test uses a tiny scaling number to quickly run the test.
 */
namespace foedus {
namespace storage {
namespace array {

// tiny numbers
/** number of branches (TPS scaling factor). */
const int BRANCHES  =   8;
/** number of tellers in 1 branch. */
const int TELLERS   =   2;
/** number of accounts in 1 branch. */
const int ACCOUNTS  =   4;
const int ACCOUNTS_PER_TELLER = ACCOUNTS / TELLERS;

/** In this testcase, we run at most this number of threads. */
const int MAX_TEST_THREADS = 4;
/** number of transaction to run per thread. */
const int XCTS_PER_THREAD = 100;
const int INITIAL_ACCOUNT_BALANCE = 100;
const int AMOUNT_RANGE_FROM = 1;
const int AMOUNT_RANGE_TO = 20;

/** number of histories in TOTAL. */
const int HISTORIES =   XCTS_PER_THREAD * MAX_TEST_THREADS;

static_assert(ACCOUNTS % TELLERS == 0, "ACCOUNTS must be multiply of TELLERS");
static_assert(HISTORIES % ACCOUNTS == 0, "HISTORIES must be multiply of ACCOUNTS");

struct BranchData {
    int64_t     branch_balance_;
    char        other_data_[96];  // just to make it at least 100 bytes
};

struct TellerData {
    uint64_t    branch_id_;
    int64_t     teller_balance_;
    char        other_data_[88];  // just to make it at least 100 bytes
};

struct AccountData {
    uint64_t    branch_id_;
    int64_t     account_balance_;
    char        other_data_[88];  // just to make it at least 100 bytes
};

struct HistoryData {
    uint64_t    account_id_;
    uint64_t    teller_id_;
    uint64_t    branch_id_;
    int64_t     amount_;
    char        other_data_[24];  // just to make it at least 50 bytes
};

ArrayStorage* branches      = nullptr;
ArrayStorage* accounts      = nullptr;
ArrayStorage* tellers       = nullptr;
ArrayStorage* histories     = nullptr;

/** Creates TPC-B tables and populate with initial records. */
class CreateTpcbTablesTask : public thread::ImpersonateTask {
 public:
    ErrorStack run(thread::Thread* context) {
        StorageManager& str_manager = context->get_engine()->get_storage_manager();
        xct::XctManager& xct_manager = context->get_engine()->get_xct_manager();
        Epoch highest_commit_epoch;
        Epoch commit_epoch;

        // Create branches
        COERCE_ERROR(str_manager.create_array(context, "branches",
                                        sizeof(BranchData), BRANCHES, &branches));
        EXPECT_TRUE(branches != nullptr);
        COERCE_ERROR(xct_manager.begin_xct(context, xct::SERIALIZABLE));
        for (int i = 0; i < BRANCHES; ++i) {
            BranchData data;
            std::memset(&data, 0, sizeof(data));  // make valgrind happy
            data.branch_balance_ = INITIAL_ACCOUNT_BALANCE * ACCOUNTS;
            COERCE_ERROR(branches->overwrite_record(context, i, &data));
        }
        COERCE_ERROR(xct_manager.precommit_xct(context, &commit_epoch));
        highest_commit_epoch.store_max(commit_epoch);

        // Create tellers
        COERCE_ERROR(str_manager.create_array(context, "tellers",
                                      sizeof(AccountData), BRANCHES * TELLERS, &tellers));
        EXPECT_TRUE(tellers != nullptr);
        COERCE_ERROR(xct_manager.begin_xct(context, xct::SERIALIZABLE));
        for (int i = 0; i < BRANCHES * TELLERS; ++i) {
            TellerData data;
            std::memset(&data, 0, sizeof(data));  // make valgrind happy
            data.branch_id_ = i / TELLERS;
            data.teller_balance_ = INITIAL_ACCOUNT_BALANCE * ACCOUNTS_PER_TELLER;
            COERCE_ERROR(tellers->overwrite_record(context, i, &data));
        }
        COERCE_ERROR(xct_manager.precommit_xct(context, &commit_epoch));
        highest_commit_epoch.store_max(commit_epoch);

        // Create accounts
        COERCE_ERROR(str_manager.create_array(context, "accounts",
                                      sizeof(TellerData), BRANCHES * ACCOUNTS, &accounts));
        EXPECT_TRUE(accounts != nullptr);
        COERCE_ERROR(xct_manager.begin_xct(context, xct::SERIALIZABLE));
        for (int i = 0; i < BRANCHES * ACCOUNTS; ++i) {
            AccountData data;
            std::memset(&data, 0, sizeof(data));  // make valgrind happy
            data.branch_id_ = i / ACCOUNTS;
            data.account_balance_ = INITIAL_ACCOUNT_BALANCE;
            COERCE_ERROR(accounts->overwrite_record(context, i, &data));
        }
        COERCE_ERROR(xct_manager.precommit_xct(context, &commit_epoch));
        highest_commit_epoch.store_max(commit_epoch);

        // Create histories
        COERCE_ERROR(str_manager.create_array(context, "histories",
                                              sizeof(HistoryData), HISTORIES, &histories));
        EXPECT_TRUE(histories != nullptr);
        COERCE_ERROR(xct_manager.begin_xct(context, xct::SERIALIZABLE));
        for (int i = 0; i < HISTORIES; ++i) {
            HistoryData data;
            std::memset(&data, 0, sizeof(data));  // make valgrind happy
            data.branch_id_ = 0;
            data.teller_id_ = 0;
            data.account_id_ = 0;
            data.amount_ = 0;
            COERCE_ERROR(histories->overwrite_record(context, i, &data));
        }
        COERCE_ERROR(xct_manager.precommit_xct(context, &commit_epoch));
        highest_commit_epoch.store_max(commit_epoch);

        CHECK_ERROR(xct_manager.wait_for_commit(highest_commit_epoch));
        return RET_OK;
    }
};

/** run TPC-B queries. */
class RunTpcbTask : public thread::ImpersonateTask {
 public:
    RunTpcbTask(int client_id, bool contended, thread::Rendezvous* start_rendezvous)
        : client_id_(client_id), contended_(contended), start_rendezvous_(start_rendezvous) {
        ASSERT_ND(client_id < MAX_TEST_THREADS);
    }
    ErrorStack run(thread::Thread* context) {
        start_rendezvous_->wait();
        assorted::UniformRandom rand;
        rand.set_current_seed(client_id_);
        Epoch highest_commit_epoch;
        xct::XctManager& xct_manager = context->get_engine()->get_xct_manager();
        for (int i = 0; i < XCTS_PER_THREAD; ++i) {
            uint64_t account_id;
            if (contended_) {
                account_id = rand.next_uint32() % (BRANCHES * ACCOUNTS);
            } else {
                const uint64_t accounts_per_thread = (BRANCHES * ACCOUNTS / MAX_TEST_THREADS);
                account_id = rand.next_uint32() % accounts_per_thread
                    + (client_id_ * accounts_per_thread);
            }
            uint64_t teller_id = account_id / ACCOUNTS_PER_TELLER;
            uint64_t branch_id = account_id / ACCOUNTS;
            uint64_t history_id = client_id_ * XCTS_PER_THREAD + i;
            int64_t  amount = rand.uniform_within(AMOUNT_RANGE_FROM, AMOUNT_RANGE_TO);
            EXPECT_GE(amount, AMOUNT_RANGE_FROM);
            EXPECT_LE(amount, AMOUNT_RANGE_TO);
            while (true) {
                ErrorStack error_stack = try_transaction(context, &highest_commit_epoch,
                    branch_id, teller_id, account_id, history_id, amount);
                if (!error_stack.is_error()) {
                    break;
                } else if (error_stack.get_error_code() == ERROR_CODE_XCT_RACE_ABORT) {
                    // abort and retry
                    if (context->get_current_xct().is_active()) {
                        CHECK_ERROR(xct_manager.abort_xct(context));
                    }
                } else {
                    COERCE_ERROR(error_stack);
                }
            }
        }
        CHECK_ERROR(xct_manager.wait_for_commit(highest_commit_epoch));
        return foedus::RET_OK;
    }

    ErrorStack try_transaction(
        thread::Thread* context, Epoch *highest_commit_epoch,
        uint64_t branch_id, uint64_t teller_id,
        uint64_t account_id, uint64_t history_id, int64_t amount) {
        xct::XctManager& xct_manager = context->get_engine()->get_xct_manager();
        CHECK_ERROR(xct_manager.begin_xct(context, xct::SERIALIZABLE));

        BranchData branch;
        CHECK_ERROR(branches->get_record(context, branch_id, &branch));
        int64_t branch_balance = branch.branch_balance_ + amount;
        CHECK_ERROR(branches->overwrite_record(context, branch_id,
                                                &branch_balance, 0, sizeof(branch_balance)));

        TellerData teller;
        CHECK_ERROR(tellers->get_record(context, teller_id, &teller));
        EXPECT_EQ(branch_id, teller.branch_id_);
        int64_t teller_balance = teller.teller_balance_ + amount;
        CHECK_ERROR(tellers->overwrite_record(context, teller_id,
                    &teller_balance, sizeof(teller.branch_id_), sizeof(teller_balance)));

        AccountData account;
        CHECK_ERROR(accounts->get_record(context, account_id, &account));
        EXPECT_EQ(branch_id, account.branch_id_);
        int64_t account_balance = account.account_balance_ + amount;
        CHECK_ERROR(accounts->overwrite_record(context, account_id,
                    &account_balance, sizeof(account.branch_id_), sizeof(account_balance)));

        HistoryData history;
        CHECK_ERROR(histories->get_record(context, history_id, &history));
        EXPECT_EQ(0, history.account_id_);
        EXPECT_EQ(0, history.amount_);
        EXPECT_EQ(0, history.branch_id_);
        EXPECT_EQ(0, history.teller_id_);
        history.account_id_ = account_id;
        history.branch_id_ = branch_id;
        history.teller_id_ = teller_id;
        history.amount_ = amount;
        CHECK_ERROR(histories->overwrite_record(context, history_id, &history));

        Epoch commit_epoch;
        CHECK_ERROR(xct_manager.precommit_xct(context, &commit_epoch));
        std::cout << "Committed! Thread-" << context->get_thread_id() << " Updated "
            << " branch[" << branch_id << "] " << branch.branch_balance_ << " -> " << branch_balance
            << " teller[" << teller_id << "] " << teller.teller_balance_ << " -> " << teller_balance
            << " account[" << account_id << "] " << account.account_balance_
                << " -> " << account_balance
            << " history[" << history_id << "] amount=" << amount
            << std::endl;
        highest_commit_epoch->store_max(commit_epoch);
        return RET_OK;
    }

 private:
    int                         client_id_;
    bool                        contended_;

    // to start all threads at the same time.
    thread::Rendezvous*         start_rendezvous_;
};


/** Verify TPC-B results. */
class VerifyTpcbTask : public thread::ImpersonateTask {
 public:
    explicit VerifyTpcbTask(int clients) : clients_(clients) {
        ASSERT_ND(clients <= MAX_TEST_THREADS);
    }
    ErrorStack run(thread::Thread* context) {
        xct::XctManager& xct_manager = context->get_engine()->get_xct_manager();
        CHECK_ERROR(xct_manager.begin_xct(context, xct::SERIALIZABLE));

        int64_t expected_branch[BRANCHES];
        int64_t expected_teller[BRANCHES * TELLERS];
        int64_t expected_account[BRANCHES * ACCOUNTS];
        for (int i = 0; i < BRANCHES; ++i) {
            expected_branch[i] = INITIAL_ACCOUNT_BALANCE * ACCOUNTS;
        }
        for (int i = 0; i < BRANCHES * TELLERS; ++i) {
            expected_teller[i] = INITIAL_ACCOUNT_BALANCE * ACCOUNTS_PER_TELLER;
        }
        for (int i = 0; i < BRANCHES * ACCOUNTS; ++i) {
            expected_account[i] = INITIAL_ACCOUNT_BALANCE;
        }

        for (int client = 0; client < clients_; ++client) {
            for (int i = 0; i < XCTS_PER_THREAD; ++i) {
                SCOPED_TRACE(testing::Message() << "Verify client=" << client << ", i=" << i);
                uint64_t history_id = client * XCTS_PER_THREAD + i;
                HistoryData history;
                CHECK_ERROR(histories->get_record(context, history_id, &history));
                EXPECT_GE(history.amount_, AMOUNT_RANGE_FROM);
                EXPECT_LE(history.amount_, AMOUNT_RANGE_TO);

                EXPECT_LT(history.branch_id_, BRANCHES);
                EXPECT_LT(history.teller_id_, BRANCHES * TELLERS);
                EXPECT_LT(history.account_id_, BRANCHES * ACCOUNTS);

                EXPECT_EQ(history.branch_id_, history.teller_id_ / TELLERS);
                EXPECT_EQ(history.branch_id_, history.account_id_ / ACCOUNTS);
                EXPECT_EQ(history.teller_id_, history.account_id_ / ACCOUNTS_PER_TELLER);

                expected_branch[history.branch_id_] += history.amount_;
                expected_teller[history.teller_id_] += history.amount_;
                expected_account[history.account_id_] += history.amount_;
            }
        }

        for (int i = 0; i < BRANCHES; ++i) {
            BranchData data;
            CHECK_ERROR(branches->get_record(context, i, &data));
            EXPECT_EQ(expected_branch[i], data.branch_balance_) << "branch-" << i;
        }
        for (int i = 0; i < BRANCHES * TELLERS; ++i) {
            TellerData data;
            CHECK_ERROR(tellers->get_record(context, i, &data));
            EXPECT_EQ(i / TELLERS, data.branch_id_) << i;
            EXPECT_EQ(expected_teller[i], data.teller_balance_) << "teller-" << i;
        }
        for (int i = 0; i < BRANCHES * ACCOUNTS; ++i) {
            AccountData data;
            CHECK_ERROR(accounts->get_record(context, i, &data));
            EXPECT_EQ(i / ACCOUNTS, data.branch_id_) << i;
            EXPECT_EQ(expected_account[i], data.account_balance_) << "account-" << i;
        }
        for (uint32_t i = 0; i < context->get_current_xct().get_read_set_size(); ++i) {
            xct::XctAccess& access = context->get_current_xct().get_read_set()[i];
            EXPECT_FALSE(access.observed_owner_id_.is_locked<15>()) << i;
        }

        CHECK_ERROR(xct_manager.abort_xct(context));
        return foedus::RET_OK;
    }

 private:
    int clients_;
};

void multi_thread_test(int thread_count, bool contended) {
    EngineOptions options = get_tiny_options();
    options.log_.log_buffer_kb_ = 1 << 12;
    options.thread_.group_count_ = 1;
    options.thread_.thread_count_per_group_ = thread_count;
    Engine engine(options);
    COERCE_ERROR(engine.initialize());
    {
        UninitializeGuard guard(&engine);
        {
            CreateTpcbTablesTask task;
            COERCE_ERROR(engine.get_thread_pool().impersonate_synchronous(&task));
        }

        {
            thread::Rendezvous start_rendezvous;
            std::vector<RunTpcbTask*> tasks;
            std::vector<thread::ImpersonateSession> sessions;
            for (int i = 0; i < thread_count; ++i) {
                tasks.push_back(new RunTpcbTask(i, contended, &start_rendezvous));
                sessions.emplace_back(engine.get_thread_pool().impersonate(tasks[i]));
                if (!sessions[i].is_valid()) {
                    COERCE_ERROR(sessions[i].invalid_cause_);
                }
            }
            start_rendezvous.signal();
            for (int i = 0; i < thread_count; ++i) {
                COERCE_ERROR(sessions[i].get_result());
                delete tasks[i];
            }
        }
        {
            VerifyTpcbTask task(thread_count);
            COERCE_ERROR(engine.get_thread_pool().impersonate_synchronous(&task));
        }
        COERCE_ERROR(engine.uninitialize());
    }
    cleanup_test(options);
}

TEST(ArrayTpcbTest, SingleThreadedNoContention) { multi_thread_test(1, false); }
TEST(ArrayTpcbTest, TwoThreadedNoContention)    { multi_thread_test(2, false); }
TEST(ArrayTpcbTest, FourThreadedNoContention)   { multi_thread_test(4, false); }

TEST(ArrayTpcbTest, SingleThreadedContended)    { multi_thread_test(1, true); }
TEST(ArrayTpcbTest, TwoThreadedContended)       { multi_thread_test(2, true); }
TEST(ArrayTpcbTest, FourThreadedContended)      { multi_thread_test(4, true); }
}  // namespace array
}  // namespace storage
}  // namespace foedus
