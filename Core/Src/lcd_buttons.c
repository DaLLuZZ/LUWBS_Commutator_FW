#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "task.h"
#include "stdio.h"
#include "main.h"
#include "gpio.h"
#include "i2c.h"
#include "nvs_settings.h"
#include "lcd_buttons.h"

static osThreadId_t buttonTaskHandle;
static uint32_t buttonTaskBuffer[1024];
static StaticTask_t buttonTaskControlBlock;
static const osThreadAttr_t buttonTask_attributes = {
    .name = "buttonTask",
    .cb_mem = &buttonTaskControlBlock,
    .cb_size = sizeof(buttonTaskControlBlock),
    .stack_mem = &buttonTaskBuffer[0],
    .stack_size = sizeof(buttonTaskBuffer),
    .priority = (osPriority_t)(osPriorityIdle + 1),
};

static void button_left_pressed_cb(void);
static void button_mid_pressed_cb(void);
static void button_right_pressed_cb(void);
static void button_left_released_cb(void);
static void button_mid_released_cb(void);
static void button_right_released_cb(void);
static GPIO_PinState button_left_read(void);
static GPIO_PinState button_mid_read(void);
static GPIO_PinState button_right_read(void);

static void lcd_send_cmd(char cmd);
static void lcd_send_data(char data);
static char lcd_read_data(void);
static void lcd_put_cursor(lcd_ctx_t* lcd_ctx, int row, int col);
static void lcd_send_string(lcd_ctx_t* lcd_ctx, char* str);
static void lcd_read_string(lcd_ctx_t* lcd_ctx, char* str, size_t size);

// LCD FSM
static uint8_t lcd_buf_a[32];
static uint8_t lcd_buf_b[32];
static uint8_t lcd_canary[] = LCD_HEALTH_CHECK_CANARY_STRING;
static lcd_ctx_t lcd_ctx = {
    .lcd_state = LCD_STATE_INVALID,
    .lcd_timestamp_ms = 0,
    .lcd_health_timestamp_ms = 0,
    .lcd_health_canary = (char*)lcd_canary,
    .lcd_cursor = {
        .pos = 0,
        .blink = 0,
        .reserved = 0,
    },
    .lcd_upd_flag = 0,
    .lcd_line_main = (char*)lcd_buf_a,
    .lcd_line_secondary = (char*)lcd_buf_b,
    .lcd_cmd = lcd_send_cmd,
    .lcd_write = lcd_send_data,
    .lcd_read = lcd_read_data,
    .button_left = {
        .state = BUTTON_STATE_RELEASED,
        .timestamp_ms = 0,
        .pressed_cb = button_left_pressed_cb,
        .released_cb = button_left_released_cb,
        .read_pin = button_left_read,
    },
    .button_mid = {
        .state = BUTTON_STATE_RELEASED,
        .timestamp_ms = 0,
        .pressed_cb = button_mid_pressed_cb,
        .released_cb = button_mid_released_cb,
        .read_pin = button_mid_read,
    },
    .button_right = {
        .state = BUTTON_STATE_RELEASED,
        .timestamp_ms = 0,
        .pressed_cb = button_right_pressed_cb,
        .released_cb = button_right_released_cb,
        .read_pin = button_right_read,
    },
};

// redefine weak funciton from stm32f4xx_hal_gpio.c
// attention! this function is called from irq context
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    button_ctx_t* button_ctx;

    switch (GPIO_Pin)
    {
        case INPUT_LEFT_Pin:
        {
            button_ctx = &lcd_ctx.button_left;
            break;
        }
        case INPUT_MID_Pin:
        {
            button_ctx = &lcd_ctx.button_mid;
            break;
        }
        case INPUT_RIGHT_Pin:
        {
            button_ctx = &lcd_ctx.button_right;
            break;
        }
        default:
        {
            // ignore interrupts from unknown pins
            return;
        }
    }

    if (button_ctx->state == BUTTON_STATE_RELEASED)
    {
        button_ctx->timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
        button_ctx->state = BUTTON_STATE_PENDING;
    }
}

static void lcd_init(lcd_ctx_t* lcd_ctx)
{
    if (!lcd_ctx)
    {
        return;
    }

    // 4 bit initialisation
    osDelay(100); // wait for 40+ ms
    lcd_ctx->lcd_cmd(0x30);
    osDelay(10); // wait for > 4.1+ ms
    lcd_ctx->lcd_cmd(0x30);
    osDelay(5); // wait for > 100+ us
    lcd_ctx->lcd_cmd(0x30);
    osDelay(10);
    lcd_ctx->lcd_cmd(0x20); // 4bit mode
    osDelay(10);
    lcd_ctx->lcd_cmd(0x20); // 4bit mode again
    osDelay(10);

    // display initialisation
    lcd_ctx->lcd_cmd(0x28); // Function set --> DL=0 (4 bit mode), N = 1 (2 line display) F = 0 (5x8 characters)
    osDelay(10);
    lcd_ctx->lcd_cmd(0x08); // Display on/off control --> D=0,C=0, B=0  ---> display off
    osDelay(10);
    lcd_ctx->lcd_cmd(0x01); // clear display
    osDelay(20);
    lcd_ctx->lcd_cmd(0x06); // Entry mode set --> I/D = 1 (increment cursor) & S = 0 (no shift)
    osDelay(10);
    lcd_ctx->lcd_cmd(0x0C); // Display on/off control --> D = 1, C and B = 0. (Cursor and blink, last two bits)

    // init display canaries for health-check algorithm
    osDelay(100);
    lcd_put_cursor(lcd_ctx, LCD_LINE_MAIN_ROW_INDEX, LCD_SYMBOLS_PER_LINE);
    lcd_send_string(lcd_ctx, lcd_ctx->lcd_health_canary);
    lcd_put_cursor(lcd_ctx, LCD_LINE_SECONDARY_ROW_INDEX, LCD_SYMBOLS_PER_LINE);
    lcd_send_string(lcd_ctx, lcd_ctx->lcd_health_canary);
}

static void button_process_state(button_ctx_t* button_ctx)
{
    if (!button_ctx)
    {
        return;
    }

    switch (button_ctx->state)
    {
        case BUTTON_STATE_PRESSED:
        {
            uint32_t timedelta = GET_CURRENT_TIMESTAMP_MS() - button_ctx->timestamp_ms;
            GPIO_PinState pin_state = button_ctx->read_pin();

            if (pin_state == BUTTON_READ_PRESSED)
            {
                button_ctx->timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }
            else if (timedelta > BUTTON_DEBOUNCE_TIME_MS && pin_state == BUTTON_READ_RELEASED)
            {
                button_ctx->state = BUTTON_STATE_RELEASED;
                button_ctx->released_cb();
                button_ctx->timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }

            break;
        }
        case BUTTON_STATE_PENDING:
        {
            uint32_t timedelta = GET_CURRENT_TIMESTAMP_MS() - button_ctx->timestamp_ms;
            GPIO_PinState pin_state = button_ctx->read_pin();

            if (pin_state == BUTTON_READ_RELEASED)
            {
                button_ctx->state = BUTTON_STATE_RELEASED;
                button_ctx->timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }
            else if (timedelta > BUTTON_DEBOUNCE_TIME_MS && pin_state == BUTTON_READ_PRESSED)
            {
                button_ctx->state = BUTTON_STATE_PRESSED;
                button_ctx->pressed_cb();
                button_ctx->timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }

            break;
        }
        case BUTTON_STATE_RELEASED:
        {
            break;
        }
        default:
        {
            return; // ignore unknown button states
        }
    }
}

static void lcd_set_state(lcd_ctx_t* lcd_ctx, lcd_state_t new_state)
{
    if (!lcd_ctx || lcd_ctx->lcd_state == new_state || new_state == LCD_STATE_INVALID || new_state >= LCD_STATE_UNKNOWN)
    {
        return; // nothing to do
    }

    lcd_ctx->lcd_state = new_state;

    switch (lcd_ctx->lcd_state)
    {
        case LCD_STATE_SHOW_GREATING:
        {
            snprintf(lcd_ctx->lcd_line_main,      LCD_LINE_LEN, "     Hello!     ");
            snprintf(lcd_ctx->lcd_line_secondary, LCD_LINE_LEN, "Press any button");
            lcd_ctx->lcd_upd_flag = 1;
            break;
        }
        case LCD_STATE_SHOW_IP_ADDRESS:
        case LCD_STATE_SHOW_SUBNET_MASK:
        case LCD_STATE_SHOW_GATEWAY:
        {
            nvs_settings_ip_addr_t addr;
            if (lcd_ctx->lcd_state == LCD_STATE_SHOW_IP_ADDRESS)
            {
                NVS_SETTING_GET(ip_addr, &addr.value);
            }
            else if (lcd_ctx->lcd_state == LCD_STATE_SHOW_SUBNET_MASK)
            {
                NVS_SETTING_GET(subnet_mask, &addr.value);
            }
            else if (lcd_ctx->lcd_state == LCD_STATE_SHOW_GATEWAY)
            {
                NVS_SETTING_GET(gateway, &addr.value);
            }
            snprintf(lcd_ctx->lcd_line_main,      LCD_LINE_LEN, LCD_IP_ADDR_FORMAT_STRING, addr.byte[0], addr.byte[1], addr.byte[2], addr.byte[3]);
            snprintf(lcd_ctx->lcd_line_secondary, LCD_LINE_LEN, "[<]  [EDIT]  [>]");
            lcd_ctx->lcd_cursor.blink = 0;
            lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            lcd_ctx->lcd_upd_flag = 1;
            break;
        }
        case LCD_STATE_SHOW_IP_MODE:
        {
            // TODO: show real value
            snprintf(lcd_ctx->lcd_line_main,      LCD_LINE_LEN, "IP mode:  Static");
            snprintf(lcd_ctx->lcd_line_secondary, LCD_LINE_LEN, "[<]  [EDIT]  [>]");
            lcd_ctx->lcd_upd_flag = 1;
            break;
        }
        case LCD_STATE_CONFIG_IP_ADDRESS:
        case LCD_STATE_CONFIG_SUBNET_MASK:
        case LCD_STATE_CONFIG_GATEWAY:
        {
            nvs_settings_ip_addr_t addr;
            if (lcd_ctx->lcd_state == LCD_STATE_CONFIG_IP_ADDRESS)
            {
                NVS_SETTING_GET(ip_addr, &addr.value);
            }
            else if (lcd_ctx->lcd_state == LCD_STATE_CONFIG_SUBNET_MASK)
            {
                NVS_SETTING_GET(subnet_mask, &addr.value);
            }
            else if (lcd_ctx->lcd_state == LCD_STATE_CONFIG_GATEWAY)
            {
                NVS_SETTING_GET(gateway, &addr.value);
            }
            snprintf(lcd_ctx->lcd_line_main,      LCD_LINE_LEN, LCD_IP_ADDR_FORMAT_STRING, addr.byte[0], addr.byte[1], addr.byte[2], addr.byte[3]);
            snprintf(lcd_ctx->lcd_line_secondary, LCD_LINE_LEN, "[+]  [SAVE]  [>]");
            lcd_ctx->lcd_cursor.pos = LCD_IP_ADDR_START_POS;
            lcd_ctx->lcd_cursor.blink = 0;
            lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            lcd_ctx->lcd_upd_flag = 1;
            break;
        }
        case LCD_STATE_CONFIG_IP_MODE:
        {
            // TODO: show real value
            snprintf(lcd_ctx->lcd_line_main,      LCD_LINE_LEN, "IP mode:  Static");
            snprintf(lcd_ctx->lcd_line_secondary, LCD_LINE_LEN, "[<]  [SAVE]  [>]");
            lcd_ctx->lcd_upd_flag = 1;
            break;
        }
        default:
        {
            return; // ignore unknown lcd state codes
        }
    }
}

static void lcd_set_prev_show_screen_state(lcd_ctx_t* lcd_ctx)
{
    if (!lcd_ctx)
    {
        return;
    }

    lcd_state_t state = lcd_ctx->lcd_state;

    if (state <= LCD_STATE_SHOW_MIN || state >= LCD_STATE_SHOW_MAX)
    {
        return; // nothing to do
    }

    state--;

    // cycle screens
    if (state <= LCD_STATE_SHOW_MIN)
    {
        state = LCD_STATE_SHOW_MAX - 1;
    }

    lcd_set_state(lcd_ctx, state);
}

static void lcd_set_next_show_screen_state(lcd_ctx_t* lcd_ctx)
{
    if (!lcd_ctx)
    {
        return;
    }

    lcd_state_t state = lcd_ctx->lcd_state;

    if (state <= LCD_STATE_SHOW_MIN || state >= LCD_STATE_SHOW_MAX)
    {
        return; // nothing to do
    }

    state++;

    // cycle screens
    if (state >= LCD_STATE_SHOW_MAX)
    {
        state = LCD_STATE_SHOW_MIN + 1;
    }

    lcd_set_state(lcd_ctx, state);
}

static void lcd_ip_config_process_inc(lcd_ctx_t* lcd_ctx)
{
    if (!lcd_ctx)
    {
        return;
    }

    // increment the digit at the cursor position
    // the first digit can be in range     [0;2]
    // the last two digits can be in range [0;9]
    // if the first digit is 2, the second can be in range [0;5]
    // if the first is 2, the second is 5, the third can be in range [0;5]
    lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos]++;

    if ((lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  0) ||
        (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  4) ||
        (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  8) ||
        (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS + 12))
    {
        // first digits
        if (lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] > '2')
        {
            lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] = '0'; // cycle
        }
    }
    else if ((lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  1) ||
             (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  5) ||
             (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  9) ||
             (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS + 13))
    {
        // second digits
        if ((lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos - 1] == '2') &&
            (lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] > '5') ||
            (lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] > '9'))
        {
            lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] = '0'; // cycle
        }
    }
    else
    {
        // third digits
        if ((lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos - 2] == '2') &&
            (lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos - 1] == '5') &&
            (lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] > '5') ||
            (lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] > '9'))
        {
            lcd_ctx->lcd_line_main[lcd_ctx->lcd_cursor.pos] = '0'; // cycle
        }
    }
    lcd_ctx->lcd_upd_flag = 1;
}

static void lcd_ip_config_process_move(lcd_ctx_t* lcd_ctx)
{
    if (!lcd_ctx)
    {
        return;
    }

    // move cursor position
    lcd_ctx->lcd_cursor.pos++;

    if ((lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  3) ||
        (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS +  7) ||
        (lcd_ctx->lcd_cursor.pos == LCD_IP_ADDR_START_POS + 11))
    {
        // skip positions with dots: "000.000.000.000"
        lcd_ctx->lcd_cursor.pos++;
    }
    if (lcd_ctx->lcd_cursor.pos > LCD_IP_ADDR_END_POS)
    {
        // cycle the cursor at IP address line
        lcd_ctx->lcd_cursor.pos = LCD_IP_ADDR_START_POS;
    }

    // show blink character at the new position
    lcd_put_cursor(lcd_ctx, LCD_LINE_MAIN_ROW_INDEX, 0);
    lcd_send_string(lcd_ctx, lcd_ctx->lcd_line_main);     // clear blink character at the previous cursor pos
    lcd_put_cursor(lcd_ctx, LCD_LINE_MAIN_ROW_INDEX, lcd_ctx->lcd_cursor.pos);
    lcd_send_string(lcd_ctx, LCD_CURSOR_BLINK_CHARACTER); // display blink character
    lcd_ctx->lcd_cursor.blink = 1;
    lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
}

static void lcd_ip_config_process_save(lcd_ctx_t* lcd_ctx)
{
    if (!lcd_ctx)
    {
        return;
    }

    nvs_settings_ip_addr_t new_ip;
    uint16_t _new_ip[4];
    sscanf(lcd_ctx->lcd_line_main, LCD_IP_ADDR_FORMAT_STRING, &_new_ip[0], &_new_ip[1], &_new_ip[2], &_new_ip[3]);

    // note: newlib-nano supports only those conversion specifiers defined in the C89 standard
    // the "hh" modifier is not available in the C89 standard for "%hhu" ("hh" - convert input to char, store in char object)
    for (uint8_t i = 0; i < sizeof(new_ip); i++)
    {
        new_ip.byte[i] = (uint8_t)(_new_ip[i] & 0xFF);
    }

    // TODO: save the result to NVS and apply it
    switch (lcd_ctx->lcd_state)
    {
        case LCD_STATE_CONFIG_IP_ADDRESS:
        {
            NVS_SETTING_SET(ip_addr, &new_ip.value, nvs_settings_apply_ip_addr);
            break;
        }
        case LCD_STATE_CONFIG_SUBNET_MASK:
        {
            NVS_SETTING_SET(subnet_mask, &new_ip.value, nvs_settings_apply_subnet_mask);
            break;
        }
        case LCD_STATE_CONFIG_GATEWAY:
        {
            NVS_SETTING_SET(gateway, &new_ip.value, nvs_settings_apply_gateway);
            break;
        }
    }
}

static void lcd_ip_config_process_idle(lcd_ctx_t* lcd_ctx)
{
    if (!lcd_ctx)
    {
        return;
    }

    // blinking character at the current cursor position
    uint32_t timedelta = GET_CURRENT_TIMESTAMP_MS() - lcd_ctx->lcd_timestamp_ms;

    if (!lcd_ctx->lcd_cursor.blink && timedelta > LCD_CURSOR_BLINK_PERIOD_MS)
    {
        lcd_put_cursor(lcd_ctx, LCD_LINE_MAIN_ROW_INDEX, lcd_ctx->lcd_cursor.pos);
        lcd_send_string(lcd_ctx, LCD_CURSOR_BLINK_CHARACTER); // display blink character
        lcd_ctx->lcd_cursor.blink = !lcd_ctx->lcd_cursor.blink;
        lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
    }
    else if (lcd_ctx->lcd_cursor.blink && timedelta > LCD_CURSOR_BLINK_DURATION_MS)
    {
        lcd_ctx->lcd_upd_flag = 1; // display the actual symbol back
        lcd_ctx->lcd_cursor.blink = !lcd_ctx->lcd_cursor.blink;
        lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
    }
}

static void lcd_process_state(lcd_ctx_t* lcd_ctx)
{
    if (!lcd_ctx)
    {
        return;
    }

    switch (lcd_ctx->lcd_state)
    {
        case LCD_STATE_INVALID:
        {
            // init lcd content
            lcd_set_state(lcd_ctx, LCD_STATE_SHOW_GREATING);

            break;
        }
        case LCD_STATE_SHOW_GREATING:
        {
            if ((lcd_ctx->button_left.state  == BUTTON_STATE_PRESSED) ||
                (lcd_ctx->button_mid.state   == BUTTON_STATE_PRESSED) ||
                (lcd_ctx->button_right.state == BUTTON_STATE_PRESSED))
            {
                lcd_set_state(lcd_ctx, LCD_STATE_SHOW_IP_ADDRESS);

                lcd_ctx->button_left.state  = BUTTON_STATE_RELEASED;
                lcd_ctx->button_mid.state   = BUTTON_STATE_RELEASED;
                lcd_ctx->button_right.state = BUTTON_STATE_RELEASED;
            }
            break;
        }
        case LCD_STATE_SHOW_IP_ADDRESS:
        case LCD_STATE_SHOW_SUBNET_MASK:
        case LCD_STATE_SHOW_GATEWAY:
        {
            if (lcd_ctx->button_left.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_prev_show_screen_state(lcd_ctx);
                lcd_ctx->button_left.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_mid.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_state(lcd_ctx, lcd_ctx->lcd_state + (LCD_STATE_CONFIG_IP_ADDRESS - LCD_STATE_SHOW_IP_ADDRESS));
                lcd_ctx->button_mid.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_right.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_next_show_screen_state(lcd_ctx);
                lcd_ctx->button_right.state = BUTTON_STATE_RELEASED;
            }
            else
            {
                uint32_t timedelta = GET_CURRENT_TIMESTAMP_MS() - lcd_ctx->lcd_timestamp_ms;
                // show description for first N milliseconds
                if (!lcd_ctx->lcd_cursor.blink && timedelta < LCD_SHOW_DESCRIPTION_DELAY_MS)
                {
                    lcd_put_cursor(lcd_ctx, LCD_LINE_MAIN_ROW_INDEX, 0);
                    if (lcd_ctx->lcd_state == LCD_STATE_SHOW_IP_ADDRESS)
                    {
                        lcd_send_string(lcd_ctx, "   IP ADDRESS   ");
                    }
                    else if (lcd_ctx->lcd_state == LCD_STATE_SHOW_SUBNET_MASK)
                    {
                        lcd_send_string(lcd_ctx, "  SUBNET  MASK  ");
                    }
                    else if (lcd_ctx->lcd_state == LCD_STATE_SHOW_GATEWAY)
                    {
                        lcd_send_string(lcd_ctx, "DEFAULT  GATEWAY");
                    }
                    lcd_ctx->lcd_cursor.blink = 1;
                }
                else if (lcd_ctx->lcd_cursor.blink && timedelta > LCD_SHOW_DESCRIPTION_DELAY_MS)
                {
                    // return to normal state
                    lcd_ctx->lcd_upd_flag = 1;
                }
            }
            break;
        }
        case LCD_STATE_SHOW_IP_MODE:
        {
            if (lcd_ctx->button_left.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_prev_show_screen_state(lcd_ctx);
                lcd_ctx->button_left.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_mid.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_state(lcd_ctx, LCD_STATE_CONFIG_IP_MODE);
                lcd_ctx->button_mid.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_right.state == BUTTON_STATE_PRESSED)
            {
                lcd_set_next_show_screen_state(lcd_ctx);
                lcd_ctx->button_right.state = BUTTON_STATE_RELEASED;
            }
            break;
        }
        case LCD_STATE_CONFIG_IP_ADDRESS:
        case LCD_STATE_CONFIG_SUBNET_MASK:
        case LCD_STATE_CONFIG_GATEWAY:
        {
            if (lcd_ctx->button_left.state == BUTTON_STATE_PRESSED)
            {
                lcd_ip_config_process_inc(lcd_ctx);
                lcd_ctx->button_left.state = BUTTON_STATE_RELEASED;
                lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }
            else if (lcd_ctx->button_mid.state == BUTTON_STATE_PRESSED)
            {
                lcd_ip_config_process_save(lcd_ctx); // save new value from the main lcd line to NVS and apply it

                lcd_set_state(lcd_ctx, lcd_ctx->lcd_state - (LCD_STATE_CONFIG_IP_ADDRESS - LCD_STATE_SHOW_IP_ADDRESS));
                lcd_ctx->button_mid.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_right.state == BUTTON_STATE_PRESSED)
            {
                lcd_ip_config_process_move(lcd_ctx);
                lcd_ctx->button_right.state = BUTTON_STATE_RELEASED;
                lcd_ctx->lcd_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
            }
            else
            {
                lcd_ip_config_process_idle(lcd_ctx);
            }
            break;
        }
        case LCD_STATE_CONFIG_IP_MODE:
        {
            if (lcd_ctx->button_left.state == BUTTON_STATE_PRESSED)
            {
                // TODO: handle mode switching
                lcd_ctx->button_left.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_mid.state == BUTTON_STATE_PRESSED)
            {
                // TODO: save the result
                lcd_set_state(lcd_ctx, LCD_STATE_SHOW_IP_MODE);
                lcd_ctx->button_mid.state = BUTTON_STATE_RELEASED;
            }
            else if (lcd_ctx->button_right.state == BUTTON_STATE_PRESSED)
            {
                // TODO: handle mode switching
                lcd_ctx->button_right.state = BUTTON_STATE_RELEASED;
            }
            break;
        }
        default:
        {
            return; // ignore unknown lcd state codes
        }
    }

    if (lcd_ctx->lcd_upd_flag)
    {
        lcd_put_cursor(lcd_ctx, LCD_LINE_MAIN_ROW_INDEX, 0);
        lcd_send_string(lcd_ctx, lcd_ctx->lcd_line_main);
        lcd_put_cursor(lcd_ctx, LCD_LINE_SECONDARY_ROW_INDEX, 0);
        lcd_send_string(lcd_ctx, lcd_ctx->lcd_line_secondary);

        lcd_ctx->lcd_upd_flag = 0;
    }
}

static void lcd_check_health(lcd_ctx_t* lcd_ctx)
{
    if (!lcd_ctx || lcd_ctx->lcd_state <= LCD_STATE_INVALID || lcd_ctx->lcd_state >= LCD_STATE_UNKNOWN)
    {
        return;
    }

    uint32_t timedelta = GET_CURRENT_TIMESTAMP_MS() - lcd_ctx->lcd_health_timestamp_ms;
    if (timedelta > LCD_HEALTH_CHECK_PERIOD_MS)
    {
        char buf_main[LCD_HEALTH_CHECK_MAX_BUF_BYTES];
        char buf_secondary[LCD_HEALTH_CHECK_MAX_BUF_BYTES];
        lcd_put_cursor(lcd_ctx, LCD_LINE_MAIN_ROW_INDEX, LCD_SYMBOLS_PER_LINE);
        lcd_read_string(lcd_ctx, buf_main, strlen(lcd_ctx->lcd_health_canary));
        lcd_put_cursor(lcd_ctx, LCD_LINE_SECONDARY_ROW_INDEX, LCD_SYMBOLS_PER_LINE);
        lcd_read_string(lcd_ctx, buf_secondary, strlen(lcd_ctx->lcd_health_canary));

        if ((!memcmp(buf_main,      lcd_ctx->lcd_health_canary, strlen(lcd_ctx->lcd_health_canary))) &&
            (!memcmp(buf_secondary, lcd_ctx->lcd_health_canary, strlen(lcd_ctx->lcd_health_canary))))
        {
            // passed (healthy)
            lcd_ctx->lcd_health_timestamp_ms = GET_CURRENT_TIMESTAMP_MS();
        }
        else
        {
            // broken -> reinit
            lcd_init(lcd_ctx);
            lcd_ctx->lcd_upd_flag = 1; // redraw lcd content after reinit
        }
    }
}

static void buttonTaskHandler(void *argument)
{
    lcd_init(&lcd_ctx); // init lcd in 4-bit mode

    /* Infinite loop */
    while (1)
    {
        button_process_state(&lcd_ctx.button_left);
        button_process_state(&lcd_ctx.button_mid);
        button_process_state(&lcd_ctx.button_right);
        lcd_check_health(&lcd_ctx);
        lcd_process_state(&lcd_ctx);

        osDelay(10);
    }
}

static GPIO_PinState button_left_read(void)
{
    return HAL_GPIO_ReadPin(INPUT_LEFT_GPIO_Port, INPUT_LEFT_Pin);
}

static GPIO_PinState button_mid_read(void)
{
    return HAL_GPIO_ReadPin(INPUT_MID_GPIO_Port, INPUT_MID_Pin);
}

static GPIO_PinState button_right_read(void)
{
    return HAL_GPIO_ReadPin(INPUT_RIGHT_GPIO_Port, INPUT_RIGHT_Pin);
}

static void button_left_pressed_cb(void)
{
    DBG_PRINTF("");
}

static void button_mid_pressed_cb(void)
{
    DBG_PRINTF("");
}

static void button_right_pressed_cb(void)
{
    DBG_PRINTF("");
}

static void button_left_released_cb(void)
{
    DBG_PRINTF("");
}

static void button_mid_released_cb(void)
{
    DBG_PRINTF("");
}

static void button_right_released_cb(void)
{
    DBG_PRINTF("");
}

static void lcd_send_cmd(char cmd)
{
    char data_up, data_low;
    data_up = (cmd & 0xF0);             // extract upper 4 bits
    data_low = ((cmd << 4) & 0xF0);     // extract lower 4 bits

    uint8_t data_b[4];

    // send upper 4 bits with enable pulse
    data_b[0] = data_up | 0x0C;         // EN = 1, RS = 0  -> bxxxx1100
    data_b[1] = data_up | 0x08;         // EN = 0, RS = 0  -> bxxxx1000

    // send lower 4 bits with enable pulse
    data_b[2] = data_low | 0x0C;        // EN = 1, RS = 0  -> bxxxx1100
    data_b[3] = data_low | 0x08;        // EN = 0, RS = 0  -> bxxxx1000

    HAL_I2C_Master_Transmit(&hi2c1, LCD_SLAVE_WRITE_ADDRESS, (uint8_t*)data_b, sizeof(data_b), 100);
}

static void lcd_send_data(char data)
{
    char data_up, data_low;
    uint8_t data_b[4];
    data_up = (data & 0xF0);
    data_low = ((data << 4) & 0xF0);
    data_b[0] = data_up  | 0x0D;  // EN = 1, RS = 1 -> bxxxx1101
    data_b[1] = data_up  | 0x09;  // EN = 0, RS = 1 -> bxxxx1001
    data_b[2] = data_low | 0x0D;  // EN = 1, RS = 1 -> bxxxx1101
    data_b[3] = data_low | 0x09;  // EN = 0, RS = 1 -> bxxxx1001

    HAL_I2C_Master_Transmit(&hi2c1, LCD_SLAVE_WRITE_ADDRESS, (uint8_t*)data_b, sizeof(data_b), 100);
}

static char lcd_read_data(void)
{
    char data_up = 0, data_low = 0, data = 0;
    uint8_t data_b[2];

    data_b[0] = 0xF0 | 0x0F;  // R/W = 1, EN = 1, RS = 1 -> bxxxx1111, where xxxx = 1111
    data_b[1] = 0xF0 | 0x0B;  // R/W = 1, EN = 0, RS = 1 -> bxxxx1011, where xxxx = 1111

    HAL_I2C_Master_Transmit(&hi2c1, LCD_SLAVE_WRITE_ADDRESS, (uint8_t*)&data_b[0], sizeof(data_b[0]),   100);
    HAL_I2C_Master_Receive(&hi2c1,  LCD_SLAVE_READ_ADDRESS,  (uint8_t*)&data_up,   sizeof(data_up),     100);
    HAL_I2C_Master_Transmit(&hi2c1, LCD_SLAVE_WRITE_ADDRESS, (uint8_t*)&data_b[1], sizeof(data_b[1]),   100);

    HAL_I2C_Master_Transmit(&hi2c1, LCD_SLAVE_WRITE_ADDRESS, (uint8_t*)&data_b[0], sizeof(data_b[0]),   100);
    HAL_I2C_Master_Receive(&hi2c1,  LCD_SLAVE_READ_ADDRESS,  (uint8_t*)&data_low,  sizeof(data_low),    100);
    HAL_I2C_Master_Transmit(&hi2c1, LCD_SLAVE_WRITE_ADDRESS, (uint8_t*)&data_b[1], sizeof(data_b[1]),   100);

    data = (data_up & 0xF0) | ((data_low >> 4) & 0x0F);

    return data;
}

static void lcd_put_cursor(lcd_ctx_t* lcd_ctx, int row, int col)
{
    if (!lcd_ctx)
    {
        return;
    }

    switch (row)
    {
        case 0:
        {
            col |= 0x80;
            break;
        }
        case 1:
        {
            col |= 0xC0;
            break;
        }
    }
    lcd_ctx->lcd_cmd(col);
}

static void lcd_send_string(lcd_ctx_t* lcd_ctx, char* str)
{
    if (!lcd_ctx || !str)
    {
        return;
    }

    while (*str) 
    {
        lcd_ctx->lcd_write(*str++);
    }
}

static void lcd_read_string(lcd_ctx_t* lcd_ctx, char* str, size_t size)
{
    if (!lcd_ctx || !str || !size)
    {
        return;
    }

    for (size_t i = 0; i < size; i++)
    {
        str[i] = lcd_ctx->lcd_read();
    }
}

void ButtonTASK_Init(void)
{
    buttonTaskHandle = osThreadNew(buttonTaskHandler, NULL, &buttonTask_attributes);
}
