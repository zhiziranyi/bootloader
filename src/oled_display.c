#include "oled_display.h"

#include <string.h>

#define OLED_PAGE_COUNT (OLED_HEIGHT / 8U)
#define OLED_CONTROL_CMD  0x00U
#define OLED_CONTROL_DATA 0x40U
#define OLED_PROBE_RETRIES 3U

static bool s_available;
static uint8_t s_address;
static uint8_t s_framebuffer[OLED_WIDTH * OLED_PAGE_COUNT];

/* 5x7 glyphs for the boot status screen: space, punctuation, digits, A-Z. */
static const uint8_t s_font[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x03, 0x04, 0x78, 0x04, 0x03}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
};

static const uint8_t *oled_glyph(char c)
{
    if (c == ' ') return s_font[0];
    if (c == '-') return s_font[1];
    if (c == '.') return s_font[2];
    if (c == ':') return s_font[3];
    if (c >= '0' && c <= '9') return s_font[4 + (uint8_t)(c - '0')];
    if (c >= 'A' && c <= 'Z') return s_font[14 + (uint8_t)(c - 'A')];
    return s_font[0];
}

/* Slow GPIO I2C deliberately matches the known-working PB6/PB7 project and
 * remains reliable with only the STM32's weak internal pull-ups. */
static void oled_i2c_delay(void)
{
    for (volatile uint32_t i = 0U; i < 600U; i++) {
        __NOP();
    }
}

static void oled_scl(GPIO_PinState state)
{
    HAL_GPIO_WritePin(OLED_SCL_PORT, OLED_SCL_PIN, state);
}

static void oled_sda(GPIO_PinState state)
{
    HAL_GPIO_WritePin(OLED_SDA_PORT, OLED_SDA_PIN, state);
}

static void oled_i2c_start(void)
{
    oled_sda(GPIO_PIN_SET);
    oled_scl(GPIO_PIN_SET);
    oled_i2c_delay();
    oled_sda(GPIO_PIN_RESET);
    oled_i2c_delay();
    oled_scl(GPIO_PIN_RESET);
}

static void oled_i2c_stop(void)
{
    oled_sda(GPIO_PIN_RESET);
    oled_i2c_delay();
    oled_scl(GPIO_PIN_SET);
    oled_i2c_delay();
    oled_sda(GPIO_PIN_SET);
    oled_i2c_delay();
}

static bool oled_i2c_send_byte(uint8_t value)
{
    for (uint8_t bit = 0U; bit < 8U; bit++) {
        oled_sda((value & 0x80U) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
        oled_i2c_delay();
        oled_scl(GPIO_PIN_SET);
        oled_i2c_delay();
        oled_scl(GPIO_PIN_RESET);
        value <<= 1U;
    }

    /* Release SDA so the display can pull it low for ACK. */
    oled_sda(GPIO_PIN_SET);
    oled_i2c_delay();
    oled_scl(GPIO_PIN_SET);
    oled_i2c_delay();
    bool acknowledged =
        HAL_GPIO_ReadPin(OLED_SDA_PORT, OLED_SDA_PIN) == GPIO_PIN_RESET;
    oled_scl(GPIO_PIN_RESET);
    oled_i2c_delay();
    return acknowledged;
}

static bool oled_i2c_probe(uint8_t address)
{
    oled_i2c_start();
    bool acknowledged = oled_i2c_send_byte(address);
    oled_i2c_stop();
    return acknowledged;
}

static HAL_StatusTypeDef oled_send(uint8_t control, const uint8_t *data, uint16_t len)
{
    if (!s_available || data == NULL) return HAL_ERROR;

    oled_i2c_start();
    if (!oled_i2c_send_byte(s_address) || !oled_i2c_send_byte(control)) {
        oled_i2c_stop();
        s_available = false;
        return HAL_ERROR;
    }
    for (uint16_t i = 0U; i < len; i++) {
        if (!oled_i2c_send_byte(data[i])) {
            oled_i2c_stop();
            s_available = false;
            return HAL_ERROR;
        }
    }
    oled_i2c_stop();
    return HAL_OK;
}

HAL_StatusTypeDef OLED_Init(void)
{
    static const uint8_t init_seq[] = {
        0xAE, 0x20, 0x00, 0x40, 0xA1, 0xC8, 0x81, 0x7F,
        0xA6, 0xA8, 0x3F, 0xD3, 0x00, 0xDA, 0x12, 0xD5,
        0x80, 0xD9, 0xF1, 0xDB, 0x40, 0x8D, 0x14, 0xAF
    };

    static const uint8_t addresses[] = { OLED_ADDRESS_3C, OLED_ADDRESS_3D };
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
    oled_scl(GPIO_PIN_SET);
    oled_sda(GPIO_PIN_SET);

    /* The working reference waits before its first command. Some modules do
     * not acknowledge immediately after the 3.3 V rail becomes stable. */
    HAL_Delay(100U);

    /* Recover a bus left busy by a brownout, then probe both common addresses. */
    for (uint8_t pulse = 0U; pulse < 9U; pulse++) {
        oled_scl(GPIO_PIN_RESET);
        oled_i2c_delay();
        oled_scl(GPIO_PIN_SET);
        oled_i2c_delay();
    }
    oled_i2c_stop();

    s_available = false;
    s_address = 0U;
    for (uint8_t a = 0U; a < (uint8_t)(sizeof(addresses) / sizeof(addresses[0])); a++) {
        for (uint8_t attempt = 0U; attempt < OLED_PROBE_RETRIES; attempt++) {
            if (oled_i2c_probe(addresses[a])) {
                s_address = addresses[a];
                s_available = true;
                break;
            }
            HAL_Delay(10U);
        }
        if (s_available) break;
    }

    OLED_Clear();
    if (!s_available) return HAL_ERROR;
    if (oled_send(OLED_CONTROL_CMD, init_seq, sizeof(init_seq)) != HAL_OK) {
        return HAL_ERROR;
    }
    return OLED_Refresh();
}

bool OLED_IsAvailable(void)
{
    return s_available;
}

uint8_t OLED_GetAddress(void)
{
    return (uint8_t)(s_address >> 1U);
}

void OLED_Clear(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

void OLED_DrawText(uint8_t x, uint8_t page, const char *text)
{
    if (page >= OLED_PAGE_COUNT || text == NULL) return;

    while (*text != '\0' && x <= (OLED_WIDTH - 6U)) {
        const uint8_t *glyph = oled_glyph(*text++);
        for (uint8_t column = 0; column < 5U; column++) {
            s_framebuffer[(uint16_t)page * OLED_WIDTH + x + column] = glyph[column];
        }
        s_framebuffer[(uint16_t)page * OLED_WIDTH + x + 5U] = 0x00;
        x = (uint8_t)(x + 6U);
    }
}

HAL_StatusTypeDef OLED_Refresh(void)
{
    for (uint8_t page = 0; page < OLED_PAGE_COUNT; page++) {
        uint8_t page_cmd[] = { (uint8_t)(0xB0U + page), 0x00, 0x10 };
        if (oled_send(OLED_CONTROL_CMD, page_cmd, sizeof(page_cmd)) != HAL_OK ||
            oled_send(OLED_CONTROL_DATA, &s_framebuffer[(uint16_t)page * OLED_WIDTH],
                      OLED_WIDTH) != HAL_OK) {
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}
