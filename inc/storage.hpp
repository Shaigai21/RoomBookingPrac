#pragma once
#include "models.hpp"
#include <vector>
#include <mutex>
#include <optional>

struct IStorage {
    virtual ~IStorage() = default;
    virtual void saveState(const nlohmann::json& snapshot) = 0;
    virtual nlohmann::json loadState() = 0;
    virtual void appendJournal(const nlohmann::json& entry) = 0;
    virtual std::vector<nlohmann::json> loadJournal() = 0;
};

class MemoryStorage: public IStorage {
public:
    MemoryStorage() = default;

    void saveState(const nlohmann::json& snapshot) override {
        std::scoped_lock lk(mutex_);
        snapshot_ = snapshot;
    }

    nlohmann::json loadState() override {
        std::scoped_lock lk(mutex_);
        return snapshot_;
    }

    void appendJournal(const nlohmann::json& entry) override {
        std::scoped_lock lk(mutex_);
        journal_.push_back(entry);
    }

    std::vector<nlohmann::json> loadJournal() override {
        std::scoped_lock lk(mutex_);
        return journal_;
    }

private:
    nlohmann::json snapshot_ = nlohmann::json::object();
    std::vector<nlohmann::json> journal_;
    std::mutex mutex_;
};
