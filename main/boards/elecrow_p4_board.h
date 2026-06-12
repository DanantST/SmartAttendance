/**
 * @file elecrow_p4_board.h
 * @brief Board-specific pin definitions for CrowPanel Advanced 7" ESP32-P4
 * Source: Elecrow official GitHub repository
 * Reference: https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen
 */

#ifndef ELECROW_P4_BOARD_H
#define ELECROW_P4_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "hal/gpio_types.h"

/* ==================== Camera Pins (MIPI CSI + SCCB) ==================== */
/* Camera uses MIPI CSI for data, SCCB (I2C) for control */
/* Source: Elecrow factory firmware v0.2.0 documentation */
#define CAM_PIN_XCLK                GPIO_NUM_15
#define CAM_PIN_SCCB_SDA            GPIO_NUM_12
#define CAM_PIN_SCCB_SCL            GPIO_NUM_13
#define CAM_PIN_RESET               GPIO_NUM_8       // Camera Reset
#define CAM_PIN_PWDN                GPIO_NUM_9       // Camera Power Down

/* Camera SCCB shared on I2C_NUM_1 */
#define CAM_SCCB_I2C_PORT           I2C_NUM_1

/* ==================== Display Pins (MIPI-DSI via DPI interface) ==================== */
/* Elecrow CrowPanel 7" uses MIPI-DSI with EK79007 bridge chip.
 * On ESP32-P4, MIPI-DSI is accessed via the DPI (RGB-like) internal bus.
 * These GPIO assignments come from Elecrow's esp_panel_board_custom_conf.h
 * and are used by the DPI controller to feed the internal MIPI-DSI TX. */
#define LCD_PCLK_GPIO               GPIO_NUM_46  /* Was 9, now moved to avoid Camera Reset conflict */
#define LCD_HSYNC_GPIO              GPIO_NUM_46
#define LCD_VSYNC_GPIO              GPIO_NUM_3
#define LCD_DE_GPIO                 GPIO_NUM_17
#define LCD_DATA0_GPIO              GPIO_NUM_10
#define LCD_DATA1_GPIO              GPIO_NUM_11
#define LCD_DATA2_GPIO              GPIO_NUM_12
#define LCD_DATA3_GPIO              GPIO_NUM_13
#define LCD_DATA4_GPIO              GPIO_NUM_14
#define LCD_DATA5_GPIO              GPIO_NUM_21
#define LCD_DATA6_GPIO              GPIO_NUM_47
#define LCD_DATA7_GPIO              GPIO_NUM_48
#define LCD_DATA8_GPIO              GPIO_NUM_45
#define LCD_DATA9_GPIO              GPIO_NUM_38
#define LCD_DATA10_GPIO             GPIO_NUM_39
#define LCD_DATA11_GPIO             GPIO_NUM_40
#define LCD_DATA12_GPIO             GPIO_NUM_41
#define LCD_DATA13_GPIO             GPIO_NUM_42
#define LCD_DATA14_GPIO             GPIO_NUM_2
#define LCD_DATA15_GPIO             GPIO_NUM_1
/* [Fix H1] GPIO 41 is assigned to BOTH LCD_DATA12_GPIO and LCD_RESET_GPIO below.
 * The reset line is held idle during normal operation so LCD_DATA12 wins in practice,
 * but this MUST be verified against the actual CrowPanel schematic before enabling
 * the LCD reset functionality. Do NOT drive LCD_RESET_GPIO while the display is active. */
#define LCD_RESET_GPIO              GPIO_NUM_41

/* ==================== Touch Panel Pins (I2C) ==================== */
/* GT911 capacitive touch controller */
/* Source: Elecrow board_config.h */
#define TOUCH_I2C_SDA               GPIO_NUM_45
#define TOUCH_I2C_SCL               GPIO_NUM_46
#define TOUCH_I2C_PORT              I2C_NUM_0    /* Use I2C_NUM_0 to match touch driver */
#define TOUCH_RESET_PIN             GPIO_NUM_40
#define TOUCH_INT_PIN               GPIO_NUM_42

/* ==================== Audio Pins (I2S for NS4168 Codec) ==================== */
/* Source: Elecrow P4 HMI AI Voice Chat Robot example */
#define AUDIO_I2S_WS                GPIO_NUM_21      // Word Select / LRCLK
#define AUDIO_I2S_BCK               GPIO_NUM_22      // Bit Clock / BCLK
#define AUDIO_I2S_DOUT              GPIO_NUM_23      // Data Out (to speaker)
#define AUDIO_I2S_DIN               GPIO_NUM_25      // Data In (from mic) - not used for playback
#define AUDIO_PA_ENABLE             GPIO_NUM_30      // Power Amplifier enable (HIGH = on)

/* Microphone Pins (PDM) */
#define MIC_PDM_CLK                 GPIO_NUM_24      // PDM clock output
#define MIC_PDM_DATA                GPIO_NUM_26      // PDM data input

/* ==================== SD Card Pins (SDMMC 1-bit mode) ==================== */
/* Elecrow HMI P4 SD Card Pins (via SDMMC) */
/* [Fix H2] SDMMC D1/D2/D3 share GPIOs with LCD_RESET(41), Touch INT(42), and
 * Touch RESET(40). Always configure SDMMC in 1-BIT (D0-only) mode — using 4-bit
 * mode will corrupt the Touch and LCD reset lines during SD transfers. */
/* Validated on CrowPanel v1.0 hardware (Phase 4 testing) */
#define SDMMC_CLK                   GPIO_NUM_43      // SD Clock
#define SDMMC_CMD                   GPIO_NUM_44      // SD Command
#define SDMMC_D0                    GPIO_NUM_39      // SD Data 0 (1-bit mode only)
#define SDMMC_D1                    GPIO_NUM_41      // Shared with LCD Reset — DO NOT USE in 4-bit mode
#define SDMMC_D2                    GPIO_NUM_42      // Shared with Touch INT — DO NOT USE in 4-bit mode
#define SDMMC_D3                    GPIO_NUM_40      // Shared with Touch RESET — DO NOT USE in 4-bit mode

/* ==================== Backlight Control ====================
 * Source: Elecrow board_config.h — BLIGHT_GPIO = 31
 * Validated on CrowPanel v1.0: 30 kHz, 11-bit, duty = (pct != 0) ? (pct*18+200) : 0 */
#define LCD_BACKLIGHT_PIN           GPIO_NUM_31
#define LCD_BACKLIGHT_PWM_CHANNEL   LEDC_CHANNEL_1   /* Use CHANNEL_1 to avoid conflict with camera XCLK on CHANNEL_0 */
#define LCD_BACKLIGHT_PWM_TIMER     LEDC_TIMER_1     /* Use TIMER_1 to avoid conflict with camera XCLK on TIMER_0 */
#define LCD_BACKLIGHT_PWM_FREQ_HZ   30000            /* Validated: 30 kHz */
#define LCD_BACKLIGHT_PWM_RES       LEDC_TIMER_11_BIT /* Validated: 11-bit */
/* Duty formula: duty = (percent != 0) ? ((percent * 18) + 200) : 0 */
#define LCD_BACKLIGHT_DUTY(pct)     ((pct) != 0U ? ((uint32_t)(pct) * 18U + 200U) : 0U)

/* ==================== Battery ==================== */
/* Battery voltage divider output */
#define BATTERY_ADC_PIN             GPIO_NUM_4
#define BATTERY_ADC_ATTEN           ADC_ATTEN_DB_12  // 0-3.3V range

/* ==================== Power Control ==================== */
#define POWER_ENABLE_PIN            GPIO_NUM_32      // Optional power enable
#define POWER_CHG_LED_PIN           GPIO_NUM_33      // Charging indicator (input)

/* ==================== Helper Macros ==================== */
#define GPIO_NC                     GPIO_NUM_NC      // Not Connected

/* ==================== Display Configuration ==================== */
#define DISPLAY_HORIZONTAL_RES      1024
#define DISPLAY_VERTICAL_RES        600
#define DISPLAY_BITS_PER_PIXEL      16               // RGB565

/* ==================== I2C Bus Configuration ==================== */
#define I2C_MASTER_PORT             I2C_NUM_1        // For touch and optional sensors
#define I2C_MASTER_FREQ_HZ          400000           // 400 kHz

/* ==================== Initialization Functions ==================== */
esp_err_t board_init(void);
esp_err_t board_power_init(void);
esp_err_t board_backlight_init(void);
void board_backlight_set(uint8_t percent);


#ifdef __cplusplus
}
#endif

#endif /* ELECROW_P4_BOARD_H */