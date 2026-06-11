#include "media_db.h"

#include <sqlite3.h>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <climits>
#include <sys/stat.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// File-scoped helpers
// ---------------------------------------------------------------------------

static std::string file_mtime_iso(const std::string& path) {
    struct stat st;
    time_t t = (::stat(path.c_str(), &st) == 0) ? st.st_mtime : ::time(nullptr);
    char buf[32];
    struct tm* tm = ::localtime(&t);
    ::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tm);
    return buf;
}

static std::string basename_of(const std::string& path) {
    auto pos = path.rfind('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

static int64_t file_size_of(const std::string& path) {
    struct stat st;
    return (::stat(path.c_str(), &st) == 0) ? (int64_t)st.st_size : 0;
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

static const char kSchema[] = R"SQL(
CREATE TABLE IF NOT EXISTS timelapses (
  id           INTEGER PRIMARY KEY,
  session_dir  TEXT NOT NULL UNIQUE,
  session_name TEXT NOT NULL,
  started_at   TEXT NOT NULL,
  stopped_at   TEXT,
  frame_count  INTEGER DEFAULT 0,
  fn_name      TEXT,
  params_json  TEXT
);
CREATE TABLE IF NOT EXISTS media (
  id           INTEGER PRIMARY KEY,
  type         TEXT NOT NULL,
  path         TEXT NOT NULL UNIQUE,
  filename     TEXT NOT NULL,
  size_bytes   INTEGER,
  captured_at  TEXT NOT NULL,
  timelapse_id INTEGER REFERENCES timelapses(id)
);
CREATE INDEX IF NOT EXISTS media_type_time ON media(type, captured_at DESC);
CREATE INDEX IF NOT EXISTS media_tl        ON media(timelapse_id, captured_at);
)SQL";

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MediaDb::MediaDb(const std::string& db_path,
                 const std::string& stills_dir,
                 const std::string& video_dir,
                 const std::string& tl_dir)
    : stills_dir_(stills_dir), video_dir_(video_dir), tl_dir_(tl_dir)
{
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "[db] open failed: " << sqlite3_errmsg(db_) << "\n";
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    exec(kSchema);
}

MediaDb::~MediaDb() {
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void MediaDb::exec(const char* sql) {
    if (!db_) return;
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "[db] exec: " << (err ? err : "?") << "\n";
        sqlite3_free(err);
    }
}

int64_t MediaDb::insert_media_locked(const std::string& type, const std::string& path,
                                      int64_t timelapse_id)
{
    if (!db_) return 0;
    const char* sql =
        "INSERT OR IGNORE INTO media(type,path,filename,size_bytes,captured_at,timelapse_id)"
        " VALUES(?,?,?,?,?,?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    std::string fname = basename_of(path);
    std::string ts    = file_mtime_iso(path);
    int64_t     sz    = file_size_of(path);

    sqlite3_bind_text (stmt, 1, type.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 2, path.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (stmt, 3, fname.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, sz);
    sqlite3_bind_text (stmt, 5, ts.c_str(),    -1, SQLITE_TRANSIENT);
    if (timelapse_id > 0)
        sqlite3_bind_int64(stmt, 6, timelapse_id);
    else
        sqlite3_bind_null(stmt, 6);

    sqlite3_step(stmt);
    // sqlite3_last_insert_rowid() is unchanged when INSERT OR IGNORE skips a row;
    // use sqlite3_changes() to detect whether a row was actually inserted.
    int64_t rowid = (sqlite3_changes(db_) > 0) ? sqlite3_last_insert_rowid(db_) : 0;
    sqlite3_finalize(stmt);
    return rowid;
}

// ---------------------------------------------------------------------------
// Public write methods
// ---------------------------------------------------------------------------

int64_t MediaDb::add_still(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    return insert_media_locked("still", path);
}

int64_t MediaDb::add_video(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    return insert_media_locked("video", path);
}

int64_t MediaDb::add_timelapse_session(const std::string& session_dir,
                                        const std::string& started_at,
                                        const std::string& fn_name,
                                        const std::string& params_json)
{
    if (!db_) return 0;
    std::lock_guard<std::mutex> lk(mtx_);
    const char* sql =
        "INSERT OR IGNORE INTO timelapses(session_dir,session_name,started_at,fn_name,params_json)"
        " VALUES(?,?,?,?,?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    std::string name = basename_of(session_dir);
    sqlite3_bind_text(stmt, 1, session_dir.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, started_at.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, fn_name.c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, params_json.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    int64_t rowid = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(stmt);
    return rowid;
}

int64_t MediaDb::add_timelapse_frame(int64_t session_id, const std::string& path) {
    if (!db_) return 0;
    std::lock_guard<std::mutex> lk(mtx_);
    int64_t id = insert_media_locked("tl_frame", path, session_id);
    if (id > 0) {
        const char* upd = "UPDATE timelapses SET frame_count=frame_count+1 WHERE id=?";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, upd, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, session_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    return id;
}

void MediaDb::finish_timelapse(int64_t session_id, const std::string& stopped_at) {
    if (!db_) return;
    std::lock_guard<std::mutex> lk(mtx_);
    const char* sql = "UPDATE timelapses SET stopped_at=? WHERE id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text (stmt, 1, stopped_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, session_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void MediaDb::rename_timelapse_session(int64_t session_id, const std::string& new_dir) {
    if (!db_) return;
    std::lock_guard<std::mutex> lk(mtx_);

    // Fetch old session_dir so we can rewrite frame paths.
    std::string old_dir;
    {
        const char* q = "SELECT session_dir FROM timelapses WHERE id=?";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, q, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, session_id);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (s) old_dir = s;
            }
            sqlite3_finalize(stmt);
        }
    }
    if (old_dir.empty()) return;

    std::string new_name = basename_of(new_dir);

    {
        const char* sql = "UPDATE timelapses SET session_dir=?,session_name=? WHERE id=?";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text (stmt, 1, new_dir.c_str(),  -1, SQLITE_TRANSIENT);
            sqlite3_bind_text (stmt, 2, new_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, session_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // Bulk-update frame paths: replace old_dir prefix with new_dir.
    // SQLite's substr() is 1-indexed; length of old_dir + 1 gives the character after the prefix.
    {
        const char* sql =
            "UPDATE media SET path=?1||substr(path,?2)"
            " WHERE timelapse_id=?3 AND type='tl_frame'";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text (stmt, 1, new_dir.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, (int64_t)old_dir.size() + 1);
            sqlite3_bind_int64(stmt, 3, session_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

// ---------------------------------------------------------------------------
// Query helpers
// ---------------------------------------------------------------------------

static MediaItem row_to_item(sqlite3_stmt* stmt) {
    MediaItem item;
    item.id    = sqlite3_column_int64(stmt, 0);
    auto s = [&](int i) -> std::string {
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
        return p ? p : "";
    };
    item.type         = s(1);
    item.path         = s(2);
    item.filename     = s(3);
    item.captured_at  = s(4);
    item.size_bytes   = sqlite3_column_int64(stmt, 5);
    item.timelapse_id = sqlite3_column_int64(stmt, 6);
    return item;
}

static TimelapseSession row_to_session(sqlite3_stmt* stmt) {
    TimelapseSession ts;
    auto s = [&](int i) -> std::string {
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
        return p ? p : "";
    };
    ts.id           = sqlite3_column_int64(stmt, 0);
    ts.session_dir  = s(1);
    ts.session_name = s(2);
    ts.started_at   = s(3);
    ts.stopped_at   = s(4);
    ts.fn_name      = s(5);
    ts.params_json  = s(6);
    ts.frame_count  = sqlite3_column_int(stmt, 7);
    return ts;
}

// ---------------------------------------------------------------------------
// Public read methods
// ---------------------------------------------------------------------------

std::vector<MediaItem> MediaDb::list_stills(int offset, int limit) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<MediaItem> result;
    if (!db_) return result;
    const char* sql =
        "SELECT id,type,path,filename,captured_at,size_bytes,timelapse_id"
        " FROM media WHERE type='still' ORDER BY captured_at DESC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);
    while (sqlite3_step(stmt) == SQLITE_ROW) result.push_back(row_to_item(stmt));
    sqlite3_finalize(stmt);
    return result;
}

std::vector<MediaItem> MediaDb::list_videos(int offset, int limit) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<MediaItem> result;
    if (!db_) return result;
    const char* sql =
        "SELECT id,type,path,filename,captured_at,size_bytes,timelapse_id"
        " FROM media WHERE type='video' ORDER BY captured_at DESC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);
    while (sqlite3_step(stmt) == SQLITE_ROW) result.push_back(row_to_item(stmt));
    sqlite3_finalize(stmt);
    return result;
}

std::vector<TimelapseSession> MediaDb::list_timelapses(int offset, int limit) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<TimelapseSession> result;
    if (!db_) return result;
    const char* sql =
        "SELECT id,session_dir,session_name,started_at,stopped_at,fn_name,params_json,frame_count"
        " FROM timelapses ORDER BY started_at DESC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);
    while (sqlite3_step(stmt) == SQLITE_ROW) result.push_back(row_to_session(stmt));
    sqlite3_finalize(stmt);
    return result;
}

std::vector<MediaItem> MediaDb::list_timelapse_frames(int64_t session_id, int offset, int limit) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<MediaItem> result;
    if (!db_) return result;
    const char* sql =
        "SELECT id,type,path,filename,captured_at,size_bytes,timelapse_id"
        " FROM media WHERE type='tl_frame' AND timelapse_id=?"
        " ORDER BY captured_at ASC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int64(stmt, 1, session_id);
    sqlite3_bind_int  (stmt, 2, limit);
    sqlite3_bind_int  (stmt, 3, offset);
    while (sqlite3_step(stmt) == SQLITE_ROW) result.push_back(row_to_item(stmt));
    sqlite3_finalize(stmt);
    return result;
}

std::optional<std::string> MediaDb::get_path_for_serving(int64_t id) {
    std::string path;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!db_) return std::nullopt;
        const char* sql = "SELECT path FROM media WHERE id=?";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (p) path = p;
        }
        sqlite3_finalize(stmt);
    }
    if (path.empty()) return std::nullopt;

    char resolved[PATH_MAX];
    if (!::realpath(path.c_str(), resolved)) return std::nullopt;
    std::string canon(resolved);

    auto under = [&](const std::string& dir) -> bool {
        char dr[PATH_MAX];
        if (!::realpath(dir.c_str(), dr)) return false;
        std::string d(dr);
        return canon.size() > d.size() &&
               canon.compare(0, d.size(), d) == 0 &&
               canon[d.size()] == '/';
    };

    if (under(stills_dir_) || under(video_dir_) || under(tl_dir_))
        return canon;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// rebuild_from_disk
// ---------------------------------------------------------------------------

MediaDb::ScanResult MediaDb::rebuild_from_disk() {
    ScanResult result;

    // ---- Pass 1: collect paths from disk (no lock held) ----

    struct SessionInfo {
        std::string dir, name, mtime;
        std::vector<std::string> frames;
    };

    std::vector<std::string> stills, videos;
    std::vector<SessionInfo> sessions;

    auto ends_with = [](const std::string& s, const std::string& suf) -> bool {
        return s.size() >= suf.size() &&
               s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    };

    try {
        for (auto& e : fs::directory_iterator(stills_dir_)) {
            if (!e.is_regular_file()) continue;
            std::string p = e.path().string();
            if (ends_with(p, ".jpg") || ends_with(p, ".jpeg") || ends_with(p, ".raw"))
                stills.push_back(p);
        }
    } catch (...) {}

    try {
        for (auto& e : fs::directory_iterator(video_dir_)) {
            if (!e.is_regular_file()) continue;
            std::string p = e.path().string();
            if (ends_with(p, ".mkv")) videos.push_back(p);
        }
    } catch (...) {}

    try {
        for (auto& sess : fs::directory_iterator(tl_dir_)) {
            if (!sess.is_directory()) continue;
            SessionInfo si;
            si.dir   = sess.path().string();
            si.name  = sess.path().filename().string();
            si.mtime = file_mtime_iso(si.dir);
            try {
                for (auto& fr : fs::directory_iterator(si.dir)) {
                    if (!fr.is_regular_file()) continue;
                    std::string fp = fr.path().string();
                    if (ends_with(fp, ".jpg")) si.frames.push_back(fp);
                }
            } catch (...) {}
            sessions.push_back(std::move(si));
        }
    } catch (...) {}

    // ---- Pass 2: insert into DB inside a single transaction ----
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!db_) return result;

        exec("BEGIN");

        for (auto& p : stills) {
            if (insert_media_locked("still", p) > 0) ++result.added;
        }
        for (auto& p : videos) {
            if (insert_media_locked("video", p) > 0) ++result.added;
        }

        for (auto& s : sessions) {
            // Get or create the session row.
            int64_t sess_id = 0;
            {
                const char* q = "SELECT id FROM timelapses WHERE session_dir=?";
                sqlite3_stmt* stmt;
                if (sqlite3_prepare_v2(db_, q, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, s.dir.c_str(), -1, SQLITE_TRANSIENT);
                    if (sqlite3_step(stmt) == SQLITE_ROW)
                        sess_id = sqlite3_column_int64(stmt, 0);
                    sqlite3_finalize(stmt);
                }
            }
            if (sess_id == 0) {
                const char* ins =
                    "INSERT OR IGNORE INTO timelapses(session_dir,session_name,started_at)"
                    " VALUES(?,?,?)";
                sqlite3_stmt* stmt;
                if (sqlite3_prepare_v2(db_, ins, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, s.dir.c_str(),   -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, s.name.c_str(),  -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 3, s.mtime.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sess_id = sqlite3_last_insert_rowid(db_);
                    sqlite3_finalize(stmt);
                    if (sess_id > 0) ++result.added;
                }
            }
            if (sess_id == 0) continue;

            for (auto& fp : s.frames) {
                if (insert_media_locked("tl_frame", fp, sess_id) > 0) ++result.added;
            }
        }

        // Recompute frame_count for all sessions in one pass.
        exec("UPDATE timelapses SET frame_count="
             "(SELECT COUNT(*) FROM media WHERE media.timelapse_id=timelapses.id)");

        exec("COMMIT");

        // ---- Remove rows whose file no longer exists ----
        std::vector<int64_t> dead;
        {
            const char* q = "SELECT id,path FROM media";
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db_, q, -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    if (p && ::access(p, F_OK) != 0)
                        dead.push_back(sqlite3_column_int64(stmt, 0));
                }
                sqlite3_finalize(stmt);
            }
        }
        if (!dead.empty()) {
            exec("BEGIN");
            for (int64_t rid : dead) {
                const char* del = "DELETE FROM media WHERE id=?";
                sqlite3_stmt* stmt;
                if (sqlite3_prepare_v2(db_, del, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(stmt, 1, rid);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                    ++result.removed;
                }
            }
            exec("UPDATE timelapses SET frame_count="
                 "(SELECT COUNT(*) FROM media WHERE media.timelapse_id=timelapses.id)");
            exec("COMMIT");
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// verify
// ---------------------------------------------------------------------------

std::vector<std::string> MediaDb::verify() {
    std::vector<std::string> missing;
    std::lock_guard<std::mutex> lk(mtx_);
    if (!db_) return missing;
    const char* q = "SELECT path FROM media";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, q, -1, &stmt, nullptr) != SQLITE_OK) return missing;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (p && ::access(p, F_OK) != 0) missing.push_back(p);
    }
    sqlite3_finalize(stmt);
    return missing;
}
