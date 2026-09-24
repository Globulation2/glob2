#include "VoiceRecorder.h"
VoiceRecorder::VoiceRecorder() : speexEncoderState(nullptr), frameSize(0),
    recordingThread(nullptr), ordersMutex(nullptr), recordingNow(false),
    recordThreadRun(false), stopRecordingTimeout(0) {}
VoiceRecorder::~VoiceRecorder() = default;
void VoiceRecorder::startRecording() {}
void VoiceRecorder::stopRecording() {}
std::shared_ptr<OrderVoiceData> VoiceRecorder::getNextOrder() { return {}; }
