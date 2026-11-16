# Phloem SDK for C and C++

Send readings from C or C++ to [Phloem](https://phloem.ai). Each reading
becomes a named channel you can watch live, replay, chart and check, the same
as one from a connector. The C++17 wrapper is header-only, over the C library.

```c
#include "phloem.h"

int main(void) {
    phloem_sink *s = phloem_open("bench-pc", NULL, NULL, NULL, NULL);
    if (!s) return 1;
    phloem_log_now(s, "/rig/pressure", 3.2);
    return phloem_finish(s) ? 1 : 0;   /* non-zero if anything was not delivered */
}
```

```cpp
#include "phloem.hpp"

int main() {
    phloem::Client c("bench-pc");
    c.log_now("/rig/pressure", 3.2);
    c.finish();   // throws if anything was not delivered
}
```

## Install

Download the archive for your platform from this repository's releases:
Linux (x64, Arm64), macOS (Apple silicon, Intel) or Windows (x64).

| Path | What |
|---|---|
| `include/phloem.h` | the C interface |
| `include/phloem.hpp` | the C++17 wrapper |
| `lib/libphloem.a` (`phloem.lib` on Windows) | static library |
| `lib/libphloem.so`, `.dylib` or `phloem.dll` | shared library |

```sh
cc  app.c   -Iinclude lib/libphloem.a -lm -lpthread -ldl                  # Linux
c++ -std=c++17 app.cpp -Iinclude lib/libphloem.a -lm -lpthread -ldl       # Linux
cc  app.c   -Iinclude lib/libphloem.a -framework CoreFoundation \
    -framework Security -framework SystemConfiguration -framework IOKit -liconv   # macOS
```

Or link the shared library with `-Llib -lphloem`.

## Two ways to send

`phloem_open(name, api_key, url, socket_path, clock_id)` picks the way by
whether it is given an API key (or `PHLOEM_API_KEY` is set).

| | Through Phloem on this machine | Straight to the cloud |
|---|---|---|
| **When** | no API key | an API key |
| **Needs** | Phloem Computer, or the Phloem service on a server; the program allowed once on **Inputs** | a workspace API key and your workspace's URL, both on the account page |
| **If the network drops** | nothing is lost: Phloem keeps the data on disk and uploads it later | data waits in memory (64 MiB by default) |

```c
phloem_sink *s = phloem_open("ci", key, url, NULL, NULL);   /* straight to the cloud */
```
```cpp
phloem::Options o;
o.api_key = key;
o.url = url;
phloem::Client c("ci", o);
```

Or set `PHLOEM_URL` and `PHLOEM_API_KEY` and pass `NULL` / leave them empty.
`phloem_route(s)` (C++ `c.route()`) says which way the data is going.

## Sending

| C | C++ | What it does |
|---|---|---|
| `phloem_log_now(s, path, v)` | `c.log_now(path, v)` | one reading, stamped with this machine's clock |
| `phloem_explicit(s, basis, path, t_ns, kind, f, i, text)` | `c.log(path, t_ns, basis, v)` | one reading at its own time; C `kind` is `'f'`, `'i'`, `'b'` or `'t'` |
| `phloem_log_many(s, path, basis, times_ns, values, n)` | `c.log_many(path, basis, times_ns, values)` | many readings of one channel |
| `phloem_uniform_f64(s, t0_ns, step_num_ns, step_den, basis, paths, n_ch, values, rows, valid)` | `c.uniform(t0_ns, phloem::hz(1000), basis, columns)` | a block of evenly spaced samples |
| `phloem_gap(s, paths, n, frontier_ns, at_least_samples, detail)` | `c.gap(paths, frontier_ns, at_least_samples, detail)` | state that data is missing; `at_least_samples < 0` when unknown |
| `phloem_declare(s, paths, archetypes, units, n)` | `c.declare(path, unit, archetype)` | a channel's unit and how to draw it (through Phloem on this machine) |
| `phloem_flush(s)` | `c.flush()` | send what is held now |
| `phloem_finish(s)` | `c.finish()` | end the stream; -1 (C) or a thrown `phloem::Error` (C++) if anything was not delivered |

**Timestamps** always say which clock they are on: `"host"` (this machine's),
`"source"` (your device's or recording's own) or `"backdated:<anchor_us>"` (a
recording replayed now). In C++: `phloem::TimeBasis::host()`, `source()`,
`backdated(anchor_us)`. There is no default.

**Errors:** every C call returns 0 on success and -1 on failure;
`phloem_last_error(buf, len)` gives the reason in words. C++ throws
`phloem::Error` with the same words. A C++ `Client` that leaves scope without
`finish()` closes quietly.

## Good to know

- **One program per name.** Straight to the cloud, a second program started
  under the same name takes over and the first one stops.
- **After a crash,** readings still in memory are lost. Do not send again what
  was already sent: it would be stored twice.

## Documentation

https://phloem.ai/docs/sdk/

## Support

Found a bug or need something? [Open an issue](https://github.com/phloem-labs/phloem-c/issues).

## Licence

Apache License 2.0. See `LICENSE`.

## Get it

Download the archive for your platform from this repository's
[Releases](https://github.com/phloem-labs/phloem-c/releases): it holds these
headers and the compiled library. The `include/` directory here is the same
headers, for reading.

