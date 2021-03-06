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

#include "pulsesensor.h"
#include "contiki.h"

#include "susensors.h"
#include "dev/sys-ctrl.h"
#include "dev/ioc.h"
#include "rest-engine.h"
#include "dev/gptimer.h"
#include "dev/gpio.h"
#include "nvic.h"
#include "deviceSetup.h"
#include "susensorcommon.h"

//TODO: Make these configurable
#define PULSE_PORT            GPIO_A_NUM
//#define PULSE_PIN             3		//target = su-cc2538-1
#define PULSE_PIN             6			//taget = radioOne

PROCESS(pulseinput_int_process, "Pulse input interrupt process handler");

static susensors_sensor_t* pulsesensor = NULL;

static struct relayRuntime pulseinputruntime[1];

struct resourceconf pulseconfig = {
		.resolution = 100,	//0.1W
		.version = 1,		//Q22.10
		.flags = METHOD_GET | METHOD_PUT | IS_OBSERVABLE | HAS_SUB_RESOURCES,			//Flags - which handle do we serve: Get/Post/PUT/Delete
		.max_pollinterval = 30, 					//How often can you ask for a new reading
		.unit = "W",
		.spec = "This is a pulse counter, counting 10000 pulses/kwh",				//Human readable spec of the sensor
		.type = PULSE_SENSOR,
		.attr = "title=\"Pulse counter\" ;rt=\"Control\"",
		//uint8_t notation;		//Qm.f => MMMM.FFFF	eg. Q1.29 = int32_t with 1 integer bit and 29 fraction bits, Q
};

static const settings_t default_pulseCounter_settings = {
		.eventsActive = ChangeEventActive,
		.AboveEventAt = {
				.type = CMP_TYPE_UINT16,
				.as.u16 = 1000
		},
		.BelowEventAt = {
				.type = CMP_TYPE_UINT16,
				.as.u16 = 0
		},
		.ChangeEvent = {
				.type = CMP_TYPE_UINT16,
				.as.u16 = 200
		},
		.RangeMin = {
				.type = CMP_TYPE_UINT16,
				.as.u16 = 0
		},
		.RangeMax = {
				.type = CMP_TYPE_UINT16,
				.as.u16 = 65400
		},
};

/**
 * \brief Retrieves the current upcounted value of the pulse input
 * 		  The counter is reset for every read.
 * \param type Returns ....
 */
//Return 0 for success
//Return 1 for invalid request
static int get(struct susensors_sensor* this, int type, void* data)
{
	int ret = 1;
	cmp_object_t* obj = (cmp_object_t*)data;

	if((enum up_parameter) type == ActualValue){
		obj->type = CMP_TYPE_UINT16;
		obj->as.u16 = (uint16_t) REG(GPT_1_BASE + GPTIMER_TAR) + 1;
		ret = 0;
	}
	return ret;
}

//Return 0 for success
//Return 1 for invalid request
static int set(struct susensors_sensor* this, int type, void* data)
{
	int ret = 1;
	return ret;
}

static int configure(struct susensors_sensor* this, int type, int value)
{
	settings_t* config = this->data.setting;
	switch(type) {
	case SUSENSORS_HW_INIT:

		/* Use Timer1A as a 16bit pulse counter */

		/*
		 * Remove the clock gate to enable GPT1 and then initialise it
		 */
		REG(SYS_CTRL_RCGCGPT) |= SYS_CTRL_RCGCGPT_GPT1;

		/* When hardware control is enabled by the GPIO Alternate Function
		 * Select (GPIO_AFSEL) register (see Section 9.1), the pin state
		 * is controlled by its alternate function (i.e., the peripheral). */
		/* Set PA3 to be pulse counter input */
		REG(IOC_GPT1OCP1) = (PULSE_PORT << 3) + PULSE_PIN;

		/* Set pin PA3 (DIO1) to peripheral mode */
		GPIO_PERIPHERAL_CONTROL(GPIO_PORT_TO_BASE(PULSE_PORT),GPIO_PIN_MASK(PULSE_PIN));

		/* Disable any pull ups or the like */
		//ioc_set_over(PULSE_PORT, PULSE_PIN, IOC_OVERRIDE_DIS);
		//ioc_set_over(PULSE_PORT, PULSE_PIN, IOC_OVERRIDE_PUE);

		/* From user manual p. 329 */
		/* 1. Ensure the timer is disabled (the TAEN bit is cleared) before making any changes. */
		REG(GPT_1_BASE + GPTIMER_CTL) = 0;

		/* 2. Write the GPTM Configuration (GPTIMER_CFG) register with a value of 0x0000.0004. */
		/* 16-bit timer configuration */
		REG(GPT_1_BASE + GPTIMER_CFG) = 0x04;

		/* 3. In the GPTM Timer Mode (GPTIMER_TnMR) register, write the TnCMR field to 0x0 and the TnMR field to 0x3.*/
		/* Capture mode, Edge-count mode*/
		REG(GPT_1_BASE + GPTIMER_TAMR) |= GPTIMER_TAMR_TAMR;		//Capture mode
		REG(GPT_1_BASE + GPTIMER_TAMR) &= ~(GPTIMER_TAMR_TACMR);	//Edge count mode
		REG(GPT_1_BASE + GPTIMER_TAMR) |= GPTIMER_TAMR_TACDIR;		//Count up

		/* 4. Configure the type of event(s) that the timer captures by writing the TnEVENT field of the G Control (GPTIMER_CTL) register. */
		/* Positive edges */
		REG(GPT_1_BASE + GPTIMER_CTL) &= ~GPTIMER_CTL_TAEVENT;	//0=Postive Edge, 1=Negative Edge, 3=Both

		/* 5. If a prescaler is to be used, write the prescale value to the GPTM Timer n Prescale Regist (GPTIMER_TnPR). */
		/* No prescaler */
		REG(GPT_1_BASE + GPTIMER_TAPR) = 0;

		/* 6. Load the timer start value into the GPTM Timer n Interval Load (GPTIMER_TnILR) registe */
		/* When the timer is counting up, this register sets the upper bound for the timeout event. */
		REG(GPT_1_BASE + GPTIMER_TAILR) = config->RangeMax.as.u16 - 1; //config->ChangeEvent.as.u16;	//When reached, its starts over from 0

		/* 7. Load the event count into the GPTM Timer n Match (GPTIMER_TnMATCHR) register. */
		REG(GPT_1_BASE + GPTIMER_TAMATCHR) = config->ChangeEvent.as.u16 - 1;

		/* 8. If interrupts are required, set the CnMIM bit in the GPTM Interrupt Mask (GPTIMER_IMR) register. */
		//REG(GPT_1_BASE + GPTIMER_IMR) |= (GPTIMER_IMR_CAMIM + GPTIMER_IMR_TATOIM);
		REG(GPT_1_BASE + GPTIMER_IMR) |= GPTIMER_IMR_CAMIM;

		process_start(&pulseinput_int_process, NULL);
		break;
	case SUSENSORS_ACTIVE:
		if(value){	//Activate
			if(!REG(GPT_1_BASE + GPTIMER_CTL) & GPTIMER_CTL_TAEN){
				/* Enable interrupts */
				//REG(GPT_1_BASE + GPTIMER_IMR) |= (GPTIMER_IMR_CAMIM + GPTIMER_IMR_TATOIM);
				REG(GPT_1_BASE + GPTIMER_IMR) |= GPTIMER_IMR_CAMIM;
				NVIC_ClearPendingIRQ(GPT1A_IRQn);
				NVIC_EnableIRQ(GPT1A_IRQn);
				REG(GPT_1_BASE + GPTIMER_CTL) |= GPTIMER_CTL_TAEN;	//Enable pulse counter
			}
		}
		else{
			if(REG(GPT_1_BASE + GPTIMER_CTL) & GPTIMER_CTL_TAEN){
				REG(GPT_1_BASE + GPTIMER_CTL) &= ~GPTIMER_CTL_TAEN;	//disable pulse counter

				/* Disable interrupts */
				//REG(GPT_1_BASE + GPTIMER_IMR) &= ~(GPTIMER_IMR_CAMIM + GPTIMER_IMR_TATOIM);
				REG(GPT_1_BASE + GPTIMER_IMR) &= ~GPTIMER_IMR_CAMIM;
				NVIC_DisableIRQ(GPT1A_IRQn);
			}
		}
		break;
	}
	return 0;
}

susensors_sensor_t* addASUPulseInputRelay(const char* name, settings_t* settings){

	if(deviceSetupGet(name, settings, &default_pulseCounter_settings) < 0) return 0;

	susensors_sensor_t d;
	d.type = (char*)name;
	d.status = get;
	d.value = set;
	d.configure = configure;
	d.eventhandler = testevent;
	d.suconfig = suconfig;
	d.data.config = &pulseconfig;
	d.data.setting = settings;

	d.setEventhandlers = NULL;

	pulseinputruntime[0].enabled = 0;
	pulseinputruntime[0].hasEvent = 0,
	pulseinputruntime[0].LastEventValue.type = CMP_TYPE_UINT8;
	pulseinputruntime[0].LastEventValue.as.u8 = 0;
	pulseinputruntime[0].ChangeEventAcc.as.u8 = 0;
	d.data.runtime = (void*) &pulseinputruntime[0];

	pulsesensor = addSUDevices(&d);
	return pulsesensor;
}

/**
 * Interrupt handling
 *
 * The ISR is added to our own vector in the SU platform startup-gcc.c
 *
 */

void pulscounter_isr(){

	if(REG(GPT_1_BASE + GPTIMER_MIS) & GPTIMER_MIS_CAMMIS){
		settings_t* config = pulsesensor->data.setting;
		volatile uint32_t cmpreg;

		//Set the next compare to the next interval.
		cmpreg = REG_H(GPT_1_BASE + GPTIMER_TAMATCHR);
		cmpreg += config->ChangeEvent.as.u16;
		cmpreg = cmpreg >= config->RangeMax.as.u16 ? cmpreg - config->RangeMax.as.u16 : cmpreg;
		REG(GPT_1_BASE + GPTIMER_TAMATCHR) = (uint16_t)cmpreg;
	}
	process_poll(&pulseinput_int_process);

	//both cases, the status flags are cleared by writing a 1 to the CnMCINT bit of the GPTM Interrupt Clear (GPTIMER_ICR) register.
	REG(GPT_1_BASE + GPTIMER_ICR) |= (GPTIMER_IMR_CAMIM + GPTIMER_IMR_TATOIM);
}

PROCESS_THREAD(pulseinput_int_process, ev, data)
{
	PROCESS_EXITHANDLER();
	PROCESS_BEGIN();

	while(1) {

		PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL);

		/* For now we only have one pulse counter, so we dont have to know the pulsecounter instance */
		struct relayRuntime* r = (struct relayRuntime*)pulsesensor->data.runtime;
		settings_t* config = pulsesensor->data.setting;

		uint16_t val = (uint16_t) REG(GPT_1_BASE + GPTIMER_TAR) + 1;
		uint16_t step = 0;

		if(r->LastValue.as.u16 > val){	//Roll over
			step = config->RangeMax.as.u16 - r->LastValue.as.u16 + val;
		}
		else{
			step = val - r->LastValue.as.u16;
		}

		r->LastValue.as.u16 = val;

		setEventU16(pulsesensor, 1, step);
	}

	PROCESS_END();
}

