#pragma once
#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_SWITCH_POWER false
/* GPIO cho cảm biến */
#define PH_SENSOR_GPIO ADC1_CHANNEL_7
#define TURBIDITY_SENSOR_GPIO ADC1_CHANNEL_6
#define WATER_LEVEL_GPIO 13 // Cảm biến mực nước (Digital)

#define PUMP_GPIO 27  // Thêm GPIO cho bơmS
#define DRAIN_GPIO 19 // Máy xả nước
#define SERVO_GPIO 5  // GPIO cho servo
#define MP3_GPIO GPIO_NUM_18

// #define CONFIG_TX_GPIO GPIO_NUM_16
// #define CONFIG_RX_GPIO GPIO_NUM_17
// #define UART_NUM UART_NUM_1

/* Chu kỳ cập nhật cảm biến (giây) */
#define SENSOR_UPDATE_PERIOD 15000

#define TEMP_THRESHOLD 0.5 // Nhiệt độ thay đổi 0.5°C
#define TURB_THRESHOLD 0.1 // Độ đục thay đổi 0.1V
#define PH_THRESHOLD 0.1   // pH thay đổi 0.1

#define DEFAULT_TEMPERATURE 26.0
#define DEFAULT_PH 7.0
#define DEFAULT_NTU 0.0
#define REPORTING_PERIOD 60 /* Seconds */

#define ServoMsMin 0.06
#define ServoMsMax 2.1
#define ServoMsAvg ((ServoMsMax - ServoMsMin) / 2.0)

// Định nghĩa cấu hình I2C
#define I2C_MASTER_SCL_IO 22      /*!< GPIO cho I2C clock */
#define I2C_MASTER_SDA_IO 21      /*!< GPIO cho I2C data  */
#define I2C_MASTER_NUM I2C_NUM_1  /*!< I2C port number */
#define I2C_MASTER_FREQ_HZ 100000 /*!< Tần số clock I2C */

typedef struct
{
    float temp;
    float ntu;
    float ph;
    bool drain_state;
    int servo_angle;
} sensorData;

extern esp_rmaker_device_t *temp_sensor_device;

extern esp_rmaker_device_t *ph_sensor_device;
extern esp_rmaker_device_t *turbidity_sensor_device;
extern esp_rmaker_device_t *pump_device;  // Thêm bơm vào
extern esp_rmaker_device_t *drain_device; // Máy xả nước
extern esp_rmaker_device_t *servo_device;

extern QueueHandle_t control_queue;
extern QueueHandle_t display_queue;

extern sensorData last_data;

void app_driver_init();
float read_temp_sensor();
float read_turbidity_sensor();
float read_ph_sensor();
void display_data(sensorData data);
void set_power_state(int target);
void control_gpio(int gpio, bool state);
void control_servo(int angle);
