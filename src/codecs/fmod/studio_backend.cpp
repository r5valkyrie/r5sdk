#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "audio_backend.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <cfloat>

#include "thirdparty/fmod/inc/fmod_studio.h"
#include "thirdparty/fmod/inc/fmod_studio.hpp"
#include "thirdparty/fmod/inc/fmod.hpp"

#include "../miles/miles_impl.h"
#include "pluginsystem/modsystem.h"

// Forward declaration for debug printing
extern ConVar* fmod_debug;

// Structure to track active FMOD event instances
struct FMODEventInstance
{
    FMOD::Studio::EventInstance* instance;
    std::string eventPath;
    uint64_t milesHash;      // The Miles event hash for tracking
    uint64_t internalId;     // Our internal ID
    bool isLooping;          // Track if this is a looping sound
    double startTime;        // Time when this instance started (for FIFO ordering)
};

class FMODStudioBackend final : public ICustomAudioBackend
{
public:
    bool Initialize() override
    {
        if (m_studioSystem)
            return true;

        FMOD::Studio::System::create(&m_studioSystem);
        if (!m_studioSystem) return false;

        // Use FMOD_STUDIO_INIT_SYNCHRONOUS_UPDATE to ensure proper cleanup
        m_studioSystem->initialize(512, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL | FMOD_INIT_3D_RIGHTHANDED, nullptr);
        m_studioSystem->getCoreSystem(&m_core);
        if (!m_core) return false;

        // Set 3D settings for proper spatial audio
        m_core->set3DSettings(1.0f, 39.37f, 1.0f); // Doppler, distance factor (inches to meters), rolloff

        LoadBaseBanks();
        LoadModBanks();
        return true;
    }

    void Shutdown() override
    {
        // Stop and release all active instances
        {
            std::lock_guard<std::mutex> lock(m_instanceMutex);
            for (auto& pair : m_activeInstances)
            {
                if (pair.second.instance)
                {
                    pair.second.instance->stop(FMOD_STUDIO_STOP_IMMEDIATE);
                    pair.second.instance->release();
                }
            }
            m_activeInstances.clear();
            m_hashToInstances.clear();
        }

        for (auto& it : m_loadedBanks)
        {
            if (it.second)
                it.second->unload();
        }
        m_loadedBanks.clear();

        if (m_studioSystem)
        {
            m_studioSystem->unloadAll();
            m_studioSystem->release();
            m_studioSystem = nullptr;
        }
        m_core = nullptr;
    }

    void Update(float /*dt*/) override
    {
        if (!m_studioSystem)
            return;

        // Sync global volume from engine cvar (Miles global state)
        SyncGlobalVolume();

        // Clean up stopped instances
        CleanupStoppedInstances();

        m_studioSystem->update();
    }

    void SetListenerPosition(const Vector3D& position, const QAngle& rotation) override
    {
        if (!m_studioSystem) return;
        FMOD_3D_ATTRIBUTES attrs{};

        Vector3D forward, up;
        AngleVectors(rotation, &forward, nullptr, &up);

        // Source engine uses X=forward, Y=left, Z=up
        // FMOD uses right-handed coordinate system
        attrs.position = { position.x, position.z, position.y };
        attrs.forward = { -forward.x, -forward.z, -forward.y };
        attrs.up = { up.x, up.z, up.y };
        m_studioSystem->setListenerAttributes(0, &attrs);
    }

    uint64_t PlayRawPCM3D(const void*, unsigned int, int, unsigned short, unsigned short, const Vector3D&, float, const char*) override
    {
        // Not supported in Studio backend
        return 0;
    }

    uint64_t PlayEvent3D(const char* eventPathOrName, const Vector3D& position, float initialVolume, uint64_t milesEventHash = 0) override
    {
        if (!m_studioSystem || !eventPathOrName) return 0;

        // Ensure path has event:/ prefix
        char pathBuf[512];
        const char* query = eventPathOrName;
        if (V_strnicmp(query, "event:/", 7) != 0)
        {
            V_snprintf(pathBuf, sizeof(pathBuf), "event:/%s", eventPathOrName);
            query = pathBuf;
        }

        FMOD::Studio::EventDescription* desc = nullptr;
        if (m_studioSystem->getEvent(query, &desc) != FMOD_OK || !desc)
            return 0;

        FMOD::Studio::EventInstance* inst = nullptr;
        if (desc->createInstance(&inst) != FMOD_OK || !inst)
            return 0;

        // Set 3D position - convert from Source coords to FMOD coords
        FMOD_3D_ATTRIBUTES attrs{};
        attrs.position = { position.x, position.z, position.y };
        attrs.forward = { 0.0f, 0.0f, 1.0f };
        attrs.up = { 0.0f, 1.0f, 0.0f };
        inst->set3DAttributes(&attrs);
        inst->setVolume(initialVolume);

        // Check if this event is oneshot or looping
        bool isOneshot = false;
        desc->isOneshot(&isOneshot);

        inst->start();

        // Only auto-release oneshot events that don't need tracking
        if (isOneshot && milesEventHash == 0)
        {
            inst->release();
            return ++m_nextId;
        }

        // Track this instance - use timestamp for FIFO ordering when stopping
        uint64_t id = ++m_nextId;
        double startTime = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        {
            std::lock_guard<std::mutex> lock(m_instanceMutex);
            
            // INSTANCE STEALING: When rapidly firing the same sound, Miles may not send
            // stop events for every start. To prevent orphaned instances piling up,
            // limit concurrent instances per hash. For sounds with "loop" in the name,
            // only allow 1 instance (new one steals from old). For other sounds, allow 2.
            if (milesEventHash != 0)
            {
                auto& instanceList = m_hashToInstances[milesEventHash];
                
                // Determine max instances based on event name
                // Loop sounds typically should only have 1 instance at a time
                bool isLoopSound = (V_stristr(query, "loop") != nullptr);
                size_t maxInstances = isLoopSound ? 1 : 2;
                
                if (fmod_debug && fmod_debug->GetBool() && !instanceList.empty())
                {
                    Msg(eDLL_T::AUDIO, "FMOD: PlayEvent3D check - existing=%zu, max=%zu, isLoop=%d for '%s'\n",
                        instanceList.size(), maxInstances, isLoopSound, query);
                }
                
                // Stop oldest instances if we're at the limit
                while (instanceList.size() >= maxInstances)
                {
                    // Find and stop the oldest instance
                    uint64_t oldestId = 0;
                    double oldestTime = DBL_MAX;
                    size_t oldestIdx = 0;
                    
                    for (size_t i = 0; i < instanceList.size(); i++)
                    {
                        auto instIt = m_activeInstances.find(instanceList[i]);
                        if (instIt != m_activeInstances.end() && instIt->second.startTime < oldestTime)
                        {
                            oldestTime = instIt->second.startTime;
                            oldestId = instanceList[i];
                            oldestIdx = i;
                        }
                    }
                    
                    if (oldestId != 0)
                    {
                        auto instIt = m_activeInstances.find(oldestId);
                        if (instIt != m_activeInstances.end() && instIt->second.instance)
                        {
                            // Stop IMMEDIATELY to prevent sound overlap during rapid fire
                            instIt->second.instance->stop(FMOD_STUDIO_STOP_IMMEDIATE);
                            instIt->second.instance->release();
                            
                            if (fmod_debug && fmod_debug->GetBool())
                            {
                                Msg(eDLL_T::AUDIO, "FMOD: Instance stealing - stopped old instance id=%llu for '%s'\n",
                                    oldestId, query);
                            }
                        }
                        m_activeInstances.erase(oldestId);
                        instanceList.erase(instanceList.begin() + oldestIdx);
                    }
                    else
                    {
                        // No valid instances found, clear the list
                        instanceList.clear();
                        break;
                    }
                }
            }
            
            FMODEventInstance tracked;
            tracked.instance = inst;
            tracked.eventPath = query;
            tracked.milesHash = milesEventHash;
            tracked.internalId = id;
            tracked.isLooping = !isOneshot;
            tracked.startTime = startTime;
            m_activeInstances[id] = tracked;

            // For rapid-fire sounds, we need to track MULTIPLE instances per hash
            // Use a multimap-style approach: store list of instance IDs per hash
            if (milesEventHash != 0)
            {
                m_hashToInstances[milesEventHash].push_back(id);
            }

            if (fmod_debug && fmod_debug->GetBool())
            {
                size_t instanceCount = milesEventHash != 0 ? m_hashToInstances[milesEventHash].size() : 1;
                Msg(eDLL_T::AUDIO, "FMOD: Started event '%s' (id=%llu, hash=0x%llX, looping=%d, instances=%zu)\n",
                    query, id, milesEventHash, !isOneshot, instanceCount);
            }
        }

        return id;
    }

    bool StopEventByHash(uint64_t milesEventHash, bool immediate = false) override
    {
        if (milesEventHash == 0) return false;

        std::lock_guard<std::mutex> lock(m_instanceMutex);
        
        // Find all instances for this hash
        auto hashIt = m_hashToInstances.find(milesEventHash);
        if (hashIt == m_hashToInstances.end() || hashIt->second.empty())
            return false;

        // Stop the OLDEST instance (FIFO) - this matches how Miles processes stop events
        // When rapidly firing, stops should match starts in order
        uint64_t oldestId = 0;
        double oldestTime = DBL_MAX;  // Use DBL_MAX to avoid Windows max() macro conflict
        size_t oldestIdx = 0;
        
        for (size_t i = 0; i < hashIt->second.size(); i++)
        {
            uint64_t id = hashIt->second[i];
            auto instIt = m_activeInstances.find(id);
            if (instIt != m_activeInstances.end() && instIt->second.startTime < oldestTime)
            {
                oldestTime = instIt->second.startTime;
                oldestId = id;
                oldestIdx = i;
            }
        }
        
        if (oldestId == 0)
        {
            // No valid instances found, clean up the hash entry
            m_hashToInstances.erase(hashIt);
            return false;
        }

        auto instIt = m_activeInstances.find(oldestId);
        if (instIt != m_activeInstances.end() && instIt->second.instance)
        {
            FMOD_STUDIO_STOP_MODE mode = immediate ? FMOD_STUDIO_STOP_IMMEDIATE : FMOD_STUDIO_STOP_ALLOWFADEOUT;
            instIt->second.instance->stop(mode);

            if (fmod_debug && fmod_debug->GetBool())
            {
                Msg(eDLL_T::AUDIO, "FMOD: Stopped event by hash 0x%llX (id=%llu, immediate=%d, remaining=%zu)\n",
                    milesEventHash, oldestId, immediate, hashIt->second.size() - 1);
            }

            // Release and remove from active instances
            instIt->second.instance->release();
            m_activeInstances.erase(instIt);
        }

        // Remove this ID from the hash->instances list
        hashIt->second.erase(hashIt->second.begin() + oldestIdx);
        
        // Clean up empty hash entries
        if (hashIt->second.empty())
        {
            m_hashToInstances.erase(hashIt);
        }
        
        return true;
    }

    int StopSamplesForEvent(const char* eventName, bool immediate = false) override
    {
        if (!eventName) return 0;

        // Ensure path has event:/ prefix for comparison
        char pathBuf[512];
        const char* query = eventName;
        if (V_strnicmp(query, "event:/", 7) != 0)
        {
            V_snprintf(pathBuf, sizeof(pathBuf), "event:/%s", eventName);
            query = pathBuf;
        }

        int count = 0;
        std::lock_guard<std::mutex> lock(m_instanceMutex);

        std::vector<uint64_t> toRemove;
        for (auto& pair : m_activeInstances)
        {
            if (pair.second.eventPath == query)
            {
                if (pair.second.instance)
                {
                    FMOD_STUDIO_STOP_MODE mode = immediate ? FMOD_STUDIO_STOP_IMMEDIATE : FMOD_STUDIO_STOP_ALLOWFADEOUT;
                    pair.second.instance->stop(mode);
                    pair.second.instance->release();
                    count++;
                }
                toRemove.push_back(pair.first);

                // Also remove from hash->instances map
                if (pair.second.milesHash != 0)
                {
                    auto hashIt = m_hashToInstances.find(pair.second.milesHash);
                    if (hashIt != m_hashToInstances.end())
                    {
                        auto& vec = hashIt->second;
                        vec.erase(std::remove(vec.begin(), vec.end(), pair.first), vec.end());
                        if (vec.empty())
                        {
                            m_hashToInstances.erase(hashIt);
                        }
                    }
                }
            }
        }

        for (uint64_t id : toRemove)
        {
            m_activeInstances.erase(id);
        }

        if (fmod_debug && fmod_debug->GetBool() && count > 0)
        {
            Msg(eDLL_T::AUDIO, "FMOD: Stopped %d instances of event '%s'\n", count, query);
        }

        return count;
    }

    void StopAll(bool immediate = false) override
    {
        std::lock_guard<std::mutex> lock(m_instanceMutex);

        FMOD_STUDIO_STOP_MODE mode = immediate ? FMOD_STUDIO_STOP_IMMEDIATE : FMOD_STUDIO_STOP_ALLOWFADEOUT;

        for (auto& pair : m_activeInstances)
        {
            if (pair.second.instance)
            {
                pair.second.instance->stop(mode);
                pair.second.instance->release();
            }
        }

        if (fmod_debug && fmod_debug->GetBool())
        {
            Msg(eDLL_T::AUDIO, "FMOD: Stopped all %zu instances\n", m_activeInstances.size());
        }

        m_activeInstances.clear();
        m_hashToInstances.clear();
    }

    bool PauseEventByHash(uint64_t milesEventHash, bool paused) override
    {
        if (milesEventHash == 0) return false;

        std::lock_guard<std::mutex> lock(m_instanceMutex);
        auto hashIt = m_hashToInstances.find(milesEventHash);
        if (hashIt == m_hashToInstances.end() || hashIt->second.empty())
            return false;

        // Pause/resume ALL instances with this hash
        bool anySuccess = false;
        for (uint64_t id : hashIt->second)
        {
            auto instIt = m_activeInstances.find(id);
            if (instIt != m_activeInstances.end() && instIt->second.instance)
            {
                instIt->second.instance->setPaused(paused);
                anySuccess = true;
            }
        }

        if (fmod_debug && fmod_debug->GetBool() && anySuccess)
        {
            Msg(eDLL_T::AUDIO, "FMOD: %s %zu instance(s) by hash 0x%llX\n",
                paused ? "Paused" : "Resumed", hashIt->second.size(), milesEventHash);
        }

        return anySuccess;
    }

    bool SetEventVolumeByHash(uint64_t milesEventHash, float volume) override
    {
        if (milesEventHash == 0) return false;

        std::lock_guard<std::mutex> lock(m_instanceMutex);
        auto hashIt = m_hashToInstances.find(milesEventHash);
        if (hashIt == m_hashToInstances.end() || hashIt->second.empty())
            return false;

        // Set volume on ALL instances with this hash
        bool anySuccess = false;
        for (uint64_t id : hashIt->second)
        {
            auto instIt = m_activeInstances.find(id);
            if (instIt != m_activeInstances.end() && instIt->second.instance)
            {
                instIt->second.instance->setVolume(volume);
                anySuccess = true;
            }
        }
        return anySuccess;
    }

    bool SetEventPositionByHash(uint64_t milesEventHash, const Vector3D& position) override
    {
        if (milesEventHash == 0) return false;

        std::lock_guard<std::mutex> lock(m_instanceMutex);
        auto hashIt = m_hashToInstances.find(milesEventHash);
        if (hashIt == m_hashToInstances.end() || hashIt->second.empty())
            return false;

        FMOD_3D_ATTRIBUTES attrs{};
        attrs.position = { position.x, position.z, position.y };
        attrs.forward = { 0.0f, 0.0f, 1.0f };
        attrs.up = { 0.0f, 1.0f, 0.0f };

        // Update position on ALL instances with this hash
        bool anySuccess = false;
        for (uint64_t id : hashIt->second)
        {
            auto instIt = m_activeInstances.find(id);
            if (instIt != m_activeInstances.end() && instIt->second.instance)
            {
                instIt->second.instance->set3DAttributes(&attrs);
                anySuccess = true;
            }
        }
        return anySuccess;
    }

    bool IsEventPlaying(uint64_t milesEventHash) override
    {
        if (milesEventHash == 0) return false;

        std::lock_guard<std::mutex> lock(m_instanceMutex);
        auto hashIt = m_hashToInstances.find(milesEventHash);
        if (hashIt == m_hashToInstances.end() || hashIt->second.empty())
            return false;

        // Return true if ANY instance with this hash is playing
        for (uint64_t id : hashIt->second)
        {
            auto instIt = m_activeInstances.find(id);
            if (instIt != m_activeInstances.end() && instIt->second.instance)
            {
                FMOD_STUDIO_PLAYBACK_STATE state;
                instIt->second.instance->getPlaybackState(&state);
                if (state == FMOD_STUDIO_PLAYBACK_PLAYING || state == FMOD_STUDIO_PLAYBACK_STARTING)
                    return true;
            }
        }
        return false;
    }

    int GetActiveInstanceCount() override
    {
        std::lock_guard<std::mutex> lock(m_instanceMutex);
        return static_cast<int>(m_activeInstances.size());
    }

    void EnumerateEvents(void (*callback)(const char* eventPath, void* userData), void* userData) override
    {
        if (!m_studioSystem || !callback) return;

        // Enumerate events from all loaded banks
        for (const auto& bankPair : m_loadedBanks)
        {
            FMOD::Studio::Bank* bank = bankPair.second;
            if (!bank) continue;

            int eventCount = 0;
            bank->getEventCount(&eventCount);
            if (eventCount <= 0) continue;

            std::vector<FMOD::Studio::EventDescription*> events(eventCount);
            bank->getEventList(events.data(), eventCount, &eventCount);

            for (int i = 0; i < eventCount; i++)
            {
                if (!events[i]) continue;

                char pathBuf[512];
                int retrieved = 0;
                if (events[i]->getPath(pathBuf, sizeof(pathBuf), &retrieved) == FMOD_OK && retrieved > 0)
                {
                    callback(pathBuf, userData);
                }
            }
        }
    }

    // Utility: print currently loaded banks
    void ListLoadedBanks() const
    {
        int count = (int)m_loadedBanks.size();
        Msg(eDLL_T::AUDIO, "FMOD: %d bank(s) loaded\n", count);
        for (const auto& it : m_loadedBanks)
        {
            Msg(eDLL_T::AUDIO, " - %s\n", it.first.c_str());
        }
    }

    // List active instances for debugging
    void ListActiveInstances() const
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_instanceMutex));
        Msg(eDLL_T::AUDIO, "FMOD: %zu active instance(s)\n", m_activeInstances.size());
        for (const auto& pair : m_activeInstances)
        {
            FMOD_STUDIO_PLAYBACK_STATE state = FMOD_STUDIO_PLAYBACK_STOPPED;
            if (pair.second.instance)
            {
                pair.second.instance->getPlaybackState(&state);
            }
            const char* stateStr = "unknown";
            switch (state)
            {
                case FMOD_STUDIO_PLAYBACK_PLAYING: stateStr = "playing"; break;
                case FMOD_STUDIO_PLAYBACK_SUSTAINING: stateStr = "sustaining"; break;
                case FMOD_STUDIO_PLAYBACK_STOPPED: stateStr = "stopped"; break;
                case FMOD_STUDIO_PLAYBACK_STARTING: stateStr = "starting"; break;
                case FMOD_STUDIO_PLAYBACK_STOPPING: stateStr = "stopping"; break;
            }
            Msg(eDLL_T::AUDIO, "  [%llu] %s (hash=0x%llX, state=%s, looping=%d)\n",
                pair.first, pair.second.eventPath.c_str(), pair.second.milesHash, stateStr, pair.second.isLooping);
        }
    }

    // Reload all FMOD banks (both base and mod banks)
    void ReloadAllBanks()
    {
        Msg(eDLL_T::AUDIO, "FMOD: Reloading all banks...\n");

        // Stop all playing sounds first
        StopAll(true);

        // Unload all current banks
        for (auto& it : m_loadedBanks)
        {
            if (it.second)
            {
                Msg(eDLL_T::AUDIO, "FMOD: Unloading bank '%s'\n", it.first.c_str());
                it.second->unload();
            }
        }
        m_loadedBanks.clear();

        // Reset master bus reference (will be reacquired on next sync)
        m_masterBus = nullptr;

        // Reload all banks
        LoadBaseBanks();
        LoadModBanks();

        Msg(eDLL_T::AUDIO, "FMOD: Bank reload complete. %d bank(s) loaded\n", (int)m_loadedBanks.size());
    }

    // Reload only base game banks
    void ReloadBaseBanks()
    {
        Msg(eDLL_T::AUDIO, "FMOD: Reloading base banks...\n");

        // Unload base banks (those without '/' in the key, indicating mod banks)
        auto it = m_loadedBanks.begin();
        while (it != m_loadedBanks.end())
        {
            if (it->first.find('/') == std::string::npos) // No '/' means base bank
            {
                if (it->second)
                {
                    Msg(eDLL_T::AUDIO, "FMOD: Unloading base bank '%s'\n", it->first.c_str());
                    it->second->unload();
                }
                it = m_loadedBanks.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Reload base banks
        LoadBaseBanks();

        Msg(eDLL_T::AUDIO, "FMOD: Base bank reload complete\n");
    }

    // Reload only mod banks
    void ReloadModBanks()
    {
        Msg(eDLL_T::AUDIO, "FMOD: Reloading mod banks...\n");

        // Unload mod banks (those with '/' in the key)
        auto it = m_loadedBanks.begin();
        while (it != m_loadedBanks.end())
        {
            if (it->first.find('/') != std::string::npos) // '/' means mod bank
            {
                if (it->second)
                {
                    Msg(eDLL_T::AUDIO, "FMOD: Unloading mod bank '%s'\n", it->first.c_str());
                    it->second->unload();
                }
                it = m_loadedBanks.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Reload mod banks
        LoadModBanks();

        Msg(eDLL_T::AUDIO, "FMOD: Mod bank reload complete\n");
    }

    bool EventExists(const char* eventPathOrName) override
    {
        if (!m_studioSystem || !eventPathOrName) return false;
        // Try with provided string, then with event:/ prefix
        FMOD::Studio::EventDescription* desc = nullptr;
        if (m_studioSystem->getEvent(eventPathOrName, &desc) == FMOD_OK && desc != nullptr)
            return true;
        char pathBuf[512];
        if (V_strnicmp(eventPathOrName, "event:/", 7) != 0)
        {
            V_snprintf(pathBuf, sizeof(pathBuf), "event:/%s", eventPathOrName);
            if (m_studioSystem->getEvent(pathBuf, &desc) == FMOD_OK && desc != nullptr)
                return true;
        }
        return false;
    }

    bool GetUserPropertyBool(const char* eventPathOrName, const char* propertyName) override
    {
        if (!m_studioSystem || !eventPathOrName)
            return false;

        // Ensure path has event:/ prefix
        char pathBuf[512];
        const char* query = eventPathOrName;
        if (V_strnicmp(query, "event:/", 7) != 0)
        {
            V_snprintf(pathBuf, sizeof(pathBuf), "event:/%s", eventPathOrName);
            query = pathBuf;
        }

        FMOD::Studio::EventDescription* desc = nullptr;
        if (m_studioSystem->getEvent(query, &desc) != FMOD_OK || !desc)
            return false;

        FMOD_STUDIO_USER_PROPERTY prop{};
        if (desc->getUserProperty(propertyName, &prop) != FMOD_OK)
            return false;

        if (prop.type == FMOD_STUDIO_USER_PROPERTY_TYPE_FLOAT)
        {
            return prop.floatvalue == 1;
        }

        return false;
    }

private:
    void CleanupStoppedInstances()
    {
        std::lock_guard<std::mutex> lock(m_instanceMutex);

        std::vector<uint64_t> toRemove;
        for (auto& pair : m_activeInstances)
        {
            if (!pair.second.instance)
            {
                toRemove.push_back(pair.first);
                continue;
            }

            FMOD_STUDIO_PLAYBACK_STATE state;
            if (pair.second.instance->getPlaybackState(&state) == FMOD_OK)
            {
                if (state == FMOD_STUDIO_PLAYBACK_STOPPED)
                {
                    pair.second.instance->release();
                    toRemove.push_back(pair.first);

                    // Remove from hash->instances map
                    if (pair.second.milesHash != 0)
                    {
                        auto hashIt = m_hashToInstances.find(pair.second.milesHash);
                        if (hashIt != m_hashToInstances.end())
                        {
                            auto& vec = hashIt->second;
                            vec.erase(std::remove(vec.begin(), vec.end(), pair.first), vec.end());
                            if (vec.empty())
                            {
                                m_hashToInstances.erase(hashIt);
                            }
                        }
                    }
                }
            }
        }

        for (uint64_t id : toRemove)
        {
            m_activeInstances.erase(id);
        }
    }

    void SyncGlobalVolume()
    {
        if (!g_milesGlobals)
            return;

        // Acquire master bus once
        if (!m_masterBus)
        {
            if (m_studioSystem->getBus("bus:/", &m_masterBus) != FMOD_OK || !m_masterBus)
            {
                // Fallback in case project uses explicit Master path
                m_studioSystem->getBus("bus:/Master", &m_masterBus);
            }
        }

        const float desired = (g_milesGlobals->soundMasterVolume < 0.0f) ? 0.0f : (g_milesGlobals->soundMasterVolume > 1.0f ? 1.0f : g_milesGlobals->soundMasterVolume);

        if (m_masterBus && fabsf(desired - m_lastGlobalVolume) > 0.001f)
        {
            m_masterBus->setVolume(desired);
            m_lastGlobalVolume = desired;
        }
    }

        void LoadBaseBanks()
        {
            char searchPattern[MAX_PATH];
            V_snprintf(searchPattern, sizeof(searchPattern), "%s/*.bank", "audio/fmod");

            WIN32_FIND_DATAA findData{};
            HANDLE hFind = FindFirstFileA(searchPattern, &findData);
            if (hFind == INVALID_HANDLE_VALUE)
                return;

            std::vector<std::string> bankFiles;
            do
            {
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                bankFiles.emplace_back(findData.cFileName);
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);

            auto loadOne = [&](const char* filename)
            {
                char fullPath[MAX_PATH];
                V_snprintf(fullPath, sizeof(fullPath), "%s/%s", "audio/fmod", filename);

                // derive base name without extension for map key
                const char* dot = strrchr(filename, '.');
                size_t baseLen = dot ? static_cast<size_t>(dot - filename) : strlen(filename);
                char base[260];
                V_strncpy(base, filename, min(baseLen + 1, sizeof(base)));

                if (m_loadedBanks.count(base))
                    return;

                Msg(eDLL_T::AUDIO, "Loading FMOD Studio bank file: %s\n", fullPath);
                FMOD::Studio::Bank* bank = nullptr;
                if (m_studioSystem->loadBankFile(fullPath, FMOD_STUDIO_LOAD_BANK_NORMAL, &bank) == FMOD_OK && bank)
                {
                    m_loadedBanks[base] = bank;
                }
                else
                {
                    Msg(eDLL_T::AUDIO, "Failed to load FMOD Studio bank: %s\n", fullPath);
                }
            };

            for (const std::string& f : bankFiles)
            {
                loadOne(f.c_str());
            }
        }

        void LoadModBanks()
        {
            if (!ModSystem()->IsEnabled())
                return;

            ModSystem()->LockModList();
            FOR_EACH_VEC(ModSystem()->GetModList(), i)
            {
                const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];
                if (!mod->IsEnabled())
                    continue;

                // Build search pattern: <modBase>/audio/fmod/*.bank
                char searchPattern[MAX_PATH];
                V_snprintf(searchPattern, sizeof(searchPattern), "%s%s", mod->GetBasePath().String(), "audio/fmod/*.bank");

                WIN32_FIND_DATAA findData{};
                HANDLE hFind = FindFirstFileA(searchPattern, &findData);
                if (hFind == INVALID_HANDLE_VALUE)
                    continue;

                do
                {
                    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        continue;

                    char fullPath[MAX_PATH];
                    V_snprintf(fullPath, sizeof(fullPath), "%s%s%s", mod->GetBasePath().String(), "audio/fmod/", findData.cFileName);

                    // Unique key per mod + filename
                    char bankKey[260];
                    V_snprintf(bankKey, sizeof(bankKey), "%s/%s", mod->name.String(), findData.cFileName);
                    if (m_loadedBanks.count(bankKey))
                        continue;

                    Msg(eDLL_T::AUDIO, "Loading FMOD Studio bank file (mod '%s'): %s\n", mod->name.String(), fullPath);
                    FMOD::Studio::Bank* bank = nullptr;
                    if (m_studioSystem->loadBankFile(fullPath, FMOD_STUDIO_LOAD_BANK_NORMAL, &bank) == FMOD_OK && bank)
                    {
                        m_loadedBanks[bankKey] = bank;
                    }
                    else
                    {
                        Msg(eDLL_T::AUDIO, "Failed to load FMOD Studio bank file (mod '%s'): %s\n", mod->name.String(), fullPath);
                    }
                } while (FindNextFileA(hFind, &findData));

                FindClose(hFind);
            }
            ModSystem()->UnlockModList();
        }

    FMOD::Studio::System* m_studioSystem = nullptr;
    FMOD::System* m_core = nullptr;
    std::unordered_map<std::string, FMOD::Studio::Bank*> m_loadedBanks;
    uint64_t m_nextId = 1;
    FMOD::Studio::Bus* m_masterBus = nullptr;
    float m_lastGlobalVolume = -1.0f;

    // Instance tracking for proper stop/pause/volume control
    mutable std::mutex m_instanceMutex;
    std::unordered_map<uint64_t, FMODEventInstance> m_activeInstances;
    // Multi-instance tracking: one hash can have MULTIPLE active instances (for rapid-fire sounds)
    std::unordered_map<uint64_t, std::vector<uint64_t>> m_hashToInstances; // milesHash -> list of internal instance IDs
};

ICustomAudioBackend* CreateFMODStudioBackend()
{
    return new FMODStudioBackend();
}

static void CC_fmod_event_exists(const CCommand& args)
{
    if (args.ArgC() < 2)
    {
        Msg(eDLL_T::AUDIO, "Usage: fmod_event_exists <event_path_or_name>\n");
        return;
    }


    ICustomAudioBackend* be = GetActiveCustomAudioBackend();
    if (!be)
    {
        Msg(eDLL_T::AUDIO, "No active custom audio backend\n");
        return;
    }
    const char* path = args[1];
    Msg(eDLL_T::AUDIO, "FMOD event '%s': %s\n", path, be->EventExists(path) ? "FOUND" : "NOT FOUND");
}
static ConCommand fmod_event_exists("fmod_event_exists", CC_fmod_event_exists, "Check if an FMOD Studio event exists in loaded banks", FCVAR_CLIENTDLL | FCVAR_RELEASE);

static void CC_fmod_list_banks(const CCommand& args)
{
    ICustomAudioBackend* be = GetActiveCustomAudioBackend();
    if (!be)
    {
        Msg(eDLL_T::AUDIO, "No active custom audio backend\n");
        return;
    }

    // Only supported by the Studio backend
    FMODStudioBackend* studio = dynamic_cast<FMODStudioBackend*>(be);
    if (!studio)
    {
        Msg(eDLL_T::AUDIO, "The active backend does not support listing FMOD banks\n");
        return;
    }
    studio->ListLoadedBanks();
}
static ConCommand fmod_list_banks("fmod_list_banks", CC_fmod_list_banks, "List currently loaded FMOD Studio banks", FCVAR_CLIENTDLL | FCVAR_RELEASE);

static void CC_fmod_reload_banks(const CCommand& args)
{
    ICustomAudioBackend* be = GetActiveCustomAudioBackend();
    if (!be)
    {
        Msg(eDLL_T::AUDIO, "No active custom audio backend\n");
        return;
    }

    // Only supported by the Studio backend
    FMODStudioBackend* studio = dynamic_cast<FMODStudioBackend*>(be);
    if (!studio)
    {
        Msg(eDLL_T::AUDIO, "The active backend does not support FMOD bank reloading\n");
        return;
    }

    const char* type = "all";
    if (args.ArgC() >= 2)
    {
        type = args[1];
    }

    if (V_stricmp(type, "all") == 0)
    {
        studio->ReloadAllBanks();
    }
    else if (V_stricmp(type, "base") == 0)
    {
        studio->ReloadBaseBanks();
    }
    else if (V_stricmp(type, "mod") == 0 || V_stricmp(type, "mods") == 0)
    {
        studio->ReloadModBanks();
    }
    else
    {
        Msg(eDLL_T::AUDIO, "Usage: fmod_reload_banks [all|base|mod]\n");
        Msg(eDLL_T::AUDIO, "  all  - Reload all banks (default)\n");
        Msg(eDLL_T::AUDIO, "  base - Reload only base game banks\n");
        Msg(eDLL_T::AUDIO, "  mod  - Reload only mod banks\n");
    }
}
static ConCommand fmod_reload_banks("fmod_reload_banks", CC_fmod_reload_banks, "Reload FMOD Studio banks [all|base|mod]", FCVAR_CLIENTDLL | FCVAR_RELEASE);

// Convenience commands for specific reload types
static void CC_fmod_reload_all(const CCommand& args)
{
    ICustomAudioBackend* be = GetActiveCustomAudioBackend();
    if (!be) { Msg(eDLL_T::AUDIO, "No active custom audio backend\n"); return; }
    FMODStudioBackend* studio = dynamic_cast<FMODStudioBackend*>(be);
    if (!studio) { Msg(eDLL_T::AUDIO, "FMOD Studio backend not active\n"); return; }
    studio->ReloadAllBanks();
}

static void CC_fmod_reload_base(const CCommand& args)
{
    ICustomAudioBackend* be = GetActiveCustomAudioBackend();
    if (!be) { Msg(eDLL_T::AUDIO, "No active custom audio backend\n"); return; }
    FMODStudioBackend* studio = dynamic_cast<FMODStudioBackend*>(be);
    if (!studio) { Msg(eDLL_T::AUDIO, "FMOD Studio backend not active\n"); return; }
    studio->ReloadBaseBanks();
}

static void CC_fmod_reload_mods(const CCommand& args)
{
    ICustomAudioBackend* be = GetActiveCustomAudioBackend();
    if (!be) { Msg(eDLL_T::AUDIO, "No active custom audio backend\n"); return; }
    FMODStudioBackend* studio = dynamic_cast<FMODStudioBackend*>(be);
    if (!studio) { Msg(eDLL_T::AUDIO, "FMOD Studio backend not active\n"); return; }
    studio->ReloadModBanks();
}

static ConCommand fmod_reload_all("fmod_reload_all", CC_fmod_reload_all, "Reload all FMOD Studio banks", FCVAR_CLIENTDLL | FCVAR_RELEASE);
static ConCommand fmod_reload_base("fmod_reload_base", CC_fmod_reload_base, "Reload base game FMOD Studio banks", FCVAR_CLIENTDLL | FCVAR_RELEASE);
static ConCommand fmod_reload_mods("fmod_reload_mods", CC_fmod_reload_mods, "Reload mod FMOD Studio banks", FCVAR_CLIENTDLL | FCVAR_RELEASE);

static void CC_fmod_list_instances(const CCommand& args)
{
    ICustomAudioBackend* be = GetActiveCustomAudioBackend();
    if (!be) { Msg(eDLL_T::AUDIO, "No active custom audio backend\n"); return; }
    FMODStudioBackend* studio = dynamic_cast<FMODStudioBackend*>(be);
    if (!studio) { Msg(eDLL_T::AUDIO, "FMOD Studio backend not active\n"); return; }
    studio->ListActiveInstances();
}
static ConCommand fmod_list_instances("fmod_list_instances", CC_fmod_list_instances, "List active FMOD event instances", FCVAR_CLIENTDLL | FCVAR_RELEASE);

static void CC_fmod_stop_all(const CCommand& args)
{
    ICustomAudioBackend* be = GetActiveCustomAudioBackend();
    if (!be) { Msg(eDLL_T::AUDIO, "No active custom audio backend\n"); return; }
    bool immediate = (args.ArgC() >= 2 && V_stricmp(args[1], "now") == 0);
    be->StopAll(immediate);
    Msg(eDLL_T::AUDIO, "FMOD: Stopped all sounds%s\n", immediate ? " immediately" : " (with fadeout)");
}
static ConCommand fmod_stop_all("fmod_stop_all", CC_fmod_stop_all, "Stop all FMOD sounds [now]", FCVAR_CLIENTDLL | FCVAR_RELEASE);