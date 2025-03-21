#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <driver/adc.h>
#include "driver/ledc.h"
// #include <driver/i2c.h>
#include <driver/uart.h>
#include "ssd1306.h"
#include <esp_log.h>
#include <math.h>
#include "driver/mcpwm.h"
#include <driver/i2c_master.h>
#include <esp_adc/adc_oneshot.h>

#include <iot_button.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>

#include "ssd1306.h"
// #include "font8x8_basic.h"
#include "string.h"

#include <app_reset.h>
#include <ws2812_led.h>
#include "app_priv.h"

// #include "DFRobotDFPlayerMini.h"

static const char *TAG = "app_driver";

static float g_temperature;

// static ssd1306_handle_t ssd1306_dev = NULL;
// Biến toàn cục để lưu handle của bus I2C
i2c_master_bus_handle_t i2c_bus_handle;

// Biến toàn cục để lưu cấu hình OLED
static SSD1306_t oled_dev;

sensorData last_data;

/* Hàm đọc giá trị từ cảm biến nhiệt độ */
float read_temp_sensor()
{
    static float delta = 0.5;
    g_temperature += delta;
    if (g_temperature > 99)
    {
        delta = -0.5;
    }
    else if (g_temperature < 1)
    {
        delta = 0.5;
    }

    // Ghi log giá trị
    return DEFAULT_TEMPERATURE;
}

/* Hàm đọc giá trị từ cảm biến pH */
float read_ph_sensor()
{
    int raw = adc1_get_raw(PH_SENSOR_GPIO);
    float voltage = raw * (3.3 / 4095); // Chuyển đổi giá trị ADC thành điện áp (0-3.3V)

    // Tính giá trị pH dựa trên công thức
    float ph = 20.5940 - (5.4450 * voltage);

    // Ghi log giá trị
    return ph;
}

/* Hàm đọc giá trị từ cảm biến độ đục */
float read_turbidity_sensor()
{
    // Đọc giá trị ADC
    int raw = adc1_get_raw(TURBIDITY_SENSOR_GPIO);
    float voltage = raw * (3.3 / 4095); // Chuyển đổi giá trị ADC thành điện áp (3.3V tham chiếu)

    // Lấy trung bình 800 lần đọc để giảm nhiễu
    float sum_voltage = 0;
    for (int i = 0; i < 800; i++)
    {
        raw = adc1_get_raw(TURBIDITY_SENSOR_GPIO);
        sum_voltage += raw * (3.3 / 4095);
    }
    voltage = sum_voltage / 800;

    // Tính giá trị NTU
    float NTU = 0;
    if (voltage < 0.36)
    {
        NTU = 3000; // Giới hạn giá trị NTU tối đa
    }
    else if (voltage > 1.8)
    {
        NTU = 0; // Giá trị NTU tối thiểu
    }
    else
    {
        NTU = (-1120.4 * (voltage + 2.4) * (voltage + 2.4)) + (5742.3 * (voltage + 2.4)) - 4352.9;
    };
    return NTU;
}

void control_gpio(int gpio, bool state)
{
    gpio_set_level(gpio, state ? 1 : 0);
    ESP_LOGI(TAG, "Drain set to %s", state ? "ON" : "OFF");
}

void set_power_state(int target)
{
    gpio_set_level(SERVO_GPIO, target);
    control_servo(target);
}

void control_servo(int angle)
{
    if (angle == 1)
        angle = ServoMsAvg;
    if (angle == 0)
        angle = ServoMsMax;

    int duty = (int)(100.0 * (angle / 20.0) * 81.91);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    last_data.servo_angle = angle;
    printf("Servo angle set to: %d\n", angle);
}

/*Hàm cấu hình cho servo*/
void init_servo()
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&ledc_timer);
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = SERVO_GPIO,
        .duty = 0,
        .hpoint = 0};
    ledc_channel_config(&ledc_channel);
}

void init_i2c_bus(void)
{
    i2c_master_bus_config_t i2c_mst_config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = I2C_MASTER_SDA_IO, // Sử dụng GPIO từ config
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    esp_err_t ret = i2c_new_master_bus(&i2c_mst_config, &i2c_bus_handle);
    if (ret != ESP_OK)
    {
        printf("Failed to initialize I2C bus: %s\n", esp_err_to_name(ret));
        while (1)
            ;
    }
    printf("I2C bus initialized successfully\n");
}

// Khởi tạo OLED
void init_oled(void)
{
    ESP_LOGI("OLED", "INTERFACE is i2c");
    ESP_LOGI("OLED", "CONFIG_SDA_GPIO=%d", I2C_MASTER_SDA_IO);
    ESP_LOGI("OLED", "CONFIG_SCL_GPIO=%d", I2C_MASTER_SCL_IO);
    ESP_LOGI("OLED", "CONFIG_RESET_GPIO=%d", CONFIG_RESET_GPIO);
    i2c_master_init(&oled_dev, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, CONFIG_RESET_GPIO);

    ESP_LOGI("OLED", "Panel is 128x64");
    ssd1306_init(&oled_dev, 128, 64);
    ssd1306_clear_screen(&oled_dev, false);
    ssd1306_contrast(&oled_dev, 0xff);
    printf("OLED initialized successfully\n");
}

// Hàm hiển thị dữ liệu trên OLED
void display_data(sensorData data)
{
    static sensorData last_displayed_data = {0}; // Lưu dữ liệu hiển thị trước đó
    char buffer[32];

    // So sánh dữ liệu mới với dữ liệu trước đó
    bool need_update = false;
    if (fabs(data.temp - last_displayed_data.temp) >= TEMP_THRESHOLD ||
        fabs(data.ntu - last_displayed_data.ntu) >= TURB_THRESHOLD ||
        fabs(data.ph - last_displayed_data.ph) >= PH_THRESHOLD)
    {
        need_update = true;
    }

    if (!need_update)
    {
        return; // Không cần cập nhật OLED nếu dữ liệu không thay đổi
    }

    // Xóa màn hình trước khi hiển thị dữ liệu mới
    ssd1306_clear_screen(&oled_dev, false);

    // Hiển thị dữ liệu mới
    sprintf(buffer, " Temp: %.2fC", data.temp);
    ssd1306_display_text(&oled_dev, 0, (const uint8_t *)buffer, 14, false);
    sprintf(buffer, " ntu: %.2fNTU", data.ntu);
    ssd1306_display_text(&oled_dev, 1, (const uint8_t *)buffer, 16, false);
    sprintf(buffer, " pH: %.2f", data.ph);
    ssd1306_display_text(&oled_dev, 2, (const uint8_t *)buffer, 8, false);

    // Lưu dữ liệu vừa hiển thị
    last_displayed_data = data;
    printf("Updated OLED display\n");
}

// void init_uart()
// {
//     const uart_config_t uart_config = {
//         .baud_rate = 9600,
//         .data_bits = UART_DATA_8_BITS,
//         .parity = UART_PARITY_DISABLE,
//         .stop_bits = UART_STOP_BITS_1,
//         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
//     };

//     // Configure UART parameters
//     uart_param_config(UART_NUM, &uart_config);

//     // Set TX and RX pins
//     uart_set_pin(UART_NUM, CONFIG_TX_GPIO, CONFIG_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

//     // Install UART driver
//     uart_driver_install(UART_NUM, 2048, 0, 0, NULL, 0);
// }

void app_sensor_init()
{

    // Cấu hình ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(PH_SENSOR_GPIO, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(TURBIDITY_SENSOR_GPIO, ADC_ATTEN_DB_11);

    // Đặt mặc định trạng thái máy xả nước
    gpio_set_level(DRAIN_GPIO, 0);
    // gpio_set_level(PUMP_GPIO, 0);
    gpio_set_level(WATER_LEVEL_GPIO, 0);
}

void app_driver_init()
{

    /* Configure power */
    gpio_config_t servo_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,      // Không cần kéo lên
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // Không cần kéo xuống
        .intr_type = GPIO_INTR_ANYEDGE,
        .pin_bit_mask = ((uint64_t)1 << SERVO_GPIO) | ((uint64_t)1 << MP3_GPIO) | ((uint64_t)1 << DRAIN_GPIO)};
    gpio_config(&servo_conf);
    init_servo();
    init_i2c_bus();
    init_oled();
    // init_i2c();
    // ssd1306_dev = ssd1306_create(I2C_MASTER_NUM, SSD1306_I2C_ADDRESS);
    // ssd1306_refresh_gram(ssd1306_dev);
    // ssd1306_clear_screen(ssd1306_dev, 0x00);
    app_sensor_init();
}
