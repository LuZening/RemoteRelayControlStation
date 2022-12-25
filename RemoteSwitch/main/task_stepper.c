#include "task_stepper.h"
#include "driver/gpio.h"
#include "driver/mcpwm.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

EventGroupHandle_t evtTaskMot = NULL;
SemaphoreHandle_t mtxMot = NULL;
#define TAG "stepper"
#define tag "stepper"

int8_t motstate = 0;
// 3.3V----10K--T--SW1=3.3K--SW2=6.8K---GND
const int nLimitSwitches = 2;
const uint16_t limitTresholds[nLimitSwitches + 1] = {3300 / 2 - 100, 3300 * 0.4 - 100, 3300 * 0.24 - 100};
uint8_t limitno = 0; 

void task_motor(void* args)
{
    /* Using DRV8825 driver */
    /* setup */
    static uint8_t evtmask = 0b11111;
    if(!evtTaskMot)
        evtTaskMot =  xEventGroupCreate();
    if(!mtxMot)
        mtxMot = xEventGroupCreate();
    /* setup stepper driver pins */
    int pinDir = 16;
    int pinStep = 17;
    gpio_set_direction(pinDir, GPIO_MODE_OUTPUT);
    gpio_set_level(pinDir, 1);
    gpio_set_direction(pinStep, GPIO_MODE_OUTPUT);
    gpio_set_level(pinStep, 0);
    // use mcpwm to generate pulses
    mcpwm_unit_t mcpwm_unit = MCPWM_UNIT_0;
    mcpwm_config_t mcpwm_config = {
        .frequency = 100,
        .cmpr_a = 0.f, // PWM_A duty cycle %
        .cmpr_b = 0.f, // PWM_B duty cycle %
        .duty_mode = MCPWM_DUTY_MODE_0,
        .counter_mode = MCPWM_UP_COUNTER
    };
    mcpwm_gpio_init(mcpwm_unit, MCPWM0A, pinStep);
    mcpwm_init(mcpwm_unit, MCPWM_TIMER_0, &mcpwm_config);
    mcpwm_set_duty(mcpwm_unit, MCPWM_TIMER_0, MCPWM_GEN_A, 0.5);
    mcpwm_set_signal_low(mcpwm_unit, MCPWM_TIMER_0, MCPWM_GEN_A);


    /* setup limit switch ADC pin */
    // IO2 = ADC1_1

    adc_channel_t adcchLimit = ADC_CHANNEL_1;
    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(adcchLimit, ADC_ATTEN_11db);
    // calibrate ADC
    static esp_adc_cal_characteristics_t adcChars1;
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_11db, ADC_WIDTH_12Bit, 1100, &adcChars1);
    //Check type of calibration value used to characterize ADC
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGD(tag, "ADC has eFuse Vref data");
    }
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGW(tag, "ADC has Two Point calibration data");
    } else {
        ESP_LOGW(tag, "ADC has no calibration data");
    }
    /* loop */
    while(1)
    {
        EventBits_t bits = xEventGroupWaitBits(
            evtTaskMot, evtmask, pdTRUE, pdFALSE, pdMS_TO_TICKS(20)
        );
        if(bits != 0)
        {
            xSemaphoreTake(mtxMot, portMAX_DELAY);
            if((bits & SIGNAL_MOT_RUN_FORWARD  > 0) || (bits & SIGNAL_MOT_RUN_BACKWARD > 0))
            {
                if((bits & SIGNAL_MOT_RUN_FORWARD > 0) && (limitno != 2) )
                {
                    motstate = MOT_FORWARD;
                    gpio_set_level(pinDir, 1);
                    // start motor
                    mcpwm_set_duty_type(mcpwm_unit, MCPWM_TIMER_0, MCPWM_GEN_A, MCPWM_DUTY_MODE_0);
                    mcpwm_start(mcpwm_unit, MCPWM_TIMER_0);
                }
                else if((bits & SIGNAL_MOT_RUN_BACKWARD > 0) && (limitno != 1) )
                {
                    motstate = MOT_BACKWARD;
                    gpio_set_level(pinDir, 0);
                    // start motor
                    mcpwm_set_duty_type(mcpwm_unit, MCPWM_TIMER_0, MCPWM_GEN_A, MCPWM_DUTY_MODE_0);
                    mcpwm_start(mcpwm_unit, MCPWM_TIMER_0);
                }
            }
            else if(bits & SIGNAL_MOT_STOP > 0)
            {
                // stop motor
                mcpwm_stop(mcpwm_unit, MCPWM_TIMER_0);
                mcpwm_set_signal_low(mcpwm_unit, MCPWM_TIMER_0, MCPWM_GEN_A);
                motstate = MOT_STOP;
            }


            xSemaphoreGive(mtxMot);

        }
        /* get ADC */
        uint32_t vADCmV, vADCmV_Filtered = 0;
        int i;
        for(i = 0; i < 8; ++i)
        {
            esp_adc_cal_get_voltage(adcchLimit, &adcChars1, &vADCmV);
            vADCmV_Filtered += vADCmV;
        }
        vADCmV_Filtered >>= 3;

        for(i = 0; i < nLimitSwitches+1; ++i)
        {
            if(vADCmV_Filtered >= limitTresholds[i])
            {
               break; 
            }
        }
        xSemaphoreTake(mtxMot, portMAX_DELAY);
        if(i > 0 && limitno != i) // limit switch triggered
        {
            /*
            * limit swtich 1: backward limit
            * limit switch 2: forward limit
            */
            if((motstate == MOT_FORWARD && limitno == 2) 
                || (motstate == MOT_BACKWARD && limitno == 1))
            {
                //stop motor
                mcpwm_stop(mcpwm_unit, MCPWM_TIMER_0);
                mcpwm_set_signal_low(mcpwm_unit, MCPWM_TIMER_0, MCPWM_GEN_A);
                motstate = MOT_STOP;
                ESP_LOGI(tag, "limit switch %d triggered", i);
            }
        }
        limitno = i;
        xSemaphoreGive(mtxMot);
    }

}