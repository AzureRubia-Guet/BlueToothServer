
// 定义ESP-NOW数据结构
typedef struct
{
    uint8_t type; // 数据类型标识符
    union
    {
        int int_data;      // 整数数据
        char str_data[52]; // 字符串数据
    };
} esp_now_data_t;


typedef struct 
{
    enum{
        light_low = 1,
        light_off,
        light_mid,
        light_high,
        breath_light,
        flowing_light
    }light;
    enum{
        fan_low = 7,
        fan_off,
        fan_mid,
        fan_high
    }fan;
    enum{
        heater_low = 15,
        heater_off,
        heater_mid,
        heater_high
    }heater;
} esp_device_data_t;

typedef struct {
    uint8_t light;
    uint8_t fan; 
    uint8_t heater;
} device_status_packet_t;

esp_device_data_t get_device_status(void);
void handle_espnow_message(const esp_now_data_t *data);
void handle_received_int_data(const esp_now_data_t *data);
void handle_received_str_data(const esp_now_data_t *data);
