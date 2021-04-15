/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
//#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "mid_wifi.h"

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "smartconfig_example";

typedef enum {
    wifi_unconfiged = 0,
    wifi_configed   = 0xAA,
}wifi_info_storage_t;


static void smartconfig_example_task(void * parm);
static void check_wifi_config_in_nvs(void);


static void event_handler(void* arg, esp_event_base_t event_base, 
                                int32_t event_id, void* event_data)
{
    //if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        //不再此处创建空中配网的任务
        //xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
    //} else 
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        //增加链接上WiFi后的信息提示
        ESP_LOGI(TAG, "Wifi Connected!");
    }  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
        ESP_LOGI(TAG, "Wifi disconnect,try to reconnect...\r\n!");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        //存放当前的配网信息
        nvs_handle_t wificonfig_set_handle;
        ESP_ERROR_CHECK( nvs_open("wificonfig",NVS_READWRITE,&wificonfig_set_handle) );
        ESP_ERROR_CHECK( nvs_set_u8(wificonfig_set_handle,"WifiConfigFlag", wifi_configed) );
        ESP_ERROR_CHECK( nvs_set_str(wificonfig_set_handle,"SSID",(const char *)ssid) );
        ESP_ERROR_CHECK( nvs_set_str(wificonfig_set_handle,"PASSWORD", (const char *)password) );
        ESP_ERROR_CHECK( nvs_commit(wificonfig_set_handle) );
        nvs_close(wificonfig_set_handle);

        //使用获取的配网信息链接无线网络
        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
        ESP_ERROR_CHECK( esp_wifi_connect() );
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}


static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY); 
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}


static void check_wifi_config_in_nvs(void)
{
    nvs_handle_t wificonfig_get_handle;
    wifi_config_t wifi_config;
    esp_err_t err;
    uint8_t u8WifiConfigVal = 0;
    uint8_t u8Ssid[33] = { 0 };
    uint8_t u8Password[65] = { 0 };
    size_t Len = 0;
    uint8_t u8GetWifiFlag = 0;

    bzero(&wifi_config, sizeof(wifi_config_t));

    nvs_open("wificonfig", NVS_READWRITE, &wificonfig_get_handle);
    nvs_get_u8(wificonfig_get_handle, "WifiConfigFlag", &u8WifiConfigVal);
    printf("wificonfigval:%X \r\n",u8WifiConfigVal);
    if (u8WifiConfigVal == wifi_configed)
    {
        Len = sizeof(u8Ssid);
        err = nvs_get_str(wificonfig_get_handle, "SSID", (char *)u8Ssid, &Len);
        if(err == ESP_OK)
        {
            memcpy(wifi_config.sta.ssid, u8Ssid, sizeof(wifi_config.sta.ssid));
            ESP_LOGI(TAG, "ssid:%s,len:%d",u8Ssid,Len);
            u8GetWifiFlag ++;
        }
        Len = sizeof(u8Password);
        err = nvs_get_str(wificonfig_get_handle, "PASSWORD",(char *)u8Password,&Len);
        if(err == ESP_OK)
        {
            memcpy(wifi_config.sta.password, u8Password, sizeof(wifi_config.sta.password));
            ESP_LOGI(TAG, "password:%s,len:%d",u8Password,Len);
            u8GetWifiFlag ++;
        }
        nvs_commit(wificonfig_get_handle);
        nvs_close(wificonfig_get_handle);

        initialise_wifi();

        if(u8GetWifiFlag == 2)
        {
            //使用获取的配网信息链接无线网络
            ESP_ERROR_CHECK( esp_wifi_disconnect() );
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_connect() );
        }
        else
        {
            xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
        }
    }
    else
    {
        nvs_commit(wificonfig_get_handle);
        nvs_close(wificonfig_get_handle);

        initialise_wifi();
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
        ESP_LOGI(TAG, "Get WifiConfig Fail,Start SmartConfig......");
    }

}



void smartConfig_wifi(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    check_wifi_config_in_nvs();
}


void test_clear_wifi_param(void)
{
    nvs_handle_t wificonfig_set_handle;
    ESP_ERROR_CHECK( nvs_open("wificonfig",NVS_READWRITE,&wificonfig_set_handle) );
    ESP_ERROR_CHECK( nvs_set_u8(wificonfig_set_handle,"WifiConfigFlag", wifi_unconfiged) );
    ESP_ERROR_CHECK( nvs_commit(wificonfig_set_handle) );
    nvs_close(wificonfig_set_handle);
    ESP_LOGI(TAG,"Set Restart now.\n");
    esp_restart(); 
}
