#include "main.h"

extern COMP_HandleTypeDef motorBemfComparatorHandle;

TIM_HandleTypeDef motorPwmTimerHandle, timer3Handle, inputTimerHandle;
DMA_HandleTypeDef inputTimerDmaHandle;

// ADC
kalman_t adcCurrentFilterState;
extern uint32_t adcVoltageRaw, adcCurrentRaw, adcTemperatureRaw;
extern uint32_t adcVoltage, adcCurrent, adcTemperature;

// motor
kalman_t motorCommutationIntervalFilterState;
uint32_t motorCommutationInterval, motorCommutationDelay;
extern bool motorStartup, motorRunning;
extern bool motorDirection, motorSlowDecay, motorBrakeActiveProportional;
extern uint32_t motorBemfZeroCrossTimestamp;
extern uint32_t motorBemfFilterLevel, motorBemfFilterDelay;
extern uint32_t motorDutyCycle, motorBemfCounter;
extern uint32_t motorBemfZeroCounterTimeout, motorBemfZeroCounterTimeoutThreshold;

// input
extern bool inputArmed, inputDataValid;
extern uint8_t  inputProtocol;
extern uint32_t inputData;
extern uint32_t inputNormed, outputPwm;

int main(void) {
  HAL_Init();
  systemClockConfig();

  configValidateOrReset();
  configRead();

  ledInit();

  systemDmaInit();
  systemBemfComparatorInit();
  systemAdcInit();
  systemMotorPwmTimerInit();
  systemTimer3Init();
  systemInputTimerInit();

  // init
  ledOff();
  kalmanInit(&adcCurrentFilterState, 1500.0f, 31);
  kalmanInit(&motorCommutationIntervalFilterState, 2500.0f, 31);

  motorDirection = escConfig()->motorDirection;
  motorSlowDecay = escConfig()->motorSlowDecay;
  // start with motor off
  inputData = 0;
  outputPwm = 0;

  watchdogInit(2000);
  motorStartupTune();

  // main loop
  while (true) {

    #if (defined(_DEBUG_) && defined(CYCLETIME_MAINLOOP))
      LED_OFF(LED_BLUE);
    #endif

    watchdogFeed();

    // brake
    if (!outputPwm) {
      switch(escConfig()->motorBrake) {
        case BRAKE_FULL:
          motorBrakeActiveProportional = false;
          motorBrakeFull();
          motorDutyCycle = 0;
          break;
        case BRAKE_PROPORTIONAL:
          motorBrakeActiveProportional = true;
          motorDutyCycle = escConfig()->motorBrakeStrength;
          motorBrakeProportional();
          break;
        case BRAKE_OFF:
          motorBrakeActiveProportional = false;
          motorBrakeOff();
          motorDutyCycle = 0;
          break;
      }

      TIM1->CCR1 = motorDutyCycle;
      TIM1->CCR2 = motorDutyCycle;
      TIM1->CCR3 = motorDutyCycle;
    }

    if (inputProtocol == AUTODETECT) {
      // noop
    } else {
      inputArmCheck();
      inputDisarmCheck();
      if (inputArmed) {
        // PROSHOT
        if ((inputProtocol == PROSHOT) && (inputDataValid)) {
          if (inputData <= DSHOT_CMD_MAX) {
            motorStartup = false;
            outputPwm = 0;
            if (!motorRunning) {
              inputDshotCommandRun();
            }
          } else {
            motorStartup = true;
            motorBrakeActiveProportional = false;
            inputNormed = constrain((inputData - DSHOT_CMD_MAX), INPUT_NORMED_MIN, INPUT_NORMED_MAX);

            if (escConfig()->motor3Dmode) {
              // up
              if (inputNormed >= escConfig()->input3DdeadbandHigh) {
                if (motorDirection == !escConfig()->motorDirection) {
                  motorDirection = escConfig()->motorDirection;
                  motorBemfCounter = 0;
                }
                outputPwm = (inputNormed - escConfig()->input3Dneutral) + escConfig()->motorStartThreshold;
              }
              // down
              if (inputNormed <= escConfig()->input3DdeadbandLow) {
                if(motorDirection == escConfig()->motorDirection) {
                  motorDirection = !escConfig()->motorDirection;
                  motorBemfCounter = 0;
                }
                outputPwm = inputNormed + escConfig()->motorStartThreshold;
              }
              // deadband
              if ((inputNormed > escConfig()->input3DdeadbandLow) && (inputNormed < escConfig()->input3DdeadbandHigh)) {
                outputPwm = 0;
              }
            } else {
              outputPwm = (inputNormed >> 1) + (escConfig()->motorStartThreshold);
              // ToDo
              //outputPwm = scaleInputToOutput(inputNormed, INPUT_NORMED_MIN, INPUT_NORMED_MAX, OUTPUT_PWM_MIN, OUTPUT_PWM_MAX) + escConfig()->motorStartThreshold;
            }

            outputPwm = constrain(outputPwm, OUTPUT_PWM_MIN, OUTPUT_PWM_MAX);
          }
        } // PROSHOT

        // SERVOPWM (use only for thrust tests ...)
        if ((inputProtocol == SERVOPWM)  && (inputDataValid)) {
          if (inputData  < DSHOT_CMD_MAX) {
            motorStartup = false;
            outputPwm = 0;
          } else {
            motorStartup = true;
            motorBrakeActiveProportional = false;
            outputPwm = constrain((inputData + DSHOT_CMD_MAX), OUTPUT_PWM_MIN, OUTPUT_PWM_MAX);
          }
        } // SERVOPWM

        // input
        motorDutyCycle = constrain(outputPwm, OUTPUT_PWM_MIN, OUTPUT_PWM_MAX);
        TIM1->CCR1 = motorDutyCycle;
        TIM1->CCR2 = motorDutyCycle;
        TIM1->CCR3 = motorDutyCycle;

        // motor BEMF filter
        if ((motorCommutationInterval < 200) && (motorDutyCycle > 500)) {
          motorBemfFilterDelay = 1;
          motorBemfFilterLevel = 0;
        } else {
          motorBemfFilterLevel = 3;
          motorBemfFilterDelay = 3;
        }

        // timeouts
        if (motorDutyCycle < 300) {
          motorBemfZeroCounterTimeoutThreshold = 400;
        } else {
          motorBemfZeroCounterTimeoutThreshold = 200;
        }

        if (++motorBemfZeroCounterTimeout > motorBemfZeroCounterTimeoutThreshold) {
          kalmanInit(&motorCommutationIntervalFilterState, 2500.0f, 31);
          motorRunning = false;
          motorDutyCycle = 0;
        }

        // motor start
        if ((motorStartup) && (!motorRunning)) {
          motorBemfZeroCounterTimeout = 0;
          motorStart();
        }

        // ToDo
        motorCommutationInterval = kalmanUpdate(&motorCommutationIntervalFilterState, (float)motorBemfZeroCrossTimestamp);
        motorCommutationDelay = 0; //timing 30°
        //motorCommutationDelay = motorCommutationInterval >> 2; //timing 15°
        //motorCommutationDelay = motorCommutationInterval >> 1; //timing 0°
        //motorCommutationDelay = constrain(motorCommutationDelay, 17, 413);
      } // inputArmed
    } // inputProtocol detected

    // ESC hardware limits
    #if (defined(WRAITH32) || defined(WRAITH32V2) || defined(WRAITH32MINI))
      adcCurrent = kalmanUpdate(&adcCurrentFilterState, (float)adcCurrentRaw);
      if ((escConfig()->limitCurrent > 0) && (adcCurrent > escConfig()->limitCurrent)) {
        inputDisarm();
        #if (!defined(_DEBUG_))
          LED_ON(LED_RED);
        #endif
      }
    #endif

    #if (defined(_DEBUG_) && defined(CYCLETIME_MAINLOOP))
      LED_ON(LED_BLUE);
    #endif
  } // main loop

} // main
