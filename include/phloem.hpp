// phloem.hpp — send data to Phloem from C++ (C++17, header only).
//
// A thin layer over the C ABI in phloem.h: a client that closes itself,
// failures as exceptions carrying the SDK's own words, and std containers in
// place of pointer-and-length pairs. It encodes nothing; every byte comes
// from the same core the C, Python, Go and Rust SDKs use.
//
//   #include "phloem.hpp"
//
//   int main() {
//       phloem::Client c("bench-pc");            // PHLOEM_URL + PHLOEM_API_KEY set: direct
//       c.log_now("/rig/pressure", 3.2);
//       c.uniform(t0_ns, phloem::hz(1000), phloem::TimeBasis::host(),
//                 {{"/rig/pressure", readings}});
//       c.finish();                              // throws if anything was not delivered
//   }
//
// Link with the phloem library (-lphloem) (static or shared).
#ifndef PHLOEM_HPP
#define PHLOEM_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "phloem.h"

namespace phloem {

// A failure, in the SDK's words.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

namespace detail {
inline std::string last_error() {
    std::string msg(phloem_last_error(nullptr, 0), '\0');
    if (!msg.empty()) {
        std::string buf(msg.size() + 1, '\0');
        phloem_last_error(buf.data(), buf.size());
        msg.assign(buf.c_str());
    }
    return msg.empty() ? std::string("phloem: the call failed") : msg;
}
inline void check(int rc) {
    if (rc != 0) throw Error(last_error());
}
inline const char *or_null(const std::string &s) { return s.empty() ? nullptr : s.c_str(); }
} // namespace detail

// Which clock a frame's stamps are on. Stated on every frame; there is no
// default.
class TimeBasis {
public:
    // The producer's own clock (a device, a bus, a recording's log time).
    static TimeBasis source() { return TimeBasis("source"); }
    // This machine's wall clock, read as the sample was taken.
    static TimeBasis host() { return TimeBasis("host"); }
    // Stamps from an earlier run, replayed now; anchor_us is the wall-clock
    // instant (microseconds since the epoch) the replay began.
    static TimeBasis backdated(std::int64_t anchor_us) {
        return TimeBasis("backdated:" + std::to_string(anchor_us));
    }
    const char *c_str() const { return wire_.c_str(); }

private:
    explicit TimeBasis(std::string wire) : wire_(std::move(wire)) {}
    std::string wire_;
};

// The interval between two samples of a uniform frame, as an exact ratio of
// nanoseconds: num_ns / den.
struct Step {
    std::uint64_t num_ns;
    std::uint64_t den;
};
// A whole rate in Hz: one sample every 1e9 / rate nanoseconds, exactly.
inline Step hz(std::uint64_t rate) { return Step{1000000000ULL, rate}; }

// One channel's samples in a uniform frame.
struct Column {
    std::string path;
    std::vector<double> values;
};

// How a client is opened. Everything but the name may be left empty.
struct Options {
    // An API key sends straight to the cloud. Empty reads PHLOEM_API_KEY; with
    // neither, the data goes to the Phloem daemon on this machine.
    std::string api_key;
    // The workspace's URL (direct): shown beside the API key on the account
    // page. Empty reads PHLOEM_URL; a key with neither is refused.
    std::string url;
    // The daemon's door (local). Empty = its default location.
    std::string socket_path;
    // The clock the stamps come off (local): a serial, a bus id.
    std::string clock_id;
};

enum class Route { local = 0, direct = 1, memory = 2 };

// A program's connection to Phloem. Move-only. A client that goes out of
// scope without finish() is closed quietly; call finish() to learn whether
// everything was delivered.
class Client {
public:
    explicit Client(const std::string &name, const Options &o = {})
        : s_(phloem_open(name.c_str(), detail::or_null(o.api_key), detail::or_null(o.url),
                         detail::or_null(o.socket_path), detail::or_null(o.clock_id))) {
        if (!s_) throw Error(detail::last_error());
    }
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    Client(Client &&o) noexcept : s_(std::exchange(o.s_, nullptr)) {}
    Client &operator=(Client &&o) noexcept {
        if (this != &o) {
            phloem_close(s_);
            s_ = std::exchange(o.s_, nullptr);
        }
        return *this;
    }
    ~Client() { phloem_close(s_); }

    Route route() const { return static_cast<Route>(phloem_route(s_)); }

    // Say what a channel is: its unit and how to draw it. Optional.
    void declare(const std::string &path, const std::string &unit = "",
                 const std::string &archetype = "") {
        const char *p = path.c_str();
        const char *u = detail::or_null(unit);
        const char *a = detail::or_null(archetype);
        detail::check(phloem_declare(s_, &p, &a, &u, 1));
    }

    // One reading, now, stamped with this machine's clock.
    void log_now(const std::string &path, double value) {
        detail::check(phloem_log_now(s_, path.c_str(), value));
    }

    // One reading at its own instant.
    void log(const std::string &path, std::int64_t t_ns, const TimeBasis &basis, double value) {
        detail::check(phloem_explicit(s_, basis.c_str(), path.c_str(), t_ns, 'f', value, 0, nullptr));
    }
    void log(const std::string &path, std::int64_t t_ns, const TimeBasis &basis,
             const std::string &text) {
        detail::check(
            phloem_explicit(s_, basis.c_str(), path.c_str(), t_ns, 't', 0.0, 0, text.c_str()));
    }

    // Many readings of one channel, each at its own instant.
    void log_many(const std::string &path, const TimeBasis &basis,
                  const std::vector<std::int64_t> &times_ns, const std::vector<double> &values) {
        if (times_ns.size() != values.size())
            throw Error("`" + path + "`: " + std::to_string(times_ns.size()) + " instants for " +
                        std::to_string(values.size()) + " values");
        detail::check(phloem_log_many(s_, path.c_str(), basis.c_str(), times_ns.data(),
                                      values.data(), values.size()));
    }

    // A uniform frame: every column sampled at t0_ns + k * step. Columns must
    // be the same length. A frame may not start before the previous one for
    // the same channel ended.
    void uniform(std::int64_t t0_ns, Step step, const TimeBasis &basis,
                 const std::vector<Column> &columns) {
        if (columns.empty()) throw Error("a uniform frame needs at least one channel");
        const std::size_t rows = columns.front().values.size();
        std::vector<const char *> paths;
        std::vector<double> flat;
        paths.reserve(columns.size());
        flat.reserve(columns.size() * rows);
        for (const auto &c : columns) {
            if (c.values.size() != rows)
                throw Error("`" + c.path + "` has " + std::to_string(c.values.size()) +
                            " samples, `" + columns.front().path + "` has " + std::to_string(rows));
            paths.push_back(c.path.c_str());
            flat.insert(flat.end(), c.values.begin(), c.values.end());
        }
        detail::check(phloem_uniform_f64(s_, t0_ns, step.num_ns, step.den, basis.c_str(),
                                         paths.data(), paths.size(), flat.data(), rows, nullptr));
    }

    // State a hole: good data stopped at frontier_ns on every path named.
    // at_least_samples < 0 = the width is unknown.
    void gap(const std::vector<std::string> &paths, std::int64_t frontier_ns,
             std::int64_t at_least_samples, const std::string &detail_words) {
        std::vector<const char *> p;
        p.reserve(paths.size());
        for (const auto &s : paths) p.push_back(s.c_str());
        detail::check(
            phloem_gap(s_, p.data(), p.size(), frontier_ns, at_least_samples, detail_words.c_str()));
    }

    void flush() { detail::check(phloem_flush(s_)); }

    // End the stream and close. Throws when anything was not delivered: on
    // the direct route, when the cloud did not confirm everything sent. The
    // client is closed either way.
    void finish() {
        phloem_sink *s = std::exchange(s_, nullptr);
        detail::check(phloem_finish(s));
    }

private:
    phloem_sink *s_;
};

} // namespace phloem

#endif // PHLOEM_HPP
