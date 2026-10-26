// SPDX-License-Identifier: AGPL-3.0-only
#include "PadService.h"

#include "Adc.h"
#include "Capture.h"
#include "ExclusiveOutputFile.h"
#include "MidiControl.h"
#include "PadCalibration.h"
#include "PadCapture.h"
#include "PadControl.h"
#include "PadRouting.h"
#include "TriggerEngine.h"
#ifdef MIMIC_USE_LITERT
#include "LiteRtModel.h"
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef __linux__
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mimic
{
namespace
{
#ifdef __linux__
using Clock = std::chrono::steady_clock;
constexpr std::uint32_t pollMicroseconds = 2000;
constexpr std::size_t preSnapshots = 8;
constexpr std::size_t postSnapshots = 16;
constexpr unsigned stageTarget = 12;
constexpr std::uintmax_t minimumFreeBytes = 16 * 1024 * 1024;
constexpr const char* phases[] = {"noise",
                                  "head-soft",
                                  "head-medium",
                                  "head-hard",
                                  "rim-soft",
                                  "rim-medium",
                                  "rim-hard",
                                  "xtalk",
                                  "rimshot-soft",
                                  "rimshot-medium",
                                  "rimshot-hard",
                                  "ambient",
                                  "head-varied",
                                  "rim-varied",
                                  "rimshot-varied"};

bool continuousPhase(const std::string& phase)
{
    return phase == "ambient" || phase.find("-varied") != std::string::npos;
}

unsigned phaseSeconds(const std::string& phase)
{
    return phase == "ambient" ? 2U : 20U;
}

volatile std::sig_atomic_t stopping = 0;

extern "C" void stopPads(int)
{
    stopping = 1;
}

class Descriptor final
{
public:
    explicit Descriptor(int valueToOwn) noexcept : value(valueToOwn) {}

    ~Descriptor()
    {
        if (value >= 0)
        {
            ::close(value);
        }
    }

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    int get() const noexcept
    {
        return value;
    }

private:
    int value;
};

std::string quoted(const std::string& value)
{
    std::ostringstream result;
    result << '"';
    for (const unsigned char character : value)
    {
        if (character == '"' || character == '\\')
        {
            result << '\\' << character;
        }
        else if (character < 32)
        {
            result << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(character) << std::dec;
        }
        else
        {
            result << character;
        }
    }
    result << '"';
    return result.str();
}

void atomicText(const std::filesystem::path& path, const std::string& text)
{
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output || !(output << text) || !output.flush())
        {
            throw std::runtime_error("Could not write pad state: " + path.string());
        }
    }
    std::filesystem::rename(temporary, path);
}

std::string newIdentifier()
{
    std::random_device random;
    std::ostringstream result;
    for (int index = 0; index < 4; ++index)
    {
        result << std::hex << std::setw(8) << std::setfill('0') << random();
    }
    return result.str();
}

struct PadDefinition
{
    std::string id;
    std::string name;
    int zoneCount = 2;
    int head;
    int rim;
    int midiChannel = 10;
    int headNote;
    int rimNote = -1;
    int rimshotNote = -1;
    std::string sd3Instrument;
    PadCalibration calibration;
    std::string calibrationError;
    std::string calibrationAttempt;
};

bool validPad(const PadDefinition& pad)
{
    return (pad.zoneCount == 1 || pad.zoneCount == 2) && pad.head >= -1 && pad.head < 32
           && pad.rim >= -1 && pad.rim < 32 && (pad.head < 0 || pad.head != pad.rim)
           && pad.midiChannel >= 1 && pad.midiChannel <= 16 && pad.headNote >= -1
           && pad.headNote <= 127 && pad.rimNote >= -1 && pad.rimNote <= 127
           && pad.rimshotNote >= -1 && pad.rimshotNote <= 127;
}

std::string channelJson(int channel)
{
    if (channel < 0)
    {
        return "null";
    }
    return "{\"adc\":" + std::to_string(channel / 16)
           + ",\"channel\":" + std::to_string(channel % 16) + "}";
}

std::string qualityJson(const PadCaptureQuality& quality, bool camelCase)
{
    std::ostringstream result;
    result << "{\"usable\":" << (quality.usable() ? "true" : "false")
           << ",\"reason\":" << quoted(quality.usable() ? "" : "Capture quality checks failed")
           << ",\"" << (camelCase ? "invalidCursors" : "invalid_cursors")
           << "\":" << quality.invalidCursors << ",\""
           << (camelCase ? "invalidChannelFrames" : "invalid_channel_frames")
           << "\":" << quality.invalidChannelFrames << ",\""
           << (camelCase ? "stalledCursors" : "stalled_cursors") << "\":" << quality.stalledCursors
           << ",\"" << (camelCase ? "pollGaps" : "poll_gaps") << "\":" << quality.pollGaps << ",\""
           << (camelCase ? "copyOverlap" : "copy_overlap") << "\":" << quality.copyOverlap << ",\""
           << (camelCase ? "clippedSamples" : "clipped_samples") << "\":" << quality.clippedSamples
           << ",\"" << (camelCase ? "unexpectedActivity" : "unexpected_activity")
           << "\":" << quality.unexpectedActivity << '}';
    return result.str();
}

std::uintmax_t directoryBytes(const std::filesystem::path& directory)
{
    std::uintmax_t total = 0;
    if (std::filesystem::exists(directory))
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
        {
            if (entry.is_regular_file())
            {
                total += entry.file_size();
            }
        }
    }
    return total;
}

struct Window
{
    PadDefinition pad;
    std::vector<std::uint8_t> sequenceBytes;
    std::string phase;
    std::string session;
    std::string id;
    std::uint64_t stage = 0;
    std::size_t onsetRecord = 0;
    unsigned threshold = 0;
    PadCaptureQuality quality;
    std::vector<PadSnapshot> snapshots;
};

struct SavedWindow
{
    std::string pad;
    std::string phase;
    std::string session;
    std::uint64_t stage = 0;
    std::filesystem::path raw;
    std::filesystem::path metadata;
    PadCaptureQuality quality;
    std::string error;
};

class WindowWriter final
{
public:
    WindowWriter(std::array<AdcLayout, 2> adcLayouts, const std::filesystem::path& dataDirectory)
        : layouts(adcLayouts), root(dataDirectory / "dataset")
    {
        std::filesystem::create_directories(root);
        datasetBytes = directoryBytes(root);
        worker = std::thread([this] { run(); });
    }

    ~WindowWriter()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ending = true;
        }
        wake.notify_one();
        worker.join();
    }

    void enqueue(Window window)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.size() >= 8)
        {
            throw std::runtime_error("Capture writer is busy; space hits farther apart");
        }
        queue.push_back(std::move(window));
        wake.notify_one();
    }

    std::vector<SavedWindow> takeResults()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<SavedWindow> result;
        result.swap(results);
        return result;
    }

    std::uintmax_t bytes() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return datasetBytes;
    }

    std::size_t pendingJobs() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size() + (working ? 1U : 0U);
    }

    void subtract(std::uintmax_t amount)
    {
        std::lock_guard<std::mutex> lock(mutex);
        datasetBytes = amount <= datasetBytes ? datasetBytes - amount : 0;
    }

    void checkSpace(std::uintmax_t additional) const
    {
        const auto available = std::filesystem::space(root).available;
        if (available < minimumFreeBytes || additional > available - minimumFreeBytes)
        {
            throw std::runtime_error("Not enough free space; 16 MiB must remain available");
        }
    }

private:
    SavedWindow save(const Window& window)
    {
        SavedWindow result{
            window.pad.id, window.phase, window.session, window.stage, {}, {}, window.quality, {}};
        const auto directory = root / window.pad.id / window.session;
        std::filesystem::create_directories(directory);
        result.raw = directory / (window.id + (window.sequenceBytes.empty() ? ".omadc" : ".omseq"));
        result.metadata = directory / (window.id + ".json");
        const auto temporary = directory / (window.id + ".json.pending");
        bool createdRaw = false;
        bool createdMetadata = false;
        bool publishedMetadata = false;
        try
        {
            if (std::filesystem::exists(result.metadata) || std::filesystem::exists(temporary))
            {
                throw std::runtime_error("Trial metadata already exists");
            }
            const auto snapshotBytes =
                static_cast<std::uintmax_t>(24 + layouts[0].dmaBytes + layouts[1].dmaBytes);
            checkSpace(36 + snapshotBytes * window.snapshots.size() + window.sequenceBytes.size()
                       + 4096);
            ExclusiveOutputFile raw(result.raw.string());
            createdRaw = true;
            const auto firstTimestamp =
                window.snapshots.empty() ? 0 : window.snapshots.front().elapsedNanoseconds;
            if (!window.sequenceBytes.empty())
            {
                raw.stream().write(reinterpret_cast<const char*>(window.sequenceBytes.data()),
                                   static_cast<std::streamsize>(window.sequenceBytes.size()));
            }
            else
            {
                writeCaptureHeader(raw.stream(), layouts, pollMicroseconds);
            }
            for (const auto& snapshot : window.snapshots)
            {
                writeCaptureRecord(raw.stream(),
                                   snapshot.elapsedNanoseconds - firstTimestamp,
                                   snapshot.cursors,
                                   snapshot.buffers);
            }
            raw.close();
            std::ostringstream metadata;
            metadata << "{\"format\":"
                     << quoted(window.sequenceBytes.empty() ? "open-mimic-pad-trial-v1"
                                                            : "open-mimic-pad-sequence-v1")
                     << ",\"capture\":" << quoted(result.raw.filename().string())
                     << ",\"channel_addressing\":\"ads7953-tag-v1\""
                     << ",\"trial_id\":" << quoted(window.id)
                     << ",\"session_id\":" << quoted(window.session)
                     << ",\"pad\":" << quoted(window.pad.id)
                     << ",\"phase\":" << quoted(window.phase) << ",\"duration_seconds\":"
                     << (window.sequenceBytes.empty() ? 0U : phaseSeconds(window.phase))
                     << ",\"zone_count\":" << window.pad.zoneCount
                     << ",\"head\":" << channelJson(window.pad.head)
                     << ",\"rim\":" << channelJson(window.pad.zoneCount == 2 ? window.pad.rim : -1)
                     << ",\"input_rim\":" << channelJson(window.pad.rim)
                     << ",\"onset_record_index\":" << window.onsetRecord
                     << ",\"threshold\":" << window.threshold
                     << ",\"hardware_gain\":null,\"poll_microseconds\":" << pollMicroseconds
                     << ",\"snapshot_count\":" << window.snapshots.size()
                     << ",\"capture_elapsed_ns\":" << firstTimestamp
                     << ",\"label_source\":\"guided_user_instruction\""
                     << ",\"segmentation\":"
                     << quoted(window.sequenceBytes.empty() ? "heuristic_collection_only"
                                                            : "continuous_no_hit_segmentation")
                     << ",\"recording_kind\":"
                     << quoted(window.sequenceBytes.empty() ? "isolated_hit_window" : "continuous")
                     << ",\"timestamps_are_snapshot_end_bounds\":true"
                     << ",\"adc_synchronization\":\"not_established\""
                     << ",\"quality\":" << qualityJson(window.quality, false) << "}\n";
            ExclusiveOutputFile manifest(temporary.string());
            createdMetadata = true;
            manifest.stream() << metadata.str();
            manifest.close();
            std::filesystem::rename(temporary, result.metadata);
            publishedMetadata = true;
            const auto size = std::filesystem::file_size(result.raw)
                              + std::filesystem::file_size(result.metadata);
            std::lock_guard<std::mutex> lock(mutex);
            datasetBytes += size;
        }
        catch (const std::exception& error)
        {
            result.error = error.what();
            std::error_code ignored;
            if (createdRaw)
            {
                std::filesystem::remove(result.raw, ignored);
            }
            if (createdMetadata)
            {
                std::filesystem::remove(temporary, ignored);
            }
            if (publishedMetadata)
            {
                std::filesystem::remove(result.metadata, ignored);
            }
            // The final manifest is published only after the raw file closes.
        }
        return result;
    }

    void run()
    {
        while (true)
        {
            Window window;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return ending || !queue.empty(); });
                if (queue.empty())
                {
                    return;
                }
                window = std::move(queue.front());
                queue.pop_front();
                working = true;
            }
            SavedWindow result;
            try
            {
                result = save(window);
            }
            catch (const std::exception& error)
            {
                result.error = error.what();
            }
            std::lock_guard<std::mutex> lock(mutex);
            results.push_back(std::move(result));
            working = false;
        }
    }

    std::array<AdcLayout, 2> layouts;
    std::filesystem::path root;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Window> queue;
    std::vector<SavedWindow> results;
    std::uintmax_t datasetBytes = 0;
    bool ending = false;
    bool working = false;
    std::thread worker;
};

class MidiEndpoint final
{
public:
    ~MidiEndpoint()
    {
        if (descriptor >= 0)
        {
            ::close(descriptor);
        }
    }

    bool send(const char* command)
    {
        if (descriptor < 0)
        {
            descriptor = ::open(midiCommandPath, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
            struct stat info{};
            if (descriptor < 0)
            {
                return false;
            }
            if (::fstat(descriptor, &info) != 0 || !S_ISFIFO(info.st_mode))
            {
                ::close(descriptor);
                descriptor = -1;
                return false;
            }
        }
        const auto bytes = std::strlen(command);
        const auto written = ::write(descriptor, command, bytes);
        if (written == static_cast<ssize_t>(bytes))
        {
            return true;
        }
        ::close(descriptor);
        descriptor = -1;
        return false;
    }

private:
    int descriptor = -1;
};

class PadService final
{
public:
    explicit PadService(std::filesystem::path dataDirectoryToUse)
        : first("/dev/ads7953.0"), second("/dev/ads7953.1"),
          layouts{first.layout(), second.layout()}, writer(layouts, dataDirectoryToUse),
          dataDirectory(std::move(dataDirectoryToUse))
    {
        for (const auto& layout : layouts)
        {
            if (layout.dmaBytes != 4096 || layout.portionBytes != 32)
            {
                throw std::runtime_error(
                    "Pad service requires the validated 4096-byte/32-byte ADC layout");
            }
        }
        for (const auto& route : padRoutes)
        {
            pads.push_back({route.id,
                            route.name,
                            route.defaultZones,
                            route.head,
                            route.rim,
                            10,
                            route.headNote,
                            route.rimNote,
                            route.rimshotNote,
                            route.sd3Instrument,
                            {},
                            {}});
        }
        try
        {
            loadConfig(dataDirectory / "config");
        }
        catch (const std::exception& exception)
        {
            error = "Saved pad configuration: " + std::string(exception.what());
        }
        std::filesystem::create_directories(dataDirectory / "profiles" / "staging");
        for (auto& pad : pads)
        {
            try
            {
                const auto path = dataDirectory / "profiles" / (pad.id + ".profile");
                if (std::filesystem::exists(path))
                {
                    pad.calibration = parseProfile(path, pad);
                }
                pad.calibrationError =
                    readSmallText(dataDirectory / "profiles" / (pad.id + ".error"));
            }
            catch (const std::exception& exception)
            {
                pad.calibrationError = exception.what();
            }
        }
#ifdef MIMIC_USE_LITERT
        engineRequested = true;
        std::ifstream engineSettings(dataDirectory / "engine-enabled");
        int enabled = 1;
        if (engineSettings >> enabled)
        {
            engineRequested = enabled != 0;
        }
        try
        {
            if (!error.empty())
            {
                throw std::runtime_error("Resolve input configuration before enabling live MIDI: "
                                         + error);
            }
            model = std::make_unique<LiteRtModel>(
                "/usr/share/open-mimic/models/shared-drum-pilot/model.tflite");
            configureLive();
        }
        catch (const std::exception& exception)
        {
            liveError = exception.what();
        }
#else
        liveError = "LiteRT support is not built";
#endif
        std::signal(SIGPIPE, SIG_IGN);
    }

    void command(const std::string& line)
    {
        try
        {
            std::istringstream input(line);
            std::string verb;
            input >> verb;
            if (verb == "engine")
            {
                std::string value;
                input >> value;
                requireEnd(input);
                if (value != "on" && value != "off")
                {
                    throw std::runtime_error("Use engine on or off");
                }
                if (value == "on" && !liveReady)
                {
                    throw std::runtime_error(liveError);
                }
                atomicText(dataDirectory / "engine-enabled", value == "on" ? "1\n" : "0\n");
                engineRequested = value == "on";
                trigger.reset();
                if (!engineRequested)
                {
                    midi.send("panic\n");
                }
            }
            else if (verb == "select")
            {
                if (wizardActive || state == "capturing")
                {
                    throw std::runtime_error("Stop calibration before selecting another pad");
                }
                std::string selected;
                input >> selected;
                requireEnd(input);
                const auto found = std::find_if(pads.begin(),
                                                pads.end(),
                                                [&selected](const PadDefinition& pad)
                                                { return pad.id == selected; });
                if (found == pads.end())
                {
                    throw std::runtime_error("Unknown pad");
                }
                stopStage();
                selectedIndex = static_cast<std::size_t>(std::distance(pads.begin(), found));
                last = lastByPad[found->id];
                captured = 0;
                phase.clear();
            }
            else if (verb == "settings")
            {
                if (state == "capturing" || wizardActive)
                {
                    throw std::runtime_error("Stop calibration before changing settings");
                }
                PadDefinition candidate{};
                input >> candidate.id >> candidate.zoneCount >> candidate.midiChannel
                    >> candidate.headNote >> candidate.rimNote >> candidate.rimshotNote;
                requireEnd(input);
                auto found = std::find_if(pads.begin(),
                                          pads.end(),
                                          [&candidate](const PadDefinition& pad)
                                          { return pad.id == candidate.id; });
                if (found != pads.end())
                {
                    candidate.head = found->head;
                    candidate.rim = found->rim;
                    candidate.sd3Instrument = found->sd3Instrument;
                }
                if (found == pads.end() || !validPad(candidate))
                {
                    throw std::runtime_error("Invalid pad configuration");
                }
                if (candidate.zoneCount == 2 && candidate.head >= 0 && candidate.rim >= 0
                    && candidate.head / 16 != candidate.rim / 16)
                {
                    throw std::runtime_error(candidate.id
                                             + ": head and rim must use the same ADC; global "
                                               "channels 0-15 are ADC0, 16-31 are ADC1");
                }
                candidate.name = found->name;
                const auto old = *found;
                *found = candidate;
                try
                {
                    saveConfig();
                }
                catch (...)
                {
                    *found = old;
                    throw;
                }
                if (old.zoneCount != candidate.zoneCount || old.head != candidate.head
                    || old.rim != candidate.rim)
                {
                    newSession();
                }
                configureLive();
            }
            else if (verb == "zones")
            {
                std::string id;
                int value = 0;
                input >> id >> value;
                requireEnd(input);
                if (state == "capturing" || wizardActive || (value != 1 && value != 2))
                {
                    throw std::runtime_error("Stop calibration before changing single/dual zone");
                }
                auto found = std::find_if(pads.begin(),
                                          pads.end(),
                                          [&](const PadDefinition& pad) { return pad.id == id; });
                if (found == pads.end())
                {
                    throw std::runtime_error("Unknown pad");
                }
                const auto previousZones = found->zoneCount;
                found->zoneCount = value;
                try
                {
                    saveConfig();
                }
                catch (...)
                {
                    found->zoneCount = previousZones;
                    throw;
                }
                configureLive();
            }
            else if (verb == "wizard")
            {
                if (wizardActive || state == "capturing")
                {
                    throw std::runtime_error("Calibration is already running");
                }
                if (input.peek() != std::char_traits<char>::eof())
                {
                    std::string id;
                    int zones = 0;
                    input >> id >> zones;
                    auto found =
                        std::find_if(pads.begin(),
                                     pads.end(),
                                     [&](const PadDefinition& pad) { return pad.id == id; });
                    if (found == pads.end() || (zones != 1 && zones != 2))
                    {
                        throw std::runtime_error("Invalid calibration pad or zone count");
                    }
                    selectedIndex = static_cast<std::size_t>(std::distance(pads.begin(), found));
                    const auto oldZones = found->zoneCount;
                    found->zoneCount = zones;
                    try
                    {
                        saveConfig();
                    }
                    catch (...)
                    {
                        found->zoneCount = oldZones;
                        throw;
                    }
                    configureLive();
                }
                requireEnd(input);
                if (selected().head < 0)
                {
                    throw std::runtime_error("This input has no measured hardware route");
                }
                if (writer.pendingJobs() != 0)
                {
                    throw std::runtime_error("Wait for pending captures to finish");
                }
                newSession();
                wizardPhases = {"ambient", "head-varied"};
                if (selected().zoneCount == 2)
                {
                    wizardPhases.insert(wizardPhases.end(), {"rim-varied", "rimshot-varied"});
                }
                wizardStep = 0;
                wizardActive = true;
                prepareWizardStep();
                midi.send("panic\n");
            }
            else if (verb == "profile-reset")
            {
                std::string id;
                input >> id;
                requireEnd(input);
                auto found = std::find_if(pads.begin(),
                                          pads.end(),
                                          [&](const PadDefinition& pad) { return pad.id == id; });
                if (found == pads.end())
                {
                    throw std::runtime_error("Unknown calibration pad");
                }
                std::filesystem::remove(dataDirectory / "profiles" / (id + ".profile"));
                std::filesystem::remove(dataDirectory / "profiles" / (id + ".error"));
                found->calibration = {};
                found->calibrationError.clear();
                found->calibrationAttempt.clear();
                configureLive();
            }
            else if (verb == "profile-load" || verb == "profile-failed")
            {
                std::string id, token;
                input >> id >> token;
                requireEnd(input);
                if (token.size() != 32
                    || token.find_first_not_of("0123456789abcdef") != std::string::npos)
                {
                    throw std::runtime_error("Invalid calibration transfer identity");
                }
                auto found = std::find_if(pads.begin(),
                                          pads.end(),
                                          [&](const PadDefinition& pad) { return pad.id == id; });
                if (found == pads.end())
                {
                    throw std::runtime_error("Unknown calibration pad");
                }
                receiveProfile(*found, token, verb == "profile-failed");
            }
            else if (verb == "archive-seal" || verb == "archive-ack")
            {
                std::string pad, id;
                input >> pad >> id;
                requireEnd(input);
                archiveSession(pad, id, verb == "archive-ack");
            }
            else if (verb == "start")
            {
                std::string requested;
                input >> requested;
                requireEnd(input);
                if (std::none_of(std::begin(phases),
                                 std::end(phases),
                                 [&requested](const char* item) { return requested == item; }))
                {
                    throw std::runtime_error("Unknown calibration phase");
                }
                if (selected().head < 0)
                {
                    throw std::runtime_error("Assign the head ADC channel first");
                }
                if (selected().zoneCount == 2 && selected().rim < 0)
                {
                    throw std::runtime_error(
                        "Assign both ADC channels before calibrating a dual-zone pad");
                }
                if (requested.compare(0, 3, "rim") == 0 && selected().zoneCount != 2)
                {
                    throw std::runtime_error("Rim capture requires a dual-zone pad");
                }
                if (std::filesystem::exists(dataDirectory / "dataset" / selected().id / session
                                            / "ready.json"))
                {
                    throw std::runtime_error("This session is sealed; start a new calibration");
                }
                writer.checkSpace(3 * 1024 * 1024);
                stopStage();
                phase = requested;
                state = "capturing";
                midi.send("panic\n");
                trigger.reset();
                stageSequence++;
                captured = 0;
                submitted = 0;
                rejected = 0;
                armed = false;
                quietPolls = 0;
                history.clear();
                nextWindow = Clock::now() + std::chrono::milliseconds(100);
                if (continuousPhase(phase))
                {
                    beginWizardStep();
                }
            }
            else if (verb == "stop")
            {
                requireEnd(input);
                wizardActive = false;
                stopStage();
                state = "stopped";
            }
            else if (verb == "new-session")
            {
                requireEnd(input);
                newSession();
            }
            else if (verb == "discard")
            {
                requireEnd(input);
                if (state == "capturing")
                {
                    throw std::runtime_error("Stop capture before discarding a trial");
                }
                if (last.raw.empty())
                {
                    throw std::runtime_error("No trial from this run is available to discard");
                }
                const auto bytes = std::filesystem::file_size(last.raw)
                                   + std::filesystem::file_size(last.metadata);
                std::filesystem::remove(last.metadata);
                std::filesystem::remove(last.raw);
                writer.subtract(bytes);
                lastByPad.erase(last.pad);
                auto& count = counts[last.pad + "/" + last.phase];
                if (last.quality.usable() && count > 0)
                {
                    --count;
                }
                if (last.quality.usable() && last.pad == selected().id && last.phase == phase
                    && last.stage == stageSequence && captured > 0)
                {
                    --captured;
                }
                last = {};
            }
            else
            {
                throw std::runtime_error("Unknown pad command");
            }
            error.clear();
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
        }
    }

    void tick()
    {
        if (wizardActive && state == "countdown" && Clock::now() >= wizardDeadline)
        {
            beginWizardStep();
        }
        for (const auto& result : writer.takeResults())
        {
            if (!result.error.empty())
            {
                stopStage();
                state = "error";
                error = result.error;
                continue;
            }
            if (result.session == session)
            {
                if (result.quality.usable())
                {
                    ++counts[result.pad + "/" + result.phase];
                }
                lastByPad[result.pad] = result;
                if (result.pad == selected().id)
                {
                    last = result;
                }
                if (result.pad == selected().id && result.phase == phase
                    && result.stage == stageSequence)
                {
                    if (continuousPhase(result.phase))
                    {
                        // Preserve annotated continuous takes even when a gap splits
                        // them into separate usable intervals. Never hide the flags.
                        captured = phaseSeconds(result.phase);
                        state = "review";
                    }
                    else if (result.quality.usable())
                    {
                        ++captured;
                        if (captured >= stageTarget && submitted >= stageTarget)
                        {
                            state = "review";
                        }
                    }
                    else
                    {
                        if (submitted > 0)
                        {
                            --submitted;
                        }
                        ++rejected;
                        if (rejected >= stageTarget && state == "capturing")
                        {
                            stopStage();
                            state = "error";
                            error = "Capture quality is unstable; check clipping and ADC timing "
                                    "before retrying";
                        }
                    }
                }
            }
        }
        if (wizardActive && state == "review" && writer.pendingJobs() == 0)
        {
            ++wizardStep;
            if (wizardStep < wizardPhases.size())
            {
                prepareWizardStep();
            }
            else
            {
                wizardActive = false;
                state = "complete";
                archiveSession(selected().id, session, false);
            }
        }
        if (state == "sequence-writing")
        {
            return;
        }
        if (state == "capturing" && continuousPhase(phase))
        {
            collectSequence();
            return;
        }
        if (engineRequested && liveReady && !wizardActive && state != "capturing")
        {
            processLiveFrames();
            havePrevious = false;
            history.clear();
            return;
        }
        haveFastPoll = false;
        PadSnapshot snapshot;
        snapshot.buffers = {std::vector<std::uint8_t>(layouts[0].dmaBytes),
                            std::vector<std::uint8_t>(layouts[1].dmaBytes)};
        snapshot.cursors = {first.snapshot(snapshot.buffers[0]),
                            second.snapshot(snapshot.buffers[1])};
        snapshot.elapsedNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
        PadSnapshotAnalysis analysis;
        if (havePrevious)
        {
            analysis = analysePadSnapshot(previous, snapshot);
        }
        trigger.reset();
        previous = snapshot;
        havePrevious = true;
        if (state == "capturing")
        {
            cumulative += analysis.quality;
        }
        for (std::size_t channel = 0; channel < 32; ++channel)
        {
            displayPeaks[channel] = std::max(displayPeaks[channel], analysis.peaks[channel]);
            const auto peak = static_cast<double>(analysis.peaks[channel]);
            if (peak < noise[channel] + 12.0)
            {
                noise[channel] += 0.01 * (peak - noise[channel]);
            }
        }
        if (state == "capturing")
        {
            collect(snapshot, analysis);
        }
        history.push_back({std::move(snapshot), analysis.quality});
        if (history.size() > preSnapshots)
        {
            history.pop_front();
        }
    }

    void status(bool running)
    {
        std::ostringstream output;
        output << "{\"running\":" << (running ? "true" : "false") << ",\"error\":" << quoted(error)
               << ",\"state\":" << quoted(state) << ",\"selectedPad\":" << quoted(selected().id)
               << ",\"phase\":" << quoted(phase) << ",\"captured\":" << captured
               << ",\"target\":" << (continuousPhase(phase) ? phaseSeconds(phase) : stageTarget)
               << ",\"sessionId\":" << quoted(session) << ",\"rejected\":" << rejected
               << ",\"threshold\":"
               << std::max(detectionThreshold(selected().head), detectionThreshold(selected().rim))
               << ",\"lastCapture\":" << quoted(last.raw.string())
               << ",\"lastQuality\":" << qualityJson(last.quality, true)
               << ",\"datasetBytes\":" << writer.bytes() << ",\"writerPending\":"
               << (writer.pendingJobs() + (pending.snapshots.empty() ? 0U : 1U))
               << ",\"freeBytes\":" << std::filesystem::space(dataDirectory).available
               << ",\"dataDirectory\":" << quoted(dataDirectory.string())
               << ",\"continuous\":" << (continuousPhase(phase) ? "true" : "false")
               << ",\"wizardActive\":" << (wizardActive ? "true" : "false")
               << ",\"wizardStep\":" << wizardStep << ",\"wizardSteps\":" << wizardPhases.size()
               << ",\"countdown\":"
               << (state == "countdown"
                       ? std::max<std::int64_t>(
                             0,
                             1L
                                 + std::chrono::duration_cast<std::chrono::seconds>(wizardDeadline
                                                                                    - Clock::now())
                                       .count())
                       : 0L)
               << ",\"uploadState\":"
               << quoted(std::filesystem::exists(dataDirectory / "archive-receipts" / selected().id
                                                 / (session + ".json"))
                             ? "uploaded"
                             : (state == "complete" ? "pending" : "idle"))
               << ",\"rimshotModelReady\":"
#ifdef MIMIC_USE_LITERT
               << (model && model->classCount() == 4 ? "true" : "false")
#else
               << "false"
#endif
               << ",\"archiveError\":" << quoted(archiveError())
               << ",\"archiveConnected\":" << (archiveConnected() ? "true" : "false")
               << ",\"channelAddressing\":\"ads7953-tag-v1\""
               << ",\"modelState\":" << quoted(liveReady ? "loaded" : "unavailable")
               << ",\"engine\":{\"enabled\":" << (engineRequested ? "true" : "false")
               << ",\"active\":"
               << (engineRequested && liveReady && !wizardActive && state != "capturing" ? "true"
                                                                                         : "false")
               << ",\"error\":" << quoted(liveError) << ",\"events\":" << liveEvents
               << ",\"midiQueued\":" << midiQueued << ",\"midiDropped\":" << midiDropped
               << ",\"unmappedNotes\":" << unmappedNotes << ",\"dmaResets\":" << dmaResets
               << ",\"adcFrames\":[" << adcFrames[0] << "," << adcFrames[1] << "]"
               << ",\"rejected\":" << trigger.rejectedCount()
               << ",\"clipped\":" << trigger.clippedCount()
               << ",\"lastPad\":" << quoted(lastLivePad) << ",\"lastZone\":" << lastLiveZone
               << ",\"lastVelocity\":" << lastLiveVelocity
               << ",\"lastProbability\":" << lastLiveProbability << ",\"lastPeak\":" << lastLivePeak
               << "},\"pads\":[";
        bool separator = false;
        for (const auto& pad : pads)
        {
            if (separator)
            {
                output << ',';
            }
            separator = true;
            output << "{\"id\":" << quoted(pad.id) << ",\"name\":" << quoted(pad.name)
                   << ",\"zoneCount\":" << pad.zoneCount << ",\"head\":" << pad.head
                   << ",\"rim\":" << pad.rim << ",\"midiChannel\":" << pad.midiChannel
                   << ",\"headNote\":" << pad.headNote << ",\"rimNote\":" << pad.rimNote
                   << ",\"rimshotNote\":" << pad.rimshotNote
                   << ",\"sd3Instrument\":" << quoted(pad.sd3Instrument)
                   << ",\"calibration\":" << calibrationJson(pad) << '}';
        }
        output << "],\"meters\":[";
        for (std::size_t index = 0; index < displayPeaks.size(); ++index)
        {
            if (index > 0)
            {
                output << ',';
            }
            output << displayPeaks[index];
        }
        output << "],\"quality\":" << qualityJson(cumulative, true) << ",\"counts\":{";
        separator = false;
        for (const auto* item : phases)
        {
            if (separator)
            {
                output << ',';
            }
            separator = true;
            output << quoted(item) << ':' << counts[selected().id + "/" + item];
        }
        output << "}}\n";
        atomicText(padStatusPath, output.str());
        displayPeaks.fill(0);
    }

    void failCapture(const std::string& message)
    {
        wizardActive = false;
        stopStage();
        state = "error";
        error = message;
    }

    void invalidCommand()
    {
        error = "Pad command exceeds 256 bytes";
    }

    [[nodiscard]] std::chrono::microseconds pollInterval() const
    {
        // Idle snapshots serve display meters only. Continuous, quality-checked
        // waveform collection retains the original 2 ms cadence.
        return std::chrono::microseconds(
            (state == "capturing" || wizardActive || (engineRequested && liveReady))
                ? pollMicroseconds
                : 20000);
    }

    void stopStage()
    {
        pending = {};
        state = "idle";
        history.clear();
    }

private:
    static std::string archiveError()
    {
        std::ifstream input("/run/mimic-pads/archive-error");
        char text[512]{};
        input.getline(text, sizeof(text));
        return text;
    }

    static bool archiveConnected()
    {
        std::error_code error;
        const auto time =
            std::filesystem::last_write_time("/run/mimic-pads/archive-heartbeat", error);
        return !error
               && std::filesystem::file_time_type::clock::now() - time < std::chrono::seconds(15);
    }

    void prepareWizardStep()
    {
        stopStage();
        phase = wizardPhases.at(wizardStep);
        captured = submitted = rejected = 0;
        state = "countdown";
        wizardDeadline = Clock::now() + std::chrono::seconds(phase == "ambient" ? 0 : 3);
    }

    void beginWizardStep()
    {
        writer.checkSpace(continuousPhase(phase) ? phaseSeconds(phase) * 1024 * 1024
                                                 : 3 * 1024 * 1024);
        state = "capturing";
        ++stageSequence;
        history.clear();
        armed = false;
        quietPolls = 0;
        nextWindow = Clock::now();
        if (continuousPhase(phase))
        {
            first.resetFrameReader();
            second.resetFrameReader();
            sequenceStart = sequencePoll = Clock::now();
            sequenceBytes.clear();
            sequenceBytes.reserve(phaseSeconds(phase) * 1024 * 1024);
            const char magic[] = "OMSEQ001";
            sequenceBytes.insert(sequenceBytes.end(), magic, magic + 8);
            appendSequence<std::uint32_t>(1);
            appendSequence<std::uint32_t>(pollMicroseconds);
            sequenceQuality = {};
        }
    }

    template <class Integer> void appendSequence(Integer value)
    {
        for (std::size_t byte = 0; byte < sizeof(Integer); ++byte)
        {
            sequenceBytes.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
        }
    }

    void collectSequence()
    {
        const auto now = Clock::now();
        auto left = first.readCompletedFrames();
        auto right = second.readCompletedFrames();
        const auto finished = Clock::now();
        const bool gap = now - sequencePoll > std::chrono::milliseconds(8)
                         || finished - now > std::chrono::milliseconds(8);
        sequencePoll = now;
        const bool invalid = !left.valid || !right.valid;
        if (gap || invalid)
        {
            if (gap)
            {
                ++sequenceQuality.pollGaps;
            }
            if (invalid)
            {
                ++sequenceQuality.invalidChannelFrames;
            }
            first.resetFrameReader();
            second.resetFrameReader();
            left.count = right.count = 0;
        }
        if (sequenceBytes.size() > phaseSeconds(phase) * 1024 * 1024)
        {
            throw std::runtime_error("Continuous capture exceeded its bounded buffer");
        }
        appendSequence<std::uint64_t>(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - sequenceStart).count()));
        appendSequence<std::uint16_t>(static_cast<std::uint16_t>(left.count));
        appendSequence<std::uint16_t>(static_cast<std::uint16_t>(right.count));
        appendSequence<std::uint32_t>(gap || invalid ? 1U : 0U);
        for (unsigned adc = 0; adc < 2; ++adc)
        {
            const auto& batch = adc == 0 ? left : right;
            for (std::size_t frame = 0; frame < batch.count; ++frame)
            {
                for (unsigned channel = 0; channel < 16; ++channel)
                {
                    const auto sample = batch.frames[frame][channel];
                    appendSequence<std::uint16_t>(
                        static_cast<std::uint16_t>((channel << 12) | sample));
                    displayPeaks[adc * 16 + channel] =
                        std::max(displayPeaks[adc * 16 + channel], sample);
                    if (sample >= 4090)
                    {
                        ++sequenceQuality.clippedSamples;
                    }
                }
            }
        }
        captured = static_cast<unsigned>(std::min<std::int64_t>(
            phaseSeconds(phase),
            std::chrono::duration_cast<std::chrono::seconds>(now - sequenceStart).count()));
        if (now - sequenceStart >= std::chrono::seconds(phaseSeconds(phase)))
        {
            Window window;
            window.pad = selected();
            window.phase = phase;
            window.session = session;
            window.id = newIdentifier();
            window.stage = stageSequence;
            window.quality = sequenceQuality;
            window.sequenceBytes = std::move(sequenceBytes);
            writer.enqueue(std::move(window));
            state = "sequence-writing";
        }
    }

    void archiveSession(const std::string& pad, const std::string& id, bool acknowledge)
    {
        if (id.size() != 32 || id.find_first_not_of("0123456789abcdef") != std::string::npos
            || std::none_of(padRoutes.begin(),
                            padRoutes.end(),
                            [&](const PadRoute& route) { return pad == route.id; }))
        {
            throw std::runtime_error("Invalid archive session identity");
        }
        if ((!acknowledge && (writer.pendingJobs() != 0 || !pending.snapshots.empty()))
            || ((state == "capturing" || state == "sequence-writing") && id == session))
        {
            throw std::runtime_error("Wait for this session's captures to finish");
        }
        const auto directory = dataDirectory / "dataset" / pad / id;
        const auto receipt = dataDirectory / "archive-receipts" / pad / (id + ".json");
        if (acknowledge && !std::filesystem::exists(directory))
        {
            // The receiver revalidates its durable archive before retrying an
            // acknowledgment interrupted after deletion but before the receipt.
            std::filesystem::create_directories(receipt.parent_path());
            atomicText(receipt,
                       "{\"format\":\"open-mimic-archive-receipt-v1\",\"pad\":" + quoted(pad)
                           + ",\"session\":" + quoted(id) + "}\n");
            return;
        }
        if (!std::filesystem::is_directory(directory) || std::filesystem::is_symlink(directory)
            || std::filesystem::is_symlink(directory.parent_path()))
        {
            throw std::runtime_error("Archive session directory is unavailable");
        }
        std::vector<std::filesystem::path> files;
        std::uintmax_t bytes = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            if (entry.is_symlink() || !entry.is_regular_file())
            {
                throw std::runtime_error("Unexpected entry in archive session");
            }
            if (entry.path().extension() != ".json" && entry.path().extension() != ".omadc"
                && entry.path().extension() != ".omseq")
            {
                throw std::runtime_error("Archive session has unpublished files");
            }
            files.push_back(entry.path());
            bytes += entry.file_size();
        }
        const auto ready = directory / "ready.json";
        if (!acknowledge)
        {
            if (!std::filesystem::exists(ready))
            {
                atomicText(ready,
                           "{\"format\":\"open-mimic-session-ready-v1\",\"pad\":" + quoted(pad)
                               + ",\"session\":" + quoted(id) + "}\n");
            }
            return;
        }
        if (!std::filesystem::exists(ready))
        {
            throw std::runtime_error("Archive session is not sealed");
        }
        for (const auto& file : files)
        {
            if (file != ready)
            {
                std::filesystem::remove(file);
            }
        }
        std::filesystem::remove(ready);
        std::filesystem::remove(directory);
        writer.subtract(bytes);
        std::filesystem::create_directories(receipt.parent_path());
        atomicText(receipt,
                   "{\"format\":\"open-mimic-archive-receipt-v1\",\"pad\":" + quoted(pad)
                       + ",\"session\":" + quoted(id) + "}\n");
        if (last.pad == pad && last.session == id)
        {
            last = {};
        }
        auto found = lastByPad.find(pad);
        if (found != lastByPad.end() && found->second.session == id)
        {
            lastByPad.erase(found);
        }
    }

    static std::string readSmallText(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
        {
            return {};
        }
        if (std::filesystem::is_symlink(path) || std::filesystem::file_size(path) > 8192)
        {
            throw std::runtime_error("Calibration file exceeds safe size or path bounds");
        }
        std::ifstream input(path);
        if (!input)
        {
            throw std::runtime_error("Could not read calibration file");
        }
        return std::string(std::istreambuf_iterator<char>(input), {});
    }

    static PadCalibration parseProfile(const std::filesystem::path& path, const PadDefinition& pad)
    {
        std::istringstream input(readSmallText(path));
        auto calibration = readPadCalibration(input);
        if (calibration.pad != pad.id || calibration.head != pad.head || calibration.rim != pad.rim)
        {
            throw std::runtime_error("Calibration profile does not match the physical input");
        }
        return calibration;
    }

    void receiveProfile(PadDefinition& pad, const std::string& token, bool failed)
    {
        const auto staged =
            dataDirectory / "profiles" / "staging" / (token + (failed ? ".error" : ".profile"));
        try
        {
            if (failed)
            {
                const auto message = readSmallText(staged);
                if (message.empty())
                {
                    throw std::runtime_error("Empty calibration failure report");
                }
                pad.calibrationError = message.substr(0, 500);
                atomicText(dataDirectory / "profiles" / (pad.id + ".error"), pad.calibrationError);
            }
            else
            {
                auto calibration = parseProfile(staged, pad);
                if (calibration.zones != pad.zoneCount)
                {
                    throw std::runtime_error("Sensor type changed; record a new calibration");
                }
                const auto contents = writePadCalibration(calibration);
                const bool changed = !pad.calibration.response.calibrated
                                     || writePadCalibration(pad.calibration) != contents;
                if (changed)
                {
                    atomicText(dataDirectory / "profiles" / (pad.id + ".profile"), contents);
                    pad.calibration = std::move(calibration);
                    configureLive();
                }
                pad.calibrationError.clear();
                std::filesystem::remove(dataDirectory / "profiles" / (pad.id + ".error"));
            }
        }
        catch (const std::exception& exception)
        {
            // A failed replacement must never erase an already working response.
            pad.calibrationError = exception.what();
            atomicText(dataDirectory / "profiles" / (pad.id + ".error"), pad.calibrationError);
        }
        pad.calibrationAttempt = token;
        std::error_code ignored;
        std::filesystem::remove(staged, ignored);
    }

    static std::string calibrationJson(const PadDefinition& pad)
    {
        const auto& profile = pad.calibration;
        const bool calibrated = profile.response.calibrated;
        const bool active = calibrated && profile.zones == pad.zoneCount;
        std::ostringstream output;
        output << "{\"state\":"
               << quoted(active       ? "active"
                         : calibrated ? "stale"
                                      : "none")
               << ",\"session\":" << quoted(profile.session)
               << ",\"sourceHash\":" << quoted(profile.sourceHash)
               << ",\"attempt\":" << quoted(pad.calibrationAttempt)
               << ",\"error\":" << quoted(pad.calibrationError)
               << ",\"headThreshold\":" << (active ? profile.response.sensors[0].threshold : 16.0f)
               << ",\"rimThreshold\":" << (active ? profile.response.sensors[1].threshold : 16.0f)
               << ",\"velocityRanges\":[";
        for (unsigned index = 0; index < 3; ++index)
        {
            if (index != 0)
            {
                output << ',';
            }
            const auto& range = profile.response.articulations[index];
            output << "{\"minimum\":" << range.minimum << ",\"maximum\":" << range.maximum
                   << ",\"samples\":" << range.samples << '}';
        }
        output << "]}";
        return output.str();
    }

    void configureLive()
    {
        std::vector<TriggerPad> inputs;
        for (const auto& pad : pads)
        {
            auto response = pad.calibration.response;
            if (pad.calibration.zones != pad.zoneCount)
            {
                response.calibrated = false;
            }
            inputs.push_back({pad.head, pad.rim, pad.zoneCount, response});
        }
        try
        {
            trigger.configure(inputs);
            haveFastPoll = false;
#ifdef MIMIC_USE_LITERT
            liveReady = model != nullptr;
            if (liveReady)
            {
                liveError.clear();
            }
#endif
        }
        catch (const std::exception& exception)
        {
            liveReady = false;
            liveError = exception.what();
        }
    }

    void processLiveFrames()
    {
        const auto now = Clock::now();
        if (!haveFastPoll || now - lastFastPoll > std::chrono::milliseconds(8))
        {
            if (haveFastPoll)
            {
                ++dmaResets;
            }
            trigger.reset();
            first.resetFrameReader();
            second.resetFrameReader();
        }
        auto left = first.readCompletedFrames();
        auto right = second.readCompletedFrames();
        const auto finished = Clock::now();
        lastFastPoll = now;
        haveFastPoll = true;
        if (!left.valid || !right.valid || finished - now > std::chrono::milliseconds(8))
        {
            trigger.reset();
            first.resetFrameReader();
            second.resetFrameReader();
            ++dmaResets;
            return;
        }
        trigger.beginBatch();
        std::array<std::uint16_t, 32> peaks{};
        for (unsigned adc = 0; adc < 2; ++adc)
        {
            const auto& batch = adc == 0 ? left : right;
            adcFrames[adc] += batch.count;
            for (std::size_t frame = 0; frame < batch.count; ++frame)
            {
                const auto& samples = batch.frames[frame];
                for (std::size_t channel = 0; channel < 16; ++channel)
                {
                    peaks[adc * 16 + channel] =
                        std::max(peaks[adc * 16 + channel],
                                 static_cast<std::uint16_t>(samples[channel] & 4095U));
                }
                trigger.processFrame(adc, samples);
            }
        }
        for (std::size_t channel = 0; channel < peaks.size(); ++channel)
        {
            displayPeaks[channel] = std::max(displayPeaks[channel], peaks[channel]);
            if (peaks[channel] < noise[channel] + 12.0)
            {
                noise[channel] += 0.01 * (peaks[channel] - noise[channel]);
            }
        }
        for (std::size_t index = 0; index < trigger.eventCount(); ++index)
        {
            const auto& event = trigger.event(index);
            const auto& pad = pads[event.pad];
            ++liveEvents;
            lastLivePad = pad.id;
            lastLiveZone = event.zone;
            lastLiveVelocity = event.velocity;
            lastLiveProbability = event.probability;
            lastLivePeak = event.peak;
            const auto note = event.zone == 1   ? pad.headNote
                              : event.zone == 2 ? pad.rimNote
                                                : pad.rimshotNote;
            if (note < 0)
            {
                ++unmappedNotes;
                continue;
            }
            char command[96]{};
            std::snprintf(command,
                          sizeof(command),
                          "hit %d %d %u 100\n",
                          pad.midiChannel,
                          note,
                          event.velocity);
            if (midi.send(command))
            {
                ++midiQueued;
            }
            else
            {
                ++midiDropped;
            }
        }
    }

#ifdef MIMIC_USE_LITERT
    std::unique_ptr<LiteRtModel> model;
#endif
    TriggerEngine trigger{[this](const DrumFeatures& features)
                          {
#ifdef MIMIC_USE_LITERT
                              if (model)
                                  return model->predict(features);
#else
                              (void)features;
#endif
                              return DrumPrediction{};
                          }};
    MidiEndpoint midi;
    std::array<std::uint64_t, 2> adcFrames{};
    bool haveFastPoll = false;
    Clock::time_point lastFastPoll{};
    bool engineRequested = false;
    bool liveReady = false;
    std::string liveError;
    std::string lastLivePad;
    unsigned lastLiveZone = 0;
    unsigned lastLiveVelocity = 0;
    float lastLiveProbability = 0.0f, lastLivePeak = 0.0f;
    std::uint64_t liveEvents = 0, midiQueued = 0, midiDropped = 0, unmappedNotes = 0, dmaResets = 0;

    struct HistoryEntry
    {
        PadSnapshot snapshot;
        PadCaptureQuality quality;
    };

    const PadDefinition& selected() const
    {
        return pads[selectedIndex];
    }

    static void requireEnd(std::istringstream& input)
    {
        if (input.fail())
        {
            throw std::runtime_error("Missing pad command arguments");
        }
        std::string extra;
        if (input >> extra)
        {
            throw std::runtime_error("Unexpected pad command arguments");
        }
    }

    void loadConfig(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        if (!input)
        {
            if (std::filesystem::exists(path))
            {
                throw std::runtime_error("Could not read " + path.string());
            }
            return;
        }
        auto proposed = pads;
        std::set<std::string> present;
        bool haveHeader = false;
        int version = 0;
        std::string line;
        while (std::getline(input, line))
        {
            line = line.substr(0, line.find('#'));
            std::istringstream record(line);
            std::string firstField;
            if (!(record >> firstField))
            {
                continue;
            }
            if (!haveHeader)
            {
                record >> version;
                requireEnd(record);
                if (firstField != "OPEN_MIMIC_PADS" || (version != 2 && version != 3))
                {
                    throw std::runtime_error(
                        "Pad configuration requires version 3 (fixed hardware routing)");
                }
                haveHeader = true;
                continue;
            }
            PadDefinition candidate{};
            candidate.id = firstField;
            if (version == 2)
            {
                record >> candidate.zoneCount >> candidate.head >> candidate.rim
                    >> candidate.midiChannel >> candidate.headNote >> candidate.rimNote;
            }
            else
            {
                record >> candidate.zoneCount >> candidate.midiChannel >> candidate.headNote
                    >> candidate.rimNote >> candidate.rimshotNote;
            }
            requireEnd(record);
            const auto found = std::find_if(proposed.begin(),
                                            proposed.end(),
                                            [&candidate](const PadDefinition& pad)
                                            { return candidate.id == pad.id; });
            if (found != proposed.end())
            {
                candidate.head = found->head;
                candidate.rim = found->rim;
            }
            if (found == proposed.end() || !validPad(candidate)
                || !present.insert(candidate.id).second)
            {
                throw std::runtime_error("Invalid or duplicate pad configuration row: "
                                         + candidate.id);
            }
            candidate.name = found->name;
            candidate.head = found->head;
            candidate.rim = found->rim;
            candidate.sd3Instrument = found->sd3Instrument;
            if (version == 2)
            {
                // Adopt the requested SD3 preset on the one-time fixed-routing migration.
                candidate.headNote = found->headNote;
                candidate.rimNote = found->rimNote;
                candidate.rimshotNote = found->rimshotNote;
            }
            *found = candidate;
        }
        if (!haveHeader || input.bad())
        {
            throw std::runtime_error("Incomplete pad configuration");
        }
        pads = std::move(proposed);
        if (version == 2)
        {
            saveConfig();
        }
    }

    void saveConfig()
    {
        std::ostringstream output;
        output << "OPEN_MIMIC_PADS 3\n";
        for (const auto& pad : pads)
        {
            output << pad.id << ' ' << pad.zoneCount << ' ' << pad.midiChannel << ' '
                   << pad.headNote << ' ' << pad.rimNote << ' ' << pad.rimshotNote << '\n';
        }
        atomicText(dataDirectory / "config", output.str());
    }

    void newSession()
    {
        wizardActive = false;
        stopStage();
        session = newIdentifier();
        counts.clear();
        captured = 0;
        submitted = 0;
        rejected = 0;
        phase.clear();
        last = {};
        lastByPad.clear();
        cumulative = {};
    }

    unsigned detectionThreshold(int channel) const
    {
        return channel < 0 ? 8U
                           : static_cast<unsigned>(std::clamp(
                                 std::ceil(noise[static_cast<std::size_t>(channel)] * 3.0 + 4.0),
                                 8.0,
                                 512.0));
    }

    void collect(const PadSnapshot& snapshot, const PadSnapshotAnalysis& analysis)
    {
        if (!pending.snapshots.empty())
        {
            pending.snapshots.push_back(snapshot);
            pending.quality += analysis.quality;
            if (pending.phase == "noise")
            {
                for (std::size_t index = 0; index < 32; ++index)
                {
                    if (analysis.peaks[index] >= detectionThreshold(static_cast<int>(index)))
                    {
                        ++pending.quality.unexpectedActivity;
                    }
                }
            }
            if (pending.snapshots.size() >= preSnapshots + 1 + postSnapshots)
            {
                writer.enqueue(std::move(pending));
                pending = {};
                ++submitted;
                nextWindow = Clock::now() + std::chrono::milliseconds(20);
                armed = false;
                quietPolls = 0;
            }
            return;
        }
        if (submitted >= stageTarget || history.size() < preSnapshots || Clock::now() < nextWindow)
        {
            return;
        }
        const auto head = static_cast<std::size_t>(selected().head);
        const auto rim = selected().zoneCount == 2 ? selected().rim : -1;
        const auto threshold =
            std::max(detectionThreshold(selected().head), detectionThreshold(rim));
        const auto localPeak =
            std::max(analysis.peaks[head],
                     rim >= 0 ? analysis.peaks[static_cast<std::size_t>(rim)] : std::uint16_t{0});
        bool active = localPeak >= threshold;
        if (phase == "xtalk" || phase == "noise")
        {
            active = false;
            for (std::size_t index = 0; index < 32; ++index)
            {
                active =
                    active || analysis.peaks[index] >= detectionThreshold(static_cast<int>(index));
            }
        }
        if (!active)
        {
            if (++quietPolls >= 3)
            {
                armed = true;
            }
        }
        else
        {
            quietPolls = 0;
        }
        const bool begin = phase == "noise" ? !active && armed : active && armed;
        if (!begin)
        {
            return;
        }
        pending.pad = selected();
        pending.phase = phase;
        pending.session = session;
        pending.id = newIdentifier();
        pending.stage = stageSequence;
        pending.threshold = threshold;
        pending.onsetRecord = history.size();
        pending.snapshots.reserve(preSnapshots + 1 + postSnapshots);
        for (const auto& entry : history)
        {
            pending.snapshots.push_back(entry.snapshot);
            pending.quality += entry.quality;
        }
        pending.snapshots.push_back(snapshot);
        pending.quality += analysis.quality;
        armed = false;
    }

    Adc first;
    Adc second;
    std::array<AdcLayout, 2> layouts;
    WindowWriter writer;
    std::filesystem::path dataDirectory;
    std::vector<PadDefinition> pads;
    std::size_t selectedIndex = 0;
    std::string session = newIdentifier();
    std::string phase;
    std::string state = "idle";
    std::string error;
    std::map<std::string, unsigned> counts;
    std::array<std::uint16_t, 32> displayPeaks{};
    std::array<double, 32> noise{};
    PadCaptureQuality cumulative;
    std::deque<HistoryEntry> history;
    PadSnapshot previous;
    Window pending;
    SavedWindow last;
    std::map<std::string, SavedWindow> lastByPad;
    Clock::time_point start = Clock::now();
    Clock::time_point nextWindow = Clock::now();
    unsigned captured = 0;
    unsigned submitted = 0;
    unsigned rejected = 0;
    unsigned quietPolls = 0;
    std::uint64_t stageSequence = 0;
    bool wizardActive = false;
    std::vector<std::string> wizardPhases;
    std::size_t wizardStep = 0;
    Clock::time_point wizardDeadline{};
    Clock::time_point sequenceStart{};
    Clock::time_point sequencePoll{};
    std::vector<std::uint8_t> sequenceBytes;
    PadCaptureQuality sequenceQuality;
    bool havePrevious = false;
    bool armed = false;
};

void sendCommand(const std::string& line)
{
    Descriptor endpoint(::open(padCommandPath, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    struct stat info{};
    if (endpoint.get() < 0 || ::fstat(endpoint.get(), &info) != 0 || !S_ISFIFO(info.st_mode))
    {
        throw std::runtime_error("Pad service is not available");
    }
    const auto command = line + '\n';
    ssize_t written;
    do
    {
        written = ::write(endpoint.get(), command.data(), command.size());
    } while (written < 0 && errno == EINTR);
    if (written != static_cast<ssize_t>(command.size()))
    {
        throw std::runtime_error("Pad command queue is full");
    }
}
#endif
} // namespace

int runPadService(const std::string& dataDirectory)
{
#ifdef __linux__
    std::filesystem::create_directories(padRuntimeDirectory);
    const std::filesystem::path requestedRoot(dataDirectory);
    if (!requestedRoot.is_absolute())
    {
        throw std::invalid_argument("Pad data directory must be absolute");
    }
    auto dataRoot = std::filesystem::weakly_canonical(requestedRoot);
    while (dataRoot != dataRoot.root_path() && dataRoot.filename().empty())
    {
        dataRoot = dataRoot.parent_path();
    }
    if (dataRoot == dataRoot.root_path() || dataRoot.parent_path() == dataRoot.root_path())
    {
        throw std::invalid_argument("Pad data directory must be a dedicated subdirectory below a "
                                    "top-level system directory");
    }
    const bool createdDataDirectory = std::filesystem::create_directories(dataRoot);
    ::chmod(padRuntimeDirectory, 0700);
    if (createdDataDirectory)
    {
        ::chmod(dataRoot.c_str(), 0700);
    }
    Descriptor ownership(
        ::open("/run/mimic-pads/service.lock", O_RDWR | O_CREAT | O_CLOEXEC, 0600));
    if (ownership.get() < 0 || ::flock(ownership.get(), LOCK_EX | LOCK_NB) != 0)
    {
        throw std::runtime_error("Another pad service owns the ADC collection workflow");
    }
    if (::mkfifo(padCommandPath, 0600) != 0 && errno != EEXIST)
    {
        throw std::system_error(errno, std::generic_category(), "Creating pad command endpoint");
    }
    Descriptor commands(::open(padCommandPath, O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    struct stat info{};
    if (commands.get() < 0 || ::fstat(commands.get(), &info) != 0 || !S_ISFIFO(info.st_mode))
    {
        throw std::runtime_error("Could not open pad command FIFO");
    }
    stopping = 0;
    const auto previousInterrupt = std::signal(SIGINT, stopPads);
    const auto previousTerminate = std::signal(SIGTERM, stopPads);
    if (previousInterrupt == SIG_ERR || previousTerminate == SIG_ERR)
    {
        throw std::runtime_error("Could not install pad service signal handlers");
    }
    try
    {
        PadService service(dataRoot);
        auto nextPoll = Clock::now();
        auto nextStatus = Clock::now();
        std::string pending;
        bool overflow = false;
        while (stopping == 0)
        {
            std::array<char, 512> bytes{};
            const auto amount = ::read(commands.get(), bytes.data(), bytes.size());
            for (ssize_t index = 0; index < amount; ++index)
            {
                const auto byte = bytes[static_cast<std::size_t>(index)];
                if (byte == '\n')
                {
                    if (overflow)
                    {
                        service.invalidCommand();
                    }
                    else
                    {
                        service.command(pending);
                    }
                    pending.clear();
                    overflow = false;
                }
                else if (pending.size() < 256 && !overflow)
                {
                    pending += byte;
                }
                else
                {
                    pending.clear();
                    overflow = true;
                }
            }
            try
            {
                service.tick();
            }
            catch (const std::exception& error)
            {
                service.failCapture(error.what());
            }
            const auto now = Clock::now();
            if (now >= nextStatus)
            {
                service.status(true);
                nextStatus = now + std::chrono::milliseconds(200);
            }
            nextPoll += service.pollInterval();
            if (nextPoll < Clock::now())
            {
                nextPoll = Clock::now();
            }
            std::this_thread::sleep_until(nextPoll);
        }
        service.stopStage();
        service.status(false);
    }
    catch (const std::exception& error)
    {
        atomicText(padStatusPath,
                   "{\"running\":false,\"state\":\"error\",\"error\":" + quoted(error.what())
                       + "}\n");
        std::signal(SIGINT, previousInterrupt);
        std::signal(SIGTERM, previousTerminate);
        throw;
    }
    std::signal(SIGINT, previousInterrupt);
    std::signal(SIGTERM, previousTerminate);
    return 0;
#else
    (void)dataDirectory;
    throw std::runtime_error("Pad collection requires the Linux ADC devices");
#endif
}

int runPadCommand(int argc, char** argv)
{
#ifdef __linux__
    if (argc < 3)
    {
        throw std::invalid_argument(
            "Usage: mimicd pad-command select|config|start|stop|discard|new-session ...");
    }
    std::ostringstream command;
    for (int index = 2; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument.empty() || argument.size() > 64
            || !std::all_of(argument.begin(),
                            argument.end(),
                            [](unsigned char character)
                            {
                                return (character >= 'a' && character <= 'z')
                                       || (character >= '0' && character <= '9')
                                       || character == '-';
                            }))
        {
            throw std::invalid_argument("Invalid pad command argument");
        }
        if (index > 2)
        {
            command << ' ';
        }
        command << argument;
    }
    if (command.str().size() > 256)
    {
        throw std::invalid_argument("Pad command is too long");
    }
    sendCommand(command.str());
    return 0;
#else
    (void)argc;
    (void)argv;
    throw std::runtime_error("Pad commands require the Linux pad service");
#endif
}
} // namespace mimic
