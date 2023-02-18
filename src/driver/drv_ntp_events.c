//
#include <time.h>

#include "../new_common.h"
#include "../new_cfg.h"
// Commands register, execution API and cmd tokenizer
#include "../cmnds/cmd_public.h"
#include "../httpserver/new_http.h"
#include "../logging/logging.h"
#include "../ota/ota.h"

#include "drv_ntp.h"

#define LOG_FEATURE LOG_FEATURE_NTP

unsigned int ntp_eventsTime = 0;

typedef struct ntpEvent_s {
	byte hour;
	byte minute;
	byte second;
	byte weekDayFlags;
	int id;
	char *command;
	struct ntpEvent_s *next;
} ntpEvent_t;

ntpEvent_t *ntp_events = 0;


void NTP_RunEventsForSecond(unsigned int runTime) {
	ntpEvent_t *e;
	struct tm *ltm;

	// NOTE: on windows, you need _USE_32BIT_TIME_T 
	ltm = localtime((time_t*)&runTime);
	
	if (ltm == 0) {
		return;
	}

	e = ntp_events;

	while (e) {
		if (e->command) {
			// base check
			if (e->hour == ltm->tm_hour && e->second == ltm->tm_sec && e->minute == ltm->tm_min) {
				// weekday check
				if (BIT_CHECK(e->weekDayFlags, ltm->tm_wday)) {
					CMD_ExecuteCommand(e->command, 0);
				}
			}
		}
		e = e->next;
	}
}
void NTP_RunEvents(unsigned int newTime, bool bTimeValid) {
	unsigned int delta;
	unsigned int i;

	// new time invalid?
	if (bTimeValid == false) {
		ntp_eventsTime = 0;
		return;
	}
	// old time invalid, but new one ok?
	if (ntp_eventsTime == 0) {
		ntp_eventsTime = newTime;
		return;
	}
	// time went backwards
	if (newTime < ntp_eventsTime) {
		ntp_eventsTime = newTime;
		return;
	}
	if (ntp_events) {
		// NTP resynchronization could cause us to skip some seconds in some rare cases?
		delta = newTime - ntp_eventsTime;
		// a large shift in time is not expected, so limit to a constant number of seconds
		if (delta > 100)
			delta = 100;
		for (i = 0; i < delta; i++) {
			NTP_RunEventsForSecond(ntp_eventsTime + i);
		}
	}
	ntp_eventsTime = newTime;
}
void NTP_AddClockEvent(int hour, int minute, int second, int weekDayFlags, int id, const char* command) {
	ntpEvent_t* newEvent = (ntpEvent_t*)malloc(sizeof(ntpEvent_t));
	if (newEvent == NULL) {
		// handle error
		return;
	}

	newEvent->hour = hour;
	newEvent->minute = minute;
	newEvent->second = second;
	newEvent->weekDayFlags = weekDayFlags;
	newEvent->id = id;
	newEvent->command = strdup(command);
	newEvent->next = ntp_events;

	ntp_events = newEvent;
}
void NTP_RemoveClockEvent(int id) {
	ntpEvent_t* curr = ntp_events;
	ntpEvent_t* prev = NULL;

	while (curr != NULL) {
		if (curr->id == id) {
			if (prev == NULL) {
				ntp_events = curr->next;
			}
			else {
				prev->next = curr->next;
			}
			free(curr->command);
			free(curr);
			return;
		}
		prev = curr;
		curr = curr->next;
	}
}
// addClockEvent [Time] [WeekDayFlags] [UniqueIDForRemoval]
// Bit flag 0 - sunday, bit flag 1 - monday, etc...
// Example: do event on 15:30 every weekday, unique ID 123
// addClockEvent 15:30:00 0xff 123 POWER0 ON
// Example: do event on 8:00 every sunday
// addClockEvent 8:00:00 0x01 234 backlog led_temperature 500; led_dimmer 100; led_enableAll 1;
commandResult_t CMD_NTP_AddClockEvent(const void *context, const char *cmd, const char *args, int cmdFlags) {
	int hour, minute, second;
	const char *s;
	int flags;
	int id;

	Tokenizer_TokenizeString(args, 0);
	// following check must be done after 'Tokenizer_TokenizeString',
	// so we know arguments count in Tokenizer. 'cmd' argument is
	// only for warning display
	if (Tokenizer_CheckArgsCountAndPrintWarning(cmd, 4)) {
		return CMD_RES_NOT_ENOUGH_ARGUMENTS;
	}
	s = Tokenizer_GetArg(0);
	if (sscanf(s, "%i:%i:%i", &hour, &minute, &second) <= 1) {
		return CMD_RES_BAD_ARGUMENT;
	}
	flags = Tokenizer_GetArgInteger(1);
	id = Tokenizer_GetArgInteger(2);
	s = Tokenizer_GetArgFrom(3);

	NTP_AddClockEvent(hour, minute, second, flags, id, s);

    return CMD_RES_OK;
}

commandResult_t CMD_NTP_RemoveClockEvent(const void* context, const char* cmd, const char* args, int cmdFlags) {
	int id;

	// tokenize the args string
	Tokenizer_TokenizeString(args, 0);

	// Check the number of arguments
	if (Tokenizer_CheckArgsCountAndPrintWarning(cmd, 1)) {
		return CMD_RES_NOT_ENOUGH_ARGUMENTS;
	}

	// Parse the id parameter
	id = Tokenizer_GetArgInteger(0);

	// Remove the clock event with the given id
	NTP_RemoveClockEvent(id);

	return CMD_RES_OK;
}

void NTP_Init_Events() {

    CMD_RegisterCommand("addClockEvent",CMD_NTP_AddClockEvent, NULL);
	CMD_RegisterCommand("removeClockEvent", CMD_NTP_RemoveClockEvent, NULL);
}
