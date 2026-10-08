# Disconnected sleep override

The pinned `zmk-feature-non-lipo-battery-management` revision
`43fa56fb7d1e065393e926b6caa0ea02cb86eca7` incorrectly tests the integer
`CONFIG_ZMK_NON_LIPO_ADV_SLEEP_TIMEOUT` using `IS_ENABLED()`. At the default
60,000 ms its advertising timeout is compiled out. Simply enabling that code
would introduce unsafe shutdown after *any* disconnect (including losing only
one of the central's host/split links), stop monitoring permanently on USB
power, and ignore device-suspend failures.

## Integration

`cmake/non_lipo_override.cmake` validates the upstream source SHA-256, writes a
copy into the build directory, disables its two old timeout blocks, and replaces
exactly that source on the upstream library target. The west checkout remains
unchanged. `zephyr/module.yml` orders this module after the battery module.
The ADC driver, battery conversion, threshold and **existing low-voltage shutdown
path remain byte-for-byte unchanged** in the generated source. Dependency pins,
BLE parameters, keymap and the reusable build workflow are unchanged.

The local `src/disconnected_sleep.c` owns only the optional disconnected timeout.
It uses `#if CONFIG_ZMK_NON_LIPO_ADV_SLEEP_TIMEOUT > 0`; zero disables it. The
existing default stays 60,000 ms. The source hash and target checks intentionally
fail configuration if an upstream update needs a fresh review; the upstream
source is also a CMake configure dependency for incremental builds.

## Behaviour

- Start a full timeout on boot. Poll once a second; the sleep boundary may be
  delayed by that polling interval and work-queue load.
- Stay awake while USB is powered or **any** LE connection is connected or
  disconnecting. Host and split connections both count. Inspection errors block
  sleep. `CONNECTING` does not count: the pinned Zephyr also uses it for a
  reserved legacy advertising connection, even with no peer connected.
- Bluetooth/USB events invalidate the old deadline, including a transient
  reconnect between polls. After the last disconnect or USB unplug, allow a
  fresh full timeout. Monitoring continues while plugged in, even after the old
  deadline expires.
- Run on ZMK's low-priority queue, serially with battery sampling/shutdown. Check
  the configured matrix is ready and already wake-enabled; preserve its wake
  setup rather than invoking soft-off. Resume devices on a partial suspend
  failure or an aborted attempt, and start another full grace interval.
- After device suspend, recheck both live links and USB plus the event generation.
  The final check and `sys_poweroff()` are IRQ-locked on this single-core nRF52.
  No deliberate log-flush delay opens a reconnect window.

This guard applies to disconnected sleep only. The existing critical-voltage
shutdown and ZMK's separate 9,000,000 ms inactivity sleep retain their original
semantics. ZMK's core inactivity PM path is not globally serialized with its
battery queue; fixing that existing core limitation would require a separate
upstream change. The guard observes the host stack's current connection state,
not an RF handshake whose HCI event has not yet been processed.

## Validation

Run the deterministic host harness (C compiler and Python 3):

```
python3 tests/power_management/run_tests.py
```

It compiles the actual production source with Zephyr fakes at timeout 60,000,
1,000 and zero. It covers boot without peers, the advertising reservation,
partial/all disconnect, USB through expiry and unplug, transient reconnect,
reconnect/USB insertion during suspend, live-link discovery without a callback,
suspend failure/resume, missing wake setup and failed connection inspection.
These tests validate software control flow, not electrical wake or BLE timing.

Build both halves with the repository's existing pinned build workflow before
flashing. Hardware checks remain required before merging:

1. With healthy battery and no host/split links, verify sleep near 60 seconds,
   then key wake and normal reconnection on both halves.
2. On the central, retain the host while dropping split, then retain split while
   dropping host. Neither partial disconnect should cause disconnected sleep.
3. Remove every link; verify a full timeout, then key wake.
4. Remain USB-powered beyond 60 seconds, unplug with no links, verify another
   full timeout and key wake. Reconnect USB near expiry; it should remain on.
5. Reconnect a host/split link near expiry repeatedly; a live link must keep the
   keyboard awake. Check keys and pointing still work after aborted suspension.
6. Check normal battery readings and the existing critical-voltage safety
   behaviour separately, with a controlled supply and suitable precautions.

`settings_reset` has neither this monitor nor the battery-source replacement;
it is only a reset image, not a runtime sleep test.
