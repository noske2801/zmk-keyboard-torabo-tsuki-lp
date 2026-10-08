// SPDX-License-Identifier: GPL-2.0-or-later
// copyright (C) 2025 sekigon-gonnoc

#include <zephyr/sys/util_macro.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

LOG_MODULE_REGISTER(split_power_mgmt, CONFIG_ZMK_LOG_LEVEL);

#define SLEEP1_TIMEOUT_MS 5000   // 5 seconds to sleep1 from active
#define SLEEP2_TIMEOUT_MS 15000  // 15 seconds idle in total
#define SLEEP3_TIMEOUT_MS 30000  // 30 seconds idle in total
#define ACTIVE_CONN_INTERVAL CONFIG_ZMK_SPLIT_BLE_PREF_INT
#define SLEEP1_CONN_INTERVAL (CONFIG_ZMK_SPLIT_BLE_PREF_INT*2)
#define SLEEP2_CONN_INTERVAL (CONFIG_ZMK_SPLIT_BLE_PREF_INT*4)
#define SLEEP3_CONN_INTERVAL (CONFIG_ZMK_SPLIT_BLE_PREF_INT*8)
#define CONN_LATENCY CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY
#define SLEEP1_CONN_LATENCY ((CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY+1)/2)
#define SLEEP2_CONN_LATENCY ((CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY+3)/4)  
#define SLEEP3_CONN_LATENCY ((CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY+7)/8) 
#define SUPERVISION_TIMEOUT CONFIG_ZMK_SPLIT_BLE_PREF_TIMEOUT

enum power_mode {
    POWER_MODE_ACTIVE,
    POWER_MODE_SLEEP1,
    POWER_MODE_SLEEP2,
    POWER_MODE_SLEEP3,
};

/* Requests can be accepted before the controller applies them. Read back the
 * actual parameters, retry at most three times per target/connection, and let
 * parameter callbacks wake the worker early. These are retry timings, not BLE
 * connection parameters. */
#define PARAM_RETRY_MS 2000
#define PARAM_MAX_ATTEMPTS 3

static void power_mode_transition(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(power_mode_work, power_mode_transition);

/* Callbacks only publish events. The worker alone owns transition/retry state.
 * Never hold this lock across a Bluetooth call (which may block or callback). */
static struct k_spinlock event_lock;
static struct {
    struct bt_conn *conn; /* Owns one reference while connected. */
    int64_t last_activity;
    uint32_t generation;
    uint32_t revision;
    bool usb_powered;
} events;

static struct {
    uint32_t generation;
    enum power_mode target;
    unsigned int attempts;
    int64_t next_attempt;
} transition;

static const struct bt_le_conn_param mode_params[] = {
    {ACTIVE_CONN_INTERVAL, ACTIVE_CONN_INTERVAL, CONN_LATENCY, SUPERVISION_TIMEOUT},
    {SLEEP1_CONN_INTERVAL, SLEEP1_CONN_INTERVAL, SLEEP1_CONN_LATENCY, SUPERVISION_TIMEOUT},
    {SLEEP2_CONN_INTERVAL, SLEEP2_CONN_INTERVAL, SLEEP2_CONN_LATENCY, SUPERVISION_TIMEOUT},
    {SLEEP3_CONN_INTERVAL, SLEEP3_CONN_INTERVAL, SLEEP3_CONN_LATENCY, SUPERVISION_TIMEOUT},
};

/* Caller holds event_lock. Rescheduling is ISR-safe and cannot run the worker
 * inline. Publishing and scheduling together prevents a lost newer event. */
static void publish_event(void) {
    events.revision++;
    k_work_reschedule(&power_mode_work, K_NO_WAIT);
}

static bool params_match(struct bt_conn *conn, const struct bt_le_conn_param *param) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.type == BT_CONN_TYPE_LE &&
           info.le.interval >= param->interval_min && info.le.interval <= param->interval_max &&
           info.le.latency == param->latency && info.le.timeout == param->timeout;
}

static void power_mode_transition(struct k_work *work) {
    ARG_UNUSED(work);
    k_spinlock_key_t key = k_spin_lock(&event_lock);
    struct bt_conn *conn = events.conn ? bt_conn_ref(events.conn) : NULL;
    const uint32_t generation = events.generation;
    const uint32_t revision = events.revision;
    const int64_t last_activity = events.last_activity;
    const bool usb_powered = events.usb_powered;
    k_spin_unlock(&event_lock, key);
    if (!conn) {
        return;
    }

    const int64_t now = k_uptime_get();
    const int64_t idle = now - last_activity;
    enum power_mode target = POWER_MODE_ACTIVE;
    int64_t next_wake = 0;
    if (!usb_powered) {
        if (idle >= SLEEP3_TIMEOUT_MS) {
            target = POWER_MODE_SLEEP3;
        } else if (idle >= SLEEP2_TIMEOUT_MS) {
            target = POWER_MODE_SLEEP2;
            next_wake = last_activity + SLEEP3_TIMEOUT_MS;
        } else if (idle >= SLEEP1_TIMEOUT_MS) {
            target = POWER_MODE_SLEEP1;
            next_wake = last_activity + SLEEP2_TIMEOUT_MS;
        } else {
            next_wake = last_activity + SLEEP1_TIMEOUT_MS;
        }
    }

    if (generation != transition.generation || target != transition.target) {
        transition.generation = generation;
        transition.target = target;
        transition.attempts = 0;
        transition.next_attempt = 0;
    }

    /* A fresh input after an exhausted burst may try ACTIVE again, but input
     * arriving before the cooldown cannot turn retries into a busy loop. */
    if (target == POWER_MODE_ACTIVE && transition.attempts == PARAM_MAX_ATTEMPTS &&
        last_activity >= transition.next_attempt) {
        transition.attempts = 0;
        transition.next_attempt = 0;
    }

    const struct bt_le_conn_param *param = &mode_params[target];
    if (params_match(conn, param)) {
        transition.attempts = 0;
        transition.next_attempt = 0;
    } else {
        if (transition.attempts < PARAM_MAX_ATTEMPTS && now >= transition.next_attempt) {
            /* Skip an obsolete snapshot before issuing a controller request.
             * A later disconnect is safe: this worker holds its own reference.
             * A later input/USB event is reconciled by the next worker run. */
            key = k_spin_lock(&event_lock);
            bool current = events.generation == generation && events.revision == revision;
            k_spin_unlock(&event_lock, key);
            if (current) {
                int err = bt_conn_le_param_update(conn, param);
                transition.attempts++;
                transition.next_attempt = now + PARAM_RETRY_MS;
                /* EALREADY means the stack currently has these parameters;
                 * still verify them rather than treating a request as applied. */
                if ((err == 0 || err == -EALREADY) && params_match(conn, param)) {
                    transition.attempts = 0;
                    transition.next_attempt = 0;
                } else if (err && err != -EALREADY) {
                    LOG_WRN("Split parameter request failed: %d (attempt %u)", err,
                            transition.attempts);
                }
            }
        }
        if (transition.next_attempt > now &&
            (!next_wake || transition.next_attempt < next_wake)) {
            next_wake = transition.next_attempt;
        }
    }

    key = k_spin_lock(&event_lock);
    if (events.revision != revision || events.generation != generation) {
        k_work_reschedule(&power_mode_work, K_NO_WAIT);
    } else if (next_wake) {
        k_work_reschedule(&power_mode_work, K_MSEC(MAX(1, next_wake - k_uptime_get())));
    }
    k_spin_unlock(&event_lock, key);
    bt_conn_unref(conn);
}

static void reset_idle_timer(void) {
    k_spinlock_key_t key = k_spin_lock(&event_lock);
    events.last_activity = k_uptime_get();
    publish_event();
    k_spin_unlock(&event_lock, key);
}

static int position_state_changed_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    reset_idle_timer();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split_power_mgmt_position, position_state_changed_listener);
ZMK_SUBSCRIPTION(split_power_mgmt_position, zmk_position_state_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
static int usb_conn_state_changed_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    k_spinlock_key_t key = k_spin_lock(&event_lock);
    events.usb_powered = zmk_usb_is_powered();
    publish_event(); /* Also wakes a peripheral link already in deepest idle. */
    k_spin_unlock(&event_lock, key);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split_power_mgmt_usb, usb_conn_state_changed_listener);
ZMK_SUBSCRIPTION(split_power_mgmt_usb, zmk_usb_conn_state_changed);
#endif

static bool is_split_peripheral_conn(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL &&
           info.type == BT_CONN_TYPE_LE;
}

static void power_mgmt_bt_conn_connected_cb(struct bt_conn *conn, uint8_t err) {
    if (err || !is_split_peripheral_conn(conn)) {
        return;
    }
    struct bt_conn *new_conn = bt_conn_ref(conn);
    k_spinlock_key_t key = k_spin_lock(&event_lock);
    struct bt_conn *old_conn = events.conn;
    events.conn = new_conn;
    events.generation++;
    events.last_activity = k_uptime_get();
    publish_event();
    k_spin_unlock(&event_lock, key);
    if (old_conn) {
        bt_conn_unref(old_conn);
    }
    LOG_INF("Split peripheral connected (local central)");
}

static void power_mgmt_bt_conn_disconnected_cb(struct bt_conn *conn, uint8_t reason) {
    k_spinlock_key_t key = k_spin_lock(&event_lock);
    if (conn != events.conn) {
        k_spin_unlock(&event_lock, key);
        return;
    }
    events.conn = NULL;
    events.generation++;
    publish_event();
    k_spin_unlock(&event_lock, key);
    bt_conn_unref(conn);
    LOG_INF("Split peripheral disconnected (reason: 0x%02x)", reason);
}

static void power_mgmt_bt_conn_le_param_updated_cb(struct bt_conn *conn, uint16_t interval,
                                                  uint16_t latency, uint16_t timeout) {
    ARG_UNUSED(interval);
    ARG_UNUSED(latency);
    ARG_UNUSED(timeout);
    k_spinlock_key_t key = k_spin_lock(&event_lock);
    if (conn == events.conn) {
        publish_event(); /* Worker reads actual values; stale callbacks cannot set a mode. */
    }
    k_spin_unlock(&event_lock, key);
}

static struct bt_conn_cb power_mgmt_bt_conn_callbacks = {
    .connected = power_mgmt_bt_conn_connected_cb,
    .disconnected = power_mgmt_bt_conn_disconnected_cb,
    .le_param_updated = power_mgmt_bt_conn_le_param_updated_cb,
};

static void mouse_input_callback(struct input_event *evt) {
    ARG_UNUSED(evt);
    reset_idle_timer();
}

static int split_power_mgmt_init(void) {
    k_spinlock_key_t key = k_spin_lock(&event_lock);
    events.usb_powered = zmk_usb_is_powered();
    k_spin_unlock(&event_lock, key);
    bt_conn_cb_register(&power_mgmt_bt_conn_callbacks);
    LOG_INF("Split power management initialized (central)");
    return 0;
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET_OR_NULL(DT_NODELABEL(trackball)), mouse_input_callback);
SYS_INIT(split_power_mgmt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_ZMK_SPLIT_ROLE_CENTRAL */
