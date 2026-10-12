#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "heart_adc.h"

// gpio34, which is adc1 channel 6. adc1 matters - see the note below
#define HEART_ADC_UNIT     ADC_UNIT_1
#define HEART_ADC_CHANNEL  ADC_CHANNEL_6
#define HEART_ADC_ATTEN    ADC_ATTEN_DB_12

static const char *TAG = "heart_adc";

static adc_oneshot_unit_handle_t s_adc = NULL;

esp_err_t heart_adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = HEART_ADC_UNIT,
    };

    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "couldn't open adc1: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = HEART_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    err = adc_oneshot_config_channel(s_adc, HEART_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "couldn't configure the channel: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return err;
    }

    ESP_LOGI(TAG, "pulse sensor adc ready on adc1 ch6 (gpio34)");
    return ESP_OK;
}

esp_err_t heart_adc_read(uint16_t *raw)
{
    if (s_adc == NULL || raw == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int value = 0;
    esp_err_t err = adc_oneshot_read(s_adc, HEART_ADC_CHANNEL, &value);
    if (err != ESP_OK) {
        return err;
    }

    *raw = (uint16_t) (value < 0 ? 0 : value);
    return ESP_OK;
}
