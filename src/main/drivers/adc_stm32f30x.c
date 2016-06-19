/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"
#include "system.h"
#include "common/utils.h"
#include "gpio.h"

#include "sensor.h"
#include "accgyro.h"

#include "adc.h"
#include "adc_impl.h"
#include "io.h"
#include "rcc.h"

#ifndef ADC_INSTANCE
#define ADC_INSTANCE                ADC1
#endif

const adcDevice_t adcHardware[] = { 
    { .ADCx = ADC1, .rccADC = RCC_AHB(ADC12), .rccDMA = RCC_AHB(DMA1), .DMAy_Channelx = DMA1_Channel1 },  
    { .ADCx = ADC2, .rccADC = RCC_AHB(ADC12), .rccDMA = RCC_AHB(DMA2), .DMAy_Channelx = DMA2_Channel1 }  
};

ADCDevice adcDeviceByInstance(ADC_TypeDef *instance)
{
    if (instance == ADC1) 
        return ADCDEV_1;

    if (instance == ADC2)
        return ADCDEV_2;

    return ADCINVALID;
}

void adcInit(drv_adc_config_t *init)
{
    UNUSED(init);
    ADC_InitTypeDef ADC_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;

    uint8_t i;
    uint8_t adcChannelCount = 0;

    memset(&adcConfig, 0, sizeof(adcConfig));

#ifdef VBAT_ADC_PIN
    if (init->enableVBat) {
	    IOInit(IOGetByTag(IO_TAG(VBAT_ADC_PIN)), OWNER_SYSTEM, RESOURCE_ADC);
	    IOConfigGPIO(IOGetByTag(IO_TAG(VBAT_ADC_PIN)), IO_CONFIG(GPIO_Mode_AN, 0, GPIO_OType_OD, GPIO_PuPd_NOPULL));

        adcConfig[ADC_BATTERY].adcChannel = VBAT_ADC_CHANNEL;
        adcConfig[ADC_BATTERY].dmaIndex = adcChannelCount;
        adcConfig[ADC_BATTERY].sampleTime = ADC_SampleTime_601Cycles5;
        adcConfig[ADC_BATTERY].enabled = true;
        adcChannelCount++;
    }
#endif

#ifdef RSSI_ADC_PIN
    if (init->enableRSSI) {
	    IOInit(IOGetByTag(IO_TAG(RSSI_ADC_PIN)), OWNER_SYSTEM, RESOURCE_ADC);
	    IOConfigGPIO(IOGetByTag(IO_TAG(RSSI_ADC_PIN)), IO_CONFIG(GPIO_Mode_AN, 0, GPIO_OType_OD, GPIO_PuPd_NOPULL));

        adcConfig[ADC_RSSI].adcChannel = RSSI_ADC_CHANNEL;
        adcConfig[ADC_RSSI].dmaIndex = adcChannelCount;
        adcConfig[ADC_RSSI].sampleTime = ADC_SampleTime_601Cycles5;
        adcConfig[ADC_RSSI].enabled = true;
        adcChannelCount++;
    }
#endif

#ifdef CURRENT_METER_ADC_GPIO
    if (init->enableCurrentMeter) {
	    IOInit(IOGetByTag(IO_TAG(CURRENT_METER_ADC_PIN)), OWNER_SYSTEM, RESOURCE_ADC);
	    IOConfigGPIO(IOGetByTag(IO_TAG(CURRENT_METER_ADC_PIN)), IO_CONFIG(GPIO_Mode_AN, 0, GPIO_OType_OD, GPIO_PuPd_NOPULL));

        adcConfig[ADC_CURRENT].adcChannel = CURRENT_METER_ADC_CHANNEL;
        adcConfig[ADC_CURRENT].dmaIndex = adcChannelCount;
        adcConfig[ADC_CURRENT].sampleTime = ADC_SampleTime_601Cycles5;
        adcConfig[ADC_CURRENT].enabled = true;
        adcChannelCount++;
    }
#endif

#ifdef EXTERNAL1_ADC_GPIO
    if (init->enableExternal1) {
	    IOInit(IOGetByTag(IO_TAG(EXTERNAL1_ADC_PIN)), OWNER_SYSTEM, RESOURCE_ADC);
	    IOConfigGPIO(IOGetByTag(IO_TAG(EXTERNAL1_ADC_PIN)), IO_CONFIG(GPIO_Mode_AN, 0, GPIO_OType_OD, GPIO_PuPd_NOPULL));

        adcConfig[ADC_EXTERNAL1].adcChannel = EXTERNAL1_ADC_CHANNEL;
        adcConfig[ADC_EXTERNAL1].dmaIndex = adcChannelCount;
        adcConfig[ADC_EXTERNAL1].sampleTime = ADC_SampleTime_601Cycles5;
        adcConfig[ADC_EXTERNAL1].enabled = true;
        adcChannelCount++;
    }
#endif

    ADCDevice device = adcDeviceByInstance(ADC_INSTANCE);
    if (device == ADCINVALID)
        return;
    
    adcDevice_t adc = adcHardware[device];     

    RCC_ADCCLKConfig(RCC_ADC12PLLCLK_Div256);  // 72 MHz divided by 256 = 281.25 kHz
    RCC_ClockCmd(adc.rccADC, ENABLE);
    RCC_ClockCmd(adc.rccDMA, ENABLE);

    DMA_DeInit(adc.DMAy_Channelx);

    DMA_StructInit(&DMA_InitStructure);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&adc.ADCx->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)adcValues;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = adcChannelCount;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = adcChannelCount > 1 ? DMA_MemoryInc_Enable : DMA_MemoryInc_Disable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;

    DMA_Init(adc.DMAy_Channelx, &DMA_InitStructure);

    DMA_Cmd(adc.DMAy_Channelx, ENABLE);

    // calibrate

    ADC_VoltageRegulatorCmd(adc.ADCx, ENABLE);
    delay(10);
    ADC_SelectCalibrationMode(adc.ADCx, ADC_CalibrationMode_Single);
    ADC_StartCalibration(adc.ADCx);
    while (ADC_GetCalibrationStatus(adc.ADCx) != RESET);
    ADC_VoltageRegulatorCmd(adc.ADCx, DISABLE);

    ADC_CommonInitTypeDef ADC_CommonInitStructure;

    ADC_CommonStructInit(&ADC_CommonInitStructure);
    ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_CommonInitStructure.ADC_Clock = ADC_Clock_SynClkModeDiv4;
    ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_1;
    ADC_CommonInitStructure.ADC_DMAMode = ADC_DMAMode_Circular;
    ADC_CommonInitStructure.ADC_TwoSamplingDelay = 0;
    ADC_CommonInit(adc.ADCx, &ADC_CommonInitStructure);

    ADC_StructInit(&ADC_InitStructure);

    ADC_InitStructure.ADC_ContinuousConvMode    = ADC_ContinuousConvMode_Enable;
    ADC_InitStructure.ADC_Resolution            = ADC_Resolution_12b;
    ADC_InitStructure.ADC_ExternalTrigConvEvent = ADC_ExternalTrigConvEvent_0;
    ADC_InitStructure.ADC_ExternalTrigEventEdge = ADC_ExternalTrigEventEdge_None;
    ADC_InitStructure.ADC_DataAlign             = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_OverrunMode           = ADC_OverrunMode_Disable;
    ADC_InitStructure.ADC_AutoInjMode           = ADC_AutoInjec_Disable;
    ADC_InitStructure.ADC_NbrOfRegChannel       = adcChannelCount;

    ADC_Init(adc.ADCx, &ADC_InitStructure);

    uint8_t rank = 1;
    for (i = 0; i < ADC_CHANNEL_COUNT; i++) {
        if (!adcConfig[i].enabled) {
            continue;
        }
        ADC_RegularChannelConfig(adc.ADCx, adcConfig[i].adcChannel, rank++, adcConfig[i].sampleTime);
    }

    ADC_Cmd(adc.ADCx, ENABLE);

    while (!ADC_GetFlagStatus(adc.ADCx, ADC_FLAG_RDY));

    ADC_DMAConfig(adc.ADCx, ADC_DMAMode_Circular);

    ADC_DMACmd(adc.ADCx, ENABLE);

    ADC_StartConversion(adc.ADCx);
}

