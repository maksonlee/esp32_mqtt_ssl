/* MQTT over SSL Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_ota_ops.h"
#include <sys/param.h>

#include <sys/time.h> 
#include "esp_netif_sntp.h"
#include "esp_timer.h"


static const char *TAG = "MQTTS_EXAMPLE";
static char *topic = "v1/devices/me/telemetry";
static char *message = "{'ts': %lld, changed': 0, 'device_type': 8, 'fall_detection_type': 0, 'fall_is_walking': 0, 'fall_region_status': 0, 'fall_target_number': 0, 'fall_position_x': -707, 'fall_position_y': 3910, 'fall_position_z': 1184}";
static long long time_now;
static long long time_prev = 0;
static esp_mqtt_client_handle_t client;

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGI(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

long long get_time() {
    struct timeval tv;
	gettimeofday(&tv, NULL);
    long long now = tv.tv_sec * 1000 + (tv.tv_usec + 500) / 1000;
	return now;
}

static void send_message_callback(void* arg)
{
    time_now = get_time();

    if (time_prev != 0) {
        int delta = time_now - time_prev;
        if (delta > 1100) {
            ESP_LOGI(TAG, "%lld, %lld, %d", time_prev, time_now, delta);
        }
    }
    time_prev = time_now;

    char buf[300] = {0};
    sprintf(buf, message, time_now);
    esp_mqtt_client_publish(client, topic, buf, 0, 0, 0);
}

void mqtt_app_start(void *pvParameter)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = CONFIG_BROKER_URI
        },
        .credentials.username = CONFIG_ACCESS_TOKEN,
        .network = {
            .reconnect_timeout_ms = 5000,
            .timeout_ms = 5000
        }
    };

    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);    


    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &send_message_callback
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000000));

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.google.com");
    esp_netif_sntp_init(&config);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        printf("Failed to update system time within 10s timeout");
    }

    xTaskCreatePinnedToCore(&mqtt_app_start, "", 2048, NULL, 20, NULL, 1);
}
