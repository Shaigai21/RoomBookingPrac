#include <BookingManager.hpp>

namespace booking {

    BookingManager::BookingManager(std::shared_ptr<IRepository> repo,
                                   std::shared_ptr<IStorage> storage,
                                   std::shared_ptr<IConflictStrategy> strategy)
        : repo_(std::move(repo))
        , storage_(std::move(storage))
        , strategy_(std::move(strategy)) {
    }

    void BookingManager::pushUndo(std::unique_ptr<ICommand> cmd) {
        std::lock_guard lk(history_mutex_);
        undo_stack_.push_back(std::move(cmd));
        if (undo_stack_.size() > UNDO_LIMIT) {
            undo_stack_.pop_front();
        }
        redo_stack_.clear();
    }

    std::optional<std::string> BookingManager::undo() {
        std::unique_lock lk(history_mutex_);
        if (undo_stack_.empty()) {
            return std::nullopt;
        }
        auto cmd = std::move(undo_stack_.back());
        undo_stack_.pop_back();
        std::string desc = cmd->describe();
        cmd->undo();
        redo_stack_.push_back(std::move(cmd));
        return std::optional<std::string>(std::string("Undid: ") + desc);
    }

    std::optional<std::string> BookingManager::redo() {
        std::unique_lock lk(history_mutex_);
        if (redo_stack_.empty()) {
            return std::nullopt;
        }
        auto cmd = std::move(redo_stack_.back());
        redo_stack_.pop_back();
        std::string desc = cmd->describe();
        cmd->execute();
        undo_stack_.push_back(std::move(cmd));
        return std::optional<std::string>(std::string("Redid: ") + desc);
    }

    std::optional<Booking> BookingManager::getBooking(BookingId id) {
        return repo_->getBooking(id);
    }

    std::vector<Booking> BookingManager::listBookings(RoomId room,
                                                      std::chrono::system_clock::time_point from,
                                                      std::chrono::system_clock::time_point to) {
        std::vector<Booking> out;
        auto all = repo_->listAll();
        for (auto& b : all) {
            if (b.room_id != room) {
                continue;
            }
            auto inst = generateInstances(b, from, to);
            out.insert(out.end(), inst.begin(), inst.end());
        }
        return out;
    }

    bool BookingManager::canModify(const User& actor, const Booking& target) const {
        if (actor.role == Role::Admin) {
            return true;
        }
        if (actor.role == Role::Manager) {
            return true;
        }
        if (actor.role == Role::User) {
            return actor.id == target.user_id;
        }
        return false;
    }

    bool BookingManager::canCreate(const User& actor) const {
        return actor.role == Role::Admin ||
               actor.role == Role::Manager ||
               actor.role == Role::User;
    }

    bool BookingManager::canCancel(const User& actor, const Booking& target) const {
        if (actor.role == Role::Admin) {
            return true;
        }
        if (actor.role == Role::Manager) {
            return true;
        }
        if (actor.role == Role::User) {
            return actor.id == target.user_id;
        }
        return false;
    }

    std::optional<BookingId> BookingManager::createBooking(const Booking& req, const User& actor) {
        if (!canCreate(actor)) {
            throw std::runtime_error("Access denied: create");
        }

        std::lock_guard lk(mutex_);

        using namespace std::chrono;

        auto from = req.start - hours(24);
        auto to = req.recurrence.until
                      ? (*req.recurrence.until + hours(1))
                      : (req.start + hours(24 * 365));

        auto requestedInst = generateInstances(req, from, to);

        if (requestedInst.empty()) {
            requestedInst.push_back(req);
        }

        std::vector<Booking> existingInst;
        for (auto& ex : repo_->listAll()) {
            bool related = (ex.room_id == req.room_id);
            if (!related) {
                for (auto& r : ex.resources) {
                    for (auto& rr : req.resources) {
                        if (r.id == rr.id) {
                            related = true;
                        }
                    }
                }
            }
            if (!related) {
                continue;
            }

            auto insts = generateInstances(ex, from, to);
            existingInst.insert(existingInst.end(), insts.begin(), insts.end());
        }

        for (auto& inst : requestedInst) {
            auto res = strategy_->resolve(inst, existingInst, actor);
            if (!res.ok) {
                return std::nullopt;
            }

            if (res.suggested_start) {
                Booking adjusted = req;
                auto dur = adjusted.end - adjusted.start;
                adjusted.start = *res.suggested_start;
                adjusted.end = adjusted.start + dur;

                auto cmd = std::make_unique<CreateBookingCommand>(*repo_, adjusted);
                cmd->execute();
                auto id = cmd->id();
                pushUndo(std::move(cmd));
                return id;
            }
        }

        auto cmd = std::make_unique<CreateBookingCommand>(*repo_, req);
        cmd->execute();
        auto id = cmd->id();
        pushUndo(std::move(cmd));
        return id;
    }

    std::optional<BookingId> BookingManager::createBooking(const CreateRequest& req) {
        return createBooking(req.booking, req.actor);
    }

    bool BookingManager::modifyBooking(const ChangeRequest& req) {
        std::lock_guard lk(mutex_);

        auto old = repo_->getBooking(req.id);
        if (!old) {
            return false;
        }

        if (!canModify(req.actor, *old)) {
            throw std::runtime_error("Access denied: modify");
        }

        Booking updated = *old;
        if (req.title) {
            updated.title = *req.title;
        }
        if (req.description) {
            updated.description = *req.description;
        }
        if (req.start) {
            updated.start = *req.start;
        }
        if (req.end) {
            updated.end = *req.end;
        }

        auto cmd = std::make_unique<UpdateBookingCommand>(*repo_, *old, updated);
        cmd->execute();
        pushUndo(std::move(cmd));
        return true;
    }

    bool BookingManager::cancelBooking(BookingId id, const User& actor) {
        std::lock_guard lk(mutex_);
        auto ob = repo_->getBooking(id);
        if (!ob) {
            return false;
        }
        if (!canCancel(actor, *ob)) {
            throw std::runtime_error("Access denied: cancel");
        }

        auto cmd = std::make_unique<RemoveBookingCommand>(*repo_, id);
        cmd->execute();
        pushUndo(std::move(cmd));
        return true;
    }

    void BookingManager::setStrategy(std::shared_ptr<IConflictStrategy> s) {
        std::lock_guard lk(mutex_);
        strategy_ = s;
    }

    std::vector<BookingId> BookingManager::importFromCalendar(
        ICalendarAdapter& adapter,
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to,
        const User& actor) {
        if (actor.role != Role::Admin && actor.role != Role::Manager) {
            throw std::runtime_error("Access denied: import");
        }

        std::vector<BookingId> imported;
        auto events = adapter.fetch(from, to);

        for (auto const& ev : events) {
            Booking b;
            b.room_id = ev.room_id;
            b.user_id = ev.user_id;
            b.start = ev.start;
            b.end = ev.end;
            b.title = ev.title;
            b.description = ev.description;

            if (auto id = createBooking(b, actor)) {
                imported.push_back(*id);
            }
        }

        return imported;
    }

} // namespace booking
