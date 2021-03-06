/*
 * Copyright (c) 2012, Texas Instruments Incorporated - http://www.ti.com/
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */
/*---------------------------------------------------------------------------*/
/**
 * \addtogroup radioone-button-sensor
 * @{
 *
 * \file
 * Driver for for the RadioOne-CC2538 user button
 */
/*---------------------------------------------------------------------------*/
#include "contiki.h"
#include "dev/nvic.h"
#include "dev/ioc.h"
#include "dev/gpio.h"
#include "button-sensor.h"
#include "sys/timer.h"
#include "sys/ctimer.h"
#include "sys/process.h"
#include "susensorcommon.h"
#include "board.h"
#include "deviceSetup.h"
#include <stdint.h>
#include <string.h>

typedef enum su_basic_actions su_button_actions;

struct resourceconf pushbuttonconfig = {
		.resolution = 1,
		.version = 1,
		.flags = METHOD_GET | METHOD_PUT | IS_OBSERVABLE | HAS_SUB_RESOURCES,
		.max_pollinterval = 2,
		.unit = "",
		.spec = "Push button; OFF=0, ON=1, TOGGLE=2",
		.type = BUTTON_SENSOR,
		.attr = "title=\"Relay output\" ;rt=\"Control\"",
};

static const settings_t default_pushbutton_settings = {
		.eventsActive = BelowEventActive,
		.AboveEventAt = {
				.type = CMP_TYPE_UINT8,
				.as.u8 = 1
		},
		.BelowEventAt = {
				.type = CMP_TYPE_UINT8,
				.as.u8 = 0
		},
		.ChangeEvent = {
				.type = CMP_TYPE_UINT8,
				.as.u8 = 1
		},
		.RangeMin = {
				.type = CMP_TYPE_UINT8,
				.as.u8 = 0
		},
		.RangeMax = {
				.type = CMP_TYPE_UINT8,
				.as.u8 = 1
		},
};

/*---------------------------------------------------------------------------*/
#define BUTTON_USER_PORT_BASE  GPIO_PORT_TO_BASE(BUTTON_USER_PORT)
#define BUTTON_USER_PIN_MASK   GPIO_PIN_MASK(BUTTON_USER_PIN)
/*---------------------------------------------------------------------------*/
#define DEBOUNCE_DURATION (CLOCK_SECOND >> 4)
#define BUTTON_PRESS_EVENT_INTERVAL	(CLOCK_SECOND)	//Long press

static struct timer debouncetimer;
/*---------------------------------------------------------------------------*/
static clock_time_t press_duration = 0;
static struct ctimer press_counter;
static uint8_t press_event_counter;
static struct susensors_sensor* thisptr;
process_event_t button_press_duration_exceeded;
/*---------------------------------------------------------------------------*/

//Long press event - postes a new event every second.
//Use this for reset, reconnect etc..
static void
duration_exceeded_callback(void *data)
{
  press_event_counter++;
  process_post(PROCESS_BROADCAST, button_press_duration_exceeded,
               &press_event_counter);
  ctimer_set(&press_counter, press_duration, duration_exceeded_callback,
             NULL);
}
/*---------------------------------------------------------------------------*/
/**
 * \brief Retrieves the value of the button pin
 * \param type Returns the pin level or the counter of press duration events.
 *             type == BUTTON_SENSOR_VALUE_TYPE_LEVEL or
 *             type == BUTTON_SENSOR_VALUE_TYPE_PRESS_DURATION
 *             respectively
 */
static int get(struct susensors_sensor* this, int type, void* data)
{
  switch(type) {
  case BUTTON_SENSOR_VALUE_TYPE_LEVEL:
    return GPIO_READ_PIN(BUTTON_USER_PORT_BASE, BUTTON_USER_PIN_MASK);
  case BUTTON_SENSOR_VALUE_TYPE_PRESS_DURATION:
    return 0; //press_event_counter;
  }

  return 0;
}
/*---------------------------------------------------------------------------*/
/**
 * \brief Callback registered with the GPIO module. Gets fired with a button
 * port/pin generates an interrupt
 * \param port The port number that generated the interrupt
 * \param pin The pin number that generated the interrupt. This is the pin
 * absolute number (i.e. 0, 1, ..., 7), not a mask
 */
static void
btn_callback(uint8_t port, uint8_t pin)
{
  if(!timer_expired(&debouncetimer)) {
    return;
  }

  timer_set(&debouncetimer, DEBOUNCE_DURATION);

  if(press_duration) {
    press_event_counter = 0;
    if(get(thisptr, BUTTON_SENSOR_VALUE_TYPE_LEVEL, NULL) == BUTTON_SENSOR_PRESSED_LEVEL) {
      ctimer_set(&press_counter, press_duration, duration_exceeded_callback,
                 NULL);
      if(((settings_t*)(thisptr->data.setting))->eventsActive & BelowEventActive)
    	  susensors_changed(thisptr, SUSENSORS_BELOW_EVENT);
    } else {
      ctimer_stop(&press_counter);
      if(((settings_t*)(thisptr->data.setting))->eventsActive & AboveEventActive)
    	  susensors_changed(thisptr, SUSENSORS_ABOVE_EVENT);
    }
  }

  if(((settings_t*)(thisptr->data.setting))->eventsActive & ChangeEventActive)
	  susensors_changed(thisptr, SUSENSORS_CHANGE_EVENT);
}
/*---------------------------------------------------------------------------*/
/**
 * \brief Init function for the User button.
 * \param type SENSORS_ACTIVE: Activate / Deactivate the sensor (value == 1
 *             or 0 respectively)
 *
 * \param value Depends on the value of the type argument
 * \return Depends on the value of the type argument
 */
static int configure(struct susensors_sensor* this, int type, int value)
{
  switch(type) {
  case SUSENSORS_HW_INIT:
    button_press_duration_exceeded = process_alloc_event();

    /* Software controlled */
    GPIO_SOFTWARE_CONTROL(BUTTON_USER_PORT_BASE, BUTTON_USER_PIN_MASK);

    /* Set pin to input */
    GPIO_SET_INPUT(BUTTON_USER_PORT_BASE, BUTTON_USER_PIN_MASK);

    /* Enable edge detection */
    GPIO_DETECT_EDGE(BUTTON_USER_PORT_BASE, BUTTON_USER_PIN_MASK);

    /* Both Edges */
    GPIO_TRIGGER_BOTH_EDGES(BUTTON_USER_PORT_BASE, BUTTON_USER_PIN_MASK);

    /* Disable pull-ups */
    ioc_set_over(BUTTON_USER_PORT, BUTTON_USER_PIN, IOC_OVERRIDE_DIS);

    gpio_register_callback(btn_callback, BUTTON_USER_PORT, BUTTON_USER_PIN);
    break;
  case SUSENSORS_ACTIVE:
    if(value) {
      thisptr = this;
      GPIO_ENABLE_INTERRUPT(BUTTON_USER_PORT_BASE, BUTTON_USER_PIN_MASK);
      NVIC_EnableIRQ(BUTTON_USER_VECTOR);
    } else {
      GPIO_DISABLE_INTERRUPT(BUTTON_USER_PORT_BASE, BUTTON_USER_PIN_MASK);
      NVIC_DisableIRQ(BUTTON_USER_VECTOR);
    }
    return value;
  case BUTTON_SENSOR_CONFIG_TYPE_INTERVAL:
    press_duration = (clock_time_t)value;
    break;
  default:
    break;
  }

  return 1;
}

susensors_sensor_t* addASUButtonSensor(const char* name, settings_t* settings){

	if(deviceSetupGet(name, settings, &default_pushbutton_settings) < 0) return 0;

	susensors_sensor_t d;
	d.type = (char*)name;
	d.status = get;
	d.value = NULL;
	d.configure = configure;
	d.eventhandler = testevent;
	d.suconfig = suconfig;
	d.data.config = &pushbuttonconfig;
	d.data.setting = settings;

	d.setEventhandlers = NULL;

	d.configure(&d, BUTTON_SENSOR_CONFIG_TYPE_INTERVAL, BUTTON_PRESS_EVENT_INTERVAL);

	return addSUDevices(&d);
}

/** @} */
