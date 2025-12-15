#include <FileJsonStorage.hpp>
#include <fstream>
#include <system_error>

namespace booking {

    FileJsonStorage::FileJsonStorage(std::filesystem::path snapshot_path, std::filesystem::path journal_path)
        : snapshot_path_(std::move(snapshot_path))
        , journal_path_(std::move(journal_path)) {
    }

    void FileJsonStorage::atomicWrite(const std::filesystem::path& path, const nlohmann::json& j) {
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

    void FileJsonStorage::saveState(const nlohmann::json& snapshot) {
        std::scoped_lock lk(mutex_);
        if (!snapshot_path_.parent_path().empty()) {
            std::filesystem::create_directories(snapshot_path_.parent_path());
        }
        atomicWrite(snapshot_path_, snapshot);
    }

    nlohmann::json FileJsonStorage::loadState() {
        std::scoped_lock lk(mutex_);
        if (!std::filesystem::exists(snapshot_path_)) {
            return nlohmann::json::object();
        }
        std::ifstream ifs(snapshot_path_);
        if (!ifs) {
            return nlohmann::json::object();
        }
        nlohmann::json j;
        ifs >> j;
        return j;
    }

    void FileJsonStorage::appendJournal(const nlohmann::json& entry) {
        std::scoped_lock lk(mutex_);
        if (!journal_path_.parent_path().empty()) {
            std::filesystem::create_directories(journal_path_.parent_path());
        }
        std::ofstream ofs(journal_path_, std::ios::app);
        if (!ofs) {
            throw std::runtime_error("Cannot open journal file for append: " + journal_path_.string());
        }
        ofs << entry.dump() << '\n';
    }

    std::vector<nlohmann::json> FileJsonStorage::loadJournal() {
        std::scoped_lock lk(mutex_);
        std::vector<nlohmann::json> out;
        if (!std::filesystem::exists(journal_path_)) {
            return out;
        }
        std::ifstream ifs(journal_path_);
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

} // namespace booking
