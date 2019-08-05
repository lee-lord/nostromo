#include "adc.h"

ADC_HandleTypeDef adcHandle;
DMA_HandleTypeDef adcDmaHandle;

uint32_t adcDmaBuffer[3];
adcData_t adcRaw, adcScaled;
float consumptionMah;

void adcRead(void) {
  #if (defined(WRAITH32) || defined(WRAITH32V2) || defined(TYPHOON32V2))
    adcRaw.voltage = adcDmaBuffer[0];
    adcRaw.current = adcDmaBuffer[1];
    adcRaw.temperature = adcDmaBuffer[2];
  #endif

  #if (defined(DYS35ARIA))
    adcRaw.current = adcDmaBuffer[0];
    adcRaw.temperature = adcDmaBuffer[1];
  #endif
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* adcHandle) {
  adcRead();
}
