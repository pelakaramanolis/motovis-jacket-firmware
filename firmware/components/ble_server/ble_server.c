#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "ble_server.h"
#include "ble_uuids.h"

// qemu has no bluetooth controller. the controller init asserts rather
// than returning an error, so there is nothing to catch - we just
// don't call it. set this to 1 for real hardware
#define BLE_USE_RADIO   0

#define APP_ID          0x55
#define DEVICE_NAME     "MotoVis Jacket"

#define USER_DESC_UUID  0x2901
#define CCCD_UUID       0x2902
#define CCCD_LEN        2

// service, plus 4 attributes per characteristic: declaration, value,
// user description, cccd. worked out from the table so adding a row
// can't leave it wrong
#define NUM_HANDLES     (1 + 4 * BLE_CHAR_COUNT)

static const char *TAG = "ble";

typedef struct {
    const uint8_t *uuid;
    const char    *label;
    uint16_t       len;
    uint8_t       *value;

    uint16_t       char_handle;
    uint16_t       desc_handle;
    uint16_t       cccd_handle;
    uint8_t        cccd[CCCD_LEN];
    bool           subscribed;
} ble_char_entry_t;

static uint8_t s_gps_value[sizeof(motovis_gps_payload_t)];
static uint8_t s_heart_value[sizeof(motovis_heart_payload_t)];
static uint8_t s_battery_value[sizeof(motovis_battery_payload_t)];
static uint8_t s_accel_value[sizeof(motovis_accel_payload_t)];
static uint8_t s_alert_value[sizeof(motovis_alert_payload_t)];

// same labels the original used, so they look the same in a ble browser
static ble_char_entry_t s_chars[BLE_CHAR_COUNT] = {
    [BLE_CHAR_GPS] = {
        .uuid  = MOTOVIS_CHAR_UUID_GPS,
        .label = "MotoVis GPS Packet",
        .len   = sizeof(motovis_gps_payload_t),
        .value = s_gps_value,
    },
    [BLE_CHAR_HEART] = {
        .uuid  = MOTOVIS_CHAR_UUID_HEART_RATE,
        .label = "MotoVis Heart Packet",
        .len   = sizeof(motovis_heart_payload_t),
        .value = s_heart_value,
    },
    [BLE_CHAR_BATTERY] = {
        .uuid  = MOTOVIS_CHAR_UUID_BATTERY,
        .label = "MotoVis Battery Packet",
        .len   = sizeof(motovis_battery_payload_t),
        .value = s_battery_value,
    },
    [BLE_CHAR_ACCEL] = {
        .uuid  = MOTOVIS_CHAR_UUID_ACCEL,
        .label = "MotoVis Accel Packet",
        .len   = sizeof(motovis_accel_payload_t),
        .value = s_accel_value,
    },
    [BLE_CHAR_ALERT] = {
        .uuid  = MOTOVIS_CHAR_UUID_ALERT,
        .label = "MotoVis Crash Alert",
        .len   = sizeof(motovis_alert_payload_t),
        .value = s_alert_value,
    },
};

// where we are in building the database. bluedroid only lets us add one
// attribute at a time and tells us when each one is done
typedef enum {
    BUILD_CHAR = 0,
    BUILD_DESC,
    BUILD_CCCD,
} build_phase_t;

static int           s_build_index = 0;
static build_phase_t s_build_phase = BUILD_CHAR;

static uint16_t      s_service_handle = 0;
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t      s_conn_id = 0;
static bool          s_connected = false;

static bool s_ble_ready = false;
static bool s_adv_data_ready = false;
static bool s_scan_rsp_ready = false;
static bool s_advertising = false;

// the stack answers reads and writes itself from the attribute table.
// that's what lets a phone connect later and read the last alert
static esp_attr_control_t s_auto_rsp = { .auto_rsp = ESP_GATT_AUTO_RSP };

// only one add is ever in flight, so one scratch struct is enough
static esp_attr_value_t s_scratch;

// name goes in the advertisement. the 128-bit uuid won't fit alongside it
static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp     = false,
    .include_name     = true,
    .include_txpower  = false,
    .min_interval     = 0x0006,
    .max_interval     = 0x0010,
    .appearance       = 0x0000,
    .manufacturer_len = 0,
    .service_data_len = 0,
    .service_uuid_len = 0,
    .p_service_uuid   = NULL,
    .flag             = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// so the uuid goes in the scan response instead
static esp_ble_adv_data_t s_scan_rsp_data = {
    .set_scan_rsp     = true,
    .include_name     = false,
    .include_txpower  = false,
    .min_interval     = 0x0006,
    .max_interval     = 0x0010,
    .appearance       = 0x0000,
    .manufacturer_len = 0,
    .service_data_len = 0,
    .service_uuid_len = sizeof(MOTOVIS_SERVICE_UUID),
    .p_service_uuid   = (uint8_t *) MOTOVIS_SERVICE_UUID,
    .flag             = 0,
};

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void start_advertising(const char *why)
{
    if (!(s_ble_ready && s_adv_data_ready && s_scan_rsp_ready)) {
        ESP_LOGW(TAG, "not advertising yet (%s), setup isn't finished", why);
        return;
    }

    esp_err_t err = esp_ble_gap_start_advertising(&s_adv_params);
    if (err != ESP_OK) {
        s_advertising = false;
        ESP_LOGE(TAG, "start advertising failed: %s", esp_err_to_name(err));
    }
}

// adds whichever attribute is next. called again from the callbacks each
// time the stack says the previous one landed
static void add_next_attribute(void)
{
    if (s_build_index >= BLE_CHAR_COUNT) {
        ESP_LOGI(TAG, "gatt database built, %d characteristics, %d handles",
                 BLE_CHAR_COUNT, NUM_HANDLES);
        return;
    }

    ble_char_entry_t *e = &s_chars[s_build_index];
    esp_err_t err;

    if (s_build_phase == BUILD_CHAR) {
        esp_bt_uuid_t uuid;
        motovis_uuid128_to_esp(e->uuid, &uuid);

        s_scratch.attr_max_len = e->len;
        s_scratch.attr_len     = e->len;
        s_scratch.attr_value   = e->value;

        err = esp_ble_gatts_add_char(
            s_service_handle, &uuid,
            ESP_GATT_PERM_READ,
            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
            &s_scratch, &s_auto_rsp);

    } else if (s_build_phase == BUILD_DESC) {
        esp_bt_uuid_t uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = { .uuid16 = USER_DESC_UUID },
        };

        // the stack copies the label, it only ever reads through this pointer
        s_scratch.attr_max_len = (uint16_t) strlen(e->label);
        s_scratch.attr_len     = (uint16_t) strlen(e->label);
        s_scratch.attr_value   = (uint8_t *) e->label;

        err = esp_ble_gatts_add_char_descr(
            s_service_handle, &uuid, ESP_GATT_PERM_READ, &s_scratch, &s_auto_rsp);

    } else {
        esp_bt_uuid_t uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = { .uuid16 = CCCD_UUID },
        };

        s_scratch.attr_max_len = CCCD_LEN;
        s_scratch.attr_len     = CCCD_LEN;
        s_scratch.attr_value   = e->cccd;

        err = esp_ble_gatts_add_char_descr(
            s_service_handle, &uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            &s_scratch, &s_auto_rsp);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adding attribute for %s failed: %s", e->label, esp_err_to_name(err));
    }
}

esp_err_t ble_server_publish(ble_char_t which, const void *payload, uint16_t len)
{
    if (which >= BLE_CHAR_COUNT || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ble_char_entry_t *e = &s_chars[which];
    if (len != e->len) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(e->value, payload, len);

    // before the handle exists we've still kept the value, so the
    // characteristic starts life holding real data
    if (e->char_handle == 0) {
        return ESP_OK;
    }

    esp_err_t err = esp_ble_gatts_set_attr_value(e->char_handle, len, e->value);
    if (err != ESP_OK) {
        return err;
    }

    if (s_connected && e->subscribed && s_gatts_if != ESP_GATT_IF_NONE) {
        // false on the end means notify, not indicate. no acknowledgement
        return esp_ble_gatts_send_indicate(
            s_gatts_if, s_conn_id, e->char_handle, len, e->value, false);
    }

    return ESP_OK;
}

bool ble_server_connected(void)
{
    return s_connected;
}

bool ble_server_subscribed(ble_char_t which)
{
    return (which < BLE_CHAR_COUNT) && s_chars[which].subscribed;
}

static void reset_all_subscriptions(void)
{
    // every new connection starts from scratch. a phone has to ask again
    for (int i = 0; i < BLE_CHAR_COUNT; i++) {
        s_chars[i].subscribed = false;
        s_chars[i].cccd[0] = 0;
        s_chars[i].cccd[1] = 0;
    }
}

// a write landed somewhere. if it was one of our cccds, remember it
static void note_cccd_write(const esp_ble_gatts_cb_param_t *param)
{
    for (int i = 0; i < BLE_CHAR_COUNT; i++) {
        if (s_chars[i].cccd_handle != param->write.handle) {
            continue;
        }

        if (param->write.len < 1) {
            return;
        }

        // bit 0 is notify, bit 1 is indicate. we only do notify
        bool on = (param->write.value[0] & 0x01) != 0;
        s_chars[i].subscribed = on;

        ESP_LOGI(TAG, "%s notifications %s", s_chars[i].label, on ? "on" : "off");
        return;
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        s_adv_data_ready = true;
        start_advertising("adv data ready");
        break;

    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        s_scan_rsp_ready = true;
        start_advertising("scan response ready");
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_advertising = true;
            ESP_LOGI(TAG, "advertising as \"%s\"", DEVICE_NAME);
        } else {
            s_advertising = false;
            ESP_LOGE(TAG, "advertising didn't start, status=%d", param->adv_start_cmpl.status);
        }
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_advertising = false;
        break;

    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG, "connection params: min=%d max=%d latency=%d timeout=%d",
                 param->update_conn_params.min_int,
                 param->update_conn_params.max_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;

    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

    case ESP_GATTS_REG_EVT: {
        s_gatts_if = gatts_if;

        esp_err_t err = esp_ble_gap_set_device_name(DEVICE_NAME);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "set device name failed: %s", esp_err_to_name(err));
            return;
        }

        // both of these finish later, in the gap callbacks above
        esp_ble_gap_config_adv_data(&s_adv_data);
        esp_ble_gap_config_adv_data(&s_scan_rsp_data);

        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id = { .inst_id = 0x00, .uuid = {0} },
        };
        motovis_uuid128_to_esp(MOTOVIS_SERVICE_UUID, &service_id.id.uuid);

        err = esp_ble_gatts_create_service(gatts_if, &service_id, NUM_HANDLES);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "create service failed: %s", esp_err_to_name(err));
        }
        break;
    }

    case ESP_GATTS_CREATE_EVT:
        s_service_handle = param->create.service_handle;
        esp_ble_gatts_start_service(s_service_handle);

        // kick off the walk through the table
        s_build_index = 0;
        s_build_phase = BUILD_CHAR;
        add_next_attribute();
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        s_chars[s_build_index].char_handle = param->add_char.attr_handle;
        s_build_phase = BUILD_DESC;
        add_next_attribute();
        break;

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        if (s_build_phase == BUILD_DESC) {
            s_chars[s_build_index].desc_handle = param->add_char_descr.attr_handle;
            s_build_phase = BUILD_CCCD;
        } else {
            s_chars[s_build_index].cccd_handle = param->add_char_descr.attr_handle;
            s_build_index++;
            s_build_phase = BUILD_CHAR;
        }
        add_next_attribute();
        break;

    case ESP_GATTS_WRITE_EVT:
        // the stack already applied it and answered. we just want to know
        note_cccd_write(param);
        break;

    case ESP_GATTS_CONNECT_EVT: {
        s_connected = true;
        s_advertising = false;
        s_conn_id = param->connect.conn_id;
        reset_all_subscriptions();

        ESP_LOGI(TAG, "phone connected, conn_id=%d", s_conn_id);

        esp_ble_conn_update_params_t cp = {0};
        memcpy(cp.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        cp.latency = 0;
        cp.min_int = 0x10;
        cp.max_int = 0x20;
        cp.timeout = 400;
        esp_ble_gap_update_conn_params(&cp);
        break;
    }

    case ESP_GATTS_DISCONNECT_EVT:
        s_connected = false;
        s_conn_id = 0;
        reset_all_subscriptions();
        ESP_LOGI(TAG, "phone disconnected, reason=0x%02x", param->disconnect.reason);

        // go back to being findable
        start_advertising("disconnected");
        break;

    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG, "mtu is now %d", param->mtu.mtu);
        break;

    default:
        break;
    }
}

esp_err_t ble_server_init(void)
{
    esp_err_t err;

    // the stack keeps pairing keys in nvs, so this has to come first
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

#if !BLE_USE_RADIO
    ESP_LOGW(TAG, "radio not started, no controller here. gatt table not built");
    return ESP_OK;
#else
    // we never use classic bluetooth, and the esp32 can't get this
    // memory back later, so give it up before the controller starts
    err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "couldn't release classic bt memory: %s", esp_err_to_name(err));
    }

    esp_bt_controller_config_t bt_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        return err;
    }

    esp_bluedroid_config_t bluedroid_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    err = esp_bluedroid_init_with_cfg(&bluedroid_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_gap_register_callback(gap_event_handler);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_gatts_register_callback(gatts_event_handler);
    if (err != ESP_OK) {
        return err;
    }

    s_ble_ready = true;

    // everything from here happens in the callbacks
    err = esp_ble_gatts_app_register(APP_ID);
    if (err != ESP_OK) {
        return err;
    }

    // a ceiling, not a promise. the phone proposes and the smaller wins
    err = esp_ble_gatt_set_local_mtu(247);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "couldn't ask for a bigger mtu: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "ble server starting, %d characteristics", BLE_CHAR_COUNT);
    return ESP_OK;
#endif
}
