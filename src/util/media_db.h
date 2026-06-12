#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

struct MediaItem {
    int64_t     id{0};
    std::string type;       // "still", "video", "tl_frame"
    std::string path;
    std::string filename;
    std::string captured_at; // ISO8601
    int64_t     size_bytes{0};
    int64_t     timelapse_id{0}; // 0 if not a tl_frame
    std::string blurhash;        // empty until thumbnail has been generated
};

struct TimelapseSession {
    int64_t     id{0};
    std::string session_dir;
    std::string session_name;
    std::string started_at;
    std::string stopped_at;   // empty if still running
    std::string fn_name;
    std::string params_json;
    int         frame_count{0};
    int64_t     first_frame_id{0};      // media.id of the first frame, 0 if none
    std::string first_frame_blurhash;   // blurhash of the first frame, empty if none
};

class MediaDb {
public:
    explicit MediaDb(const std::string& db_path,
                     const std::string& stills_dir,
                     const std::string& video_dir,
                     const std::string& tl_dir);
    ~MediaDb();

    MediaDb(const MediaDb&) = delete;
    MediaDb& operator=(const MediaDb&) = delete;

    int64_t add_still(const std::string& path);
    int64_t add_video(const std::string& path);
    int64_t add_timelapse_session(const std::string& session_dir,
                                  const std::string& started_at,
                                  const std::string& fn_name,
                                  const std::string& params_json);
    int64_t add_timelapse_frame(int64_t session_id, const std::string& path);
    void    finish_timelapse(int64_t session_id, const std::string& stopped_at);
    void    rename_timelapse_session(int64_t session_id, const std::string& new_dir);

    std::vector<MediaItem>        list_stills(int offset, int limit);
    std::vector<MediaItem>        list_videos(int offset, int limit);
    std::vector<TimelapseSession> list_timelapses(int offset, int limit);
    std::vector<MediaItem>        list_timelapse_frames(int64_t session_id, int offset, int limit);

    // Returns nullopt if id unknown or resolved path escapes the allowed dirs.
    std::optional<std::string>    get_path_for_serving(int64_t id);
    void                          store_blurhash(int64_t id, const std::string& hash);

    struct ScanResult { int added{0}; int removed{0}; };
    ScanResult               rebuild_from_disk();
    std::vector<std::string> verify();

private:
    void exec(const char* sql);
    // Insert one media row; caller must hold mtx_.
    int64_t insert_media_locked(const std::string& type, const std::string& path,
                                 int64_t timelapse_id = 0);

    sqlite3*    db_{nullptr};
    std::string stills_dir_, video_dir_, tl_dir_;
    mutable std::mutex mtx_;
};
