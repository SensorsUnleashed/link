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

#ifndef SENSORS_H_
#define SENSORS_H_

#include "contiki.h"
#include "lib/list.h"
#include "cmp_helpers.h"

#define SU_VER_MAJOR	0	//Is increased when there has been changes to the protocol, which is not backwards compatible
#define SU_VER_MINOR	0	//Is increased if the protocol is changed, but still backwards compatible
#define SU_VER_DEV		2	//Is increased for every minor fix

#include "coap-observe-client.h"
#define DEVICES_MAX		10

#define SUSENSORS_NO_EVENT		0
#define SUSENSORS_ABOVE_EVENT	(1 << 1)	//2
#define SUSENSORS_BELOW_EVENT	(1 << 2)	//4
#define SUSENSORS_CHANGE_EVENT	(1 << 3)	//8
#define SUSENSORS_NEW_PAIR		(1 << 4)

extern const char* strAbove;
extern const char* strBelow;
extern const char* strChange;

typedef int (* eventhandler_ptr)(void* this, int len, const uint8_t* payload);

enum su_basic_events {
	aboveEvent,
	belowEvent,
	changeEvent,
	noEvent,
};
enum su_basic_actions {
	setOn,
	setOff,
	setToggle,
};

enum su_timer_actions{
	timerStop,
	timerStart,
	timerRestart,
};

enum susensors_configcmd {
	SUSENSORS_EVENTSETUP_SET,
	SUSENSORS_EVENTSETUP_GET,
	SUSENSORS_CEVENT_GET,
	SUSENSORS_AEVENT_GET,
	SUSENSORS_BEVENT_GET,
	SUSENSORS_RANGEMAX_GET,
	SUSENSORS_RANGEMIN_GET,
	SUSENSORS_EVENTSTATE_GET,
	SUSENSORS_STORE_SETUP,
};

enum susensors_event_cmd {
	SUSENSORS_ABOVE_EVENT_SET,
	SUSENSORS_BELOW_EVENT_SET,
	SUSENSORS_CHANGE_EVENT_SET,
};

/* some constants for the configure API */
#define SUSENSORS_HW_INIT 	128 /* internal - used only for initialization */
#define SUSENSORS_ACTIVE 	129 /* ACTIVE => 0 -> turn off, 1 -> turn on */
#define SUSENSORS_READY 	130 /* read only */

//TODO: Rename this to something common
struct relayRuntime {
	uint8_t enabled;
	uint8_t hasEvent;
	cmp_object_t LastEventValue;
	cmp_object_t LastValue;
	cmp_object_t ChangeEventAcc;	///Accumulated steps; for determining if event should be fired
};

struct ledRuntime {
	uint8_t mask;
};

/* Used for extra material needed for using a sensor */
struct extras{
	int type;
	void* config;		//Will contain struct resourceconf file
	void* setting;		//Will contain stored settings
	void* runtime;		//Will contain current runtime data
	void* resource;		//Will contain the build resource from the config file
};

struct storedSetting_s {
	/* Pseudocode:
	 *
	 * Eg. We want a toggle button to signal when its on, but not off
	 * AboveEventAt = 1
	 * BelowEventAt = 2	//Off because its outside button spec
	 * changeEvent  = 2	//Off because its outside button spec
	 *
	 * We want a temperature sensor to feed us with updates with every 0.5C
	 * AboveEvent = 9999	//Off because its outside button spec
	 * BelowEvent = 9999	//Off because its outside button spec
	 * changeEvent = 0.5	//For every 0.5C low/high from last event, a new will be emitted
	 *
	 * It is possible to have all event types enabled together, but its the subscriber that
	 * will need to detect which event was fired. They are all alike.
	 * */
	uint8_t cfs_file_id;		///The file id of the setup file from flash
	uint8_t eventsActive;		///Generation of events on or Off (Determined by eventstate)
	cmp_object_t AboveEventAt;	///When resource crosses this line from low to high give an event (>=)
	cmp_object_t BelowEventAt;	///When resource crosses this line from high to low give an event (<=)
    cmp_object_t ChangeEvent;	///When value has changed more than changeEvent + lastevent value <>= value

    cmp_object_t RangeMin;		///What is the minimum value this device can read
    cmp_object_t RangeMax;		///What is the maximum value this device can read
};
typedef struct storedSetting_s settings_t;

struct susensors_sensor {
	struct susensors_sensor* next;
	char *       type;

	unsigned char event_flag;

	/* Set device values */
	int (* value)     			(struct susensors_sensor* this, int type, void* data);
	/* Get/set device hardware specific configuration */
	int (* configure) 			(struct susensors_sensor* this, int type, int value);
	/* Get device values */
	int (* status)    			(struct susensors_sensor* this, int type, void* data);
	/* Force emitting an event - used for testing from the configuration tool */
	int (* eventhandler)		(struct susensors_sensor* this, int len, uint8_t* payload);

	eventhandler_ptr (* setEventhandlers)	(struct susensors_sensor* this, int8_t trigger);

	/* Get/set device suconfig (common to all devices) */
	int (* suconfig)  			(struct susensors_sensor* this, int type, void* data);

	struct extras data;

	LIST_STRUCT(pairs);
};

typedef struct susensors_sensor susensors_sensor_t;

void initSUSensors();
susensors_sensor_t* addSUDevices(susensors_sensor_t* device);
susensors_sensor_t* susensors_find(const char *type, unsigned short len);
susensors_sensor_t* susensors_next(susensors_sensor_t* s);
susensors_sensor_t* susensors_first(void);

int missingJustCalled(uip_ip6addr_t* srcip);

void susensors_changed(susensors_sensor_t* s, uint8_t event);

PROCESS_NAME(susensors_process);

#endif /* SENSORS_H_ */
