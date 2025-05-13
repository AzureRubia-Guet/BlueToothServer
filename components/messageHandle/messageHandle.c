#include <stdio.h>
#include "messageHandle.h"
#include <string.h>

#define MATCH_STR(data, str) (strstr(data, str) != NULL)

static esp_device_data_t device_status =
    {
        light_off, fan_off, heater_off};

static esp_device_data_t temp =
    {
        light_off, fan_off, heater_off};

// 定义数据类型标识符
#define INT_TYPE 0 
#define STR_TYPE 1


esp_device_data_t get_device_status(void)
{
    return device_status;
}


void handle_espnow_message(const esp_now_data_t *data)
{
    switch (data->type)
    {
    case INT_TYPE:
        handle_received_int_data(data);
        break;
    case STR_TYPE:
        handle_received_str_data(data);
        break;
    default:
        break;
    }
}


void handle_received_int_data(const esp_now_data_t *data)
{
    printf("Received int data: %d", data->int_data);
    if (data->int_data >= 1 && data->int_data <= 6) // 灯光部分
    {
        if (device_status.light == light_off && data->int_data != 1)
            return;
        switch (data->int_data)
        {
        case 1: // 开灯
            device_status.light = (device_status.light == light_off ? light_low : device_status.light);
            break;
        case 2: // 关灯
            device_status.light = light_off;
            break;
        case 3:
            switch (device_status.light)
            {
            case light_low:
                device_status.light = light_mid;
                break;
            case light_mid:
                device_status.light = light_high;
                break;
            default:
                break;
            }
            break;
        case 4:
            switch (device_status.light)
            {
            case light_mid:
                device_status.light = light_low;
                break;
            case light_high:
                device_status.light = light_mid;
                break;
            case breath_light:
                device_status.light = temp.light;
                break;
            case flowing_light:
                device_status.light = temp.light;
                break;
            default:
                break;
            }
            break;
        case 5:
            temp.light = device_status.light;
            device_status.light = breath_light;
            break;
        case 6:
            temp.light = device_status.light;
            device_status.light = flowing_light;
            break;
        }
    }
    else if (data->int_data >= 7 && data->int_data <= 14)
    {
        if (device_status.fan == fan_off && data->int_data != 7)
            return;
        switch (data->int_data)
        {
        case 7: // 开
        case 11:
            device_status.fan = (device_status.fan == fan_off ? fan_low : device_status.fan);
            break;
        case 8: // 关
        case 12:
            device_status.fan = fan_off;
            break;
        case 9:
        case 13:
            switch (device_status.fan)
            {
            case fan_low:
                device_status.fan = fan_mid;
                break;
            case fan_mid:
                device_status.fan = fan_high;
                break;
            default:
                break;
            }
            break;
        case 10:
        case 14:
            switch (device_status.fan)
            {
            case fan_mid:
                device_status.fan = fan_low;
                break;
            case fan_high:
                device_status.fan = fan_mid;
                break;
            default:
                break;
            }
            break;
        }
    }
    else if (data->int_data >= 15 && data->int_data <= 18)
    {
        if (device_status.heater == heater_off && data->int_data != 15)
            return;
        switch (data->int_data)
        {

        case 15:
            device_status.heater = (device_status.heater == heater_off ? heater_low : device_status.heater);
            break;

        case 16:
            device_status.heater = heater_off;
            break;

        case 17:
            switch (device_status.heater)
            {
            case heater_low:
                device_status.heater = heater_mid;
                break;
            case heater_mid:
                device_status.heater = heater_high;
                break;
            default:
                break;
            }
            break;

        case 18:
            switch (device_status.heater)
            {
            case heater_mid:
                device_status.heater = heater_low;
                break;
            case heater_high:
                device_status.heater = heater_mid;
                break;
            default:
                break;
            }
            break;
        }
    }
}


void handle_received_str_data(const esp_now_data_t *data)
{
    if (MATCH_STR("deng", data->str_data))
    {
        if (device_status.light == light_off && !MATCH_STR("kai", data->str_data))
            return;
        if (MATCH_STR("liang", data->str_data))
        {
            switch (device_status.light)
            {
            case light_low:
                device_status.light = light_mid;
                break;
            case light_mid:
                device_status.light = light_high;
                break;
            default:
                break;
            }
        }
        else if (MATCH_STR("an", data->str_data))
        {
            switch (device_status.light)
            {
            case light_mid:
                device_status.light = light_low;
                break;
            case light_high:
                device_status.light = light_mid;
                break;
            default:
                break;
            }
        }
        else if (MATCH_STR("kai", data->str_data))
        {
            device_status.light = (device_status.light == light_off ? light_low : device_status.light);
        }
        else if (MATCH_STR("guan", data->str_data))
        {
            device_status.light = light_off;
        }
    }
    else if (MATCH_STR("feng", data->str_data))
    {
        if (device_status.fan == fan_off && !MATCH_STR("kai", data->str_data))
            return;
        if (MATCH_STR("kuai", data->str_data))
        {
            switch (device_status.fan)
            {
            case fan_low:
                device_status.fan = fan_mid;
                break;
            case fan_mid:
                device_status.fan = fan_high;
                break;
            default:
                break;
            }
        }
        else if (MATCH_STR("man", data->str_data))
        {
            switch (device_status.fan)
            {
            case fan_mid:
                device_status.fan = fan_low;
                break;
            case fan_high:
                device_status.fan = fan_mid;
                break;
            default:
                break;
            }
        }
        else if (MATCH_STR("kai", data->str_data))
        {
            device_status.fan = (device_status.fan == fan_off ? fan_low : device_status.fan);
        }
        else if (MATCH_STR("guan", data->str_data))
        {
            device_status.fan = fan_off;
        }
    }
    else if(MATCH_STR("dian re qi", data->str_data))
    {
        if(device_status.heater == heater_off && !MATCH_STR("kai", data->str_data))
        {
            return;
        }
        if (MATCH_STR("sheng", data->str_data))
        {
            switch (device_status.heater)
            {
            case heater_low:
                device_status.heater = heater_mid;
                break;
            case heater_mid:
                device_status.heater = heater_high;
                break;
            default:
                break;
            }
        }
        else if (MATCH_STR("jiang", data->str_data))
        {
            switch (device_status.heater)
            {
            case heater_mid:
                device_status.heater = heater_low;
                break;
            case heater_high:
                device_status.heater = heater_mid;
                break;
            default:
                break;
            }
        }
        else if (MATCH_STR("kai", data->str_data))
        {
            device_status.heater = (device_status.heater == heater_off ? heater_low : device_status.heater);
        }
        else if (MATCH_STR("guan", data->str_data))
        {
            device_status.heater = heater_off;
        }
    }
}