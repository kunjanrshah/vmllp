#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"

#include "esp_http_client.h"

#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"

#include <stdio.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "freertos/queue.h"

#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_spiffs.h"

#define ECHO_TEST_TXD 17
#define ECHO_TEST_RXD 16
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM 2 //(CONFIG_EXAMPLE_UART_PORT_NUM)

#define ECHO_TASK_STACK_SIZE 4096 //  (CONFIG_EXAMPLE_TASK_STACK_SIZE)

// static const char *TAG = "UART TEST";

#define BUF_SIZE (1024)

#define EXAMPLE_ESP_WIFI_CHANNEL 1
#define EXAMPLE_MAX_STA_CONN 4

#define MAX_HTTP_OUTPUT_BUFFER 100
static const char *TAG = "HTTP_CLIENT_VMLLP";

typedef struct
{
    char *data;
} example_espnow_send_param_t;

static EventGroupHandle_t s_wifi_event_group;

static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;

esp_err_t _http_event_handler(esp_http_client_event_t *evt);
// static void http_test_task(char *pvParameters);
static void http_rest_with_url(uint8_t *post_data);
static const int RX_BUF_SIZE = 500;

#define TXD_PIN 17
#define RXD_PIN 16

void init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_2, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_2, &uart_config);
    uart_set_pin(UART_NUM_2, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

uint8_t data[300] = {0};

static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    while (1)
    {
        const int rxBytes = uart_read_bytes(UART_NUM_2, data, RX_BUF_SIZE, 500 / portTICK_RATE_MS);
        if (rxBytes > 1)
        {
            data[rxBytes] = 0;
            ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            http_rest_with_url(data);
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

static void smartconfig_example_task(void *parm);

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        //  ESP_LOGI(TAG, "HTTP_EVENT_DATA, data=%s", (char *)evt->data);

        if (esp_http_client_is_chunked_response(evt->client))
        {
            printf("%.*s\n", evt->data_len, (char *)evt->data);
        }

        // /*
        //  *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
        //  *  However, event handler can also be used in case chunked encoding is used.
        //  */
        // if (!esp_http_client_is_chunked_response(evt->client))
        // {
        //     // If user_data buffer is configured, copy the response into the buffer
        //     if (evt->user_data)
        //     {
        //         memcpy(evt->user_data + output_len, evt->data, evt->data_len);
        //     }
        //     else
        //     {
        //         if (output_buffer == NULL)
        //         {
        //             output_buffer = (char *)malloc(esp_http_client_get_content_length(evt->client));
        //             output_len = 0;
        //             if (output_buffer == NULL)
        //             {
        //                 ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
        //                 return ESP_FAIL;
        //             }
        //         }
        //         memcpy(output_buffer + output_len, evt->data, evt->data_len);
        //     }
        //     output_len += evt->data_len;
        // }

        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL)
        {
            // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
        if (err != 0)
        {
            if (output_buffer != NULL)
            {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
        }
        break;
    }
    return ESP_OK;
}

static void http_rest_with_url(uint8_t *post_data)
{
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {'\0'}; // Buffer to store response of http request
    esp_http_client_config_t config = {
        .url = "http://superbinstruments.com/export-product/addUpdateProduct.php",
        .method = HTTP_METHOD_POST,
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // POST
    //  const char *post_data = "{\"srno\":\"5109\",\"size\":\"sss\",\"finish\":\"fff\",\"alloy\":\"aaa\",\"temper\":\"ttt\",\"gross_weight\":\"ggg\",\"tare_weight\":\"ttt\",\"net_weight\":\"nnn\",\"method\":\"add\"}";
    // printf("Enter POST Data: %s", post_data);
    // ESP_LOGI(TAG, "POST Data: %s", post_data);
    //   ESP_LOGE(TAG, "POST Data LENGTH: %d", strlen(post_data));

    // if (strlen(post_data) > 10)
    // {
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, (char *)post_data, strlen((char *)post_data));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
        if (data_read >= 0)
        {
            ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %lld",
                     esp_http_client_get_status_code(client),
                     esp_http_client_get_content_length(client));

                    if(esp_http_client_get_status_code(client)==200){
                        //TODO LED ON
                    }

            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, data_read);
            // ESP_LOGE(TAG, "%s", output_buffer);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to read response");
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    // }
    // else
    // {
    //     ESP_LOGE(TAG, "POST Data is EMPTY: %s", post_data);
    // }

    esp_http_client_cleanup(client);
}

TaskHandle_t rxTask = NULL;

static void smartconfig_example_task(void *parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
    while (1)
    {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if (uxBits & CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if (uxBits & ESPTOUCH_DONE_BIT)
        {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            // if (rxTask == NULL)
            //{
            xTaskCreate(rx_task, "uart_rx_task", 1024 * 4, NULL, configMAX_PRIORITIES, &rxTask);
            // }
            vTaskDelete(NULL);
        }
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
    {
        ESP_LOGI(TAG, "Scan done");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
    {
        ESP_LOGI(TAG, "Found channel");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
    {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = {0};
        uint8_t password[65] = {0};

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true)
        {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        ESP_LOGI(TAG, "Opening file");
        FILE *f = fopen("/spiffs/vmllp_ssid.txt", "w");
        if (f == NULL)
        {
            ESP_LOGE(TAG, "Failed to open file for writing");
            return;
        }
        fprintf(f, (char *)ssid);
        fclose(f);
        ESP_LOGI(TAG, "SSID in vmllp_ssid file written");

        ESP_LOGI(TAG, "Opening file");
        FILE *f1 = fopen("/spiffs/vmllp_password.txt", "w");
        if (f1 == NULL)
        {
            ESP_LOGE(TAG, "Failed to open file for writing");
            return;
        }
        fprintf(f1, (char *)password);
        fclose(f1);
        ESP_LOGI(TAG, "Password in vmllp_password file written");

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        esp_wifi_connect();
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
    {
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
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define EXAMPLE_ESP_MAXIMUM_RETRY 5
// static const char *TAG = "wifi station";

static int s_retry_num = 0;

static void event_handler1(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_sta(char *ssid, char *password)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler1, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler1, NULL, &instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config_t));
    memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi_config.sta.password, password, strlen(password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap ");
        if (rxTask == NULL)
        {
            xTaskCreate(rx_task, "uart_rx_task", 1024 * 4, NULL, configMAX_PRIORITIES, &rxTask);
        }

        // xTaskCreate(echo_task, "uart_echo_task", 2048, NULL, 10, NULL);
        // xTaskCreate(&http_test_task, "http_test_task", 8192, NULL, 5, NULL);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID");
        // Check if destination file exists before renaming
        struct stat st;
        if (stat("/spiffs/vmllp_ssid.txt", &st) == 0)
        {
            // Delete it if it exists
            unlink("/spiffs/vmllp_ssid.txt");
            unlink("/spiffs/vmllp_password.txt");
        }
    } 
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init();

    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file");
    FILE *f = fopen("/spiffs/vmllp_ssid.txt", "r");
    FILE *f1 = fopen("/spiffs/vmllp_password.txt", "r");
    if (f == NULL || f1 == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        initialise_wifi();
    }
    else
    {
        char line[64];
        fgets(line, sizeof(line), f);
        fclose(f);
        // strip newline
        char *pos = strchr(line, '\n');
        if (pos)
        {
            *pos = '\0';
        }
        ESP_LOGI(TAG, "Read SSID from file: '%s'", line);

        char line1[64];
        fgets(line1, sizeof(line), f1);
        fclose(f1);
        // strip newline
        char *pos1 = strchr(line1, '\n');
        if (pos1)
        {
            *pos1 = '\0';
        }
        ESP_LOGI(TAG, "Read Password from file: '%s'", line1);

        wifi_init_sta(line, line1);
    }
}
