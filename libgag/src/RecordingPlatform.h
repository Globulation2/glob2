// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
// Browser implementation lives outside the shared libraries.
extern "C"
{
int browserRecordingAvailable();
int browserRecordingRoom();
void browserRecordingStart(const char *,int,int,int,int,const char *,double);
int browserRecordingFrame(const unsigned char *,int,int,double,const char *);
int browserRecordingAudio(const std::int16_t *,int,double);
void browserRecordingStop(double);
void browserRecordingFail(const char *);
void browserRecordingEvent(double,const char *,const char *);
void browserRecordingDrops(double,double);
char *browserRecordingStatus();
char *browserRecordingBase();
char *browserRecordingFilesStatus();
void browserRecordingFilesRefresh();
void browserRecordingFilesRecover(const char *);
void browserRecordingFilesRemove(const char *);
}
