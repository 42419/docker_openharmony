/*
 * Copyright (c) 2024 iSoftStone Education Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdbool.h>

#include "los_task.h"
#include "ohos_init.h"
#include "cmsis_os.h"
#include "config_network.h"
#include "smart_farm.h"
#include "smart_farm_event.h"
#include "su_03t.h"
#include "iot.h"
#include "lcd.h"
#include "picture.h"
#include "adc_key.h"

// 默认WiFi配置
#define DEFAULT_ROUTE_SSID      "xiaomoV1"
#define DEFAULT_ROUTE_PASSWORD  "15139977792"

#define MSG_QUEUE_LENGTH                                16
#define BUFFER_LEN                                      50
#define WIFI_RETRY_DELAY                               5000


/***************************************************************
 * 函数名称: iot_thread
 * 说    明: iot线程
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void iot_thread(void *args) {
  uint8_t mac_address[12] = {0x00, 0xdc, 0xb6, 0x90, 0x01, 0x00,0};

  // 尝试从Flash读取WiFi配置，如果不存在则使用默认配置
  char ssid[32] = {0};
  char password[32] = {0};
  char mac_addr[32] = {0};
  
  FlashDeinit();
  FlashInit();

  // 尝试从Flash获取配置
  if (VendorGet(VENDOR_ID_WIFI_ROUTE_SSID, ssid, sizeof(ssid)) != 0 || 
      strlen(ssid) == 0) {
      strncpy(ssid, DEFAULT_ROUTE_SSID, sizeof(ssid) - 1);
      printf("[WiFi] 使用默认 SSID: %s\n", ssid);
  }
  
  if (VendorGet(VENDOR_ID_WIFI_ROUTE_PASSWD, password, sizeof(password)) != 0 || 
      strlen(password) == 0) {
      strncpy(password, DEFAULT_ROUTE_PASSWORD, sizeof(password) - 1);
      printf("[WiFi] 使用默认密码\n");
  }

  VendorSet(VENDOR_ID_WIFI_MODE, "STA", 3);
  VendorSet(VENDOR_ID_MAC, mac_address, 6);
  VendorSet(VENDOR_ID_WIFI_ROUTE_SSID, ssid, strlen(ssid) + 1);
  VendorSet(VENDOR_ID_WIFI_ROUTE_PASSWD, password, strlen(password) + 1);

  int wifi_retry_count = 0;

reconnect:
  SetWifiModeOff();
  printf("[WiFi] 第 %d 次尝试连接 %s ...\n",
         wifi_retry_count + 1, ssid);

  int ret = SetWifiModeOn();
  if(ret != 0){
    printf("[WiFi] 连接失败, 错误码: %d, 5秒后重试...\n", ret);
    wifi_retry_count++;
    LOS_Msleep(WIFI_RETRY_DELAY);
    goto reconnect;
  }

  unsigned char ip[4] = {0};
  VendorGet(VENDOR_ID_NET_IP, ip, 4);
  printf("\n");
  printf("========================================\n");
  printf("||       网络连接成功                  ||\n");
  printf("||  WiFi: %s\n", ssid);
  printf("||  IP : %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
  printf("||  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
         mac_address[0], mac_address[1], mac_address[2],
         mac_address[3], mac_address[4], mac_address[5]);
  printf("||  EMQX: %s:1883\n", EMQX_HOST_ADDR);
  printf("========================================\n\n");

  mqtt_init();

  while (1) {
    if (!wait_message()) {
      printf("[EMQX] 连接断开, 重新连接 WiFi...\n");
      wifi_retry_count = 0; // Reset retry count for next connection
      goto reconnect;
    }
    LOS_Msleep(1);
  }
}


/***************************************************************
 * 函数名称: smart_farm_thread
 * 说    明: 智慧家居主线程
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void smart_farm_thread(void *arg)
{
    e_iot_data iot_data = {0};
    UINT32 last_upload_tick = 0;
    UINT32 upload_interval;
    UINT32 sensor_interval;
    UINT32 last_sensor_tick = 0;

    upload_interval = LOS_MS2Tick(2000);
    sensor_interval = LOS_MS2Tick(1000);

    i2c_dev_init();
    lcd_dev_init();
    motor_dev_init();
    light_dev_init();
    su03t_init();

    lcd_show_ui();

    while(1)
    {
        UINT32 now = osKernelGetTickCount();

        event_info_t event_info = {0};
        int ret = smart_farm_event_wait(&event_info, 200);
        if(ret == LOS_OK){
            printf("[事件] 类型=%d 数据=%d\n", event_info.event, event_info.data.iot_data);
            switch (event_info.event)
            {
                case event_key_press:
                    smart_farm_key_process(event_info.data.key_no);
                    break;
                case event_iot_cmd:
                    smart_farm_iot_cmd_process(event_info.data.iot_data);
                    if (mqtt_is_connected()) {
                        iot_data.light_state = get_light_state();
                        iot_data.motor_state = get_motor_state();
                        send_msg_to_mqtt(&iot_data);
                        last_upload_tick = osKernelGetTickCount();
                        printf("[IoT] 状态变化，立即上报\n");
                    }
                    break;
                case event_su03t:
                    smart_farm_su03t_cmd_process(event_info.data.su03t_data);
                    break;
               default:break;
            }
        }

        now = osKernelGetTickCount();
        if (now - last_sensor_tick >= sensor_interval) {
            double temp, humi, lum;
            sht30_read_data(&temp, &humi);
            bh1750_read_data(&lum);
            lcd_set_illumination(lum);
            lcd_set_temperature(temp);
            lcd_set_humidity(humi);

            iot_data.illumination = lum;
            iot_data.temperature = temp;
            iot_data.humidity = humi;
            iot_data.light_state = get_light_state();
            iot_data.motor_state = get_motor_state();

            last_sensor_tick = now;
        }

        if (mqtt_is_connected())
        {
            lcd_set_network_state(true);

            if (now - last_upload_tick >= upload_interval) {
                send_msg_to_mqtt(&iot_data);
                last_upload_tick = now;
            }
        } else {
            lcd_set_network_state(false);
        }

        lcd_show_ui();
    }
}

/***************************************************************
 * 函数名称: device_read_thraed
 * 说    明: 设备读取线程
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
// void device_read_thraed(void *arg)
// {
//     double read_data[3] = {0};

//     i2c_dev_init();

//     while(1)
//     {
//         bh1750_read_data(&read_data[0]);
//         sht30_read_data(&read_data[1]);
//         LOS_QueueWrite(m_msg_queue, (void *)&read_data, sizeof(read_data), LOS_WAIT_FOREVER);
//         LOS_QueueWrite(m_su03_msg_queue, (void *)&read_data, sizeof(read_data), LOS_WAIT_FOREVER);
//         LOS_Msleep(500);
//     }
// }

/***************************************************************
 * 函数名称: iot_smart_farm_example
 * 说    明: 开机自启动调用函数
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void iot_smart_farm_example()
{
    printf("========================================\n");
    printf("||         智慧农场 IoT 系统           ||\n");
    printf("========================================\n");

    unsigned int thread_id_1;
    unsigned int thread_id_2;
    unsigned int thread_id_3;
    TSK_INIT_PARAM_S task_1 = {0};
    TSK_INIT_PARAM_S task_2 = {0};
    TSK_INIT_PARAM_S task_3 = {0};
    unsigned int ret = LOS_OK;
    
    smart_farm_event_init();

    task_3.pfnTaskEntry = (TSK_ENTRY_FUNC)iot_thread;
    task_3.uwStackSize = 20480*5;
    task_3.pcName = "iot thread";
    task_3.usTaskPrio = 22;
    ret = LOS_TaskCreate(&thread_id_3, &task_3);
    if (ret != LOS_OK)
    {
        printf("[错误] 任务创建失败 ret:0x%x\n", ret);
        return;
    }

    task_1.pfnTaskEntry = (TSK_ENTRY_FUNC)smart_farm_thread;
    task_1.uwStackSize = 2048;
    task_1.pcName = "smart farm thread";
    task_1.usTaskPrio = 24;
    
    ret = LOS_TaskCreate(&thread_id_1, &task_1);
    if (ret != LOS_OK)
    {
        printf("[错误] 任务创建失败 ret:0x%x\n", ret);
        return;
    }

    task_2.pfnTaskEntry = (TSK_ENTRY_FUNC)adc_key_thread;
    task_2.uwStackSize = 2048;
    task_2.pcName = "key thread";
    task_2.usTaskPrio = 24;
    ret = LOS_TaskCreate(&thread_id_2, &task_2);
    if (ret != LOS_OK)
    {
        printf("[错误] 任务创建失败 ret:0x%x\n", ret);
        return;
    }
}

APP_FEATURE_INIT(iot_smart_farm_example);
