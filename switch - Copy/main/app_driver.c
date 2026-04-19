/* Switch demo implementation using button and RGB slot machine LEDs

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sdkconfig.h>
#include <esp_log.h>
#include <esp_check.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <iot_button.h>
#include <button_gpio.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <driver/gpio.h>
#include <driver/ledc.h>

#include <app_reset.h>

#include "app_priv.h"

/* This is the button that is used for toggling the power */
#define BUTTON_GPIO          CONFIG_EXAMPLE_BOARD_BUTTON_GPIO
#define BUTTON_ACTIVE_LEVEL  0

/* This is the GPIO on which the power will be set */
#define OUTPUT_GPIO    CONFIG_EXAMPLE_OUTPUT_GPIO
static bool g_power_state = DEFAULT_POWER;

#define WIFI_RESET_BUTTON_TIMEOUT       3
#define FACTORY_RESET_BUTTON_TIMEOUT    10

#define SLOT_LED_COUNT               3
#define SLOT_COLOR_COUNT             6
#define SLOT_ANIMATION_DELAY_MS      100
#define SLOT_STARTUP_SPINS           15
#define SLOT_EXTRA_SPINS_MIN         6
#define SLOT_EXTRA_SPINS_VARIATION   8

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} rgb_color_t;

typedef struct {
    int red_gpio;
    int green_gpio;
    int blue_gpio;
    ledc_channel_t red_channel;
    ledc_channel_t green_channel;
} slot_led_config_t;

#ifdef CONFIG_LED_TYPE_RGB
static const char *TAG = "app_driver";
static TaskHandle_t g_slot_task = NULL;

static const rgb_color_t s_slot_colors[SLOT_COLOR_COUNT] = {
    {255,   0,   0},   /* Red */
    {255, 180,   0},   /* Yellow */
    {  0, 255,   0},   /* Green */
    {255,   0, 255},   /* Magenta */
    {  0, 180, 255},   /* Cyan */
    {180, 180, 255},   /* White */
};

static const slot_led_config_t s_slot_leds[SLOT_LED_COUNT] = {
    {CONFIG_RGB_LED_1_RED_GPIO, CONFIG_RGB_LED_1_GREEN_GPIO, CONFIG_RGB_LED_1_BLUE_GPIO, LEDC_CHANNEL_0, LEDC_CHANNEL_1},
    {CONFIG_RGB_LED_2_RED_GPIO, CONFIG_RGB_LED_2_GREEN_GPIO, CONFIG_RGB_LED_2_BLUE_GPIO, LEDC_CHANNEL_2, LEDC_CHANNEL_3},
    {CONFIG_RGB_LED_3_RED_GPIO, CONFIG_RGB_LED_3_GREEN_GPIO, CONFIG_RGB_LED_3_BLUE_GPIO, LEDC_CHANNEL_4, LEDC_CHANNEL_5},
};
#endif

static int power_to_gpio_level(bool state)
{
    return state ? 1 : 0;
}

#ifdef CONFIG_LED_TYPE_RGB
static uint32_t color_to_pwm_duty(uint8_t color)
{
#ifdef CONFIG_RGB_LED_ACTIVE_LEVEL_HIGH
    return color;
#else
    return 255 - color;
#endif
}

static int blue_to_gpio_level(uint8_t blue)
{
    bool is_on = (blue > 0);
#ifdef CONFIG_RGB_LED_ACTIVE_LEVEL_HIGH
    return is_on ? 1 : 0;
#else
    return is_on ? 0 : 1;
#endif
}

static void set_slot_led_color(uint8_t led_index, rgb_color_t color)
{
    if (led_index >= SLOT_LED_COUNT) {
        return;
    }

    const slot_led_config_t *cfg = &s_slot_leds[led_index];
    ledc_set_duty(LEDC_LOW_SPEED_MODE, cfg->red_channel, color_to_pwm_duty(color.red));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, cfg->red_channel);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, cfg->green_channel, color_to_pwm_duty(color.green));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, cfg->green_channel);
    gpio_set_level(cfg->blue_gpio, blue_to_gpio_level(color.blue));
}

static void set_all_slot_leds_off(void)
{
    for (uint8_t i = 0; i < SLOT_LED_COUNT; i++) {
        set_slot_led_color(i, (rgb_color_t){0, 0, 0});
    }
}

static void slot_machine_task(void *arg)
{
    uint8_t color_idx[SLOT_LED_COUNT];

    for (uint8_t i = 0; i < SLOT_LED_COUNT; i++) {
        color_idx[i] = esp_random() % SLOT_COLOR_COUNT;
        set_slot_led_color(i, s_slot_colors[color_idx[i]]);
    }

    for (uint8_t spin = 0; spin < SLOT_STARTUP_SPINS && g_power_state; spin++) {
        for (uint8_t led = 0; led < SLOT_LED_COUNT; led++) {
            color_idx[led] = (color_idx[led] + 1) % SLOT_COLOR_COUNT;
            set_slot_led_color(led, s_slot_colors[color_idx[led]]);
        }
        vTaskDelay(pdMS_TO_TICKS(SLOT_ANIMATION_DELAY_MS));
    }

    for (uint8_t led = 0; led < SLOT_LED_COUNT && g_power_state; led++) {
        uint8_t extra_spins = SLOT_EXTRA_SPINS_MIN + (esp_random() % SLOT_EXTRA_SPINS_VARIATION);
        for (uint8_t spin = 0; spin < extra_spins && g_power_state; spin++) {
            for (uint8_t moving_led = led; moving_led < SLOT_LED_COUNT; moving_led++) {
                color_idx[moving_led] = (color_idx[moving_led] + 1) % SLOT_COLOR_COUNT;
                set_slot_led_color(moving_led, s_slot_colors[color_idx[moving_led]]);
            }
            vTaskDelay(pdMS_TO_TICKS(SLOT_ANIMATION_DELAY_MS));
        }
        color_idx[led] = esp_random() % SLOT_COLOR_COUNT;
        set_slot_led_color(led, s_slot_colors[color_idx[led]]);
    }

    if (!g_power_state) {
        set_all_slot_leds_off();
    }
    g_slot_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t slot_leds_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "Failed to configure LEDC timer");

    uint64_t blue_gpio_mask = 0;
    for (uint8_t i = 0; i < SLOT_LED_COUNT; i++) {
        const slot_led_config_t *cfg = &s_slot_leds[i];
        ledc_channel_config_t red_cfg = {
            .gpio_num = cfg->red_gpio,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = cfg->red_channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = color_to_pwm_duty(0),
            .hpoint = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&red_cfg), TAG, "Failed to configure red PWM channel");

        ledc_channel_config_t green_cfg = {
            .gpio_num = cfg->green_gpio,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = cfg->green_channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = color_to_pwm_duty(0),
            .hpoint = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&green_cfg), TAG, "Failed to configure green PWM channel");

        blue_gpio_mask |= ((uint64_t)1 << cfg->blue_gpio);
    }

    gpio_config_t blue_io_cfg = {
        .pin_bit_mask = blue_gpio_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&blue_io_cfg), TAG, "Failed to configure blue GPIO");

    set_all_slot_leds_off();
    return ESP_OK;
}

static void slot_machine_start(void)
{
    if (g_slot_task != NULL) {
        return;
    }
    if (xTaskCreate(slot_machine_task, "slot_machine", 4096, NULL, 5, &g_slot_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start slot machine task");
        g_slot_task = NULL;
    }
}
#endif

static void push_btn_cb(void *arg, void *data)
{
    bool new_state = !g_power_state;
    app_driver_set_state(new_state);
#ifdef CONFIG_EXAMPLE_ENABLE_TEST_NOTIFICATIONS
    /* This snippet has been added just to demonstrate how the APIs esp_rmaker_param_update_and_notify()
     * and esp_rmaker_raise_alert() can be used to trigger push notifications on the phone apps.
     * Normally, there should not be a need to use these APIs for such simple operations. Please check
     * API documentation for details.
     */
    if (new_state) {
        esp_rmaker_param_update_and_notify(
                esp_rmaker_device_get_param_by_name(switch_device, ESP_RMAKER_DEF_POWER_NAME),
                esp_rmaker_bool(new_state));
    } else {
        esp_rmaker_param_update_and_report(
                esp_rmaker_device_get_param_by_name(switch_device, ESP_RMAKER_DEF_POWER_NAME),
                esp_rmaker_bool(new_state));
        esp_rmaker_raise_alert("Switch was turned off");
    }
#else
    esp_rmaker_param_update_and_report(
            esp_rmaker_device_get_param_by_name(switch_device, ESP_RMAKER_DEF_POWER_NAME),
            esp_rmaker_bool(new_state));
#endif
}

static void set_power_state(bool target)
{
    gpio_set_level(OUTPUT_GPIO, power_to_gpio_level(target));
#ifdef CONFIG_LED_TYPE_RGB
    if (target) {
        slot_machine_start();
    } else {
        set_all_slot_leds_off();
    }
#endif
}

void app_driver_init(void)
{
    button_config_t btn_cfg = {
        .long_press_time = 0,  /* Use default */
        .short_press_time = 0, /* Use default */
    };
    button_gpio_config_t gpio_cfg = {
        .gpio_num = BUTTON_GPIO,
        .active_level = BUTTON_ACTIVE_LEVEL,
        .enable_power_save = false,
    };
    button_handle_t btn_handle = NULL;
    if (iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn_handle) == ESP_OK && btn_handle) {
        /* Register a callback for a button single click event */
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, NULL, push_btn_cb, NULL);
        /* Register Wi-Fi reset and factory reset functionality on same button */
        app_reset_button_register(btn_handle, WIFI_RESET_BUTTON_TIMEOUT, FACTORY_RESET_BUTTON_TIMEOUT);
    }

    /* Configure power */
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 1,
    };
    io_conf.pin_bit_mask = ((uint64_t)1 << OUTPUT_GPIO);
    /* Configure the GPIO */
    gpio_config(&io_conf);

#ifdef CONFIG_LED_TYPE_RGB
    if (slot_leds_init() != ESP_OK) {
        ESP_LOGE(TAG, "Slot LED initialization failed");
    }
#endif

    set_power_state(g_power_state);
}

int app_driver_set_state(bool state)
{
    if(g_power_state != state) {
        g_power_state = state;
        set_power_state(g_power_state);
    }
    return ESP_OK;
}

bool app_driver_get_state(void)
{
    return g_power_state;
}
