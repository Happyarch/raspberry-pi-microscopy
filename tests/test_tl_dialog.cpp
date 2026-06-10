#include <gtest/gtest.h>
#include <string>
#include <cstdint>
#include <algorithm>

// ---------------------------------------------------------------------------
// Simulate the dialog callback logic from main.cpp in isolation.
// No SDL, no libcamera required.
// ---------------------------------------------------------------------------

struct TlDialogState {
    bool        tl_dialog_open{false};
    int         tl_dialog_field{0};
    std::string tl_dialog_interval{"5"};
    std::string tl_dialog_frames{"0"};
};

struct TlDialogCallbacks {
    bool        recording{false};
    bool        tl_active{false};

    bool        tl_started{false};
    uint64_t    started_interval_ms{0};
    int         started_max_frames{0};

    TlDialogState state;

    void on_tap() {
        if (!recording && !tl_active) state.tl_dialog_open = true;
    }
    void on_char(char c) {
        auto& s = (state.tl_dialog_field == 0) ? state.tl_dialog_interval
                                                : state.tl_dialog_frames;
        if (s.size() < 6) s += c;
    }
    void on_backspace() {
        auto& s = (state.tl_dialog_field == 0) ? state.tl_dialog_interval
                                                : state.tl_dialog_frames;
        if (!s.empty()) s.pop_back();
    }
    void on_tab() { state.tl_dialog_field ^= 1; }
    void on_confirm() {
        double iv_s = 0.5;
        try {
            if (!state.tl_dialog_interval.empty())
                iv_s = std::max(0.5, std::stod(state.tl_dialog_interval));
        } catch (...) {}
        int max_f = 0;
        try {
            if (!state.tl_dialog_frames.empty())
                max_f = std::stoi(state.tl_dialog_frames);
        } catch (...) {}
        started_interval_ms = static_cast<uint64_t>(iv_s * 1000);
        started_max_frames  = max_f;
        tl_started          = true;
        state.tl_dialog_open  = false;
        state.tl_dialog_field = 0;
    }
    void on_cancel() {
        state.tl_dialog_open  = false;
        state.tl_dialog_field = 0;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(TlDialog, TapOpensDiaglogWhenIdle) {
    TlDialogCallbacks cb;
    cb.on_tap();
    EXPECT_TRUE(cb.state.tl_dialog_open);
}

TEST(TlDialog, TapBlockedWhenRecording) {
    TlDialogCallbacks cb;
    cb.recording = true;
    cb.on_tap();
    EXPECT_FALSE(cb.state.tl_dialog_open);
}

TEST(TlDialog, TapBlockedWhenTlActive) {
    TlDialogCallbacks cb;
    cb.tl_active = true;
    cb.on_tap();
    EXPECT_FALSE(cb.state.tl_dialog_open);
}

TEST(TlDialog, ConfirmStartsTl) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_interval = "10";
    cb.state.tl_dialog_frames   = "20";
    cb.on_confirm();
    EXPECT_TRUE(cb.tl_started);
    EXPECT_EQ(cb.started_interval_ms, 10000u);
    EXPECT_EQ(cb.started_max_frames, 20);
    EXPECT_FALSE(cb.state.tl_dialog_open);
}

TEST(TlDialog, ConfirmClosesDialog) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_open = true;
    cb.on_confirm();
    EXPECT_FALSE(cb.state.tl_dialog_open);
}

TEST(TlDialog, CancelClosesWithoutStarting) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_open = true;
    cb.on_cancel();
    EXPECT_FALSE(cb.state.tl_dialog_open);
    EXPECT_FALSE(cb.tl_started);
}

TEST(TlDialog, BackspaceRemovesLastChar) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_interval = "10";
    cb.on_backspace();
    EXPECT_EQ(cb.state.tl_dialog_interval, "1");
}

TEST(TlDialog, BackspaceOnEmptyIsNoop) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_interval = "";
    cb.on_backspace();
    EXPECT_EQ(cb.state.tl_dialog_interval, "");
}

TEST(TlDialog, TabSwitchesField) {
    TlDialogCallbacks cb;
    EXPECT_EQ(cb.state.tl_dialog_field, 0);
    cb.on_tab();
    EXPECT_EQ(cb.state.tl_dialog_field, 1);
    cb.on_tab();
    EXPECT_EQ(cb.state.tl_dialog_field, 0);
}

TEST(TlDialog, CharAppendsToActiveField) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_interval = "";
    cb.on_char('1'); cb.on_char('5');
    EXPECT_EQ(cb.state.tl_dialog_interval, "15");
}

TEST(TlDialog, CharAppendToFramesField) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_field = 1;
    cb.state.tl_dialog_frames = "";
    cb.on_char('5'); cb.on_char('0');
    EXPECT_EQ(cb.state.tl_dialog_frames, "50");
}

TEST(TlDialog, CharCappedAt6) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_interval = "99999";  // 5 chars
    cb.on_char('9');
    EXPECT_EQ(cb.state.tl_dialog_interval, "999999");  // 6 chars — allowed
    cb.on_char('9');
    EXPECT_EQ(cb.state.tl_dialog_interval, "999999");  // 7th rejected
}

TEST(TlDialog, EmptyIntervalDefaultsTo500ms) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_interval = "";
    cb.on_confirm();
    EXPECT_EQ(cb.started_interval_ms, 500u);  // 0.5 s minimum
}

TEST(TlDialog, SubHalfSecondClampedTo500ms) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_interval = "0.1";
    cb.on_confirm();
    EXPECT_EQ(cb.started_interval_ms, 500u);
}

TEST(TlDialog, ZeroFramesMeansUnlimited) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_interval = "5";
    cb.state.tl_dialog_frames   = "0";
    cb.on_confirm();
    EXPECT_EQ(cb.started_max_frames, 0);
}

TEST(TlDialog, ConfirmResetsField) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_field = 1;
    cb.on_confirm();
    EXPECT_EQ(cb.state.tl_dialog_field, 0);
}

TEST(TlDialog, CancelResetsField) {
    TlDialogCallbacks cb;
    cb.state.tl_dialog_field = 1;
    cb.on_cancel();
    EXPECT_EQ(cb.state.tl_dialog_field, 0);
}
