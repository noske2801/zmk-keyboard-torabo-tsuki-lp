/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/pm.h>
#include <zmk/usb.h>
#include <zmk/workqueue.h>

/* This is an integer duration, not an IS_ENABLED() boolean. */
#if CONFIG_ZMK_NON_LIPO_ADV_SLEEP_TIMEOUT > 0

LOG_MODULE_REGISTER(disconnected_sleep, CONFIG_ZMK_LOG_LEVEL);

#define CHECK_INTERVAL_MS 1000

/* Callbacks only invalidate the deadline. All timer state and PM operations
 * run on the battery's low-priority queue, so low-voltage shutdown cannot run
 * concurrently with this path. No bt_conn pointer survives a foreach call. */
static atomic_t link_generation;
static atomic_val_t observed_generation;
static int64_t disconnected_since;
static struct k_work_delayable disconnected_work;

static void check_connection(struct bt_conn *conn, void *data) {
    struct bt_conn_info info;
    bool *busy = data;

    /* Include both host and split links. CONNECTING is not a live link:
     * Zephyr also uses it for the reserved legacy advertising connection.
     * Counting that reservation would prevent disconnected sleep forever. */
    if (bt_conn_get_info(conn, &info) != 0 || info.state == BT_CONN_STATE_CONNECTED ||
        info.state == BT_CONN_STATE_DISCONNECTING) {
        *busy = true;
    }
}

static bool sleep_blocked(void) {
    bool busy = zmk_usb_is_powered();
    bt_conn_foreach(BT_CONN_TYPE_LE, check_connection, &busy);
    return busy;
}

static void reset_deadline(void) {
    disconnected_since = k_uptime_get();
    observed_generation = atomic_get(&link_generation);
}

static void disconnected_timeout(struct k_work *work) {
    atomic_val_t generation = atomic_get(&link_generation);

    if (sleep_blocked() || generation != observed_generation) {
        reset_deadline();
        goto reschedule;
    }
    if (k_uptime_get() - disconnected_since < CONFIG_ZMK_NON_LIPO_ADV_SLEEP_TIMEOUT) {
        goto reschedule;
    }

    /* Physical layouts enable the selected matrix as a wakeup source at init.
     * Refuse this optional sleep if key wake has not been armed. Do not use
     * soft-off, which deliberately disables ordinary matrix wake sources. */
    const struct device *kscan = DEVICE_DT_GET(DT_CHOSEN(zmk_kscan));
    if (!device_is_ready(kscan) || !pm_device_wakeup_is_enabled(kscan)) {
        LOG_WRN("Disconnected sleep skipped: key wakeup is not armed");
        reset_deadline();
        goto reschedule;
    }

    if (zmk_pm_suspend_devices() < 0) {
        zmk_pm_resume_devices();
        LOG_WRN("Disconnected sleep skipped: device suspend failed");
        reset_deadline();
        goto reschedule;
    }

    /* Suspend can yield. A reconnect or USB insertion invalidates the attempt.
     * On this single-core nRF52, IRQ locking makes the final live-state check
     * and system-off indivisible with respect to BT/USB callbacks. */
    unsigned int key = irq_lock();
    if (generation == atomic_get(&link_generation) && !sleep_blocked()) {
        sys_poweroff();
    }
    irq_unlock(key);
    zmk_pm_resume_devices();
    reset_deadline();

reschedule:
    /* Keep monitoring while connected or USB-powered, including after expiry:
     * unplug/last disconnect always receives a fresh full timeout. */
    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &disconnected_work,
                              K_MSEC(CHECK_INTERVAL_MS));
}

static void connected(struct bt_conn *conn, uint8_t err) {
    if (!err) {
        atomic_inc(&link_generation);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    atomic_inc(&link_generation);
}

BT_CONN_CB_DEFINE(disconnected_sleep_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static int usb_changed(const zmk_event_t *event) {
    atomic_inc(&link_generation);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(disconnected_sleep_usb, usb_changed);
ZMK_SUBSCRIPTION(disconnected_sleep_usb, zmk_usb_conn_state_changed);

static int disconnected_sleep_init(void) {
    k_work_init_delayable(&disconnected_work, disconnected_timeout);
    reset_deadline();
    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &disconnected_work,
                              K_MSEC(CHECK_INTERVAL_MS));
    return 0;
}

SYS_INIT(disconnected_sleep_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_ZMK_NON_LIPO_ADV_SLEEP_TIMEOUT > 0 */
