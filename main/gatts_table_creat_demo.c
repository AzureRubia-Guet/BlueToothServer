
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "gatts_table_creat_demo.h"
#include "esp_gatt_common_api.h"

#include "esp_now.h"
#include "esp_wifi.h"
#include <esp_log.h>
#include <esp_system.h>

#include "freertos/queue.h"

#include "messageHandle.h"

#define GATTS_TABLE_TAG "GATTS_TABLE_DEMO"

/* The max length of characteristic value. When the GATT client performs a write or prepare write operation,
 *  the data length must be less than GATTS_DEMO_CHAR_VAL_LEN_MAX.
 */
#define GATTS_DEMO_CHAR_VAL_LEN_MAX 500
#define PREPARE_BUF_MAX_SIZE 1024
#define CHAR_DECLARATION_SIZE (sizeof(uint8_t))
#define ADV_CONFIG_FLAG (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)
#define PROFILE_NUM 1
#define PROFILE_APP_IDX 0
#define ESP_APP_ID 0x55
#define SAMPLE_DEVICE_NAME "ESP_GATTS_SERVER"
#define SVC_INST_ID 0
static uint8_t adv_config_done = 0;

#define TAG "ESP_handle_data"

QueueHandle_t esp_now_queue = NULL;
QueueHandle_t esp_now_send_queue = NULL;

// 广播地址
uint8_t broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-NOW接收回调函数
static void esp_now_recv_callback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len)
{
    if (data_len == sizeof(esp_now_data_t))
    {
        esp_now_data_t received_data;
        memcpy(&received_data, data, data_len);
        if (xQueueSend(esp_now_queue, &received_data, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "ESP-NOW queue full"); // 队列满处理
        }
    }
}

uint16_t heart_rate_handle_table[HRS_IDX_NB];

void send_status_notification()
{
    esp_device_data_t status = get_device_status();
    device_status_packet_t packet = {
        .light = (uint8_t)status.light,
        .fan = (uint8_t)status.fan,
        .heater = (uint8_t)status.heater};

    esp_ble_gatts_send_indicate(
        active_connection.gatts_if,
        active_connection.conn_id,
        heart_rate_handle_table[IDX_CHAR_VAL_A],
        sizeof(packet),
        (uint8_t *)&packet,
        false);
}

// 数据处理任务
static void esp_now_task(void *arg)
{
    esp_now_data_t data;
    while (1)
    {
        if (xQueueReceive(esp_now_queue, &data, portMAX_DELAY) == pdPASS)
        {
            handle_espnow_message(&data); // 调用处理函数
            // 处理完数据后发送通知
            if (active_connection.notify_enable)
            {
                ESP_LOGI(TAG, "发送通知");
                send_status_notification();
            }
        }
    }
    vTaskDelete(NULL);
}

// 发送任务函数
static void esp_now_send_task(void *arg)
{
    esp_now_data_t send_data;
    while (1)
    {
        if (xQueueReceive(esp_now_send_queue, &send_data, portMAX_DELAY) == pdPASS)
        {
            // 实际发送数据
            esp_err_t send_ret = esp_now_send(broadcast_addr, (uint8_t *)&send_data, sizeof(send_data));
            if (send_ret != ESP_OK)
            {
                ESP_LOGE(TAG, "ESP-NOW发送失败: %s", esp_err_to_name(send_ret));
            }
            else
            {
                ESP_LOGI(TAG, "数据已通过ESP-NOW广播");
            }
        }
    }
    vTaskDelete(NULL);
}





typedef struct
{
    uint8_t *prepare_buf;
    int prepare_len;
} prepare_type_env_t;

static prepare_type_env_t prepare_write_env;

#define CONFIG_SET_RAW_ADV_DATA
#ifdef CONFIG_SET_RAW_ADV_DATA
static uint8_t raw_adv_data[] = {
    /* Flags */
    0x02, ESP_BLE_AD_TYPE_FLAG, 0x06,
    /* TX Power Level */
    0x02, ESP_BLE_AD_TYPE_TX_PWR, 0xEB,
    /* Complete 16-bit Service UUIDs */
    0x03, ESP_BLE_AD_TYPE_16SRV_CMPL, 0xFF, 0x00,
    /* Complete Local Name */
    0x0F, ESP_BLE_AD_TYPE_NAME_CMPL,
    'E', 'S', 'P', '_', 'G', 'A', 'T', 'T', 'S', '_', 'D', 'E', 'M', 'O'};

static uint8_t raw_scan_rsp_data[] = {
    /* Flags */
    0x02, ESP_BLE_AD_TYPE_FLAG, 0x06,
    /* TX Power Level */
    0x02, ESP_BLE_AD_TYPE_TX_PWR, 0xEB,
    /* Complete 16-bit Service UUIDs */
    0x03, ESP_BLE_AD_TYPE_16SRV_CMPL, 0xFF, 0x00};

#endif /* CONFIG_SET_RAW_ADV_DATA */

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst heart_rate_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE, /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

/* Service */
static const uint16_t GATTS_SERVICE_UUID_TEST = 0x00FF;
static const uint16_t GATTS_CHAR_UUID_TEST_A = 0xFF01;

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_read_write_notify = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t heart_measurement_ccc[2] = {0x00, 0x00};
static const uint8_t char_value[4] = {0};

/* Full Database Description - Used to add attributes into the database */
static const esp_gatts_attr_db_t gatt_db[HRS_IDX_NB] =
    {
        // Service Declaration
        [IDX_SVC] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(GATTS_SERVICE_UUID_TEST), (uint8_t *)&GATTS_SERVICE_UUID_TEST}},

        /* Characteristic Declaration */
        [IDX_CHAR_A] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},

        /* Characteristic Value */
        [IDX_CHAR_VAL_A] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&GATTS_CHAR_UUID_TEST_A, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}},

        /* Client Characteristic Configuration Descriptor */
        [IDX_CHAR_CFG_A] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(heart_measurement_ccc), (uint8_t *)heart_measurement_ccc}},

};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {

    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        adv_config_done &= (~ADV_CONFIG_FLAG);
        if (adv_config_done == 0)
        {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
        if (adv_config_done == 0)
        {
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        /* advertising start complete event to indicate advertising start successfully or failed */
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "advertising start failed");
        }
        else
        {
            ESP_LOGI(GATTS_TABLE_TAG, "advertising start successfully");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "Advertising stop failed");
        }
        else
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Stop adv successfully\n");
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "update connection params status = %d, conn_int = %d, latency = %d, timeout = %d",
                 param->update_conn_params.status,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

void example_prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGI(GATTS_TABLE_TAG, "prepare write, handle = %d, value len = %d", param->write.handle, param->write.len);
    esp_gatt_status_t status = ESP_GATT_OK;
    if (param->write.offset > PREPARE_BUF_MAX_SIZE)
    {
        status = ESP_GATT_INVALID_OFFSET;
    }
    else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE)
    {
        status = ESP_GATT_INVALID_ATTR_LEN;
    }
    if (status == ESP_GATT_OK && prepare_write_env->prepare_buf == NULL)
    {
        prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t));
        prepare_write_env->prepare_len = 0;
        if (prepare_write_env->prepare_buf == NULL)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "%s, Gatt_server prep no mem", __func__);
            status = ESP_GATT_NO_RESOURCES;
        }
    }

    /*send response when param->write.need_rsp is true */
    if (param->write.need_rsp)
    {
        esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
        if (gatt_rsp != NULL)
        {
            gatt_rsp->attr_value.len = param->write.len;
            gatt_rsp->attr_value.handle = param->write.handle;
            gatt_rsp->attr_value.offset = param->write.offset;
            gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
            esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            if (response_err != ESP_OK)
            {
                ESP_LOGE(GATTS_TABLE_TAG, "Send response error");
            }
            free(gatt_rsp);
        }
        else
        {
            ESP_LOGE(GATTS_TABLE_TAG, "%s, malloc failed", __func__);
        }
    }
    if (status != ESP_GATT_OK)
    {
        return;
    }
    memcpy(prepare_write_env->prepare_buf + param->write.offset,
           param->write.value,
           param->write.len);
    prepare_write_env->prepare_len += param->write.len;
}

void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && prepare_write_env->prepare_buf)
    {
        esp_log_buffer_hex(GATTS_TABLE_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }
    else
    {
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf)
    {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
    {
        esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(SAMPLE_DEVICE_NAME);
        if (set_dev_name_ret)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "set device name failed, error code = %x", set_dev_name_ret);
        }

        esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
        if (raw_adv_ret)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "config raw adv data failed, error code = %x ", raw_adv_ret);
        }
        adv_config_done |= ADV_CONFIG_FLAG;
        esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
        if (raw_scan_ret)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "config raw scan rsp data failed, error code = %x", raw_scan_ret);
        }
        adv_config_done |= SCAN_RSP_CONFIG_FLAG;

        esp_err_t create_attr_ret = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, HRS_IDX_NB, SVC_INST_ID);
        if (create_attr_ret)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "create attr table failed, error code = %x", create_attr_ret);
        }
    }
    break;
    case ESP_GATTS_READ_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_READ_EVT");
        break;
    case ESP_GATTS_WRITE_EVT:
        // the data length of gattc write  must be less than GATTS_DEMO_CHAR_VAL_LEN_MAX.
        ESP_LOGI(GATTS_TABLE_TAG, "GATT_WRITE_EVT, handle = %d, value len = %d, value :", param->write.handle, param->write.len);
        esp_log_buffer_hex(GATTS_TABLE_TAG, param->write.value, param->write.len);
        if (param->write.handle == heart_rate_handle_table[IDX_CHAR_CFG_A])
        {
            uint16_t cccd_value = *(uint16_t *)param->write.value;
            ESP_LOGI(GATTS_TABLE_TAG, "CCCD写入值: 0x%04X", cccd_value); // 添加此行
            if (cccd_value == 0x0001)
            {
                // 标记通知已启用
                active_connection.notify_enable = true;
                ESP_LOGI(TAG, "通知已启用");
            }
            // 发送写入响应
            esp_ble_gatts_send_response(
                gatts_if,
                param->write.conn_id,
                param->write.trans_id,
                ESP_GATT_OK,
                NULL);
        }

        if (heart_rate_handle_table[IDX_CHAR_VAL_A] == param->write.handle)
        {
            esp_now_data_t received_data;
            received_data.type = 0;
            received_data.int_data = param->write.value[0];
            handle_espnow_message(&received_data);
            // 通过ESP-NOW广播数据
            if (xQueueSend(esp_now_send_queue, &received_data, 0) != pdPASS)
            {
                ESP_LOGE(TAG, "ESP-NOW发送队列已满");
            }

            // 发送响应
            esp_ble_gatts_send_response(
                gatts_if, param->write.conn_id, param->write.trans_id,
                ESP_GATT_OK, NULL);

            // 处理完数据后发送通知
            if (active_connection.notify_enable)
            {
                ESP_LOGI(TAG, "发送通知");
                send_status_notification();
            }
        }

        break;
    case ESP_GATTS_EXEC_WRITE_EVT:
        // the length of gattc prepare write data must be less than GATTS_DEMO_CHAR_VAL_LEN_MAX.
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
        example_exec_write_event_env(&prepare_write_env, param);
        break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_CONF_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONF_EVT, status = %d, attr_handle %d", param->conf.status, param->conf.handle);
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "SERVICE_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONNECT_EVT, conn_id = %d", param->connect.conn_id);
        esp_log_buffer_hex(GATTS_TABLE_TAG, param->connect.remote_bda, 6);
        // 保存连接ID和gatts_if到全局变量
        active_connection.conn_id = param->connect.conn_id;
        active_connection.gatts_if = gatts_if;
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        /* For the iOS system, please refer to Apple official documents about the BLE connection parameters restrictions. */
        conn_params.latency = 0;
        conn_params.max_int = 0x20; // max_int = 0x20*1.25ms = 40ms
        conn_params.min_int = 0x10; // min_int = 0x10*1.25ms = 20ms
        conn_params.timeout = 400;  // timeout = 400*10ms = 4000ms
        // start sent the update connection parameters to the peer device.
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_DISCONNECT_EVT, reason = 0x%x", param->disconnect.reason);
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
    {
        if (param->add_attr_tab.status != ESP_GATT_OK)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "create attribute table failed, error code=0x%x", param->add_attr_tab.status);
        }
        else if (param->add_attr_tab.num_handle != HRS_IDX_NB)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "create attribute table abnormally, num_handle (%d) \
                        doesn't equal to HRS_IDX_NB(%d)",
                     param->add_attr_tab.num_handle, HRS_IDX_NB);
        }
        else
        {
            ESP_LOGI(GATTS_TABLE_TAG, "create attribute table successfully, the number handle = %d\n", param->add_attr_tab.num_handle);
            memcpy(heart_rate_handle_table, param->add_attr_tab.handles, sizeof(heart_rate_handle_table));
            esp_ble_gatts_start_service(heart_rate_handle_table[IDX_SVC]);
        }
        break;
    }
    case ESP_GATTS_STOP_EVT:
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    case ESP_GATTS_UNREG_EVT:
    case ESP_GATTS_DELETE_EVT:
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{

    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            heart_rate_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
        }
        else
        {
            ESP_LOGE(GATTS_TABLE_TAG, "reg app failed, app_id %04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
    }
    do
    {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++)
        {
            /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == heart_rate_profile_tab[idx].gatts_if)
            {
                if (heart_rate_profile_tab[idx].gatts_cb)
                {
                    heart_rate_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

void app_main(void)
{
    esp_err_t ret;

    /* Initialize NVS. */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 初始化ESP-NOW
    ret = esp_now_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));
        return;
    }
    // 配置对等设备
    esp_now_peer_info_t peer_info = {
        .peer_addr = {0},
        .channel = 1, // 设置信道
        .ifidx = ESP_IF_WIFI_STA,
        .encrypt = false};
    memcpy(peer_info.peer_addr, broadcast_addr, 6);

    // 添加对等设备
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));
    // 创建队列（放在wifi启动之后）
    esp_now_queue = xQueueCreate(5, sizeof(esp_now_data_t));
    if (!esp_now_queue)
    {
        ESP_LOGE(TAG, "Failed to create ESP-NOW queue");
        return;
    }
    // 注册ESP-NOW接收回调
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_callback));
    // 创建发送队列（放在wifi启动之后）
    esp_now_send_queue = xQueueCreate(5, sizeof(esp_now_data_t));
    if (!esp_now_send_queue)
    {
        ESP_LOGE(TAG, "Failed to create ESP-NOW发送队列");
        return;
    }
    // 创建发送任务
    xTaskCreate(esp_now_send_task, "esp_now_send", 4096, NULL, 5, NULL);

    // 创建处理任务
    xTaskCreate(esp_now_task, "esp_now_task", 4096, NULL, 5, NULL);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "gatts register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "gap register error, error code = %x", ret);
        return;
    }

    ret = esp_ble_gatts_app_register(ESP_APP_ID);
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "gatts app register error, error code = %x", ret);
        return;
    }

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }
}
