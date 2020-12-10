/***************************************************************************//**
 * @file main_tg_gg.c
 * @brief Demonstrates interrupt-drive calibration of the LFRCO against
 *        the HFXO with output to a pin.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 *******************************************************************************
 * # Evaluation Quality
 * This code has been minimally tested to ensure that it builds and is suitable 
 * as a demonstration for evaluation purposes only. This code will be maintained
 * at the sole discretion of Silicon Labs.
 ******************************************************************************/

#include "em_chip.h"
#include "em_cmu.h"
#include "em_cryotimer.h"
#include "em_emu.h"
#include "em_gpio.h"

/*
 * Top value for the calibration down counter.  Maximum allowed is
 * 2^20-1 = 0xFFFFF, which equates to 2^20 counts.  A larger value
 * translates into greater accuracy at the expense of a longer
 * calibration period
 *
 * In this example, calibration is run for DOWNCOUNT counts of the
 * HFXO.  At the same time, the LFRCO clocks the up counter.  At a
 * given frequency, DOWNCOUNT counts of the HFXO to take a certain
 * amount of time:
 *
 *    DOWNCOUNT
 * -------------- = calibration time
 * HFXO frequency
 *
 * It follows that the up counter will run for some number of counts
 * of the LFRCO during the same period of time:
 *
 *       UP
 * --------------- = calibration time
 * LFRCO frequency
 *
 * So, to calibrate for a specified HFXO frequency...
 *
 *      LFRCO frequency * DOWNCOUNT
 * UP = ---------------------------
 *            HXCO frequency
 *
 * For example, if the desired LFRCO frequency is 32.768 kHz, then the
 * up counter should reach...
 *
 * 32768 x 1048576
 * --------------- = 716 (rounded up from 715.8)
 *    48000000
 *
 * ...when clocked from a 48 MHz crystal connected to the HFXO.
 *
 * This can be run for any desired frequency to the extent that it is
 * within the range of the LFRCO.
 */
#define DOWNCOUNT   0xFFFFF

/*
 * Setup the CRYOTIMER to generate interrupts at 16-second intervals.
 * The LFRCO is used as the CRYOTIMER clock, but the fact that the
 * clock frequency might change due to calibration isn't really
 * relevant for this example.
 */
void startCryo(void)
{
  CRYOTIMER_Init_TypeDef cryoInit = CRYOTIMER_INIT_DEFAULT;

  CMU_ClockEnable(cmuClock_CRYOTIMER, true);

  // Set CRYOTIMER parameters
  cryoInit.osc = cryotimerOscLFRCO;
  cryoInit.presc = cryotimerPresc_128;
  cryoInit.period = cryotimerPeriod_4k;
  cryoInit.enable = true;

  // Interrupt setup
  CRYOTIMER_IntClear(CRYOTIMER_IF_PERIOD);
  CRYOTIMER_IntEnable(CRYOTIMER_IEN_PERIOD);
  NVIC_ClearPendingIRQ(CRYOTIMER_IRQn);
  NVIC_EnableIRQ(CRYOTIMER_IRQn);

  CRYOTIMER_Init(&cryoInit);
}

void CRYOTIMER_IRQHandler(void)
{
  // Disable the CRYOTIMER until the next run
  CRYOTIMER_Enable(false);

  // Clear the CRYOTIMER interrupt
  CRYOTIMER_IntClear(CRYOTIMER_IF_PERIOD);

  /*
   * Force the write to CRYOTIMER_IFC to complete before proceeding to
   * make sure the interrupt is not re-triggered when exiting this
   * IRQ handler as the CRYOTIMER is in an asynchronous clock domain.
   */
  __DSB();
}

// Global variables used in calibration ISR
bool tuned, lastUpGT, lastUpLT;
uint32_t idealCount, tuningVal, prevUp, prevTuning;

// Setup a calibration run
void startCal(uint32_t freq)
{
  /*
   * Calibration has not yet run, so the LFRCO is not tuned, and the
   * last up-count greater-than and less-than flags are false.
   */
  tuned = false;
  lastUpGT = false;
  lastUpLT = false;

  // Determine ideal up count based on the desired frequency
  idealCount = (uint32_t)(((float)freq / (float)SystemHFXOClockGet()) * (float)(DOWNCOUNT + 1));

  // Get current tuningVal value
  tuningVal = CMU_OscillatorTuningGet(cmuOsc_LFRCO);

  // Setup the calibration circuit
  CMU_CalibrateConfig(DOWNCOUNT, cmuOsc_HFXO, cmuOsc_LFRCO);

  // Enable continuous calibration
  CMU_CalibrateCont(true);

  // Start the up counter
  CMU_CalibrateStart();

  // Enable calibration ready interrupt
  CMU_IntClear(_CMU_IFC_MASK);
  CMU_IntEnable(CMU_IEN_CALRDY);
  NVIC_ClearPendingIRQ(CMU_IRQn);
  NVIC_EnableIRQ(CMU_IRQn);
}

/*
 * Before calling this function make sure that the LFRCO and HFXO are
 * already running.
 *
 * Calibration is an iterative process because the CMU hardware simply
 * returns a count.  It doesn't determine whether or not a specific
 * tuningVal is correct.  To keep things simple, this function implements
 * the search algorithm and not any of the setup that might differ from
 * system to system (e.g. enabling the HFXO or LFXO as the reference
 * oscillator).
 */
void CMU_IRQHandler(void)
{
  uint32_t upCount;

  // Clear the calibration ready flag
  CMU_IntClear(CMU_IFC_CALRDY);

  // Get the up counter value
  upCount = CMU_CalibrateCountGet();

  /*
   * If the up counter result is less than the tuned value, the LFRCO
   * is running at a lower frequency, so the tuning value has to be
   * decremented.
   */
  if (upCount < idealCount)
  {
    // Was the up counter greater than the tuned value on the last run?
    if (lastUpGT)
    {
      /*
       * If the difference between the ideal count and the up count
       * from this run is greater than it was on the last run, then
       * the last run produced a more accurate tuning, so revert to
       * the previous tuning value.  If not, the current value gets
       * us the most accurate tuning.
       */
      if ((idealCount - upCount) > (prevUp - idealCount))
        tuningVal = prevTuning;

      // Done tuning now
      tuned = true;
    }
    // Up counter for the last run not greater than the tuned value
    else
    {
      /*
       * If the difference is 1, decrementing the tuning value again
       * will only increase the frequency further away from the
       * intended target, so tuning is now complete.
       */
      if ((idealCount - upCount) == 1)
        tuned = true;
      /*
       * The difference between this run and the ideal count for the
       * desired frequency is > 1, so decrease the tuning value to
       * increase the LFRCO frequency.  After the next calibration run,
       * the up counter value will increase.  Save the tuning value
       * from this run; if it's close, it might be more accurate than
       * the result from the next run.
       */
      else
      {
        prevTuning = tuningVal;
        tuningVal--;
        lastUpLT = true;  // Remember that the up counter was less than the ideal this run
        prevUp = upCount;
      }
    }
  }

  /*
   * If the up counter result is greater than the tuned value, the
   * LFRCO is running at a higher frequency, so the tuning value has
   * to be incremented.
   */
  if (upCount > idealCount)
  {
    // Was the up counter less than the tuned value on the last run?
    if (lastUpLT)
    {
      /*
       * If the difference between the up count and the ideal count
       * from this run is greater than it was on the last run, then
       * the last run produced a more accurate tuning, so revert to
       * the previous tuning value.  If not, the current value gets
       * the most accurate tuning.
       */
      if ((upCount - idealCount) > (idealCount - prevUp))
        tuningVal = prevTuning;

      // Done tuning now
      tuned = true;
    }
    // Up counter for the last run not less than the tuned value
    else
    {
      /*
       * If the difference is 1, incrementing the tuning value again
       * will only decrease the frequency further away from the
       * intended target, so tuning is now complete.
       */
      if ((upCount - idealCount) == 1)
        tuned = true;
      /*
       * The difference between this run and the ideal count for the
       * desired frequency is > 1, so increase the tuning value to
       * decrease the HFRCO frequency.  After the next calibration run,
       * the up counter value will decrease.  Save the tuning value
       * from this run; if it's close, it might be more accurate than
       * the result from the next run.
       */
      else
      {
        prevTuning = tuningVal;
        tuningVal++;
        lastUpGT = true;  // Remember that the up counter was less than the ideal this run
        prevUp = upCount;
      }
    }
  }

  // Up counter result is equal to the desired value, end of calibration
  if (upCount == idealCount)
    tuned = true;
  // Otherwise set new tuning value
  else
    CMU_OscillatorTuningSet(cmuOsc_LFRCO, tuningVal);

  // If not tuned, run calibration again, otherwise stop
  if (!tuned)
    CMU_CalibrateStart();
  else
    CMU_CalibrateStop();
}

int main(void)
{
  CHIP_Init();

  // Initialize DCDC with kit specific parameters
  EMU_DCDCInit_TypeDef dcdcInit = EMU_DCDCINIT_DEFAULT;
  EMU_DCDCInit(&dcdcInit);

  // Start the HFXO with safe default parameters
  CMU_HFXOInit_TypeDef hfxoInit = CMU_HFXOINIT_DEFAULT;
  CMU_HFXOInit(&hfxoInit);
  CMU_OscillatorEnable(cmuOsc_HFXO, true, true);

  // Switch the HFCLK to the HFXO.
  CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFXO);

  // Enable the LFRCO
  CMU_OscillatorEnable(cmuOsc_LFRCO, true, true);

  // Drive LFRCO onto PA12 to observe calibration
  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_PinModeSet(gpioPortA, 12, gpioModePushPull, 0);
  CMU->ROUTELOC0 = CMU_ROUTELOC0_CLKOUT0LOC_LOC5;
  CMU->CTRL |= CMU_CTRL_CLKOUTSEL0_LFRCOQ;
  CMU->ROUTEPEN = CMU_ROUTEPEN_CLKOUT0PEN;

  while (1)
  {
    // Setup calibration run
    startCal(32768);

    // Do other stuff while calibration is ongoing
    while (!tuned)
    {
      __NOP();
    }

    // Start the 16-second CRYOTIMER interrupt
    startCryo();

    // Wait for the CRYOTIMER interrupt before repeating
    EMU_EnterEM1();
  }
}
