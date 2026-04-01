#pragma once

#include "mathlib/vector.h"
#include <cstdint>

// Minimal interface for a custom audio backend used only for mod/custom WAV playback.
class ICustomAudioBackend
{
public:
    virtual ~ICustomAudioBackend() = default;

    // Initialize/shutdown the backend. Return true on success.
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    // Per-frame maintenance; dt in seconds (may be 0 if unknown).
    virtual void Update(float dt) = 0;

    // Listener transforms (we only need position for now).
    virtual void SetListenerPosition(const Vector3D& position, const QAngle& rotation) = 0;

    // Play a raw PCM buffer as a 3D sound. Returns a unique sample id (>0) or 0 on failure.
    virtual uint64_t PlayRawPCM3D(
        const void* pcmData,
        unsigned int byteLength,
        int sampleRate,
        unsigned short numChannels,
        unsigned short bitsPerSample,
        const Vector3D& position,
        float initialVolume,
        const char* eventName) = 0;

    // Play a 3D event from a bank; return unique id (>0) on success, 0 if unsupported or not found.
    // If milesEventHash is provided, it will be used for tracking the event instance for stop calls.
    virtual uint64_t PlayEvent3D(
        const char* eventPathOrName,
        const Vector3D& position,
        float initialVolume,
        uint64_t milesEventHash = 0) = 0;

    // Stop a specific event instance by its Miles event hash/ID
    virtual bool StopEventByHash(uint64_t milesEventHash, bool immediate = false) = 0;

    // Stop all samples for a specific logical event name. Returns number of samples stopped.
    virtual int StopSamplesForEvent(const char* eventName, bool immediate = false) = 0;

    // Stop all active samples.
    virtual void StopAll(bool immediate = false) = 0;

    // Pause/Resume event by hash
    virtual bool PauseEventByHash(uint64_t milesEventHash, bool paused) = 0;

    // Set volume for event by hash
    virtual bool SetEventVolumeByHash(uint64_t milesEventHash, float volume) = 0;

    // Set 3D position for event by hash
    virtual bool SetEventPositionByHash(uint64_t milesEventHash, const Vector3D& position) = 0;

    // Optional: load an FMOD Studio bank by base name (without extension). Default no-op.
    virtual bool LoadBankFile(const char* /*bankName*/) { return false; }

    // Optional: check if an event exists in loaded content (FMOD Studio). Default false.
    virtual bool EventExists(const char* /*eventPathOrName*/) { return false; }

    // Optional: check if an event has a boolean user property. Default false.
    virtual bool GetUserPropertyBool(const char* /*eventPathOrName*/, const char* /*propertyName*/) { return false; }

    // Check if an event is currently playing by hash
    virtual bool IsEventPlaying(uint64_t milesEventHash) { return false; }

    // Get the number of active instances for debugging
    virtual int GetActiveInstanceCount() { return 0; }

    // Enumerate all event paths in loaded banks (for pre-caching hashes)
    virtual void EnumerateEvents(void (*callback)(const char* eventPath, void* userData), void* userData) {}
};

// Active backend singleton accessors
ICustomAudioBackend* GetActiveCustomAudioBackend();
void SetActiveCustomAudioBackend(ICustomAudioBackend* backendInstance);

// Factory for FMOD backend (implemented in fmod_backend.cpp)
ICustomAudioBackend* CreateFMODBackend();

// Factory for FMOD Studio backend (implemented in fmod_studio_backend.cpp)
ICustomAudioBackend* CreateFMODStudioBackend();


