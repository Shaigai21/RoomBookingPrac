#include <BookingManager.hpp>
#include <iostream>

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

        TBooking req_copy = req;
        req_copy.OwnerPriority = actor.Priority;

        auto requestedInst = GenerateInstances(req_copy, from, to);

        if (requestedInst.empty()) {
            requestedInst.push_back(req_copy);
        }

        std::vector<TBooking> existingInst;
        for (auto& ex : Repo->ListAll()) {
            bool related = (ex.RoomIdInternal == req_copy.RoomIdInternal);
            if (!related) {
                for (auto& r : ex.Resources) {
                    for (auto& rr : req_copy.Resources) {
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

            if (!res.ToPreempt.empty()) {
                if (actor.Role != ERole::Admin && actor.Role != ERole::Manager) {
                    return std::nullopt;
                }

                for (BookingId bid : res.ToPreempt) {
                    auto old = Repo->GetBooking(bid);
                    if (!old) {
                        continue;
                    }

                    auto rm = std::make_unique<TRemoveBookingCommand>(*Repo, bid);
                    rm->Execute();

                    if (Repo->GetBooking(bid)) {
                        Repo->RemoveBooking(bid);
                    }

                    PushUndo(std::move(rm));
                }

                existingInst.erase(
                    std::remove_if(existingInst.begin(), existingInst.end(),
                                   [&](const TBooking& b) {
                                       return std::find(res.ToPreempt.begin(),
                                                        res.ToPreempt.end(),
                                                        b.Id) != res.ToPreempt.end();
                                   }),
                    existingInst.end());
            }

            if (res.SuggestedStart) {
                TBooking adjusted = req_copy;
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

        auto cmd = std::make_unique<TCreateBookingCommand>(*Repo, req_copy);
        cmd->Execute();
        auto id = cmd->id();
        PushUndo(std::move(cmd));
        return id;
    }

    std::optional<BookingId> TBookingManager::CreateBooking(const TCreateRequest& req) {
        return CreateBooking(req.Booking, req.Actor);
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

} // namespace NBooking
