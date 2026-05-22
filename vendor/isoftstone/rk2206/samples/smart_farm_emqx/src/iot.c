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
#include <stdlib.h>

#include "MQTTClient.h"
#include "cJSON.h"
#include "cmsis_os2.h"
#include "config_network.h"
#include "iot.h"
#include "los_task.h"
#include "ohos_init.h"
#include "smart_farm_event.h"


#define EMQX_HOST_ADDR "47.99.60.222"

#define MQTT_CLIENT_ID "smart_farm_rk2206"

#define PUBLISH_TOPIC   "smart_farm/report"
#define SUBCRIB_TOPIC   "smart_farm/command"
#define RESPONSE_TOPIC  "smart_farm/response"
#define STATUS_TOPIC    "smart_farm/status"

#define MAX_BUFFER_LENGTH 512
#define MAX_STRING_LENGTH 64

static unsigned char sendBuf[MAX_BUFFER_LENGTH];
static unsigned char readBuf[MAX_BUFFER_LENGTH];

Network network;
MQTTClient client;

static char mqtt_client_id[64]=MQTT_CLIENT_ID;
static char mqtt_hostaddr[64]=EMQX_HOST_ADDR;

static char publish_topic[128] = PUBLISH_TOPIC;
static char subcribe_topic[128] = SUBCRIB_TOPIC;
static char response_topic[128] = RESPONSE_TOPIC;
static char status_topic[128] = STATUS_TOPIC;

static char will_payload[] = "{\"status\":\"offline\"}";
static char online_payload[] = "{\"status\":\"online\"}";

static unsigned int mqttConnectFlag = 0;

extern bool motor_state;
extern bool light_state;
extern bool auto_state;

/***************************************************************
* 函数名称: send_msg_to_mqtt
* 说    明: 发送信息到iot
* 参    数: e_iot_data *iot_data：数据
* 返 回 值: 无
***************************************************************/
void send_msg_to_mqtt(e_iot_data *iot_data) {
  int rc;
  MQTTMessage message;
  char payload[MAX_BUFFER_LENGTH] = {0};
  char str[MAX_STRING_LENGTH] = {0};

  if (mqttConnectFlag == 0) {
    printf("mqtt not connect\n");
    return;
  }

  cJSON *root = cJSON_CreateObject();
  if (root != NULL) {
    sprintf(str, "%5.2fLux", iot_data->illumination);
    cJSON_AddStringToObject(root, "illumination", str);
    sprintf(str, "%5.2f℃", iot_data->temperature);
    cJSON_AddStringToObject(root, "temperature", str);
    sprintf(str, "%5.2f%%", iot_data->humidity);
    cJSON_AddStringToObject(root, "humidity", str);
    if (iot_data->motor_state == true) {
      cJSON_AddStringToObject(root, "motorStatus", "ON");
    } else {
      cJSON_AddStringToObject(root, "motorStatus", "OFF");
    }
    if (iot_data->light_state == true) {
      cJSON_AddStringToObject(root, "lightStatus", "ON");
    } else {
      cJSON_AddStringToObject(root, "lightStatus", "OFF");
    }
    if (iot_data->auto_state == true) {
      cJSON_AddStringToObject(root, "autoStatus", "ON");
    } else {
      cJSON_AddStringToObject(root, "autoStatus", "OFF");
    }

    char *palyload_str = cJSON_PrintUnformatted(root);
    strcpy(payload, palyload_str);

    cJSON_free(palyload_str);
    cJSON_Delete(root);
  }

  message.qos = 0;
  message.retained = 0;
  message.payload = payload;
  message.payloadlen = strlen(payload);

  if ((rc = MQTTPublish(&client, publish_topic, &message)) != 0) {
    printf("Return code from MQTT publish is %d\n", rc);
    mqttConnectFlag = 0;
  } else {
    printf("mqtt publish success:%s\n", payload);
  }
}

/***************************************************************
* 函数名称: set_light_state
* 说    明: 设置灯状态
* 参    数: cJSON *root
* 返 回 值: 无
***************************************************************/
void set_light_state(cJSON *root) {
  cJSON *para_obj = NULL;
  cJSON *status_obj = NULL;
  char *value = NULL;

  event_info_t event={0};
  event.event=event_iot_cmd;

  para_obj = cJSON_GetObjectItem(root, "paras");
  // 修复参数字段名称，云端下发的是"switch"而不是"onoff"
  status_obj = cJSON_GetObjectItem(para_obj, "switch");
  if (status_obj != NULL) {
    value = cJSON_GetStringValue(status_obj);
    if (!strcmp(value, "ON")) {
      event.data.iot_data = IOT_CMD_LIGHT_ON;
      // light_state = true;
    } else if (!strcmp(value, "OFF")) {
      event.data.iot_data = IOT_CMD_LIGHT_OFF;
      // light_state = false;
    }
    smart_farm_event_send(&event);
  }
}

/***************************************************************
* 函数名称: set_motor_state
* 说    明: 设置电机状态
* 参    数: cJSON *root
* 返 回 值: 无
***************************************************************/
void set_motor_state(cJSON *root) {
  cJSON *para_obj = NULL;
  cJSON *status_obj = NULL;
  char *value = NULL;

  event_info_t event={0};
  event.event=event_iot_cmd;

  para_obj = cJSON_GetObjectItem(root, "paras");
  // 修复参数字段名称，云端下发的是"switch"而不是"onoff"
  status_obj = cJSON_GetObjectItem(para_obj, "switch");
  if (status_obj != NULL) {
    value = cJSON_GetStringValue(status_obj);
    if (!strcmp(value, "ON")) {
      // motor_state = true;
      event.data.iot_data = IOT_CMD_MOTOR_ON;
    } else if (!strcmp(value, "OFF")) {
      // motor_state = false;
      event.data.iot_data = IOT_CMD_MOTOR_OFF;
    }
    smart_farm_event_send(&event);
  }
}

/***************************************************************
* 函数名称: set_auto_state
* 说    明: 设置自动模式状态
* 参    数: cJSON *root
* 返 回 值: 无
***************************************************************/
void set_auto_state(cJSON *root) {
  cJSON *para_obj = NULL;
  cJSON *status_obj = NULL;
  char *value = NULL;

  para_obj = cJSON_GetObjectItem(root, "paras");
  // 修复参数字段名称，云端下发的是"switch"而不是"onoff"
  status_obj = cJSON_GetObjectItem(para_obj, "switch");
  if (status_obj != NULL) {
    value = cJSON_GetStringValue(status_obj);
    if (!strcmp(value, "ON")) {
      // auto_state = true;
    } else if (!strcmp(value, "OFF")) {
      // auto_state = false;
    }
  }
}

/***************************************************************
* 函数名称: mqtt_message_arrived
* 说    明: 接收mqtt数据
* 参    数: MessageData *data
* 返 回 值: 无
***************************************************************/
void mqtt_message_arrived(MessageData *data) {
  cJSON *root = NULL;
  cJSON *cmd_name = NULL;
  char *cmd_name_str = NULL;

  printf("Message arrived on topic %.*s: %.*s\n",
         data->topicName->lenstring.len, data->topicName->lenstring.data,
         data->message->payloadlen, data->message->payload);

  root =
      cJSON_ParseWithLength(data->message->payload, data->message->payloadlen);
  if (root != NULL) {
    printf("JSON parsing successful\n");
    cmd_name = cJSON_GetObjectItem(root, "command_name");
    if (cmd_name != NULL) {
      cmd_name_str = cJSON_GetStringValue(cmd_name);
      printf("Received command: %s\n", cmd_name_str);
      if (!strcmp(cmd_name_str, "light_control")) {
        printf("Processing light_control command\n");
        set_light_state(root);
      } else if (!strcmp(cmd_name_str, "motor_control")) {
        printf("Processing motor_control command\n");
        set_motor_state(root);
      } else if (!strcmp(cmd_name_str, "auto_control")) {
        printf("Processing auto_control command\n");
        set_auto_state(root);
      } else {
        printf("Unknown command: %s\n", cmd_name_str);
      }
    } else {
      printf("Command name not found in message\n");
      char *json_str = cJSON_Print(root);
      if (json_str) {
        printf("Full JSON content: %s\n", json_str);
        cJSON_free(json_str);
      }
    }
  } else {
    printf("Failed to parse JSON message\n");
    printf("Message content: %.*s\n", data->message->payloadlen, (char*)data->message->payload);
  }

  cJSON_Delete(root);
}

/***************************************************************
* 函数名称: wait_message
* 说    明: 等待信息
* 参    数: 无
* 返 回 值: 无
***************************************************************/
int wait_message() {
  uint8_t rec = MQTTYield(&client, 5000);
  if (rec != 0) {
    mqttConnectFlag = 0;
  }
  if (mqttConnectFlag == 0) {
    return 0;
  }
  return 1;
}

/***************************************************************
* 函数名称: mqtt_init
* 说    明: mqtt初始化
* 参    数: 无
* 返 回 值: 无
***************************************************************/
void mqtt_init() {
  int rc;

  printf("Starting MQTT connection to EMQX at %s:1883...\n", mqtt_hostaddr);

  NetworkInit(&network);

begin:
  printf("NetworkConnect ...\n");
  NetworkConnect(&network, mqtt_hostaddr, 1883);
  printf("MQTTClientInit ...\n");
  MQTTClientInit(&client, &network, 2000, sendBuf, sizeof(sendBuf), readBuf,
                 sizeof(readBuf));

  MQTTString clientId = MQTTString_initializer;
  clientId.cstring = mqtt_client_id;

  MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
  data.clientID = clientId;
  data.willFlag = 1;
  data.will.topicName.cstring = status_topic;
  data.will.message.cstring = will_payload;
  data.will.qos = 1;
  data.will.retained = 1;
  data.MQTTVersion = 4;
  data.keepAliveInterval = 60;
  data.cleansession = 1;

  printf("MQTTConnect ...\n");
  rc = MQTTConnect(&client, &data);
  if (rc != 0) {
    printf("MQTTConnect: %d\n", rc);
    NetworkDisconnect(&network);
    MQTTDisconnect(&client);
    osDelay(200);
    goto begin;
  }

  printf("MQTTSubscribe ...\n");
  rc = MQTTSubscribe(&client, subcribe_topic, 0, mqtt_message_arrived);
  if (rc != 0) {
    printf("MQTTSubscribe: %d\n", rc);
    osDelay(200);
    goto begin;
  }

  mqttConnectFlag = 1;

  MQTTMessage status_msg;
  status_msg.qos = 1;
  status_msg.retained = 1;
  status_msg.payload = online_payload;
  status_msg.payloadlen = strlen(online_payload);
  rc = MQTTPublish(&client, status_topic, &status_msg);
  if (rc != 0) {
    printf("Online status publish failed: %d\n", rc);
  }

  printf("MQTT connected to EMQX successfully!\n");
}

/***************************************************************
* 函数名称: mqtt_is_connected
* 说    明: mqtt连接状态
* 参    数: 无
* 返 回 值: unsigned int 状态
***************************************************************/
unsigned int mqtt_is_connected() { return mqttConnectFlag; }
