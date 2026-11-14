# Optional Android Auto channel 10 diagnostic

Source-only implementation for `feature/bmw-6wb`, based on
`20045d8e97d7d6ea14b071106fdfe07fbff73922`. No QNX or host compilation was
performed for this change. Runtime, scheduler impact and QNX compatibility
still require validation on the user's PC and vehicle. No installation,
autostart, renderer, MOST/DSI, OEM bypass or navigation logic changes.

## Enablement and lifecycle

Only the exact value `NBT_NAV_DIAG=1` enables the observer. Absent, empty,
`0`, `true` and other values leave it disabled. Configuration is read when
an Android Auto session starts; restart the daemon manually after changing it.
Disabled: no recorder, queue, exporter thread or diagnostic clock calls on RX;
the dispatcher evaluates only the channel/pointer condition.

The diagnostic file is **JSON Lines, not CSV**:

* `/dev/shmem/nbt-aad-nav-diag.jsonl` — current, appended across sessions/runs.
* `/dev/shmem/nbt-aad-nav-diag.jsonl.1` — previous.
* `/dev/shmem/nbt-aad-nav-diag.jsonl.2` — oldest retained.

Each file is limited to 2 MiB; rotation occurs between complete records.
Permission is 0600. The exporter creates the file after its first queued event,
normally `session_start`, shortly after a phone connects, not merely at daemon
startup or only at shutdown. USB logging settings do not redirect this file.
`/dev/shmem` is volatile: retrieve it before switching off the NBT.
Startup logs contain `[NavDiag] enabled: ...`, or `[NavDiag] unavailable` on
initialization failure. An exporter error prints `[NavDiag] export stopped`;
Android Auto continues, but subsequent diagnostic captures are dropped.
Disabling does not remove old files; rotation replaces the oldest generation.

## Scope, schema and privacy

The observer copies only channel 10 bodies, after decryption and complete frame
reassembly, before the functional dispatcher. It includes unknown IDs and
channel-setup messages; an unknown body is not assumed to be a known navigation
message. It never changes the original frame or calls the renderer.

Recognized schemas are the documented subset in `proto/aa_nav.proto`:
8003 status, 8006 state (all steps, maneuvers, roads, lanes, cues, destinations),
8007 position (step distance/time, repeated destination distance/ETA/time,
current road), 8004 legacy turn and 8005 legacy distance. All repeated
occurrences are retained with indexed field paths, including fields unused by
NavView. Protocol definitions do not prove Maps or Waze actually sends a field.
Unknown fields retain number, wire type and byte size, **not their contents**;
unknown length-delimited data is not recursively guessed to be a message.
Unsupported wire types/malformed encodings produce an explicit parse error;
when field size cannot be determined it is marked unknown. Fixed32/64 unknown
values remain uninterpreted. No audio/video channels are observed.

By default, road names, destination addresses and instruction text are redacted;
field paths and byte sizes remain. Numeric fields, numeric display distances
and narrowly validated clock/AM-PM ETA strings are retained. Free text requires
explicit `NBT_NAV_DIAG_TEXT=1`; then known UTF-8 strings are recorded (maximum
256 bytes each), potentially including private locations. Unknown contents
remain omitted even with that option. Do not enable text indiscriminately.
`NBT_NAV_DIAG_SOURCE=maps` or `waze` is an optional **manual label**; it is not
application detection. Other values give `unspecified`. Keep separate sessions
for the two apps and do not use destinations whose disclosure is unwanted.

## Bounded recording and interpretation

The RX path copies at most 8192 bytes into a preallocated 32-slot pool (about
256 KiB), using one trylock. Busy/full queues drop the observation instead of
waiting; AA processing continues. Parsing, JSON formatting, file writes and
rotation run on a separate pthread. Its idle polling interval is 100 ms;
this is not an AA receive delay. It holds the queue mutex only while copying
one fixed-size record; an RX collision drops a diagnostic record. No condition
variable wakeup, retry, file I/O or allocation occurs in the RX capture path.
This architecture bounds RX work; its actual timing is not measured here.

Limits: 8192 captured body bytes, 512 decoded fields, nesting depth 8, optional
known text 256 bytes. Truncation, malformed bodies and field/depth limits are
explicit. These bounded logs intentionally cannot promise every field of an
arbitrarily large message. Unknown schema cannot supply semantic names.

`mono_ns` is CLOCK_MONOTONIC nanoseconds at queue capture, not physical arrival
or phone transmission time. `run_mono_ns` plus PID identifies an observer run;
`session` increments per AA connection. `sequence` counts observed channel 10
messages, including diagnostic drops. Counters are cumulative for the observer
run, not reset per session:

* `seen`: channel 10 messages presented to the observer.
* `captured`: accepted by its RAM queue, not a guarantee of persistence.
* `lost`: diagnostic observations dropped by contention/full queue/export failure;
  this does not mean AA packets were lost.
* `truncated`: input bodies exceeding the copy limit (also counted if dropped).

Counters on each record are producer snapshots. A normal observer destruction
(after RX joins) drains the queue and writes final counters. Existing daemon
exit paths can use `_exit`, signals or termination that bypass destruction;
a final footer and the last queued records are **not guaranteed**. File errors,
rotation and a power loss can also limit retained history. Do not change daemon
shutdown behavior just to collect this diagnostic.

## Future vehicle operation — one command at a time

No commands below were executed on the BMW. Keep manual startup. Stop the
daemon using the existing HMI **Force Stop**, then inspect the configuration:

```sh
cat /opt/aa/nbt-aad.env
```

The current HMI loader accepts at most **15 env file entries**, with 160-byte
line buffers (`rhmiapp/nbt_aad_app.cpp`). Ensure the diagnostic entry is among
those accepted; avoid duplicate keys. Do not remove unrelated settings.
Enable while retaining other lines, checking each step before the next:

```sh
grep -v '^NBT_NAV_DIAG=' /opt/aa/nbt-aad.env > /dev/shmem/nbt-aad.env.navdiag.tmp
```

```sh
echo 'NBT_NAV_DIAG=1' >> /dev/shmem/nbt-aad.env.navdiag.tmp
```

```sh
cat /dev/shmem/nbt-aad.env.navdiag.tmp
```

Only if the staged contents are correct:

```sh
cat /dev/shmem/nbt-aad.env.navdiag.tmp > /opt/aa/nbt-aad.env
```

```sh
cat /opt/aa/nbt-aad.env
```

Start using the existing HMI **Start**, connect AA, and exercise Maps or Waze.
This does not enable daemon autostart. Check the ordinary daemon log for the
NavDiag enablement/error messages and, over SSH, check the dedicated file:

```sh
ls -l /dev/shmem/nbt-aad-nav-diag.jsonl
```

If absent after AA connection, inspect the enablement/error message and loaded
env entries. Stop if ordinary AA or vehicle functions behave abnormally; this
source-only review cannot certify runtime behavior.

For disabling, with daemon stopped, repeat the staging/inspection/copy commands
above, changing only the echo line to:

```sh
echo 'NBT_NAV_DIAG=0' >> /dev/shmem/nbt-aad.env.navdiag.tmp
```

Do not append a second value without removing the existing key first. No backup
is created; the temporary file is a staging file, not an automatic rollback.

## Copy to Windows without text conversion

End navigation, allow the exporter to drain while the NBT is still running,
then use HMI Force Stop. Retrieve before vehicle power-off; abrupt exit may
still lose the tail. In Windows PowerShell, replace `NBT_SSH_HOST` with the same
actual host used for SSH. Use a fresh PC destination folder. Execute separately:

```powershell
scp -O root@NBT_SSH_HOST:/dev/shmem/nbt-aad-nav-diag.jsonl .
```

```powershell
scp -O root@NBT_SSH_HOST:/dev/shmem/nbt-aad-nav-diag.jsonl.1 .
```

```powershell
scp -O root@NBT_SSH_HOST:/dev/shmem/nbt-aad-nav-diag.jsonl.2 .
```

Older generations may not exist yet. SCP preserves bytes; `-O` selects the legacy
SCP protocol if the QNX SSH installation lacks SFTP. WinSCP using SCP/binary
transfer is an alternative. Do not transfer through Get-Content/Set-Content,
a terminal paste, or a PowerShell text pipeline. To hash a downloaded file:

```powershell
Get-FileHash .\nbt-aad-nav-diag.jsonl -Algorithm SHA256
```

The hash identifies the PC copy; it does not by itself establish equality to a
remote hash. The offline reader never rewrites its input or prints field values:

```powershell
python milestone-7/tools/analyze_nav_diag.py nbt-aad-nav-diag.jsonl.2 nbt-aad-nav-diag.jsonl.1 nbt-aad-nav-diag.jsonl
```

Pass only files actually present. It tolerates incomplete JSON tails with a
count and nonzero exit status, deduplicates repeated records and summarizes
message IDs, field paths, unknown metadata and counters. Detailed optional text
remains in the original JSONL, not the summary.

## Checks performed versus pending

Executable-free checks: Python static contract tests (schema correspondence,
hook order/scope, RX restrictions, bounded/private defaults, lexical delimiters,
unchanged 6WB/MOST sources), Python reader tests using synthetic JSONL, and diff
whitespace checks. These are not a compiler or proof of thread/running behavior.

`tests/test_nav_diag.cpp` is prepared for subsequent compilation/testing on the
user's PC. It covers modern fixture helpers, legacy messages, repeated fields,
unknown fields/IDs/fixed wire sizes, malformed varints/lengths, limits, privacy,
disabled recorder, deterministic queue contention/overflow and file rotation.
It has **not been compiled or executed**. Existing C++ regression tests likewise
were not run under the explicit stop on all compilation. No new executable,
size or executable SHA-256 is available from this work.
