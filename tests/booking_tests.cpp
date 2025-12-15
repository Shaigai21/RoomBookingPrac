#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#include <BookingManager.hpp>

using namespace booking;

static User AdminUser() {
    return User{1, "admin", Role::Admin, 100};
}
static User ManagerUser() {
    return User{2, "mgr", Role::Manager, 50};
}
static User NormalUser() {
    return User{3, "user", Role::User, 10};
}

static Booking MakeBooking(RoomId room, int startOffsetMin, int durationMin) {
    Booking b;
    b.room_id = room;
    b.user_id = 3;
    b.title = "title";
    b.description = "desc";
    b.start =
        std::chrono::system_clock::now() + std::chrono::minutes(startOffsetMin);
    b.end = b.start + std::chrono::minutes(durationMin);
    return b;
}

TEST(Conflicts, PartialOverlapFails) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.createBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.createBooking(MakeBooking(1, 30, 60), u);
    EXPECT_FALSE(id2);
}

TEST(Conflicts, TouchingIntervalsAllowed) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.createBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.createBooking(MakeBooking(1, 60, 60), u);
    EXPECT_TRUE(id2.has_value());
}

TEST(Conflicts, FullyContainedFails) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    mgr.createBooking(MakeBooking(1, 0, 120), u);
    auto id2 = mgr.createBooking(MakeBooking(1, 30, 10), u);

    EXPECT_FALSE(id2);
}

TEST(Conflicts, RecurrenceInstanceConflict) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    Booking r = MakeBooking(1, 0, 60);
    r.recurrence.type = Recurrence::Type::Daily;
    r.recurrence.until = r.start + std::chrono::hours(48);

    auto id1 = mgr.createBooking(r, u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.createBooking(MakeBooking(1, 24 * 60, 60), u);
    EXPECT_FALSE(id2);
}

TEST(Resources, SameResourceConflicts) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    Booking a = MakeBooking(1, 0, 60);
    a.resources.push_back(Resource{"10"});
    mgr.createBooking(a, u);

    Booking b = MakeBooking(1, 30, 60);
    b.resources.push_back(Resource{"10"});

    auto id2 = mgr.createBooking(b, u);
    EXPECT_FALSE(id2);
}

TEST(History, CreateUndoRedoCancel) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.createBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id1);

    auto msg1 = mgr.undo();
    ASSERT_TRUE(msg1);
    ASSERT_FALSE(mgr.getBooking(*id1));

    auto msg2 = mgr.redo();
    ASSERT_TRUE(msg2);
    ASSERT_TRUE(mgr.getBooking(*id1));

    mgr.cancelBooking(*id1, u);
    ASSERT_FALSE(mgr.getBooking(*id1));
}

TEST(History, HistoryLimitWorks) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    for (int i = 0; i < 400; i++) {
        mgr.createBooking(MakeBooking(1, i * 2, 1), u);
    }

    int undoCount = 0;
    while (mgr.undo()) {
        undoCount++;
    }

    EXPECT_LE(undoCount, 300);
}

TEST(Multithreading, ParallelCreatesNoCrashes) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 20;

    std::atomic<int> created{0};
    std::vector<std::thread> th;

    for (int t = 0; t < THREADS; t++) {
        th.emplace_back([&] {
            for (int i = 0; i < PER_THREAD; i++) {
                RoomId r = 100 + (i % 5);
                auto id = mgr.createBooking(MakeBooking(r, i * 2, 1), u);
                if (id) {
                    created++;
                }
            }
        });
    }

    for (auto& x : th) {
        x.join();
    }

    EXPECT_GT(created.load(), 0);
}

TEST(Conflicts, LongIntervalContainsShortFails) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.createBooking(MakeBooking(1, 0, 240), u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.createBooking(MakeBooking(1, 60, 30), u);
    EXPECT_FALSE(id2);
}

TEST(Conflicts, Recurrence_NoConflictDifferentTimes) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    Booking a = MakeBooking(1, 0, 60);
    a.recurrence.type = Recurrence::Type::Daily;
    a.recurrence.until = a.start + std::chrono::hours(2);

    Booking b = MakeBooking(1, 120, 60);
    b.recurrence.type = Recurrence::Type::Daily;
    b.recurrence.until = b.start + std::chrono::hours(2);

    auto id1 = mgr.createBooking(a, u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.createBooking(b, u);
    EXPECT_TRUE(id2.has_value());
}

TEST(Resources, SameResources_Conflict) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    Booking a = MakeBooking(1, 0, 60);
    a.resources.push_back(Resource{"projector-A"});

    Booking b = MakeBooking(1, 0, 60);
    b.resources.push_back(Resource{"projector-A"});

    auto id1 = mgr.createBooking(a, u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.createBooking(b, u);
    EXPECT_FALSE(id2.has_value());
}

TEST(Conflicts, DifferentRooms_NoConflict) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    auto id1 = mgr.createBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.createBooking(MakeBooking(2, 0, 60), u);
    EXPECT_TRUE(id2.has_value());
}

TEST(Resources, DifferentResources_NoConflict) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    Booking a = MakeBooking(1, 0, 60);
    a.resources.push_back(Resource{"projector-A"});

    Booking b = MakeBooking(1, 120, 60);
    b.resources.push_back(Resource{"projector-B"});

    auto id1 = mgr.createBooking(a, u);
    ASSERT_TRUE(id1);

    auto id2 = mgr.createBooking(b, u);
    EXPECT_TRUE(id2.has_value());
}

TEST(History, CancelThenUndoRestoresBooking) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());
    auto u = NormalUser();

    auto id = mgr.createBooking(MakeBooking(1, 0, 60), u);
    ASSERT_TRUE(id);

    ASSERT_TRUE(mgr.cancelBooking(*id, u));
    ASSERT_FALSE(mgr.getBooking(*id));

    auto msg = mgr.undo();
    ASSERT_TRUE(msg);
    EXPECT_TRUE(mgr.getBooking(*id));
}

TEST(RBAC, UserCannotCancelForeignBooking) {
    auto storage = std::make_shared<MemoryStorage>();
    auto repo = std::make_shared<Repository>(storage);

    BookingManager mgr(repo, storage, std::make_shared<RejectStrategy>());

    auto owner = NormalUser();
    auto other = User{42, "other", Role::User, 10};

    auto id = mgr.createBooking(MakeBooking(1, 0, 60), owner);
    ASSERT_TRUE(id);

    EXPECT_THROW(
        mgr.cancelBooking(*id, other),
        std::runtime_error);
}