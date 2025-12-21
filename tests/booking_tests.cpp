#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#include <BookingManager.hpp>

using namespace NBooking;

static TUser AdminUser() {
    return TUser{1, "admin", ERole::Admin, 100};
}
static TUser ManagerUser() {
    return TUser{2, "mgr", ERole::Manager, 50};
}
static TUser NormalUser() {
    return TUser{3, "user", ERole::User, 10};
}

static TBooking MakeBooking(RoomId room, int startOffsetMin, int durationMin) {
    TBooking b;
    b.RoomIdInternal = room;
    b.UserIdInternal = 3;
    b.Title = "title";
    b.Description = "desc";
    b.Start =
        std::chrono::system_clock::now() + std::chrono::minutes(startOffsetMin);
    b.End = b.Start + std::chrono::minutes(durationMin);
    return b;
}

TEST(Conflicts, PartialOverlapFails) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.CreateBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(MakeBooking(1, 30, 60), u);
    EXPECT_FALSE(id2);
}

TEST(Conflicts, TouchingIntervalsAllowed) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.CreateBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(MakeBooking(1, 60, 60), u);
    EXPECT_TRUE(id2.has_value());
}

TEST(Conflicts, FullyContainedFails) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    mgr.CreateBooking(MakeBooking(1, 0, 120), u);
    auto id2 = mgr.CreateBooking(MakeBooking(1, 30, 10), u);

    EXPECT_FALSE(id2);
}

TEST(Conflicts, RecurrenceInstanceConflict) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    TBooking r = MakeBooking(1, 0, 60);
    r.Recurrence.type = TRecurrence::Type::Daily;
    r.Recurrence.Until = r.Start + std::chrono::hours(48);

    auto id1 = mgr.CreateBooking(r, u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(MakeBooking(1, 24 * 60, 60), u);
    EXPECT_FALSE(id2);
}

TEST(Resources, SameResourceConflicts) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    TBooking a = MakeBooking(1, 0, 60);
    a.Resources.push_back(TResource{"10"});
    mgr.CreateBooking(a, u);

    TBooking b = MakeBooking(1, 30, 60);
    b.Resources.push_back(TResource{"10"});

    auto id2 = mgr.CreateBooking(b, u);
    EXPECT_FALSE(id2);
}

TEST(History, CreateUndoRedoCancel) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.CreateBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id1);

    auto msg1 = mgr.Undo();
    ASSERT_TRUE(msg1);
    ASSERT_FALSE(mgr.GetBooking(*id1));

    auto msg2 = mgr.Redo();
    ASSERT_TRUE(msg2);
    ASSERT_TRUE(mgr.GetBooking(*id1));

    mgr.CancelBooking(*id1, u);
    ASSERT_FALSE(mgr.GetBooking(*id1));
}

TEST(History, HistoryLimitWorks) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    for (int i = 0; i < 400; i++) {
        mgr.CreateBooking(MakeBooking(1, i * 2, 1), u);
    }

    int undoCount = 0;
    while (mgr.Undo()) {
        undoCount++;
    }

    EXPECT_LE(undoCount, 300);
}

TEST(Multithreading, ParallelCreatesNoCrashes) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 20;

    std::atomic<int> created{0};
    std::vector<std::thread> th;

    for (int t = 0; t < THREADS; t++) {
        th.emplace_back([&] {
            for (int i = 0; i < PER_THREAD; i++) {
                RoomId r = 100 + (i % 5);
                auto id = mgr.CreateBooking(MakeBooking(r, i * 2, 1), u);
                if (id) {
                    created++;
                }
            }
        });
    }

    for (auto& x : th) {
        x.join();
    }

    EXPECT_EQ(created.load(), 20);
}

TEST(Conflicts, LongIntervalContainsShortFails) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.CreateBooking(MakeBooking(1, 0, 240), u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(MakeBooking(1, 60, 30), u);
    EXPECT_FALSE(id2);
}

TEST(Conflicts, RecurrenceNoConflictDifferentTimes) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    TBooking a = MakeBooking(1, 0, 60);
    a.Recurrence.type = TRecurrence::Type::Daily;
    a.Recurrence.Until = a.Start + std::chrono::hours(2);

    TBooking b = MakeBooking(1, 120, 60);
    b.Recurrence.type = TRecurrence::Type::Daily;
    b.Recurrence.Until = b.Start + std::chrono::hours(2);

    auto id1 = mgr.CreateBooking(a, u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(b, u);
    EXPECT_TRUE(id2.has_value());
}

TEST(Resources, SameResourcesConflict) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    TBooking a = MakeBooking(1, 0, 60);
    a.Resources.push_back(TResource{"projector-A"});

    TBooking b = MakeBooking(1, 0, 60);
    b.Resources.push_back(TResource{"projector-A"});

    auto id1 = mgr.CreateBooking(a, u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(b, u);
    EXPECT_FALSE(id2.has_value());
}

TEST(Conflicts, DifferentRoomsNoConflict) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.CreateBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(MakeBooking(2, 0, 60), u);
    EXPECT_TRUE(id2.has_value());
}

TEST(Resources, DifferentResourcesNoConflict) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    TBooking a = MakeBooking(1, 0, 60);
    a.Resources.push_back(TResource{"projector-A"});

    TBooking b = MakeBooking(1, 120, 60);
    b.Resources.push_back(TResource{"projector-B"});

    auto id1 = mgr.CreateBooking(a, u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(b, u);
    EXPECT_TRUE(id2.has_value());
}

TEST(History, CancelThenUndoRestoresBooking) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());
    auto u = NormalUser();

    auto id = mgr.CreateBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id);

    ASSERT_TRUE(mgr.CancelBooking(*id, u));
    ASSERT_FALSE(mgr.GetBooking(*id));

    auto msg = mgr.Undo();
    ASSERT_TRUE(msg);
    EXPECT_TRUE(mgr.GetBooking(*id));
}

TEST(RBAC, UserCannotCancelForeignBooking) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TRejectStrategy>());

    auto owner = NormalUser();
    auto other = TUser{42, "other", ERole::User, 10};

    auto id = mgr.CreateBooking(MakeBooking(1, 0, 60), owner);
    ASSERT_TRUE(id);

    EXPECT_THROW(
        mgr.CancelBooking(*id, other),
        std::runtime_error);
}

TEST(Strategy, AutoBumpShiftsToFreeSlot) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TAutoBumpStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.CreateBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(MakeBooking(1, 30, 60), u);
    ASSERT_TRUE(id2);

    auto b2 = mgr.GetBooking(*id2);
    ASSERT_TRUE(b2);
    EXPECT_GE(b2->Start, mgr.GetBooking(*id1)->End);
}

TEST(Strategy, PreemptAdminPreemptsUser) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TPreemptStrategy>());

    auto user = NormalUser();
    auto admin = AdminUser();

    auto id1 = mgr.CreateBooking(MakeBooking(1, 0, 60), user);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(MakeBooking(1, 30, 61), admin);
    ASSERT_TRUE(id2);
    EXPECT_EQ(*id1, *id2);
    EXPECT_EQ(*id2, 1);
}

TEST(Strategy, PreemptUserCannotPreemptAdmin) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TPreemptStrategy>());

    auto admin = AdminUser();
    auto user = NormalUser();

    auto id1 = mgr.CreateBooking(MakeBooking(1, 0, 60), admin);
    ASSERT_TRUE(id1);

    auto id2 = mgr.CreateBooking(MakeBooking(1, 2, 62), user);
    EXPECT_FALSE(id2);
}

TEST(Strategy, QuorumAllowsWithEnoughAttendees) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TQuorumStrategy>(2));
    auto u = NormalUser();

    mgr.CreateBooking(MakeBooking(1, 0, 60), u);

    TBooking b = MakeBooking(1, 0, 60);
    b.Attendees = {1, 2};

    auto id2 = mgr.CreateBooking(b, u);
    EXPECT_TRUE(id2.has_value());
}

TEST(Strategy, QuorumRejectsWithoutEnoughAttendees) {
    auto storage = std::make_shared<TMemoryStorage>();
    auto repo = std::make_shared<TRepository>(storage);

    TBookingManager mgr(repo, storage, std::make_shared<TQuorumStrategy>(3));
    auto u = NormalUser();

    mgr.CreateBooking(MakeBooking(1, 0, 60), u);

    TBooking b = MakeBooking(1, 0, 60);
    b.Attendees = {1};

    auto id2 = mgr.CreateBooking(b, u);
    EXPECT_FALSE(id2);
}
