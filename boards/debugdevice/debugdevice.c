/*******************************************************************************
 * Copyright (c) 2018, Ole Nissen.
 *  All rights reserved. 
 *  
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions 
 *  are met: 
 *  1. Redistributions of source code must retain the above copyright 
 *  notice, this list of conditions and the following disclaimer. 
 *  2. Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following
 *  disclaimer in the documentation and/or other materials provided
 *  with the distribution. 
 *  3. The name of the author may not be used to endorse or promote
 *  products derived from this software without specific prior
 *  written permission.  
 *  
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 *  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  
 *
 * This file is part of the Sensors Unleashed project
 *******************************************************************************/

#include "contiki.h"
#include "dev/leds.h"
#include "susensors.h"
#include "susensorcommon.h"
#include "button-sensor.h"
#include "relay.h"
#include "ledindicator.h"
#include "pulsesensor.h"
#include "mainsdetect.h"
#include "timerdevice.h"
#include "resources/res-susensors.h"
#include "cfs-coffee-arch.h"
#include "rpl.h"
#include "deviceSetup.h"

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]", (lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3], (lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif

PROCESS(device_process, "SU devicehandler");
AUTOSTART_PROCESSES(&device_process);

struct ledRuntime led_yellow = { LEDS_YELLOW };
extern  resource_t  res_sysinfo;

process_event_t systemchange;
MEMB(settings_memb, settings_t, 10);

PROCESS_THREAD(device_process, ev, data)
{
	static struct etimer et;

	PROCESS_BEGIN();

	initSUSensors();

	settings_t* relaysetting = (settings_t*)memb_alloc(&settings_memb);
	settings_t* yellow_led_setting = (settings_t*)memb_alloc(&settings_memb);
	settings_t* pulseCounter_settings = (settings_t*)memb_alloc(&settings_memb);
	settings_t* mainsDetector_settings = (settings_t*)memb_alloc(&settings_memb);
	settings_t* pushbutton_settings = (settings_t*)memb_alloc(&settings_memb);
	settings_t* timer_settings = (settings_t*)memb_alloc(&settings_memb);

	susensors_sensor_t* d;
	d = addASURelay(RELAY_ACTUATOR, relaysetting);
	if(d != NULL) {
		setResource(d, res_susensor_activate(d));
	}
	d = addASULedIndicator("su/led_yellow", yellow_led_setting, &led_yellow);
	if(d != NULL){
		setResource(d, res_susensor_activate(d));
	}
	d = addASUPulseInputRelay(PULSE_SENSOR, pulseCounter_settings);
	if(d != NULL){
		setResource(d, res_susensor_activate(d));
	}
	d = addASUMainsDetector(MAINSDETECT_ACTUATOR, mainsDetector_settings);
	if(d != NULL){
		setResource(d, res_susensor_activate(d));
	}
	d = addASUButtonSensor(BUTTON_SENSOR, pushbutton_settings);
	if(d != NULL){
		setResource(d, res_susensor_activate(d));
	}
	d = addASUTimerDevice(TIMER_DEVICE, timer_settings);
	if(d != NULL){
		setResource(d, res_susensor_activate(d));
	}

	rest_activate_resource(&res_sysinfo, "su/nodeinfo");
	systemchange = process_alloc_event();

	//Wait for the routing table to be ready
	leds_on(LEDS_RED);
	while(1){
		etimer_set(&et, CLOCK_SECOND);
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
		leds_toggle(LEDS_RED);

		if(rpl_is_reachable()){
			break;
		}
	}

	process_start(&susensors_process, NULL);

	leds_on(LEDS_RED);

	while(1) {
		PROCESS_YIELD();

		if(ev == systemchange){
			cfs_coffee_format();
		}
	}
	PROCESS_END();
}


