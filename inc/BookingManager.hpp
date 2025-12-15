#pragma once
#include <memory>
#include <mutex>
#include <deque>
#include <vector>

#include "models.hpp"
#include "storage.hpp"
#include "strategies.hpp"
#include "commands.hpp"
#include "calendar.hpp"

namespace booking {

    class BookingManager {
    public:
        BookingManager(std::shared_ptr<IRepository> repo,
                       std::shared_ptr<IStorage> storage,
                       std::shared_ptr<IConflictStrategy> strategy);

        std::optional<BookingId> createBooking(const Booking& req, const User& actor);
        std::optional<BookingId> createBooking(const CreateRequest& req);
        bool modifyBooking(const ChangeRequest& req);
        bool cancelBooking(BookingId id, const User& actor);

        std::optional<Booking> getBooking(BookingId id);
        std::vector<Booking> listBookings(RoomId room,
                                          std::chrono::system_clock::time_point from,
                                          std::chrono::system_clock::time_point to);

        std::optional<std::string> undo();
        std::optional<std::string> redo();

        void internalCreate(Booking b);
        void internalRemove(BookingId id);
        void internalRestore(Booking b);
        std::vector<BookingId> importFromCalendar(
            ICalendarAdapter& adapter,
            std::chrono::system_clock::time_point from,
            std::chrono::system_clock::time_point to,
            const User& actor);
        void setStrategy(std::shared_ptr<IConflictStrategy> s);

    private:
        bool canCreate(const User& actor) const;
        bool canModify(const User& actor, const Booking& target) const;
        bool canCancel(const User& actor, const Booking& target) const;

        void pushUndo(std::unique_ptr<ICommand> cmd);

    private:
        std::shared_ptr<IRepository> repo_;
        std::shared_ptr<IStorage> storage_;
        std::shared_ptr<IConflictStrategy> strategy_;

        std::mutex mutex_;
        std::mutex history_mutex_;

        std::deque<std::unique_ptr<ICommand>> undo_stack_;
        std::deque<std::unique_ptr<ICommand>> redo_stack_;
        const size_t UNDO_LIMIT = 300;
    };

} // namespace booking
