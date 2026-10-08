/* SPDX-License-Identifier: GPL-2.0-or-later
 * Deterministic regression harness for the actual production board.c.
 * Interleavings are injected at Bluetooth API boundaries, never simulated by
 * copying the transition algorithm into a separate test implementation.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define CONFIG_ZMK_SPLIT_ROLE_CENTRAL 1
#define CONFIG_USB_DEVICE_STACK 1
#define CONFIG_ZMK_SPLIT_BLE_PREF_INT 6
#define CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY 30
#define CONFIG_ZMK_SPLIT_BLE_PREF_TIMEOUT 400
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define CONFIG_ZMK_LOG_LEVEL 0
#define IS_ENABLED(x) (x)
#define ARG_UNUSED(x) (void)(x)
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define LOG_WRN(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)
#define INPUT_CALLBACK_DEFINE(...)
#define SYS_INIT(...)
#define ZMK_EV_EVENT_BUBBLE 0
#define BT_CONN_ROLE_CENTRAL 0
#define BT_CONN_TYPE_LE 1
#define K_NO_WAIT 0
#define K_MSEC(x) (x)
typedef int zmk_event_t;
struct input_event { int unused; };
struct k_work { int unused; };
struct k_work_delayable { struct k_work work; };
#define K_WORK_DELAYABLE_DEFINE(name, cb) struct k_work_delayable name
struct k_spinlock { int held; };
typedef int k_spinlock_key_t;
static int64_t now, scheduled;
static bool usb;
static k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) {
    assert(!lock->held); lock->held = 1; return 0;
}
static void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) {
    (void)key; assert(lock->held); lock->held = 0;
}
static int64_t k_uptime_get(void) { return now; }
static int k_work_reschedule(struct k_work_delayable *work, int64_t delay) {
    (void)work; scheduled = now + delay; return 0;
}
static bool zmk_usb_is_powered(void) { return usb; }
struct bt_le_conn_param { uint16_t interval_min, interval_max, latency, timeout; };
struct bt_conn_info {
    int type, role;
    struct { uint16_t interval, latency, timeout; } le;
};
struct bt_conn { int refs; bool connected; struct bt_conn_info info; };
struct bt_conn_cb {
    void (*connected)(struct bt_conn *, uint8_t);
    void (*disconnected)(struct bt_conn *, uint8_t);
    void (*le_param_updated)(struct bt_conn *, uint16_t, uint16_t, uint16_t);
};
static void bt_conn_cb_register(struct bt_conn_cb *cb) { (void)cb; }
static struct bt_conn *bt_conn_ref(struct bt_conn *conn) {
    assert(conn->refs > 0); conn->refs++; return conn;
}
static void bt_conn_unref(struct bt_conn *conn) { assert(conn->refs > 0); conn->refs--; }
static int request_count, request_error;
static bool apply_immediately;
static int read_error;
static void (*request_hook)(struct bt_conn *);
static struct bt_le_conn_param last_request;
static int bt_conn_get_info(struct bt_conn *conn, struct bt_conn_info *info) {
    assert(conn->refs > 0);
    if (!conn->connected) return -ENOTCONN;
    if (read_error) return read_error;
    *info = conn->info; return 0;
}
static void apply(struct bt_conn *conn, const struct bt_le_conn_param *param) {
    conn->info.le.interval = param->interval_min;
    conn->info.le.latency = param->latency;
    conn->info.le.timeout = param->timeout;
}
static int bt_conn_le_param_update(struct bt_conn *conn, const struct bt_le_conn_param *param) {
    assert(conn->refs >= 2);
    request_count++; last_request = *param;
    if (request_hook) { void (*hook)(struct bt_conn *) = request_hook; request_hook = NULL; hook(conn); }
    if (apply_immediately) apply(conn, param);
    return request_error;
}
#include "../../src/board.c"

static struct bt_conn a, b;
static void run(void) { scheduled = -1; power_mode_transition(&power_mode_work.work); }
static void wake_at(int64_t time) { now = time; run(); }
static void setup(void) {
    memset(&events, 0, sizeof(events)); memset(&transition, 0, sizeof(transition));
    memset(&event_lock, 0, sizeof(event_lock));
    a = (struct bt_conn){.refs = 1, .connected = true,
        .info = {.type = BT_CONN_TYPE_LE, .role = BT_CONN_ROLE_CENTRAL}};
    b = a; apply(&a, &mode_params[0]); apply(&b, &mode_params[0]);
    now = 0; usb = false; request_count = 0; request_error = 0;
    read_error = 0; apply_immediately = true; request_hook = NULL; scheduled = -1;
    split_power_mgmt_init(); power_mgmt_bt_conn_connected_cb(&a, 0); run();
    assert(a.refs == 2 && scheduled == 5000);
}
static void param_callback(struct bt_conn *conn) {
    power_mgmt_bt_conn_le_param_updated_cb(conn, conn->info.le.interval,
                                         conn->info.le.latency, conn->info.le.timeout);
}
static void disconnect(struct bt_conn *conn) {
    conn->connected = false; power_mgmt_bt_conn_disconnected_cb(conn, 0x13);
}
static void reconnect_during_request(struct bt_conn *conn) {
    disconnect(conn); power_mgmt_bt_conn_connected_cb(&b, 0);
}
static void input_during_request(struct bt_conn *conn) { (void)conn; reset_idle_timer(); }
static void usb_during_request(struct bt_conn *conn) {
    (void)conn; usb = true; usb_conn_state_changed_listener(NULL);
}
int main(void) {
    setup();
    wake_at(4999); assert(request_count == 0 && scheduled == 5000);
    wake_at(5000); assert(last_request.interval_min == 12 && last_request.latency == 15);
    assert(scheduled == 15000);
    wake_at(15000); assert(last_request.interval_min == 24 && last_request.latency == 8);
    assert(scheduled == 30000);
    wake_at(30000); assert(last_request.interval_min == 48 && last_request.latency == 4);
    assert(last_request.timeout == 400 && scheduled == -1);
    usb = true; usb_conn_state_changed_listener(NULL); assert(scheduled == now); run();
    assert(a.info.le.interval == 6 && a.info.le.latency == 30 && scheduled == -1);
    usb = false; usb_conn_state_changed_listener(NULL); run(); assert(a.info.le.interval == 48);
    reset_idle_timer(); run(); assert(a.info.le.interval == 6 && scheduled == 35000);
    puts("PASS cumulative boundaries, numeric policy, input and deepest-idle USB");

    setup(); apply_immediately = false; wake_at(5000);
    assert(a.info.le.interval == 6 && transition.attempts == 1 && scheduled == 7000);
    apply(&a, &last_request); param_callback(&a); run();
    assert(transition.attempts == 0 && request_count == 1 && scheduled == 15000);
    puts("PASS accepted request is not applied; callback/readback confirms");

    setup(); request_error = -EALREADY; wake_at(5000);
    assert(transition.attempts == 0 && scheduled == 15000);
    setup(); request_error = -EALREADY; apply_immediately = false; wake_at(5000);
    assert(transition.attempts == 1 && scheduled == 7000);
    puts("PASS EALREADY requires matching readback");

    const int errors[] = {0, -EBUSY, -EINVAL, -ENOTCONN, -EALREADY};
    for (unsigned int i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        setup(); apply_immediately = false; request_error = errors[i];
        wake_at(30000); wake_at(32000); wake_at(34000); wake_at(36000);
        assert(request_count == 3 && scheduled == -1);
        param_callback(&a); run(); assert(request_count == 3);
        reset_idle_timer(); run(); assert(transition.attempts == 0);
        wake_at(41000); assert(request_count == 4);
    }
    puts("PASS bounded retries for accepted-but-unapplied and error requests");

    setup(); wake_at(30000); apply_immediately = false;
    reset_idle_timer(); run(); assert(transition.attempts == 1);
    now = 31000; reset_idle_timer(); run(); assert(transition.attempts == 1);
    now = 32000; reset_idle_timer(); run(); assert(transition.attempts == 2);
    now = 34000; reset_idle_timer(); run(); assert(transition.attempts == 3);
    now = 35000; reset_idle_timer(); run(); assert(transition.attempts == 3);
    now = 36000; reset_idle_timer(); run(); assert(transition.attempts == 1);
    apply_immediately = true; now = 38000; reset_idle_timer(); run();
    assert(a.info.le.interval == 6 && transition.attempts == 0);
    puts("PASS fresh activity recovers exhausted ACTIVE after bounded cooldown");

    setup(); request_hook = input_during_request; wake_at(5000);
    assert(scheduled == now); run(); assert(a.info.le.interval == 6 && a.refs == 2);
    setup(); request_hook = usb_during_request; wake_at(30000);
    assert(scheduled == now); run(); assert(a.info.le.interval == 6 && scheduled == -1);
    puts("PASS input/USB interleaved with in-flight request is reconciled");

    setup(); request_hook = reconnect_during_request; wake_at(5000);
    assert(a.refs == 1 && b.refs == 2 && events.conn == &b && scheduled == now);
    run(); assert(transition.generation == events.generation && b.info.le.interval == 6);
    uint32_t revision = events.revision; param_callback(&a);
    power_mgmt_bt_conn_disconnected_cb(&a, 0x13); assert(events.revision == revision);
    disconnect(&b); run(); assert(b.refs == 1 && events.conn == NULL && scheduled == -1);
    reset_idle_timer(); run(); assert(request_count == 1 && scheduled == -1);
    puts("PASS disconnect/reconnect, stale callbacks, no reference leak or stale mutation");

    setup(); apply_immediately = false; wake_at(5000); reset_idle_timer(); run();
    assert(transition.target == POWER_MODE_ACTIVE);
    apply(&a, &mode_params[1]); param_callback(&a); apply_immediately = true; run();
    assert(a.info.le.interval == 6);
    puts("PASS late old-target completion is repaired after new activity");

    setup(); disconnect(&a); run();
    b.info.role = 1; power_mgmt_bt_conn_connected_cb(&b, 0); run();
    assert(!events.conn && b.refs == 1);
    b.info.role = 0; power_mgmt_bt_conn_connected_cb(&b, 1); run(); assert(!events.conn);
    puts("PASS host-role and failed connections are ignored");
    return 0;
}
