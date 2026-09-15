#include "driver/i2c_master.h"
#include "esp_log.h"

#include "adxl345.h"

/* wiring. these are the classic esp32 devkit i2c pins - double check them
against the feather v2 pinout before trusting them on real hardware*/
#define ADXL_SDA_PIN        21
#define ADXL_SCL_PIN        22
#define ADXL_I2C_PORT       I2C_NUM_0
#define ADXL_I2C_HZ         400000
#define ADXL_TIMEOUT_MS     50

// the adxl345 sits at 0x53 when the SDO pin is tied to ground
#define ADXL_ADDR           0x53

// registers, straight out of table 19 in the datasheet
#define REG_DEVID           0x00    // always reads 0xe5
#define REG_BW_RATE         0x2C    // how fast the chip samples
#define REG_POWER_CTL       0x2D    // standby vs measuring
#define REG_DATA_FORMAT     0x31    // range and resolution
#define REG_DATAX0          0x32    // first of six data bytes

#define DEVID_EXPECTED      0xE5

/* 0x08 sets the measure bit. without this the chip powers up in standby
 and every read comes back as zeros, which looks exactly like a dead sensor*/
#define POWER_CTL_MEASURE   0x08

/* 0x0A is 100 hz output. that's ten times faster than we read it, which is
 on purpose - see the note about the fifo below */
#define BW_RATE_100HZ       0x0A

/* 0x0B is +/-16g with FULL_RES turned on.
   the range matters because a motorcycle impact is tens of g. at the +/-2g
   the original firmware used, every reading during a real crash pins at the
   ceiling and the crash is invisible to us.
   FULL_RES (bit 3) is the other half of the fix: it keeps resolution at about
  4 mg per count no matter which range we pick, so widening the range doesn't
  quietly break the scale factor below */

#define DATA_FORMAT_16G_FULLRES  0x0B

// with FULL_RES on, one count is 3.9 mg. 4 is close enough and keeps this integer-only
#define MG_PER_COUNT        4

static const char *TAG = "adxl345";

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), ADXL_TIMEOUT_MS);
}

static esp_err_t read_regs(uint8_t start_reg, uint8_t *out, size_t len)
{
    // write the register we want, then read from it without letting go of
    // the bus in between. that's what transmit_receive does for us
    return i2c_master_transmit_receive(s_dev, &start_reg, 1,
                                       out, len, ADXL_TIMEOUT_MS);
}

esp_err_t adxl345_init(void)
{
    esp_err_t err;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = ADXL_I2C_PORT,
        .sda_io_num                   = ADXL_SDA_PIN,
        .scl_io_num                   = ADXL_SCL_PIN,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };

    err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "couldn't set up the i2c bus: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ADXL_ADDR,
        .scl_speed_hz    = ADXL_I2C_HZ,
    };

    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "couldn't add the accelerometer to the bus: %s",
                 esp_err_to_name(err));
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return err;
    }

    /* before configuring anything, check we're actually talking to an adxl345.
       if this comes back wrong it's either bad wiring or a different chip, and
       it's much nicer to find out here than to debug weird numbers later */
    uint8_t devid = 0;
    err = read_regs(REG_DEVID, &devid, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no answer from the accelerometer: %s", esp_err_to_name(err));
        goto fail;
    }
    if (devid != DEVID_EXPECTED) {
        ESP_LOGE(TAG, "chip id was 0x%02X, expected 0x%02X - check the wiring",
                 devid, DEVID_EXPECTED);
        err = ESP_ERR_NOT_FOUND;
        goto fail;
    }

    err = write_reg(REG_BW_RATE, BW_RATE_100HZ);
    if (err != ESP_OK) { goto fail; }

    err = write_reg(REG_DATA_FORMAT, DATA_FORMAT_16G_FULLRES);
    if (err != ESP_OK) { goto fail; }

    // measure mode goes last, so the chip is fully configured before it
    // starts producing data
    err = write_reg(REG_POWER_CTL, POWER_CTL_MEASURE);
    if (err != ESP_OK) { goto fail; }

    ESP_LOGI(TAG, "accelerometer ready: +/-16g, full res, 100 hz");
    return ESP_OK;

fail:
    i2c_master_bus_rm_device(s_dev);
    i2c_del_master_bus(s_bus);
    s_dev = NULL;
    s_bus = NULL;
    return err;
}

esp_err_t adxl345_read_mg(int16_t *x_mg, int16_t *y_mg, int16_t *z_mg)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t raw[6];

    /* grab all six bytes in one transaction. reading them one at a time would
     let the chip update mid-read and hand us x from one sample and z from
     the next, which is the same torn-read problem the mutex solves elsewhere*/

    esp_err_t err = read_regs(REG_DATAX0, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }

    // each axis is two bytes, low byte first, and the value is signed
    int16_t rx = (int16_t) ((uint16_t) raw[0] | ((uint16_t) raw[1] << 8));
    int16_t ry = (int16_t) ((uint16_t) raw[2] | ((uint16_t) raw[3] << 8));
    int16_t rz = (int16_t) ((uint16_t) raw[4] | ((uint16_t) raw[5] << 8));

    *x_mg = (int16_t) (rx * MG_PER_COUNT);
    *y_mg = (int16_t) (ry * MG_PER_COUNT);
    *z_mg = (int16_t) (rz * MG_PER_COUNT);

    return ESP_OK;
}