/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host harness includes the production monitor, with deterministic Zephyr fakes. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>

#define CONFIG_ZMK_LOG_LEVEL 3
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define APPLICATION 0
#define LOG_MODULE_REGISTER(...)
#define LOG_WRN(...)
#define K_MSEC(ms) (ms)
#define DT_CHOSEN(...) 0
#define DEVICE_DT_GET(...) (&matrix)
#define SYS_INIT(...)
#define BT_CONN_CB_DEFINE(name) struct bt_conn_cb name
#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)
#define ZMK_EV_EVENT_BUBBLE 0
#define BT_CONN_TYPE_LE 1
#define BT_CONN_STATE_DISCONNECTED 0
#define BT_CONN_STATE_CONNECTING 1
#define BT_CONN_STATE_CONNECTED 2
#define BT_CONN_STATE_DISCONNECTING 3

typedef int atomic_t;
typedef int atomic_val_t;
typedef int zmk_event_t;
struct k_work {};
struct k_work_delayable { struct k_work work; };
struct k_work_q {};
struct device {};
struct bt_conn { int state; int info_error; };
struct bt_conn_info { int state; };
struct bt_conn_cb {
    void (*connected)(struct bt_conn *, uint8_t);
    void (*disconnected)(struct bt_conn *, uint8_t);
};
static struct device matrix;
static struct k_work_q lowprio;
static struct bt_conn links[3];
static bool usb, ready, wake, locked;
static int64_t now;
static int schedules, resumes, suspends, suspend_error, suspend_event;
static jmp_buf powered_off;
static int atomic_get(atomic_t *v) { return *v; }
static void atomic_inc(atomic_t *v) { ++*v; }
static int64_t k_uptime_get(void) { return now; }
static bool zmk_usb_is_powered(void) { return usb; }
static struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return &lowprio; }
static void k_work_init_delayable(struct k_work_delayable *w, void (*fn)(struct k_work *)) {}
static int k_work_schedule_for_queue(struct k_work_q *q, struct k_work_delayable *w, int delay) {
    assert(q == &lowprio && delay > 0);
    ++schedules;
    return 1;
}
static int bt_conn_get_info(struct bt_conn *c, struct bt_conn_info *i) {
    i->state = c->state;
    return c->info_error;
}
static void bt_conn_foreach(int type, void (*fn)(struct bt_conn *, void *), void *data) {
    for (int i = 0; i < 3; ++i) { fn(&links[i], data); }
}
static bool device_is_ready(const struct device *d) { return ready; }
static bool pm_device_wakeup_is_enabled(const struct device *d) { return wake; }
static unsigned int irq_lock(void) { assert(!locked); locked = true; return 0; }
static void irq_unlock(unsigned int key) { locked = false; }
static void sys_poweroff(void) { assert(locked); longjmp(powered_off, 1); }
static int zmk_pm_suspend_devices(void);
static void zmk_pm_resume_devices(void) { ++resumes; }

#include "../../src/disconnected_sleep.c"

#if CONFIG_ZMK_NON_LIPO_ADV_SLEEP_TIMEOUT > 0
static int zmk_pm_suspend_devices(void) {
    ++suspends;
    switch (suspend_event) {
    case 1: links[0].state = BT_CONN_STATE_CONNECTED; connected(&links[0], 0); break;
    case 2: usb = true; break;
    case 3: connected(&links[0], 0); disconnected(&links[0], 0); break;
    case 4: links[1].state = BT_CONN_STATE_CONNECTED; break; /* live scan, even without callback */
    }
    return suspend_error;
}
static void reset(void) {
    for (int i = 0; i < 3; ++i) { links[i] = (struct bt_conn){0}; }
    usb = locked = false;
    ready = wake = true;
    now = 0;
    schedules = resumes = suspends = suspend_error = suspend_event = 0;
    link_generation = 0;
    disconnected_sleep_init();
}
static bool tick(int64_t time) {
    now = time;
    if (setjmp(powered_off)) { locked = false; return true; }
    disconnected_timeout(&disconnected_work.work);
    return false;
}
static const int64_t timeout = CONFIG_ZMK_NON_LIPO_ADV_SLEEP_TIMEOUT;
int main(void) {
    reset();
    assert(!tick(timeout - 1));
    assert(tick(timeout)); /* initially disconnected */

    reset();
    links[0].state = BT_CONN_STATE_CONNECTING; /* reserved advertising conn */
    assert(tick(timeout));

    reset();
    links[0].state = links[1].state = BT_CONN_STATE_CONNECTED;
    connected(&links[0], 0); connected(&links[1], 0);
    assert(!tick(timeout));
    links[0].state = BT_CONN_STATE_DISCONNECTED; disconnected(&links[0], 0);
    assert(!tick(2 * timeout)); /* host lost, split remains */
    assert(!tick(3 * timeout));
    links[1].state = BT_CONN_STATE_DISCONNECTED; disconnected(&links[1], 0);
    assert(!tick(3 * timeout + 1));
    assert(!tick(4 * timeout));
    assert(tick(4 * timeout + 1)); /* last link gone: full grace */

    reset();
    usb = true;
    assert(!tick(2 * timeout)); assert(!tick(3 * timeout));
    usb = false; usb_changed(NULL);
    assert(!tick(3 * timeout + 1));
    assert(!tick(4 * timeout)); assert(tick(4 * timeout + 1));

    reset();
    now = timeout - 1;
    connected(&links[0], 0); disconnected(&links[0], 0);
    assert(!tick(timeout)); assert(tick(2 * timeout)); /* transient reconnect */

    reset();
    usb_changed(NULL); usb_changed(NULL); /* plug/unplug entirely between polls */
    assert(!tick(timeout)); assert(tick(2 * timeout));

    reset();
    connected(&links[0], 1); /* failed connection is not a live link */
    assert(tick(timeout));

    reset();
    links[2].state = BT_CONN_STATE_CONNECTED; /* any remaining link blocks */
    assert(!tick(timeout)); assert(suspends == 0);

    for (int event = 1; event <= 4; ++event) {
        reset(); suspend_event = event;
        assert(!tick(timeout));
        assert(suspends == 1 && resumes == 1 && !locked && schedules == 2);
        assert(!tick(timeout + 1));
    }
    reset(); suspend_error = -1;
    assert(!tick(timeout)); assert(resumes == 1);
    suspend_error = 0;
    assert(!tick(timeout + 1)); assert(tick(2 * timeout));

    reset(); wake = false;
    assert(!tick(timeout)); assert(suspends == 0);
    reset(); ready = false;
    assert(!tick(timeout)); assert(suspends == 0);
    reset(); links[0].info_error = -1;
    assert(!tick(timeout)); assert(suspends == 0);
    reset(); links[0].state = BT_CONN_STATE_DISCONNECTING;
    assert(!tick(timeout)); assert(suspends == 0);
    puts("Disconnected sleep lifecycle checks passed");
    return 0;
}
#else
static int zmk_pm_suspend_devices(void) { return 0; }
int main(void) { puts("Zero timeout compiles with monitor disabled"); return 0; }
#endif
