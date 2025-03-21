#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "esp_wifi.h"
#include <esp_log.h>
#include <nvs_flash.h>

#include <driver/adc.h>
#include "driver/ledc.h"
#include <driver/i2c.h>
#include <driver/uart.h>

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_standard_types.h>

#include <app_network.h>
// #include <app_insights.h>

#include <math.h>
#include <time.h>
#include <string.h>

#include "app_priv.h"
#include "DFRobotDFPlayerMini.h"

static const char *TAG = "app_main";

/* Thêm vào phần khai báo */
esp_rmaker_device_t *temp_sensor_device;
esp_rmaker_device_t *ph_sensor_device;
esp_rmaker_device_t *turbidity_sensor_device;
esp_rmaker_device_t *pump_device;  // Thêm bơm vào
esp_rmaker_device_t *drain_device; // Máy xả nước
esp_rmaker_device_t *servo_device;

// static bool servo_busy = false;

QueueHandle_t display_queue;
QueueHandle_t control_queue;

/* Hàm callback trạng thái của servo */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                          const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx)
    {
        ESP_LOGI(TAG, "Received write request via : %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }
    const char *device_name = esp_rmaker_device_get_name(device);
    const char *param_name = esp_rmaker_param_get_name(param);
    if (strcmp(param_name, ESP_RMAKER_DEF_POWER_NAME) == 0)
    {
        ESP_LOGI(TAG, "Received value = %s for %s - %s",
                 val.val.b ? "true" : "false", device_name, param_name);
        if (strcmp(device_name, "Servo") == 0)
        {
            set_power_state(val.val.b);
            esp_rmaker_param_update_and_report(param, val);
            ESP_LOGI(TAG, "Servo angle changed to: %d (from RainMaker)\n", val.val.b);
        }
    }
    else
    {
        /* Silently ignoring invalid params */
        return ESP_OK;
    }

    return ESP_OK;
}

void task_Sensor(void *pvParameters)
{
    sensorData data;
    while (1)
    {
        data.temp = read_temp_sensor();
        data.ph = read_ph_sensor();
        data.ntu = read_turbidity_sensor();
        data.drain_state = last_data.drain_state;
        data.servo_angle = last_data.servo_angle;

        if (xQueueSend(display_queue, &data, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGI(TAG, "Sensor task: Failed to send to display queue");
        }
        if (xQueueSend(control_queue, &data, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGI(TAG, "Sensor task: Failed to send to control queue");
        }
        ESP_LOGI(TAG, "Sensor task: Sent data to queues");

        vTaskDelay(pdMS_TO_TICKS(10000)); // Delay 10 giây
    }
}

void task_Device_control(void *pvParameters)
{
    sensorData data;
    static bool alert_flag = false;
    static uint32_t last_temp_report_time = 0;
    static uint32_t last_ph_report_time = 0;
    static uint32_t last_turb_report_time = 0;
    static uint32_t last_alert_time = 0;
    const uint32_t MIN_REPORT_INTERVAL_MS = 1000; // 1 giây
    const uint32_t ALERT_INTERVAL_MS = 60000;     // 60 giây

    while (1)
    {
        if (xQueueReceive(control_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            ESP_LOGI(TAG, "Control task: Received data");
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

            // Cập nhật nhiệt độ
            if (fabs(data.temp - last_data.temp) >= TEMP_THRESHOLD &&
                (current_time - last_temp_report_time >= MIN_REPORT_INTERVAL_MS))
            {
                esp_rmaker_param_update_and_report(esp_rmaker_device_get_param_by_name(temp_sensor_device, "Temperature"),
                                                   esp_rmaker_float(data.temp));
                ESP_LOGI(TAG, "Updated Temperature: %.2f", data.temp);
                last_temp_report_time = current_time;
            }

            // Cập nhật pH
            if (fabs(data.ph - last_data.ph) >= PH_THRESHOLD &&
                (current_time - last_ph_report_time >= MIN_REPORT_INTERVAL_MS))
            {
                esp_rmaker_param_update_and_report(esp_rmaker_device_get_param_by_name(ph_sensor_device, "pH"),
                                                   esp_rmaker_float(data.ph));
                ESP_LOGI(TAG, "Updated pH: %.2f", data.ph);
                last_ph_report_time = current_time;
            }

            // Cập nhật độ đục
            if (fabs(data.ntu - last_data.ntu) >= TURB_THRESHOLD &&
                (current_time - last_turb_report_time >= MIN_REPORT_INTERVAL_MS))
            {
                esp_rmaker_param_update_and_report(esp_rmaker_device_get_param_by_name(turbidity_sensor_device, "Turbidity"),
                                                   esp_rmaker_float(data.ntu));
                ESP_LOGI(TAG, "Updated Turbidity: %.2f", data.ntu);
                last_turb_report_time = current_time;
            }

            // Điều khiển GPIO với giới hạn tần suất
            if (data.ph > 8.10 && !alert_flag && (current_time - last_alert_time >= ALERT_INTERVAL_MS))
            {
                esp_rmaker_raise_alert("High turbidity");
                gpio_set_level(MP3_GPIO, true);
                control_gpio(DRAIN_GPIO, true);
                esp_rmaker_param_update_and_report(
                    esp_rmaker_device_get_param_by_name(drain_device, "Power"),
                    esp_rmaker_bool(true));
                alert_flag = true;
                last_alert_time = current_time;
            }
            else if (data.ph < 8.10 && alert_flag && (current_time - last_alert_time >= ALERT_INTERVAL_MS))
            {
                esp_rmaker_raise_alert("Everything is OKE");
                control_gpio(DRAIN_GPIO, false);
                gpio_set_level(MP3_GPIO, false);
                esp_rmaker_param_update_and_report(
                    esp_rmaker_device_get_param_by_name(drain_device, "Power"),
                    esp_rmaker_bool(false));
                alert_flag = false;
                last_alert_time = current_time;
            }

            ESP_LOGI(TAG, "pH: %.2f, Turbidity: %.2f NTU, Temp: %.2f C", data.ph, data.ntu, data.temp);
        }
        else
        {
            ESP_LOGW(TAG, "Control task: No data received");
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void task_Display(void *pvParameters)
{
    sensorData data;
    while (1)
    {
        if (xQueueReceive(display_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            display_data(data); // Hiển thị dữ liệu lên OLED
            ESP_LOGI(TAG, "Display task: Received data from queue\n");
        }
        else
        {
            ESP_LOGI(TAG, "Display task: No data received\n");
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS); // Delay nhỏ để tránh chiếm CPU
    }
}

void task_DFPlay()
{
    bool debug = false;
#if CONFIG_DEBUG_MODE
    debug = true;
#endif
    bool ret = DF_begin(CONFIG_TX_GPIO, CONFIG_RX_GPIO, true, true, debug);
    ESP_LOGI(TAG, "DF_begin=%d", ret);
    if (!ret)
    {
        ESP_LOGE(TAG, "DFPlayer Mini not online.");
        while (1)
        {
            vTaskDelay(1);
        }
    }
    ESP_LOGI(TAG, "DFPlayer Mini online.");
    ESP_LOGI(TAG, "Play first track on 01 folder.");
    // Play the first mp3
}

void init_nvs()
{
    /* Initialize NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void init_rmaker_device()
{
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Multi Device", "Multi Device");

    if (!node)
    {
        ESP_LOGE(TAG, "Could not initialise node. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }

    /* Create a Temperature Sensor device and add the relevant parameters to it */
    temp_sensor_device = esp_rmaker_temp_sensor_device_create("Temperature Sensor", NULL, read_temp_sensor());
    esp_rmaker_node_add_device(node, temp_sensor_device);

    // Thiết bị cảm biến pH
    ph_sensor_device = esp_rmaker_ph_sensor_device_create("pH Sensor", NULL, read_ph_sensor());
    esp_rmaker_node_add_device(node, ph_sensor_device);

    // Thiết bị cảm biến độ đục
    turbidity_sensor_device = esp_rmaker_ntu_sensor_device_create("Turbidity Sensor", NULL, read_turbidity_sensor());
    esp_rmaker_node_add_device(node, turbidity_sensor_device);

    /* Create a Switch device and add the relevant parameters to it */
    servo_device = esp_rmaker_switch_device_create("Servo", NULL, DEFAULT_SWITCH_POWER);
    esp_rmaker_device_add_cb(servo_device, write_cb, NULL);
    esp_rmaker_node_add_device(node, servo_device);

    // Khởi tạo thiết bị máy xả nước
    drain_device = esp_rmaker_switch_device_create("Water Drain", NULL, DEFAULT_SWITCH_POWER);
    // esp_rmaker_device_add_cb(drain_device, write_cb, NULL);
    esp_rmaker_node_add_device(node, drain_device);
}

void app_main()
{
    init_nvs();

    app_driver_init();

    app_network_init();

    init_rmaker_device();

    // Kích hoạt scheduling
    esp_rmaker_schedule_enable();

    setenv("TZ", "UTC-7", 1); // Múi giờ Việt Nam (UTC+7)
    tzset();

    esp_rmaker_start();

    esp_err_t err = app_network_start(POP_TYPE_RANDOM);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not start Wifi. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }

    display_queue = xQueueCreate(5, sizeof(sensorData));
    control_queue = xQueueCreate(5, sizeof(sensorData));

    xTaskCreate(task_Sensor, "Sensor Task", 4096, NULL, 3, NULL);
    xTaskCreate(task_Device_control, "Device Task", 4096, NULL, 2, NULL);
    xTaskCreate(task_Display, "Display Task", 4096, NULL, 2, NULL);
}