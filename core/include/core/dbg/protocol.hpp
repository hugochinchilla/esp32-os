// The serial debug protocol, board side. Described in docs/debug-protocol.md.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/dbg/cobs.hpp"
#include "core/fs/fs.hpp"
#include "core/ports.hpp"

namespace dbg {

constexpr uint8_t kProtocolVersion = 1;

// Largest payload in either direction, before framing.
constexpr size_t kMaxRequestPayload = 1024;
constexpr size_t kMaxReplyPayload = 1024;

enum FrameType : uint8_t {
    // Requests from the PC. The reply has the same type with bit 7 set.
    kPing = 0x01,
    kInfo = 0x02,
    kScreenshot = 0x03,
    kKey = 0x04,
    kMouse = 0x05,
    kStats = 0x06,
    kReboot = 0x07,
    kWindows = 0x08, // list of windows as JSON
    kRepaint = 0x09, // paint the whole screen again, reply with the time taken
    kText = 0x0A,    // typed text, UTF-8
    kTime = 0x0B,    // read or set the clock
    kAnimations = 0x0C, // blinking on or off
    kFileList = 0x0D,   // entries of a folder
    kFileRead = 0x0E,   // part of a file
    kFileWrite = 0x0F,  // part of a file
    kFileDelete = 0x10, // a file or an empty folder
    kFileMakeFolder = 0x11,
    // Sent by the board without being asked.
    kLog = 0x70,
    kHello = 0x71,
    kShotData = 0x72,
    kShotEnd = 0x73,
    kReplyBit = 0x80,
};

enum Status : uint8_t {
    kOk = 0,
    kUnknownCommand = 1,
    kBadArguments = 2,
    kFileError = 3, // followed by the error code (ports::FsError) and its text
};

enum PixelFormat : uint8_t { kRgb332 = 1 };
enum Encoding : uint8_t { kRunLength = 1, kBackReference = 2 };

// Screenshot request flags (optional payload byte).
enum ScreenshotFlags : uint8_t {
    kWithoutPointer = 1,
    kAcceptBackReference = 2, // the computer can decode encoding 2
};

class Protocol {
public:
    Protocol(ports::ByteSink &out, ports::Lock &lock, ports::Display &display,
             ports::Pointer &pointer, ports::InputSink &input, ports::System &system,
             const char *firmware_name);

    // Bytes received from the PC, in any size of pieces.
    void feed(const uint8_t *data, size_t size);

    // Announces the board after start-up.
    void send_hello();

    // Sends log text. Safe to call from other tasks; the text is dropped if
    // the link is busy for too long.
    void send_log(const char *text, size_t size);

    // Optional: the desktop to answer kWindows and kRepaint. Without it they
    // are unknown commands.
    void set_desktop(ports::Desktop *desktop) { desktop_ = desktop; }
    // Optional: the drives for the file commands.
    void set_storage(fs::Storage *storage) { storage_ = storage; }

    uint32_t frames_received() const { return frames_received_; }
    uint32_t receive_errors() const { return receive_errors_; }

private:
    void handle_frame(const uint8_t *frame, size_t size);
    void reply(uint8_t type, uint8_t seq, Status status, const uint8_t *body, size_t size);
    void send(uint8_t type, uint8_t seq, const uint8_t *payload, size_t size);
    size_t write_info(uint8_t *out);
    void send_screenshot(uint8_t seq, bool with_pointer, bool back_references);
    void file_command(uint8_t type, uint8_t seq, const uint8_t *args, size_t size);
    void file_error(uint8_t type, uint8_t seq, ports::FsError e);

    ports::ByteSink &out_;
    ports::Lock &lock_;
    ports::Display &display_;
    ports::Pointer &pointer_;
    ports::InputSink &input_;
    ports::System &system_;
    const char *firmware_name_;
    ports::Desktop *desktop_ = nullptr;
    fs::Storage *storage_ = nullptr;

    bool overflow_ = false;
    size_t rx_size_ = 0;
    uint32_t frames_received_ = 0;
    uint32_t receive_errors_ = 0;
    uint32_t keys_ = 0;
    uint32_t mouse_events_ = 0;

    // type, seq, payload, crc
    static constexpr size_t kRxRaw = kMaxRequestPayload + 4;
    static constexpr size_t kTxRaw = kMaxReplyPayload + 4;
    uint8_t rx_[cobs_max_encoded(kRxRaw)];
    uint8_t tx_raw_[kTxRaw];
    uint8_t tx_wire_[cobs_max_encoded(kTxRaw) + 2];
    uint8_t chunk_[kMaxReplyPayload];
};

} // namespace dbg
