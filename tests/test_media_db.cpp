#include "util/media_db.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Fixture — fresh temp tree + MediaDb for every test
// ---------------------------------------------------------------------------

class MediaDbTest : public ::testing::Test {
protected:
    std::string root_, stills_, videos_, tl_;
    std::unique_ptr<MediaDb> db_;

    void SetUp() override {
        char tmp[] = "/tmp/mdb_test_XXXXXX";
        char* r = mkdtemp(tmp);
        ASSERT_NE(r, nullptr);
        root_   = r;
        stills_ = root_ + "/stills";
        videos_ = root_ + "/videos";
        tl_     = root_ + "/tl";
        fs::create_directories(stills_);
        fs::create_directories(videos_);
        fs::create_directories(tl_);
        // On-disk DB so realpath() works for the dir prefix checks.
        db_ = std::make_unique<MediaDb>(root_ + "/test.db", stills_, videos_, tl_);
    }

    void TearDown() override {
        db_.reset();
        fs::remove_all(root_);
    }

    std::string make_file(const std::string& dir, const std::string& name,
                          const char* content = "x") {
        std::string p = dir + "/" + name;
        std::ofstream f(p); f << content;
        return p;
    }
};

// ---------------------------------------------------------------------------
// Construction / bad path
// ---------------------------------------------------------------------------

TEST_F(MediaDbTest, BadDbPath_OperationsDoNotCrash) {
    MediaDb bad("/no/such/dir/test.db", stills_, videos_, tl_);
    EXPECT_EQ(bad.list_stills(0, 10).size(), 0u);
    EXPECT_FALSE(bad.get_path_for_serving(1).has_value());
    EXPECT_EQ(bad.verify().size(), 0u);
}

// ---------------------------------------------------------------------------
// add_still / list_stills
// ---------------------------------------------------------------------------

TEST_F(MediaDbTest, AddStill_ReturnsPositiveId) {
    EXPECT_GT(db_->add_still(make_file(stills_, "a.jpg")), 0);
}

TEST_F(MediaDbTest, AddStill_DuplicatePath_ReturnsZero) {
    std::string p = make_file(stills_, "dup.jpg");
    EXPECT_GT(db_->add_still(p), 0);
    EXPECT_EQ(db_->add_still(p), 0); // INSERT OR IGNORE
}

TEST_F(MediaDbTest, ListStills_EmptyByDefault) {
    EXPECT_TRUE(db_->list_stills(0, 10).empty());
}

TEST_F(MediaDbTest, ListStills_ReturnsInsertedItem) {
    std::string p = make_file(stills_, "shot.jpg");
    db_->add_still(p);
    auto items = db_->list_stills(0, 10);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type,     "still");
    EXPECT_EQ(items[0].path,     p);
    EXPECT_EQ(items[0].filename, "shot.jpg");
    EXPECT_GT(items[0].id,       0);
}

TEST_F(MediaDbTest, ListStills_Pagination) {
    for (int i = 0; i < 5; ++i)
        db_->add_still(make_file(stills_, "f" + std::to_string(i) + ".jpg"));

    auto p0 = db_->list_stills(0, 3);
    auto p1 = db_->list_stills(3, 3);
    EXPECT_EQ(p0.size(), 3u);
    EXPECT_EQ(p1.size(), 2u);
    // No overlap between pages.
    for (auto& a : p0)
        for (auto& b : p1)
            EXPECT_NE(a.id, b.id);
}

TEST_F(MediaDbTest, ListStills_DoesNotReturnVideos) {
    db_->add_video(make_file(videos_, "v.mkv"));
    EXPECT_TRUE(db_->list_stills(0, 10).empty());
}

// ---------------------------------------------------------------------------
// add_video / list_videos
// ---------------------------------------------------------------------------

TEST_F(MediaDbTest, AddVideo_ReturnsPositiveId) {
    EXPECT_GT(db_->add_video(make_file(videos_, "clip.mkv")), 0);
}

TEST_F(MediaDbTest, ListVideos_ReturnsInsertedItem) {
    std::string p = make_file(videos_, "vid.mkv");
    db_->add_video(p);
    auto items = db_->list_videos(0, 10);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type,     "video");
    EXPECT_EQ(items[0].filename, "vid.mkv");
}

// ---------------------------------------------------------------------------
// Timelapse session lifecycle
// ---------------------------------------------------------------------------

TEST_F(MediaDbTest, AddTimelapseSession_ReturnsPositiveId) {
    std::string sd = tl_ + "/sess1";
    fs::create_directories(sd);
    EXPECT_GT(db_->add_timelapse_session(sd, "2024-01-01T00:00:00", "linear", "{}"), 0);
}

TEST_F(MediaDbTest, AddTimelapseFrame_IncrementsFrameCount) {
    std::string sd = tl_ + "/sess2";
    fs::create_directories(sd);
    int64_t sid = db_->add_timelapse_session(sd, "2024-01-01T00:00:00", "", "");
    ASSERT_GT(sid, 0);

    db_->add_timelapse_frame(sid, make_file(sd, "0001.jpg"));
    db_->add_timelapse_frame(sid, make_file(sd, "0002.jpg"));

    auto sessions = db_->list_timelapses(0, 10);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0].frame_count, 2);
}

TEST_F(MediaDbTest, ListTimelapseFrames_ForSpecificSession) {
    std::string sd = tl_ + "/sess3";
    fs::create_directories(sd);
    int64_t sid = db_->add_timelapse_session(sd, "2024-01-01T00:00:00", "", "");

    db_->add_timelapse_frame(sid, make_file(sd, "f001.jpg"));
    db_->add_timelapse_frame(sid, make_file(sd, "f002.jpg"));

    auto frames = db_->list_timelapse_frames(sid, 0, 10);
    EXPECT_EQ(frames.size(), 2u);
    for (auto& f : frames) {
        EXPECT_EQ(f.type,         "tl_frame");
        EXPECT_EQ(f.timelapse_id, sid);
    }
}

TEST_F(MediaDbTest, ListTimelapseFrames_DoesNotReturnOtherSession) {
    std::string sd1 = tl_ + "/sess_a";
    std::string sd2 = tl_ + "/sess_b";
    fs::create_directories(sd1);
    fs::create_directories(sd2);

    int64_t sid1 = db_->add_timelapse_session(sd1, "2024-01-01T00:00:00", "", "");
    int64_t sid2 = db_->add_timelapse_session(sd2, "2024-01-02T00:00:00", "", "");

    db_->add_timelapse_frame(sid1, make_file(sd1, "f1.jpg"));
    db_->add_timelapse_frame(sid2, make_file(sd2, "f2.jpg"));

    auto frames1 = db_->list_timelapse_frames(sid1, 0, 10);
    EXPECT_EQ(frames1.size(), 1u);
    EXPECT_EQ(frames1[0].timelapse_id, sid1);
}

TEST_F(MediaDbTest, FinishTimelapse_SetsStoppedAt) {
    std::string sd = tl_ + "/sess4";
    fs::create_directories(sd);
    int64_t sid = db_->add_timelapse_session(sd, "2024-01-01T10:00:00", "", "");
    db_->finish_timelapse(sid, "2024-01-01T11:00:00");

    auto sessions = db_->list_timelapses(0, 10);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0].stopped_at, "2024-01-01T11:00:00");
}

TEST_F(MediaDbTest, RenameTimelapseSession_UpdatesSessionDirAndFramePaths) {
    std::string old_dir = tl_ + "/tl_raw";
    std::string new_dir = tl_ + "/tl_2024-01-01";
    fs::create_directories(old_dir);
    fs::create_directories(new_dir);

    int64_t sid = db_->add_timelapse_session(old_dir, "2024-01-01T00:00:00", "", "");
    db_->add_timelapse_frame(sid, make_file(old_dir, "0001.jpg"));
    db_->add_timelapse_frame(sid, make_file(old_dir, "0002.jpg"));

    db_->rename_timelapse_session(sid, new_dir);

    auto sessions = db_->list_timelapses(0, 10);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0].session_dir,  new_dir);
    EXPECT_EQ(sessions[0].session_name, "tl_2024-01-01");

    // Frame paths must now use the new directory prefix.
    auto frames = db_->list_timelapse_frames(sid, 0, 10);
    EXPECT_EQ(frames.size(), 2u);
    for (auto& f : frames) {
        EXPECT_EQ(f.path.substr(0, new_dir.size()), new_dir)
            << "frame path " << f.path << " does not start with " << new_dir;
    }
}

TEST_F(MediaDbTest, RenameTimelapseSession_RenameToSameDir_NoChange) {
    std::string sd = tl_ + "/tl_same";
    fs::create_directories(sd);
    int64_t sid = db_->add_timelapse_session(sd, "2024-01-01T00:00:00", "", "");
    db_->add_timelapse_frame(sid, make_file(sd, "f.jpg"));

    db_->rename_timelapse_session(sid, sd); // rename to itself

    auto sessions = db_->list_timelapses(0, 10);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0].session_dir, sd);
    auto frames = db_->list_timelapse_frames(sid, 0, 10);
    EXPECT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].path.substr(0, sd.size()), sd);
}

// ---------------------------------------------------------------------------
// get_path_for_serving — security boundary
// ---------------------------------------------------------------------------

TEST_F(MediaDbTest, GetPathForServing_UnknownId_ReturnsNullopt) {
    EXPECT_FALSE(db_->get_path_for_serving(999).has_value());
    EXPECT_FALSE(db_->get_path_for_serving(0).has_value());
}

TEST_F(MediaDbTest, GetPathForServing_ValidStill_ReturnsPath) {
    std::string p = make_file(stills_, "serve.jpg");
    int64_t id = db_->add_still(p);
    ASSERT_GT(id, 0);

    auto result = db_->get_path_for_serving(id);
    ASSERT_TRUE(result.has_value());
    // Must end with the filename.
    EXPECT_NE(result->find("serve.jpg"), std::string::npos);
}

TEST_F(MediaDbTest, GetPathForServing_NonExistentFile_ReturnsNullopt) {
    // add_still stores the path even if the file is gone; serving must fail.
    std::string p = make_file(stills_, "gone.jpg");
    int64_t id = db_->add_still(p);
    ASSERT_GT(id, 0);
    fs::remove(p); // delete from disk

    EXPECT_FALSE(db_->get_path_for_serving(id).has_value()); // realpath() fails
}

TEST_F(MediaDbTest, GetPathForServing_PathOutsideAllowedDirs_ReturnsNullopt) {
    // Simulate DB tampering: manually insert a row outside the allowed dirs by
    // adding a symlink from within stills_ that points outside root_.
    // The canonical path after realpath() must escape to outside, triggering rejection.
    // We test via: create a file outside root_, symlink it into stills_, add to db,
    // then verify serving is rejected (realpath resolves symlink → outside dir).
    std::string outside = "/tmp/mdb_outside_test_" + std::to_string(getpid()) + ".jpg";
    {
        std::ofstream f(outside); f << "secret";
    }
    std::string symlink_path = stills_ + "/symlink.jpg";
    ::symlink(outside.c_str(), symlink_path.c_str());

    int64_t id = db_->add_still(symlink_path);
    ASSERT_GT(id, 0);

    // realpath() resolves the symlink → /tmp/mdb_outside_test_*.jpg
    // which is not under stills_, videos_, or tl_ → rejected.
    auto result = db_->get_path_for_serving(id);
    EXPECT_FALSE(result.has_value())
        << "symlink escaping the allowed dirs should be rejected";

    ::unlink(symlink_path.c_str());
    ::unlink(outside.c_str());
}

// ---------------------------------------------------------------------------
// rebuild_from_disk
// ---------------------------------------------------------------------------

TEST_F(MediaDbTest, RebuildFromDisk_FindsStillsAndVideos) {
    make_file(stills_, "scan1.jpg");
    make_file(stills_, "scan2.jpg");
    make_file(videos_, "vid.mkv");

    auto r = db_->rebuild_from_disk();
    EXPECT_EQ(r.added,   3);
    EXPECT_EQ(r.removed, 0);
    EXPECT_EQ(db_->list_stills(0, 10).size(), 2u);
    EXPECT_EQ(db_->list_videos(0, 10).size(), 1u);
}

TEST_F(MediaDbTest, RebuildFromDisk_Idempotent) {
    make_file(stills_, "idem.jpg");
    db_->rebuild_from_disk(); // first pass: adds 1
    auto r2 = db_->rebuild_from_disk(); // second pass: no new rows
    EXPECT_EQ(r2.added,   0);
    EXPECT_EQ(r2.removed, 0);
}

TEST_F(MediaDbTest, RebuildFromDisk_RemovesDeletedFiles) {
    std::string p = make_file(stills_, "gone.jpg");
    db_->add_still(p);        // register it
    fs::remove(p);            // then delete from disk

    auto r = db_->rebuild_from_disk();
    EXPECT_GE(r.removed, 1);
    EXPECT_TRUE(db_->list_stills(0, 10).empty());
}

TEST_F(MediaDbTest, RebuildFromDisk_ScansTimelapseDir) {
    std::string sd = tl_ + "/session1";
    fs::create_directories(sd);
    make_file(sd, "0001.jpg");
    make_file(sd, "0002.jpg");

    auto r = db_->rebuild_from_disk();
    // 1 session + 2 frames = at least 3 items added.
    EXPECT_GE(r.added, 3);

    auto sessions = db_->list_timelapses(0, 10);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0].frame_count, 2);
}

// ---------------------------------------------------------------------------
// verify
// ---------------------------------------------------------------------------

TEST_F(MediaDbTest, Verify_EmptyDb_ReturnsEmptyList) {
    EXPECT_TRUE(db_->verify().empty());
}

TEST_F(MediaDbTest, Verify_AllFilesPresent_ReturnsEmpty) {
    db_->add_still(make_file(stills_, "ok.jpg"));
    EXPECT_TRUE(db_->verify().empty());
}

TEST_F(MediaDbTest, Verify_FindsMissingFile) {
    std::string p = make_file(stills_, "missing.jpg");
    db_->add_still(p);
    fs::remove(p);

    auto missing = db_->verify();
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0], p);
}

TEST_F(MediaDbTest, Verify_MultipleItems_OnlyMissingReported) {
    std::string present = make_file(stills_, "present.jpg");
    std::string gone    = make_file(stills_, "gone.jpg");
    db_->add_still(present);
    db_->add_still(gone);
    fs::remove(gone);

    auto missing = db_->verify();
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0], gone);
}

// ---------------------------------------------------------------------------
// Thread safety
// ---------------------------------------------------------------------------

TEST_F(MediaDbTest, AddStill_ConcurrentCalls_NoCrashAndAllInserted) {
    constexpr int N = 8;
    std::vector<std::thread> threads;
    threads.reserve(N);
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&, i]{
            db_->add_still(make_file(stills_, "t" + std::to_string(i) + ".jpg"));
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(db_->list_stills(0, 20).size(), (size_t)N);
}
