# Split power management regression checks

Run `python3 tests/split_power_mgmt/run.py` with a host C compiler (`CC` can
select one). The harness compiles the actual `src/board.c` with deterministic
Zephyr/Bluetooth mocks. It checks:

- Cumulative 5/15/30-second idle boundaries and the unchanged BLE values
- Accepted requests versus applied parameters; callbacks and `-EALREADY` readback
- Three-attempt retry bursts, including accepted-but-unapplied requests and errors
- Fresh input recovery from an exhausted ACTIVE burst after its 2-second cooldown
- USB attachment/detachment from deepest idle, input during requests, late completions
- Disconnect/reconnect during a request, stale callbacks, and reference accounting
- Ignoring failed connections and links where the local device is not central

Callbacks publish events under a spinlock; Bluetooth requests run only in the
system workqueue, without that lock held. A worker's connection reference keeps
its snapshot alive through disconnect, and a generation/revision check prevents
it from overwriting a newer event's schedule. Deterministic injected interleavings
are regression coverage, not a proof against every possible hardware race.

Three requests are allowed per target and connection. A parameter callback or a
scheduled readback verifies application; request acceptance alone never confirms
it. After exhaustion the worker waits for a policy change, reconnect, or (for
ACTIVE) fresh input after the cooldown. It does not retry forever on an idle
battery-powered keyboard. The final attempt also gets one timed readback even
if the controller does not deliver a callback.

These host checks do not replace the three firmware builds or hardware testing.
In particular they do not establish the cause of any host-side BLE drop. Keep
this stage isolated from disconnected-battery-policy changes when comparing
hardware behavior. No keymap, dependency pin, BLE numeric setting, or logging
configuration is changed by this fix. Existing module-level boot/connection logs
are retained, and failed request logs are limited by the retry policy; key/input
values are never logged.
