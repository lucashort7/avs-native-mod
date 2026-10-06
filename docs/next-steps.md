# Next steps

The repository baseline preserves the tested short-capture behavior. Do not combine repository organization with a new live hook deployment or an unmeasured logger rewrite.

## First change: continuous logging infrastructure

Use the cr-srt logger as a local design reference, not as an unreviewed drop-in dependency.

- Keep the file handle open on the writer thread rather than reopening it for each record.
- Use a bounded queue or ring buffer between observation and writing. Never wait for disk I/O inside the detour.
- Drain records in batches with checked WriteFile results, including partial-write handling.
- Count and report dropped records when producers outpace the writer. Do not describe a lossy capture as every call logged.
- Keep resource ownership explicit. Stop producers before the final drain, and retain hook code and trampoline according to the active-hook policy.
- Preserve the caller's LastError and avoid heap allocation on the hot path where practical.

Test queue saturation, concurrent producers, invalid output paths, write failures, shutdown, record integrity and dropped-record accounting before enabling continuous game capture. Measure capture overhead and ask about gameplay FPS rather than inferring it from process responsiveness. The short sandbox capture produced no player-reported FPS drop; that does not establish continuous-capture performance.

## Then identify callbacks

Validate the installed build's function-object and StringName layouts before reading names. Correlate decoded script/function pairs with controlled pause, resume, menu, round and Safe Folder transitions. Record evidence separately from inferred meaning. Captured heap pointers are scoped to that process and object lifetime.

## Build portability

The initial build is the already exercised Windows MinGW path. Add another build system or hosted CI only when the same native fixture suite can run there. Keep third-party notices, exclude game resources, and never ship the local injector as part of this initial baseline.
