#include <FileJsonStorage.hpp>
#include <fstream>
#include <system_error>

namespace NBooking {

    TFileJsonStorage::TFileJsonStorage(std::filesystem::path snapshot_path, std::filesystem::path journal_path)
        : SnapshotPath(std::move(snapshot_path))
        , JournalPath(std::move(journal_path)) {
    }

    void TFileJsonStorage::AtomicWrite(const std::filesystem::path& path, const nlohmann::json& j) {
        std::error_code ec;
        auto tmp = path;
        tmp += ".tmp";

        std::ofstream ofs(tmp, std::ios::trunc);
        if (!ofs) {
            throw std::runtime_error("Cannot open temp file for writing: " + tmp.string());
        }
        ofs << j.dump(2);
        ofs.close();

        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp);
            throw std::runtime_error("Atomic rename failed: " + ec.message());
        }
    }

    void TFileJsonStorage::SaveState(const nlohmann::json& snapshot) {
        std::scoped_lock lk(Mutex_);
        if (!SnapshotPath.parent_path().empty()) {
            std::filesystem::create_directories(SnapshotPath.parent_path());
        }
        AtomicWrite(SnapshotPath, snapshot);
    }

    nlohmann::json TFileJsonStorage::LoadState() {
        std::scoped_lock lk(Mutex_);
        if (!std::filesystem::exists(SnapshotPath)) {
            return nlohmann::json::object();
        }
        std::ifstream ifs(SnapshotPath);
        if (!ifs) {
            return nlohmann::json::object();
        }
        nlohmann::json j;
        ifs >> j;
        return j;
    }

    void TFileJsonStorage::AppendJournal(const nlohmann::json& entry) {
        std::scoped_lock lk(Mutex_);
        if (!JournalPath.parent_path().empty()) {
            std::filesystem::create_directories(JournalPath.parent_path());
        }
        std::ofstream ofs(JournalPath, std::ios::app);
        if (!ofs) {
            throw std::runtime_error("Cannot open journal file for append: " + JournalPath.string());
        }
        ofs << entry.dump() << '\n';
    }

    std::vector<nlohmann::json> TFileJsonStorage::LoadJournal() {
        std::scoped_lock lk(Mutex_);
        std::vector<nlohmann::json> out;
        if (!std::filesystem::exists(JournalPath)) {
            return out;
        }
        std::ifstream ifs(JournalPath);
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty()) {
                continue;
            }
            try {
                out.push_back(nlohmann::json::parse(line));
            } catch (...) {
            }
        }
        return out;
    }

} // namespace NBooking
