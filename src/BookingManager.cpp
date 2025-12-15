#include <BookingManager.hpp>

namespace NBooking {

    TBookingManager::TBookingManager(std::shared_ptr<IRepository> repo,
                                     std::shared_ptr<IStorage> storage,
                                     std::shared_ptr<IConflictStrategy> strategy)
        : Repo(std::move(repo))
        , Storage(std::move(storage))
        , Strat(std::move(strategy)) {
    }

    void TBookingManager::PushUndo(std::unique_ptr<ICommand> cmd) {
        std::lock_guard lk(HistoryMutex_);
        UndoStack.push_back(std::move(cmd));
        if (UndoStack.size() > UNDO_LIMIT) {
            UndoStack.pop_front();
        }
        RedoStack.clear();
    }

    std::optional<std::string> TBookingManager::Undo() {
        std::unique_lock lk(HistoryMutex_);
        if (UndoStack.empty()) {
            return std::nullopt;
        }
        auto cmd = std::move(UndoStack.back());
        UndoStack.pop_back();
        std::string desc = cmd->Describe();
        cmd->Undo();
        RedoStack.push_back(std::move(cmd));
        return std::optional<std::string>(std::string("Undid: ") + desc);
    }

    std::optional<std::string> TBookingManager::Redo() {
        std::unique_lock lk(HistoryMutex_);
        if (RedoStack.empty()) {
            return std::nullopt;
        }
        auto cmd = std::move(RedoStack.back());
        RedoStack.pop_back();
        std::string desc = cmd->Describe();
        cmd->Execute();
        UndoStack.push_back(std::move(cmd));
        return std::optional<std::string>(std::string("Redid: ") + desc);
    }

    std::optional<TBooking> TBookingManager::GetBooking(BookingId id) {
        return Repo->GetBooking(id);
    }

    std::vector<TBooking> TBookingManager::ListBookings(RoomId room,
                                                        std::chrono::system_clock::time_point from,
                                                        std::chrono::system_clock::time_point to) {
        std::vector<TBooking> out;
        auto all = Repo->ListAll();
        for (auto& b : all) {
            if (b.RoomIdInternal != room) {
                continue;
            }
            auto inst = GenerateInstances(b, from, to);
            out.insert(out.end(), inst.begin(), inst.end());
        }
        return out;
    }

    bool TBookingManager::CanModify(const TUser& actor, const TBooking& target) const {
        if (actor.Role == ERole::Admin) {
            return true;
        }
        if (actor.Role == ERole::Manager) {
            return true;
        }
        if (actor.Role == ERole::User) {
            return actor.Id == target.UserIdInternal;
        }
        return false;
    }

    bool TBookingManager::CanCreate(const TUser& actor) const {
        return actor.Role == ERole::Admin ||
               actor.Role == ERole::Manager ||
               actor.Role == ERole::User;
    }

    bool TBookingManager::CanCancel(const TUser& actor, const TBooking& target) const {
        if (actor.Role == ERole::Admin) {
            return true;
        }
        if (actor.Role == ERole::Manager) {
            return true;
        }
        if (actor.Role == ERole::User) {
            return actor.Id == target.UserIdInternal;
        }
        return false;
    }

    std::optional<BookingId> TBookingManager::CreateBooking(const TBooking& req, const TUser& actor) {
        if (!CanCreate(actor)) {
            throw std::runtime_error("Access denied: create");
        }

        std::lock_guard lk(Mutex_);

        using namespace std::chrono;

        auto from = req.Start - hours(24);
        auto to = req.Recurrence.Until
                      ? (*req.Recurrence.Until + hours(1))
                      : (req.Start + hours(24 * 365));

        auto requestedInst = GenerateInstances(req, from, to);

        if (requestedInst.empty()) {
            requestedInst.push_back(req);
        }

        std::vector<TBooking> existingInst;
        for (auto& ex : Repo->ListAll()) {
            bool related = (ex.RoomIdInternal == req.RoomIdInternal);
            if (!related) {
                for (auto& r : ex.Resources) {
                    for (auto& rr : req.Resources) {
                        if (r.Id == rr.Id) {
                            related = true;
                        }
                    }
                }
            }
            if (!related) {
                continue;
            }

            auto insts = GenerateInstances(ex, from, to);
            existingInst.insert(existingInst.end(), insts.begin(), insts.end());
        }

        for (auto& inst : requestedInst) {
            auto res = Strat->Resolve(inst, existingInst, actor);
            if (!res.ok) {
                return std::nullopt;
            }

            if (res.SuggestedStart) {
                TBooking adjusted = req;
                auto dur = adjusted.End - adjusted.Start;
                adjusted.Start = *res.SuggestedStart;
                adjusted.End = adjusted.Start + dur;

                auto cmd = std::make_unique<TCreateBookingCommand>(*Repo, adjusted);
                cmd->Execute();
                auto id = cmd->id();
                PushUndo(std::move(cmd));
                return id;
            }
        }

        auto cmd = std::make_unique<TCreateBookingCommand>(*Repo, req);
        cmd->Execute();
        auto id = cmd->id();
        PushUndo(std::move(cmd));
        return id;
    }

    std::optional<BookingId> TBookingManager::CreateBooking(const TCreateRequest& req) {
        return CreateBooking(req.Booking, req.Actor);
    }

    bool TBookingManager::ModifyBooking(const TChangeRequest& req) {
        std::lock_guard lk(Mutex_);

        auto old = Repo->GetBooking(req.Id);
        if (!old) {
            return false;
        }

        if (!CanModify(req.Actor, *old)) {
            throw std::runtime_error("Access denied: modify");
        }

        TBooking updated = *old;
        if (req.Title) {
            updated.Title = *req.Title;
        }
        if (req.Description) {
            updated.Description = *req.Description;
        }
        if (req.Start) {
            updated.Start = *req.Start;
        }
        if (req.End) {
            updated.End = *req.End;
        }

        auto cmd = std::make_unique<TUpdateBookingCommand>(*Repo, *old, updated);
        cmd->Execute();
        PushUndo(std::move(cmd));
        return true;
    }

    bool TBookingManager::CancelBooking(BookingId id, const TUser& actor) {
        std::lock_guard lk(Mutex_);
        auto ob = Repo->GetBooking(id);
        if (!ob) {
            return false;
        }
        if (!CanCancel(actor, *ob)) {
            throw std::runtime_error("Access denied: cancel");
        }

        auto cmd = std::make_unique<TRemoveBookingCommand>(*Repo, id);
        cmd->Execute();
        PushUndo(std::move(cmd));
        return true;
    }

    void TBookingManager::SetStrategy(std::shared_ptr<IConflictStrategy> s) {
        std::lock_guard lk(Mutex_);
        Strat = s;
    }

    std::vector<BookingId> TBookingManager::ImportFromCalendar(
        ICalendarAdapter& adapter,
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to,
        const TUser& actor) {
        if (actor.Role != ERole::Admin && actor.Role != ERole::Manager) {
            throw std::runtime_error("Access denied: import");
        }

        std::vector<BookingId> imported;
        auto events = adapter.Fetch(from, to);

        for (auto const& ev : events) {
            TBooking b;
            b.RoomIdInternal = ev.RoomIdInternal;
            b.UserIdInternal = ev.UserIdInternal;
            b.Start = ev.Start;
            b.End = ev.End;
            b.Title = ev.Title;
            b.Description = ev.Description;

            if (auto id = CreateBooking(b, actor)) {
                imported.push_back(*id);
            }
        }

        return imported;
    }

} // namespace NBooking
