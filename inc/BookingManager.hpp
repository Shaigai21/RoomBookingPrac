#pragma once
#include <memory>
#include <mutex>
#include <deque>
#include <vector>

#include "common.hpp"
#include "Storage.hpp"
#include "Strategy.hpp"
#include "Command.hpp"

namespace NBooking {

    class TBookingManager {
    public:
        TBookingManager(std::shared_ptr<IRepository> repo,
                        std::shared_ptr<IStorage> storage,
                        std::shared_ptr<IConflictStrategy> strategy);

        std::optional<BookingId> CreateBooking(const TBooking& req, const TUser& actor);
        std::optional<BookingId> CreateBooking(const TCreateRequest& req);
        bool CancelBooking(BookingId id, const TUser& actor);

        std::optional<TBooking> GetBooking(BookingId id);
        std::vector<TBooking> ListBookings(RoomId room,
                                           std::chrono::system_clock::time_point from,
                                           std::chrono::system_clock::time_point to);

        std::optional<std::string> Undo();
        std::optional<std::string> Redo();

        // for debug
        void SetStrategy(std::shared_ptr<IConflictStrategy> s);

    private:
        bool CanCreate(const TUser& actor) const;
        bool CanModify(const TUser& actor, const TBooking& target) const;
        bool CanCancel(const TUser& actor, const TBooking& target) const;

        void PushUndo(std::unique_ptr<ICommand> cmd);

    private:
        std::shared_ptr<IRepository> Repo;
        std::shared_ptr<IStorage> Storage;
        std::shared_ptr<IConflictStrategy> Strat;

        std::mutex Mutex_;
        std::mutex HistoryMutex_;

        std::deque<std::unique_ptr<ICommand>> UndoStack;
        std::deque<std::unique_ptr<ICommand>> RedoStack;
        const size_t UNDO_LIMIT = 300;
    };

} // namespace NBooking
