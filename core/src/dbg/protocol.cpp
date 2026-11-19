#include "core/dbg/protocol.hpp"
#include "core/sys/frames.hpp"
#include "core/sys/log.hpp"
#include "core/proc/table.hpp"
#include "core/net/wifi.hpp"

#include <cstring>
#include <initializer_list>

#include "core/dbg/crc.hpp"

namespace dbg {

namespace {

void put16(uint8_t *out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t *out, uint32_t value)
{
    put16(out, static_cast<uint16_t>(value));
    put16(out + 2, static_cast<uint16_t>(value >> 16));
}

void put64(uint8_t *out, uint64_t value)
{
    put32(out, static_cast<uint32_t>(value));
    put32(out + 4, static_cast<uint32_t>(value >> 32));
}

uint16_t get16(const uint8_t *in)
{
    return static_cast<uint16_t>(in[0] | (in[1] << 8));
}

} // namespace

Protocol::Protocol(ports::ByteSink &out, ports::Lock &lock, ports::Display &display,
                   ports::Pointer &pointer, ports::InputSink &input, ports::System &system,
                   const char *firmware_name)
    : out_(out), lock_(lock), display_(display), pointer_(pointer), input_(input),
      system_(system), firmware_name_(firmware_name)
{
}

/* ---- Receiving ----------------------------------------------------------- */

void Protocol::feed(const uint8_t *data, size_t size)
{
    if (size > 0) {
        idle_seen_ = false;
    }
    size_t typed_from = 0; // a run of console bytes starts here
    bool typing = false;
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = data[i];
        if (console_ != nullptr && !in_frame_) {
            // Outside a frame: typed text, until a zero opens a frame.
            if (byte != 0) {
                if (!typing) {
                    typing = true;
                    typed_from = i;
                }
                continue;
            }
            if (typing) {
                console_bytes_ += static_cast<uint32_t>(i - typed_from);
                console_->write(data + typed_from, i - typed_from);
                typing = false;
            }
            in_frame_ = true;
            rx_size_ = 0;
            overflow_ = false;
            continue;
        }
        if (byte != 0) {
            if (rx_size_ < sizeof(rx_)) {
                rx_[rx_size_++] = byte;
            } else {
                overflow_ = true;
            }
            continue;
        }
        // A zero ends whatever came before it. Frames are sent with a zero on
        // both sides, so anything between two zeros that is not a valid
        // frame (line noise, stray text) is counted and dropped, and can
        // never knock the following frames out of step.
        if (rx_size_ > 0 || overflow_) {
            size_t raw = overflow_ ? 0 : cobs_decode(rx_, rx_size_, rx_);
            bool good = raw >= 4 && crc16(rx_, raw - 2) == get16(rx_ + raw - 2);
            if (good) {
                frames_received_++;
                // The frame is over: what follows is typed text again, until
                // the next zero. After a bad one this zero may be the opening
                // of the frame that follows, so it stays open: a frame's
                // bytes must never reach the console.
                in_frame_ = false;
                handle_frame(rx_, raw - 2);
            } else {
                receive_errors_++;
            }
        }
        rx_size_ = 0;
        overflow_ = false;
    }
    if (typing) {
        console_bytes_ += static_cast<uint32_t>(size - typed_from);
        console_->write(data + typed_from, size - typed_from);
    }
}

void Protocol::line_idle()
{
    if (console_ == nullptr || clock_ == nullptr || !in_frame_) {
        return;
    }
    const uint32_t now = clock_->micros();
    if (!idle_seen_) {
        idle_seen_ = true;
        idle_since_ = now;
        return;
    }
    if (static_cast<uint32_t>(now - idle_since_) < kFrameWaitUs) {
        return;
    }
    // Nothing has arrived for a good while: no frame was coming. What was
    // kept as its start was typed.
    in_frame_ = false;
    idle_seen_ = false;
    if (rx_size_ > 0 && !overflow_) {
        console_bytes_ += static_cast<uint32_t>(rx_size_);
        console_->write(rx_, rx_size_);
    }
    rx_size_ = 0;
    overflow_ = false;
}

void Protocol::send_text(const uint8_t *data, size_t size)
{
    if (size == 0) {
        return;
    }
    lock_.lock();
    out_.write(data, size);
    lock_.unlock();
}

void Protocol::handle_frame(const uint8_t *frame, size_t size)
{
    uint8_t type = frame[0];
    uint8_t seq = frame[1];
    const uint8_t *args = frame + 2;
    size_t args_size = size - 2;

    switch (type) {
    case kPing:
        reply(type, seq, kOk, args, args_size);
        break;

    case kInfo: {
        uint8_t body[64];
        reply(type, seq, kOk, body, write_info(body));
        break;
    }

    case kScreenshot:
        // An empty payload (the original request) means "with pointer".
        send_screenshot(seq, args_size == 0 || !(args[0] & kWithoutPointer),
                        args_size > 0 && (args[0] & kAcceptBackReference));
        break;

    case kKey:
        if (args_size != 3) {
            reply(type, seq, kBadArguments, nullptr, 0);
            break;
        }
        keys_++;
        reply(type, seq, kOk, nullptr, 0); // first: the event may restart the board
        input_.key(get16(args), args[2] != 0);
        break;

    case kMouse: {
        if (args_size != 7 && args_size != 8) { // the eighth, the wheel sideways, may be left out
            reply(type, seq, kBadArguments, nullptr, 0);
            break;
        }
        ports::MouseEvent event;
        event.absolute = args[0] != 0;
        event.x = static_cast<int16_t>(get16(args + 1));
        event.y = static_cast<int16_t>(get16(args + 3));
        event.buttons = args[5];
        event.wheel = static_cast<int8_t>(args[6]);
        event.wheel_x = args_size == 8 ? static_cast<int8_t>(args[7]) : 0;
        mouse_events_++;
        reply(type, seq, kOk, nullptr, 0); // first: the event may restart the board
        input_.mouse(event);
        break;
    }

    case kText: {
        char text[kMaxRequestPayload + 1];
        memcpy(text, args, args_size);
        text[args_size] = 0;
        reply(type, seq, kOk, nullptr, 0); // first: the event may restart the board
        input_.text(text);
        break;
    }

    case kTime: {
        if (desktop_ == nullptr || (args_size != 0 && args_size != 3)) {
            reply(type, seq, desktop_ == nullptr ? kUnknownCommand : kBadArguments, nullptr, 0);
            break;
        }
        if (args_size == 3) {
            if (args[0] > 23 || args[1] > 59 || args[2] > 59) {
                reply(type, seq, kBadArguments, nullptr, 0);
                break;
            }
            desktop_->set_time(args[0], args[1], args[2]);
        }
        int h, m, s;
        desktop_->get_time(h, m, s);
        const uint8_t body[3] = {static_cast<uint8_t>(h), static_cast<uint8_t>(m), static_cast<uint8_t>(s)};
        reply(type, seq, kOk, body, sizeof(body));
        break;
    }

    case kAnimations:
        if (desktop_ == nullptr || args_size != 1) {
            reply(type, seq, desktop_ == nullptr ? kUnknownCommand : kBadArguments, nullptr, 0);
            break;
        }
        desktop_->set_animations(args[0] != 0);
        reply(type, seq, kOk, nullptr, 0);
        break;

    case kMemoryReport: {
        static char text[kMaxReplyPayload];
        const size_t n = system_.memory_report(text, sizeof(text));
        reply(type, seq, n > 0 ? kOk : kUnknownCommand, reinterpret_cast<const uint8_t *>(text),
              static_cast<uint16_t>(n));
        break;
    }

    case kTasks: {
        // Clock (8), cores (1), tasks that follow (1), tasks left out (1),
        // then per task: id (2), state (1), priority (1), core (1, 0xFF
        // either), flags (1), run time (8), unused stack (4), name length
        // (1), name.
        static ports::TaskInfo tasks[kMaxTasks];
        ports::TaskTotals totals = {};
        const int count = system_.tasks(tasks, kMaxTasks, totals);
        if (count <= 0) {
            reply(type, seq, kUnknownCommand, nullptr, 0);
            break;
        }
        uint8_t *body = chunk_;
        size_t n = 11;
        int sent = 0;
        for (; sent < count; sent++) {
            const ports::TaskInfo &t = tasks[sent];
            const size_t name = strnlen(t.name, sizeof(t.name));
            if (n + 19 + name > kMaxReplyPayload - 1) {
                break;
            }
            put16(body + n, t.id);
            body[n + 2] = static_cast<uint8_t>(t.state);
            body[n + 3] = t.priority;
            body[n + 4] = static_cast<uint8_t>(t.core);
            body[n + 5] = t.stack_external ? 1 : 0;
            put64(body + n + 6, t.run_time_us);
            put32(body + n + 14, t.stack_free);
            body[n + 18] = static_cast<uint8_t>(name);
            memcpy(body + n + 19, t.name, name);
            n += 19 + name;
        }
        const int left_out = totals.left_out + (count - sent);
        put64(body, totals.clock_us);
        body[8] = totals.cores;
        body[9] = static_cast<uint8_t>(sent);
        body[10] = static_cast<uint8_t>(left_out > 255 ? 255 : left_out);
        reply(type, seq, kOk, body, static_cast<uint16_t>(n));
        break;
    }

    case kBootScreen:
        if (args_size == 1 && args[0] == 2) {
            system_.arm_fast_boot(); // 2: no screen, the next start is a fast one
            reply(type, seq, kOk, nullptr, 0);
            break;
        }
        if (boot_show_ == nullptr || args_size != 1 || (args[0] != 0 && args[0] != 3)) {
            reply(type, seq, boot_show_ == nullptr ? kUnknownCommand : kBadArguments, nullptr, 0);
            break;
        }
        boot_show_(args[0], boot_context_);
        reply(type, seq, kOk, nullptr, 0);
        break;

    case kLogRead: {
        // From (8, bytes since start; 0 the oldest kept). Reply: where the
        // next piece starts (8), then up to 1000 bytes of log text. Text
        // pushed out of the ring meanwhile is skipped.
        if (args_size != 8) {
            reply(type, seq, kBadArguments, nullptr, 0);
            break;
        }
        uint64_t from = 0;
        for (int i = 7; i >= 0; i--) {
            from = (from << 8) | args[i];
        }
        sys::Log &log = sys::log();
        if (from < log.oldest()) {
            from = log.oldest();
        }
        const size_t n = log.read(from, reinterpret_cast<char *>(chunk_ + 8), 1000);
        put64(chunk_, from);
        reply(type, seq, kOk, chunk_, static_cast<uint16_t>(8 + n));
        break;
    }

    case kFault: {
        // kind, then optionally the PID to charge it to (16 bits): the fault
        // happens as if that program had made it, so the crash record and
        // the error screen name it.
        if (args_size < 1 || args[0] > static_cast<uint8_t>(ports::Fault::Slow) || (args_size != 1 && args_size != 3)) {
            reply(type, seq, kBadArguments, nullptr, 0);
            break;
        }
        const proc::Pid pid = args_size == 3 ? static_cast<proc::Pid>(get16(args + 1)) : proc::kNone;
        if (pid != proc::kNone && !proc::table().alive(pid)) {
            reply(type, seq, kBadArguments, nullptr, 0);
            break;
        }
        reply(type, seq, kOk, nullptr, 0);
        if (pid != proc::kNone) {
            // As the program, where its code runs: on its own task if it
            // has one, so that a hang there is the program not responding.
            ports::System *system = &system_;
            const ports::Fault kind = static_cast<ports::Fault>(args[0]);
            proc::call_as(&proc::table(), pid, [system, kind] { system->fault(kind); });
            break;
        }
        system_.fault(static_cast<ports::Fault>(args[0]));
        break;
    }

    case kWifi:
        wifi_command(seq, args, args_size);
        break;

    case kFileList:
    case kFileRead:
    case kFileWrite:
    case kFileDelete:
    case kFileMakeFolder:
        file_command(type, seq, args, args_size);
        break;

    case kStats: {
        ports::SystemStats stats = {};
        system_.stats(stats);
        uint8_t body[36 + 4 * 8];
        put32(body + 0, stats.uptime_ms);
        put32(body + 4, stats.free_internal_ram);
        put32(body + 8, stats.free_external_ram);
        put32(body + 12, stats.frames_shown);
        put32(body + 16, frames_received_);
        put32(body + 20, receive_errors_);
        put32(body + 24, keys_);
        put32(body + 28, mouse_events_);
        put32(body + 32, stats.scanout_us);
        const int extra = counter_count_ < 8 ? counter_count_ : 8;
        for (int i = 0; i < extra; i++) {
            put32(body + 36 + i * 4, counters_[i]);
        }
        reply(type, seq, kOk, body, static_cast<uint16_t>(36 + extra * 4));
        break;
    }

    case kReboot:
        if (args_size >= 1 && (args[0] & 1)) {
            system_.arm_fast_boot(); // bit 0: the next start is a fast one
        }
        reply(type, seq, kOk, nullptr, 0);
        system_.reboot();
        break;

    case kWindows: {
        if (desktop_ == nullptr) {
            reply(type, seq, kUnknownCommand, nullptr, 0);
            break;
        }
        // chunk_ is free between screenshots; the reply has one byte of status.
        char *json = reinterpret_cast<char *>(chunk_);
        size_t size = desktop_->describe_windows(json, kMaxReplyPayload - 1);
        reply(type, seq, kOk, chunk_, size);
        break;
    }

    case kFrames: {
        // What the desktop counted against the video frame (sys::Frames).
        // Reply: budget, pieces, painted, over, late, worst, worst late
        // (4 each), busy (8), the 8 buckets (4 each), how many slow pieces
        // follow (1), then for each, oldest first: at_ms, total, painting,
        // pixels, timers, top, late (4 each), top's PID (2), the work (1),
        // what the top was (1). Bit 0 of the optional argument starts the
        // count again after the reply is made.
        sys::Frames *frames = desktop_ != nullptr ? desktop_->frames() : nullptr;
        if (frames == nullptr) {
            reply(type, seq, kUnknownCommand, nullptr, 0);
            break;
        }
        const sys::Frames::Totals &t = frames->totals();
        uint8_t *body = chunk_;
        size_t at = 0;
        for (const uint32_t v : {t.budget_us, t.pieces, t.painted, t.over, t.late, t.worst_us, t.worst_late_us}) {
            put32(body + at, v);
            at += 4;
        }
        put64(body + at, t.busy_us);
        at += 8;
        for (const uint32_t v : t.buckets) {
            put32(body + at, v);
            at += 4;
        }
        body[at++] = static_cast<uint8_t>(frames->slow_count());
        for (int i = 0; i < frames->slow_count(); i++) {
            const sys::Frames::Slow &s = frames->slow(i);
            for (const uint32_t v : {s.at_ms, s.total_us, s.paint_us, s.pixels, s.timers_us, s.top_us, s.late_us}) {
                put32(body + at, v);
                at += 4;
            }
            body[at++] = static_cast<uint8_t>(s.top_pid);
            body[at++] = static_cast<uint8_t>(s.top_pid >> 8);
            body[at++] = s.work;
            body[at++] = s.top;
        }
        reply(type, seq, kOk, body, at);
        if (args_size >= 1 && (args[0] & 1)) {
            frames->reset();
        }
        break;
    }

    case kPaintBench: {
        // Text, as kMemoryReport. It draws over the screen for about a
        // second and paints everything again before it answers.
        char *text = reinterpret_cast<char *>(chunk_);
        const size_t n = desktop_ != nullptr ? desktop_->paint_bench(text, kMaxReplyPayload - 1) : 0;
        reply(type, seq, n > 0 ? kOk : kUnknownCommand, chunk_, static_cast<uint16_t>(n));
        break;
    }

    case kVideoMode: {
        // The modes, and with an argument a change to one of them: the
        // desktop makes it at its next tick, after this reply has gone.
        if (desktop_ == nullptr || desktop_->video_mode_count() == 0) {
            reply(type, seq, kUnknownCommand, nullptr, 0);
            break;
        }
        if (args_size >= 1 && !desktop_->set_video_mode(args[0])) {
            reply(type, seq, kBadArguments, nullptr, 0);
            break;
        }
        uint8_t body[2 + 8 * 12];
        const int count = desktop_->video_mode_count() < 8 ? desktop_->video_mode_count() : 8;
        body[0] = static_cast<uint8_t>(count);
        body[1] = static_cast<uint8_t>(desktop_->video_mode());
        uint16_t at = 2;
        for (int i = 0; i < count; i++) {
            const ports::VideoMode m = desktop_->video_mode_at(i);
            put16(body + at, static_cast<uint16_t>(m.width));
            put16(body + at + 2, static_cast<uint16_t>(m.height));
            put32(body + at + 4, m.pixel_clock_hz);
            put32(body + at + 8, m.frame_us);
            at += 12;
        }
        reply(type, seq, kOk, body, at);
        break;
    }

    case kRepaint: {
        if (desktop_ == nullptr) {
            reply(type, seq, kUnknownCommand, nullptr, 0);
            break;
        }
        uint8_t body[4];
        put32(body, desktop_->repaint_all());
        reply(type, seq, kOk, body, sizeof(body));
        break;
    }

    default:
        reply(type, seq, kUnknownCommand, nullptr, 0);
        break;
    }
}

/* ---- Files ---------------------------------------------------------------- */

void Protocol::file_error(uint8_t type, uint8_t seq, ports::FsError e)
{
    uint8_t body[48];
    body[0] = static_cast<uint8_t>(e);
    const char *text = fs::error_text(e);
    size_t n = strlen(text);
    n = n < sizeof(body) - 1 ? n : sizeof(body) - 1;
    memcpy(body + 1, text, n);
    reply(type, seq, kFileError, body, 1 + n);
}

// Request layouts (little-endian):
//   List:   start index (2), path
//   Read:   offset (4), path                -> size (4), data
//   Write:  offset (4), flags (1), path length (1), path, data
//           flag 1: create or empty the file first
//   Delete: path      Make folder: path
void Protocol::file_command(uint8_t type, uint8_t seq, const uint8_t *args, size_t size)
{
    if (storage_ == nullptr) {
        reply(type, seq, kUnknownCommand, nullptr, 0);
        return;
    }
    char path[fs::kMaxPath];
    auto take_path = [&](const uint8_t *p, size_t n) {
        n = n < sizeof(path) - 1 ? n : sizeof(path) - 1;
        memcpy(path, p, n);
        path[n] = 0;
    };
    uint8_t *body = chunk_; // free between screenshots
    const size_t room = kMaxReplyPayload - 1;

    if (type == kFileList) {
        if (size < 2) {
            reply(type, seq, kBadArguments, nullptr, 0);
            return;
        }
        take_path(args + 2, size - 2);
        int index = get16(args);
        // count (1), more (1), then per entry: folder (1), size (4), name length (1), name
        size_t used = 2;
        uint8_t count = 0;
        bool more = false;
        for (;; index++) {
            ports::DirEntry e;
            ports::FsError err = storage_->entry(path, index, e);
            if (err == ports::FsError::NotFound && (index > 0 || count > 0)) {
                break;
            }
            if (err != ports::FsError::Ok) {
                if (err == ports::FsError::NotFound) {
                    // An empty folder, or no folder at all.
                    ports::DirEntry self;
                    err = path[0] != 0 && fs::path::relative(path)[0] == 0
                              ? ports::FsError::Ok
                              : storage_->stat(path, self);
                    if (err == ports::FsError::Ok) {
                        break;
                    }
                }
                file_error(type, seq, err);
                return;
            }
            const size_t name = strlen(e.name);
            if (used + 6 + name > room) {
                more = true;
                break;
            }
            body[used++] = e.folder ? 1 : 0;
            put32(body + used, e.size);
            used += 4;
            body[used++] = static_cast<uint8_t>(name);
            memcpy(body + used, e.name, name);
            used += name;
            count++;
        }
        body[0] = count;
        body[1] = more ? 1 : 0;
        reply(type, seq, kOk, body, used);
        return;
    }

    if (type == kFileRead) {
        if (size < 4) {
            reply(type, seq, kBadArguments, nullptr, 0);
            return;
        }
        take_path(args + 4, size - 4);
        const uint32_t offset = static_cast<uint32_t>(get16(args)) | (static_cast<uint32_t>(get16(args + 2)) << 16);
        ports::DirEntry e;
        ports::FsError err = storage_->stat(path, e);
        if (err == ports::FsError::Ok && e.folder) {
            err = ports::FsError::IsFolder;
        }
        fs::File f;
        if (err == ports::FsError::Ok) {
            err = storage_->open(path, ports::OpenMode::Read, f);
        }
        size_t got = 0;
        if (err == ports::FsError::Ok && offset < e.size) {
            err = storage_->seek(f, offset);
            if (err == ports::FsError::Ok) {
                err = storage_->read(f, body + 4, room - 4, got);
            }
        }
        if (f.open()) {
            storage_->close(f);
        }
        if (err != ports::FsError::Ok) {
            file_error(type, seq, err);
            return;
        }
        put32(body, e.size);
        reply(type, seq, kOk, body, 4 + got);
        return;
    }

    if (type == kFileWrite) {
        if (size < 6 || size < 6u + args[5]) {
            reply(type, seq, kBadArguments, nullptr, 0);
            return;
        }
        const uint32_t offset = static_cast<uint32_t>(get16(args)) | (static_cast<uint32_t>(get16(args + 2)) << 16);
        const bool create = args[4] & 1;
        take_path(args + 6, args[5]);
        const uint8_t *data = args + 6 + args[5];
        const size_t n = size - 6 - args[5];
        fs::File f;
        ports::FsError err = storage_->open(path, create ? ports::OpenMode::Write : ports::OpenMode::Update, f);
        if (err == ports::FsError::Ok && !create) {
            err = storage_->seek(f, offset);
        }
        if (err == ports::FsError::Ok && n > 0) {
            err = storage_->write(f, data, n);
        }
        if (f.open()) {
            ports::FsError c = storage_->close(f);
            err = err != ports::FsError::Ok ? err : c;
        }
        if (err != ports::FsError::Ok) {
            file_error(type, seq, err);
            return;
        }
        reply(type, seq, kOk, nullptr, 0);
        return;
    }

    take_path(args, size);
    const ports::FsError err = type == kFileDelete ? storage_->remove(path) : storage_->make_folder(path);
    if (err != ports::FsError::Ok) {
        file_error(type, seq, err);
        return;
    }
    reply(type, seq, kOk, nullptr, 0);
}

/* ---- Sending ------------------------------------------------------------- */

// Caller must hold the lock.
void Protocol::send(uint8_t type, uint8_t seq, const uint8_t *payload, size_t size)
{
    if (size > kMaxReplyPayload) {
        size = kMaxReplyPayload;
    }
    tx_raw_[0] = type;
    tx_raw_[1] = seq;
    if (size > 0) {
        memcpy(tx_raw_ + 2, payload, size);
    }
    put16(tx_raw_ + 2 + size, crc16(tx_raw_, 2 + size));
    tx_wire_[0] = 0;
    size_t encoded = cobs_encode(tx_raw_, size + 4, tx_wire_ + 1);
    tx_wire_[1 + encoded] = 0;
    out_.write(tx_wire_, encoded + 2);
}

void Protocol::reply(uint8_t type, uint8_t seq, Status status, const uint8_t *body, size_t size)
{
    uint8_t payload[kMaxReplyPayload];
    if (size > kMaxReplyPayload - 1) {
        size = kMaxReplyPayload - 1;
    }
    payload[0] = status;
    if (size > 0) {
        memcpy(payload + 1, body, size);
    }
    lock_.lock();
    send(static_cast<uint8_t>(type | kReplyBit), seq, payload, size + 1);
    lock_.unlock();
}

size_t Protocol::write_info(uint8_t *out)
{
    out[0] = kProtocolVersion;
    put16(out + 1, static_cast<uint16_t>(display_.width()));
    put16(out + 3, static_cast<uint16_t>(display_.height()));
    out[5] = kRgb332;
    size_t name = strlen(firmware_name_);
    if (name > 32) {
        name = 32;
    }
    memcpy(out + 6, firmware_name_, name);
    return 6 + name;
}

void Protocol::send_hello()
{
    uint8_t body[64];
    size_t size = write_info(body);
    lock_.lock();
    send(kHello, 0, body, size);
    lock_.unlock();
}

void Protocol::send_log(const char *text, size_t size)
{
    sys::log().write(text, size); // kept for `dmesg`, whether or not the link is listening
    if (size == 0 || !lock_.try_lock()) {
        return;
    }
    send(kLog, 0, reinterpret_cast<const uint8_t *>(text), size);
    lock_.unlock();
}

/* ---- Screenshot ---------------------------------------------------------- */

// The picture is sent run-length encoded, in chunks:
//   0..127    copy the next (n + 1) bytes
//   128..255  repeat the next byte (n - 125) times, so 3 to 130
void Protocol::send_screenshot(uint8_t seq, bool with_pointer, bool back_references)
{
    const uint8_t *src = display_.pixels();
    const int width = display_.width();
    const size_t total = static_cast<size_t>(width) * display_.height();

    // The pointer is mixed in on the way out, exactly as the monitor gets it.
    // Only pixels in the pointer's rows need the extra step.
    video::PointerState pointer = {};
    size_t pointer_lo = 0, pointer_hi = 0;
    if (with_pointer) {
        pointer = pointer_.state();
        if (pointer.visible && pointer.shape != nullptr) {
            int top = pointer.y - pointer.shape->hot_y;
            int bottom = top + pointer.shape->height;
            top = top < 0 ? 0 : top;
            bottom = bottom > display_.height() ? display_.height() : bottom;
            if (top < bottom) {
                pointer_lo = static_cast<size_t>(top) * width;
                pointer_hi = static_cast<size_t>(bottom) * width;
            }
        }
    }
    auto pixel = [&](size_t i) -> uint8_t {
        uint8_t value = src[i];
        if (i >= pointer_lo && i < pointer_hi) {
            video::compose_pointer(&value, static_cast<int>(i), 1, width, pointer);
        }
        return value;
    };

    // The CRC first, one row at a time, while chunk_ is free.
    uint32_t check = 0;
    if (static_cast<size_t>(width) <= sizeof(chunk_)) {
        for (size_t row = 0; row < total; row += width) {
            memcpy(chunk_, src + row, width);
            if (row >= pointer_lo && row < pointer_hi) {
                video::compose_pointer(chunk_, static_cast<int>(row), width, width, pointer);
            }
            check = crc32(chunk_, width, check);
        }
    } else {
        for (size_t i = 0; i < total; i++) {
            uint8_t value = pixel(i);
            check = crc32(&value, 1, check);
        }
    }

    uint8_t head[10];
    put16(head + 0, static_cast<uint16_t>(width));
    put16(head + 2, static_cast<uint16_t>(display_.height()));
    head[4] = kRgb332;
    head[5] = back_references ? kBackReference : kRunLength;
    put32(head + 6, static_cast<uint32_t>(total));
    reply(kScreenshot, seq, kOk, head, sizeof(head));

    uint16_t chunks = 0;
    size_t used = 2; // every chunk starts with its number

    auto flush = [&]() {
        put16(chunk_, chunks++);
        lock_.lock();
        send(kShotData, seq, chunk_, used);
        lock_.unlock();
        used = 2;
    };
    // Keeps a token (control byte plus its data) inside one chunk.
    auto reserve = [&](size_t bytes) {
        if (used + bytes > sizeof(chunk_)) {
            flush();
        }
    };

    if (back_references) {
        // Encoding 2 (docs/debug-protocol.md):
        //   0..127    copy the next (n + 1) bytes
        //   128..255  then a distance code d: repeat (n - 125) bytes, 3 to
        //             130, from kDistance[d] bytes back (they may overlap)
        // The few distances cover flat colour (1), the 4x4 dither (2, 4)
        // and repeated rows (1, 2 or 4 rows up).
        const size_t distance[6] = {1, 2, 4, static_cast<size_t>(width),
                                    2 * static_cast<size_t>(width), 4 * static_cast<size_t>(width)};
        size_t literal = 0; // first byte not yet sent
        auto send_literals = [&](size_t end) {
            while (literal < end) {
                size_t n = end - literal < 128 ? end - literal : 128;
                reserve(1 + n);
                chunk_[used++] = static_cast<uint8_t>(n - 1);
                for (size_t k = literal; k < literal + n; k++) {
                    chunk_[used++] = pixel(k);
                }
                literal += n;
            }
        };
        size_t i = 0;
        while (i < total) {
            size_t best = 0;
            uint8_t code = 0;
            for (uint8_t d = 0; d < 6 && best < 130; d++) {
                if (distance[d] > i) {
                    break;
                }
                size_t n = 0;
                while (n < 130 && i + n < total && pixel(i + n) == pixel(i + n - distance[d])) {
                    n++;
                }
                if (n > best) {
                    best = n;
                    code = d;
                }
            }
            if (best < 3) {
                i++;
                continue;
            }
            send_literals(i);
            reserve(2);
            chunk_[used++] = static_cast<uint8_t>(125 + best);
            chunk_[used++] = code;
            i += best;
            literal = i;
        }
        send_literals(total);
    }

    // Encoding 1, run lengths:
    //   0..127    copy the next (n + 1) bytes
    //   128..255  repeat the next byte (n - 125) times, so 3 to 130
    size_t i = back_references ? total : 0;
    while (i < total) {
        const uint8_t first = pixel(i);
        size_t run = 1;
        while (i + run < total && run < 130 && pixel(i + run) == first) {
            run++;
        }
        if (run >= 3) {
            reserve(2);
            chunk_[used++] = static_cast<uint8_t>(125 + run);
            chunk_[used++] = first;
            i += run;
            continue;
        }
        // Literal bytes, up to the next run of three or more.
        size_t start = i;
        while (i < total && i - start < 128) {
            if (i + 2 < total && pixel(i) == pixel(i + 1) && pixel(i) == pixel(i + 2)) {
                break;
            }
            i++;
        }
        size_t count = i - start;
        reserve(1 + count);
        chunk_[used++] = static_cast<uint8_t>(count - 1);
        for (size_t k = start; k < i; k++) {
            chunk_[used++] = pixel(k);
        }
    }
    if (used > 2) {
        flush();
    }

    uint8_t tail[6];
    put16(tail + 0, chunks);
    put32(tail + 2, check);
    lock_.lock();
    send(kShotEnd, seq, tail, sizeof(tail));
    lock_.unlock();
}

/* ---- Wi-Fi ---------------------------------------------------------------------- */

// The first byte says what: 0 status, 1 scan, 2 list, 3 connect, 4
// disconnect, 5 forget. Names travel as a length byte and the bytes.
void Protocol::wifi_command(uint8_t seq, const uint8_t *args, size_t size)
{
    if (wifi_ == nullptr || size == 0) {
        reply(kWifi, seq, wifi_ == nullptr ? kUnknownCommand : kBadArguments, nullptr, 0);
        return;
    }
    net::Manager &m = *wifi_;
    uint8_t *body = chunk_;
    size_t n = 0;
    // A name at args[at]: copies it out and moves `at` past it.
    char ssid[net::kSsidMax + 1];
    size_t at = 1;
    const auto name = [&](char *out, size_t most) {
        if (at >= size || args[at] > most || at + 1 + args[at] > size) {
            return false;
        }
        memcpy(out, args + at + 1, args[at]);
        out[args[at]] = 0;
        at += 1 + args[at];
        return true;
    };
    switch (args[0]) {
    case 0: { // status
        const net::LinkStatus &l = m.link();
        body[n++] = static_cast<uint8_t>(l.link);
        body[n++] = static_cast<uint8_t>(m.last_failure());
        body[n++] = m.scanning() ? 1 : 0;
        body[n++] = m.time_known() ? 1 : 0;
        body[n++] = static_cast<uint8_t>(l.rssi);
        body[n++] = l.channel;
        memcpy(body + n, l.address, 4);
        memcpy(body + n + 4, l.gateway, 4);
        memcpy(body + n + 8, l.mac, 6);
        n += 14;
        body[n++] = static_cast<uint8_t>(m.saved_count());
        const size_t length = strlen(l.ssid);
        body[n++] = static_cast<uint8_t>(length);
        memcpy(body + n, l.ssid, length);
        n += length;
        break;
    }
    case 1:
        m.scan();
        break;
    case 2: // list: the networks seen, then the remembered names
        body[n++] = m.scanning() ? 1 : 0;
        body[n++] = static_cast<uint8_t>(m.seen_count());
        for (int i = 0; i < m.seen_count(); i++) {
            const net::Manager::Seen &s = m.seen(i);
            const size_t length = strlen(s.network.ssid);
            body[n++] = static_cast<uint8_t>(s.network.rssi);
            body[n++] = s.network.channel;
            body[n++] = static_cast<uint8_t>(s.network.security);
            body[n++] = static_cast<uint8_t>((s.saved ? 1 : 0) | (s.connected ? 2 : 0));
            body[n++] = static_cast<uint8_t>(length);
            memcpy(body + n, s.network.ssid, length);
            n += length;
        }
        body[n++] = static_cast<uint8_t>(m.saved_count());
        for (int i = 0; i < m.saved_count(); i++) {
            const size_t length = strlen(m.saved_ssid(i));
            if (n + 1 + length > kMaxReplyPayload - 1) {
                break;
            }
            body[n++] = static_cast<uint8_t>(length);
            memcpy(body + n, m.saved_ssid(i), length);
            n += length;
        }
        break;
    case 3: { // connect: name, remember, has password, password
        char password[net::kPasswordMax + 1];
        if (!name(ssid, net::kSsidMax) || ssid[0] == 0 || at + 2 > size) {
            reply(kWifi, seq, kBadArguments, nullptr, 0);
            return;
        }
        const bool remember = args[at] != 0, given = args[at + 1] != 0;
        at += 2;
        if (given && !name(password, net::kPasswordMax)) {
            reply(kWifi, seq, kBadArguments, nullptr, 0);
            return;
        }
        m.connect(ssid, given ? password : nullptr, remember);
        memset(password, 0, sizeof(password));
        break;
    }
    case 4:
        m.disconnect();
        break;
    case 5:
        if (!name(ssid, net::kSsidMax)) {
            reply(kWifi, seq, kBadArguments, nullptr, 0);
            return;
        }
        m.forget(ssid);
        break;
    default:
        reply(kWifi, seq, kBadArguments, nullptr, 0);
        return;
    }
    reply(kWifi, seq, kOk, body, n);
}

} // namespace dbg
