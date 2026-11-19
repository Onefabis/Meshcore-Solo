#pragma once
#include <stdint.h>
#ifdef PRIVATE_STATUS_REPORTER
void trackerAudioStart(bool alarm);
void trackerAudioStop();
void trackerAudioLoop();
bool trackerAudioAvailable();
#endif
