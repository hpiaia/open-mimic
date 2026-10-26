// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiService.h"

#include "MidiControl.h"
#include "MidiStream.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <asm/ioctls.h>
#include <asm/termbits.h>
#include <fcntl.h>
#include <poll.h>
#include <sound/asound.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mimic
{
namespace
{
#if defined(__linux__)
using Clock = std::chrono::steady_clock;

bool validPortId(const std::string& value)
{
    if (value == "din" || value == "none")
    {
        return true;
    }
    const auto separator = value.find('D', 5);
    const auto digit = [](char character) { return character >= '0' && character <= '9'; };
    return value.size() < 32 && value.compare(0, 5, "usb-C") == 0 && separator != std::string::npos
           && separator > 5 && separator + 1 < value.size()
           && std::all_of(
               value.begin() + 5, value.begin() + static_cast<std::ptrdiff_t>(separator), digit)
           && std::all_of(
               value.begin() + static_cast<std::ptrdiff_t>(separator + 1), value.end(), digit);
}

int number(const std::string& text, int minimum, int maximum)
{
    std::size_t consumed = 0;
    const auto value = std::stoi(text, &consumed);
    if (consumed != text.size() || value < minimum || value > maximum)
    {
        throw std::invalid_argument("MIDI value outside allowed range: " + text);
    }
    return value;
}

std::string jsonString(const std::string& text)
{
    std::ostringstream result;
    result << '"';
    for (const auto character : text)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '"' || byte == '\\')
        {
            result << '\\' << character;
        }
        else if (byte < 32)
        {
            result << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(byte) << std::dec;
        }
        else
        {
            result << character;
        }
    }
    result << '"';
    return result.str();
}

void atomicText(const std::string& path, const std::string& text)
{
    const auto temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output || !(output << text) || !output.flush())
        {
            throw std::runtime_error("Could not write MIDI state: " + path);
        }
    }
    std::filesystem::rename(temporary, path);
}

std::string describe(const MidiMessage& message)
{
    const auto status = message.data[0];
    std::ostringstream result;
    if (status < 0xf0)
    {
        const auto channel = (status & 15U) + 1;
        switch (status & 0xf0)
        {
        case 0x80:
        case 0x90:
            result << ((status & 0xf0) == 0x80 || message.data[2] == 0 ? "Note off" : "Note on")
                   << " ch" << channel << " note" << static_cast<unsigned>(message.data[1])
                   << " velocity" << static_cast<unsigned>(message.data[2]);
            break;
        case 0xa0:
            result << "Poly pressure ch" << channel << " note"
                   << static_cast<unsigned>(message.data[1]) << " value"
                   << static_cast<unsigned>(message.data[2]);
            break;
        case 0xb0:
            result << "CC ch" << channel << " controller" << static_cast<unsigned>(message.data[1])
                   << " value" << static_cast<unsigned>(message.data[2]);
            break;
        case 0xc0:
            result << "Program ch" << channel << " value" << static_cast<unsigned>(message.data[1]);
            break;
        case 0xd0:
            result << "Pressure ch" << channel << " value"
                   << static_cast<unsigned>(message.data[1]);
            break;
        case 0xe0:
            result << "Pitch bend ch" << channel << " value"
                   << static_cast<int>(message.data[1] | (message.data[2] << 7)) - 8192;
            break;
        }
    }
    else if (status == 0xf0)
    {
        result << "SysEx " << message.size << " bytes";
    }
    else
    {
        switch (status)
        {
        case 0xf8:
            result << "Clock";
            break;
        case 0xfa:
            result << "Start";
            break;
        case 0xfb:
            result << "Continue";
            break;
        case 0xfc:
            result << "Stop";
            break;
        case 0xfe:
            result << "Active sensing";
            break;
        case 0xff:
            result << "System reset";
            break;
        default:
            result << "System 0x" << std::hex << static_cast<unsigned>(status);
            break;
        }
    }
    return result.str();
}

volatile std::sig_atomic_t stopping = 0;

extern "C" void stopMidi(int)
{
    stopping = 1;
}

class Descriptor final
{
public:
    explicit Descriptor(int valueToOwn = -1) noexcept : value(valueToOwn) {}

    ~Descriptor()
    {
        reset();
    }

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    int get() const noexcept
    {
        return value;
    }

    void reset(int replacement = -1) noexcept
    {
        if (value >= 0)
        {
            ::close(value);
        }
        value = replacement;
    }

private:
    int value;
};

struct Port
{
    std::string id;
    std::string path;
    std::string name;
    std::string error;
    Descriptor descriptor;
    bool input = true;
    bool output = true;
    MidiStreamParser parser;
    std::deque<std::uint8_t> outgoing;
};

int deviceIoctl(int descriptor, unsigned long request, void* argument)
{
#if defined(__GLIBC__)
    return ::ioctl(descriptor, request, argument);
#else
    // musl declares the request as int; preserve the Linux request's low
    // 32 bits without an implicit unsigned-to-signed conversion warning.
    return ::ioctl(descriptor, static_cast<int>(request), argument);
#endif
}

void setupDin(int descriptor)
{
    termios2 settings{};
    if (deviceIoctl(descriptor, TCGETS2, &settings) != 0)
    {
        throw std::system_error(errno, std::generic_category(), "Reading MIDI UART settings");
    }
    settings.c_iflag = 0;
    settings.c_oflag = 0;
    settings.c_lflag = 0;
    settings.c_cflag = CS8 | CREAD | CLOCAL | BOTHER;
    settings.c_ispeed = 31250;
    settings.c_ospeed = 31250;
    settings.c_cc[VMIN] = 1;
    settings.c_cc[VTIME] = 0;
    if (deviceIoctl(descriptor, TCSETS2, &settings) != 0)
    {
        throw std::system_error(errno, std::generic_category(), "Setting MIDI UART to 31250 8N1");
    }
    termios2 applied{};
    if (deviceIoctl(descriptor, TCGETS2, &applied) != 0 || applied.c_ispeed != 31250
        || applied.c_ospeed != 31250 || (applied.c_cflag & (CSIZE | PARENB | CSTOPB)) != CS8)
    {
        throw std::runtime_error("MIDI UART did not accept 31250 baud 8N1");
    }
}

void openPort(Port& port)
{
    port.error.clear();
    port.input = true;
    port.output = true;
    auto descriptor = ::open(port.path.c_str(), O_RDWR | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (descriptor < 0 && port.id != "din")
    {
        descriptor = ::open(port.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        port.output = false;
        if (descriptor < 0)
        {
            descriptor = ::open(port.path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
            port.output = true;
            port.input = false;
        }
    }
    if (descriptor < 0)
    {
        port.error = std::strerror(errno);
        return;
    }
    port.descriptor.reset(descriptor);
    try
    {
        if (port.id == "din")
        {
            setupDin(descriptor);
        }
        else
        {
            snd_rawmidi_info info{};
            info.stream = port.input ? SNDRV_RAWMIDI_STREAM_INPUT : SNDRV_RAWMIDI_STREAM_OUTPUT;
            if (deviceIoctl(descriptor, SNDRV_RAWMIDI_IOCTL_INFO, &info) != 0)
            {
                throw std::system_error(errno, std::generic_category(), "Querying USB MIDI port");
            }
            if (info.name[0] != 0)
            {
                port.name = std::string(
                    reinterpret_cast<const char*>(info.name),
                    ::strnlen(reinterpret_cast<const char*>(info.name), sizeof(info.name)));
            }
            for (const auto direction : {SNDRV_RAWMIDI_STREAM_INPUT, SNDRV_RAWMIDI_STREAM_OUTPUT})
            {
                if ((direction == SNDRV_RAWMIDI_STREAM_INPUT && !port.input)
                    || (direction == SNDRV_RAWMIDI_STREAM_OUTPUT && !port.output))
                {
                    continue;
                }
                snd_rawmidi_params parameters{};
                parameters.stream = direction;
                parameters.buffer_size = 4096;
                parameters.avail_min = 1;
                parameters.no_active_sensing = 1;
                if (deviceIoctl(descriptor, SNDRV_RAWMIDI_IOCTL_PARAMS, &parameters) != 0)
                {
                    throw std::system_error(
                        errno, std::generic_category(), "Configuring USB MIDI queue");
                }
            }
        }
    }
    catch (const std::exception& error)
    {
        port.error = error.what();
        port.descriptor.reset();
    }
}

struct NoteOff
{
    std::string port;
    std::uint8_t channel;
    std::uint8_t note;
    Clock::time_point due;
};

struct MidiEvent
{
    std::uint64_t sequence;
    std::int64_t timestampUs;
    std::string port;
    bool input;
    std::string text;
    std::vector<std::uint8_t> bytes;
};

class MidiService final
{
public:
    explicit MidiService(std::string dinDevicePath) : dinDevice(std::move(dinDevicePath))
    {
        std::ifstream input(midiSettingsPath);
        std::string in;
        std::string out;
        int channel = 0;
        if (input >> in >> out >> channel && validPortId(in) && validPortId(out) && channel >= 0
            && channel <= 16)
        {
            selectedInput = in;
            selectedOutput = out;
            inputChannel = channel;
        }
    }

    void refreshPorts()
    {
        std::vector<std::pair<std::string, std::string>> discovered{{"din", dinDevice}};
        std::error_code error;
        const std::filesystem::directory_iterator entries("/dev/snd", error);
        if (!error)
        {
            for (const auto& entry : entries)
            {
                const auto name = entry.path().filename().string();
                if (name.compare(0, 5, "midiC") == 0)
                {
                    const auto id = "usb-" + name.substr(4);
                    if (validPortId(id))
                    {
                        discovered.emplace_back(id, entry.path().string());
                    }
                }
            }
        }
        std::sort(discovered.begin(), discovered.end());
        ports.erase(std::remove_if(ports.begin(),
                                   ports.end(),
                                   [&discovered](const auto& port)
                                   {
                                       return std::none_of(discovered.begin(),
                                                           discovered.end(),
                                                           [&port](const auto& item)
                                                           { return item.first == port->id; });
                                   }),
                    ports.end());
        for (const auto& [id, path] : discovered)
        {
            auto* port = find(id);
            if (port == nullptr)
            {
                auto created = std::make_unique<Port>();
                created->id = id;
                created->path = path;
                created->name = id == "din" ? "DIN MIDI" : "USB MIDI " + id.substr(4);
                ports.push_back(std::move(created));
                port = ports.back().get();
            }
            if (port->descriptor.get() < 0)
            {
                openPort(*port);
            }
        }
    }

    void processCommand(const std::string& line)
    {
        try
        {
            std::istringstream stream(line);
            std::string command;
            stream >> command;
            if (command == "config")
            {
                std::string input;
                std::string output;
                int channel = -1;
                stream >> input >> output >> channel;
                if (!stream || !validPortId(input) || !validPortId(output) || channel < 0
                    || channel > 16)
                {
                    throw std::runtime_error("Invalid MIDI configuration");
                }
                // Finish our own test notes before switching the output destination.
                atomicText(midiSettingsPath,
                           input + " " + output + " " + std::to_string(channel) + "\n");
                finishTestNotes();
                selectedInput = input;
                selectedOutput = output;
                inputChannel = channel;
                for (auto& port : ports)
                {
                    port->parser.reset();
                }
            }
            else if (command == "test" || command == "hit")
            {
                int channel = 0;
                int note = -1;
                int velocity = -1;
                int duration = 0;
                stream >> channel >> note >> velocity >> duration;
                if (!stream || channel < 1 || channel > 16 || note < 0 || note > 127 || velocity < 1
                    || velocity > 127 || duration < 50 || duration > 5000)
                {
                    throw std::runtime_error("Invalid MIDI test message");
                }
                if (notes.size() >= 128)
                {
                    throw std::runtime_error("Too many pending MIDI test notes");
                }
                auto& port = outputPort();
                requireQueueSpace(port, 6);
                const auto midiChannel = static_cast<std::uint8_t>(channel - 1);
                // Retriggering a drum note replaces its pending note-off. An
                // older timer must not cut off the new strike later.
                for (auto previous = notes.begin(); previous != notes.end();)
                {
                    if (previous->port == port.id && previous->channel == midiChannel
                        && previous->note == static_cast<std::uint8_t>(note))
                    {
                        enqueue(port,
                                {static_cast<std::uint8_t>(0x80 | midiChannel),
                                 static_cast<std::uint8_t>(note),
                                 0});
                        previous = notes.erase(previous);
                    }
                    else
                    {
                        ++previous;
                    }
                }

                enqueue(port,
                        {static_cast<std::uint8_t>(0x90 | midiChannel),
                         static_cast<std::uint8_t>(note),
                         static_cast<std::uint8_t>(velocity)});
                notes.push_back({port.id,
                                 midiChannel,
                                 static_cast<std::uint8_t>(note),
                                 Clock::now() + std::chrono::milliseconds(duration)});
            }
            else if (command == "panic")
            {
                auto& port = outputPort();
                requireQueueSpace(port, 96 + notes.size() * 3);
                finishTestNotes();
                for (std::uint8_t channel = 0; channel < 16; ++channel)
                {
                    enqueue(port, {static_cast<std::uint8_t>(0xb0 | channel), 123, 0});
                    enqueue(port, {static_cast<std::uint8_t>(0xb0 | channel), 120, 0});
                }
            }
            else
            {
                throw std::runtime_error("Unknown MIDI command");
            }
            commandError.clear();
        }
        catch (const std::exception& error)
        {
            commandError = error.what();
        }
    }

    void tick()
    {
        const auto now = Clock::now();
        for (auto iterator = notes.begin(); iterator != notes.end();)
        {
            if (iterator->due <= now)
            {
                if (auto* port = find(iterator->port);
                    port != nullptr && port->descriptor.get() >= 0)
                {
                    enqueue(
                        *port,
                        {static_cast<std::uint8_t>(0x80 | iterator->channel), iterator->note, 0});
                }
                iterator = notes.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        for (auto& port : ports)
        {
            if (port->descriptor.get() < 0)
            {
                continue;
            }
            if (port->input)
            {
                std::array<std::uint8_t, 512> bytes{};
                // Bound each pass so a busy input cannot starve note-off/output.
                for (int pass = 0; pass < 8; ++pass)
                {
                    const auto amount = ::read(port->descriptor.get(), bytes.data(), bytes.size());
                    if (amount < 0 && errno == EINTR)
                    {
                        continue;
                    }
                    if (amount < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        disconnect(*port, std::strerror(errno));
                    }
                    if (amount <= 0)
                    {
                        break;
                    }
                    if (port->id == selectedInput)
                    {
                        InputContext context{this, port.get()};
                        for (std::ptrdiff_t index = 0; index < amount; ++index)
                        {
                            port->parser.push(
                                bytes[static_cast<std::size_t>(index)], &context, received);
                        }
                    }
                }
            }
            flush(*port);
        }
    }

    void writeStatus(bool running)
    {
        std::ostringstream output;
        auto error = commandError;
        for (const auto& id : {selectedInput, selectedOutput})
        {
            if (error.empty() && id != "none")
            {
                const auto* selected = find(id);
                if (selected == nullptr || selected->descriptor.get() < 0)
                {
                    error = "Selected MIDI port unavailable: " + id;
                    if (selected != nullptr && !selected->error.empty())
                    {
                        error += " (" + selected->error + ")";
                    }
                }
            }
        }
        output << "{\"running\":" << (running ? "true" : "false")
               << ",\"error\":" << jsonString(error)
               << ",\"selectedInput\":" << jsonString(selectedInput)
               << ",\"selectedOutput\":" << jsonString(selectedOutput)
               << ",\"inputChannel\":" << inputChannel << ",\"rxMessages\":" << rxMessages
               << ",\"txMessages\":" << txMessages << ",\"ports\":[";
        bool separator = false;
        std::uint64_t discarded = 0;
        for (const auto& port : ports)
        {
            if (separator)
            {
                output << ',';
            }
            separator = true;
            output << "{\"id\":" << jsonString(port->id) << ",\"name\":" << jsonString(port->name)
                   << ",\"input\":" << (port->input ? "true" : "false")
                   << ",\"output\":" << (port->output ? "true" : "false")
                   << ",\"connected\":" << (port->descriptor.get() >= 0 ? "true" : "false")
                   << ",\"error\":" << jsonString(port->error) << '}';
            discarded += port->parser.discardedMessages();
        }
        output << "],\"discardedMessages\":" << discarded << ",\"events\":[";
        separator = false;
        for (const auto& event : events)
        {
            if (separator)
            {
                output << ',';
            }
            separator = true;
            output << "{\"sequence\":" << event.sequence << ",\"timestampUs\":" << event.timestampUs
                   << ",\"port\":" << jsonString(event.port)
                   << ",\"direction\":" << (event.input ? "\"in\"" : "\"out\"")
                   << ",\"text\":" << jsonString(event.text) << ",\"bytes\":[";
            for (std::size_t index = 0; index < event.bytes.size(); ++index)
            {
                if (index != 0)
                {
                    output << ',';
                }
                output << static_cast<unsigned>(event.bytes[index]);
            }
            output << "]}";
        }
        output << "]}\n";
        atomicText(midiStatusPath, output.str());
    }

    void finishTestNotes()
    {
        for (const auto& note : notes)
        {
            if (auto* port = find(note.port); port != nullptr && port->descriptor.get() >= 0)
            {
                enqueue(*port, {static_cast<std::uint8_t>(0x80 | note.channel), note.note, 0});
            }
        }
        notes.clear();
    }

    bool hasPendingOutput() const
    {
        for (const auto& port : ports)
        {
            if (!port->outgoing.empty())
            {
                return true;
            }
            int queued = 0;
            if (port->id == "din" && port->descriptor.get() >= 0
                && deviceIoctl(port->descriptor.get(), TIOCOUTQ, &queued) == 0 && queued > 0)
            {
                return true;
            }
        }
        return false;
    }

private:
    struct InputContext
    {
        MidiService* service;
        Port* port;
    };

    static void received(void* pointer, const MidiMessage& message) noexcept
    {
        const auto& context = *static_cast<InputContext*>(pointer);
        auto& service = *context.service;
        const auto status = message.data[0];
        if (status < 0xf0 && service.inputChannel != 0 && (status & 15) + 1 != service.inputChannel)
        {
            return;
        }
        ++service.rxMessages;
        // Clock/active sensing count toward RX but do not flood the note monitor.
        if (status != 0xf8 && status != 0xfe)
        {
            service.addEvent(*context.port, true, message);
        }
    }

    void addEvent(const Port& port, bool input, const MidiMessage& message)
    {
        events.push_back(
            {++sequence,
             std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch())
                 .count(),
             port.id,
             input,
             (input ? "RX " : "TX ") + port.name + ": " + describe(message),
             {message.data.begin(),
              message.data.begin() + static_cast<std::ptrdiff_t>(message.size)}});
        // Complete bounded SysEx bytes fit below the UI's 64 KiB status limit.
        if (events.size() > 12)
        {
            events.pop_front();
        }
    }

    Port* find(const std::string& id)
    {
        const auto iterator = std::find_if(
            ports.begin(), ports.end(), [&id](const auto& port) { return port->id == id; });
        return iterator == ports.end() ? nullptr : iterator->get();
    }

    Port& outputPort()
    {
        auto* port = find(selectedOutput);
        if (port == nullptr || !port->output || port->descriptor.get() < 0)
        {
            throw std::runtime_error("Selected MIDI output is unavailable");
        }
        return *port;
    }

    void enqueue(Port& port, std::initializer_list<std::uint8_t> bytes)
    {
        if (port.outgoing.size() + bytes.size() > maximumQueueBytes)
        {
            throw std::runtime_error("MIDI output queue is full");
        }
        port.outgoing.insert(port.outgoing.end(), bytes.begin(), bytes.end());
        MidiMessage message;
        message.size = bytes.size();
        std::copy(bytes.begin(), bytes.end(), message.data.begin());
        ++txMessages;
        addEvent(port, false, message);
    }

    void requireQueueSpace(const Port& port, std::size_t bytes)
    {
        // Reserve space for every outstanding test's future note-off as well.
        if (port.outgoing.size() + bytes + notes.size() * 3 > maximumQueueBytes)
        {
            throw std::runtime_error("MIDI output queue is full");
        }
    }

    void flush(Port& port)
    {
        if (port.descriptor.get() < 0 || port.outgoing.empty())
        {
            return;
        }
        std::array<std::uint8_t, 256> bytes{};
        const auto count = std::min(bytes.size(), port.outgoing.size());
        std::copy_n(port.outgoing.begin(), count, bytes.begin());
        const auto written = ::write(port.descriptor.get(), bytes.data(), count);
        if (written > 0)
        {
            port.outgoing.erase(port.outgoing.begin(), port.outgoing.begin() + written);
        }
        else if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
            disconnect(port, std::strerror(errno));
        }
    }

    void disconnect(Port& port, const std::string& reason)
    {
        port.error = reason;
        port.descriptor.reset();
        port.parser.reset();
        port.outgoing.clear();
    }

    std::vector<std::unique_ptr<Port>> ports;
    static constexpr std::size_t maximumQueueBytes = 4096;
    std::string dinDevice;
    std::vector<NoteOff> notes;
    std::deque<MidiEvent> events;
    std::string selectedInput = "din";
    std::string selectedOutput = "din";
    std::string commandError;
    int inputChannel = 0;
    std::uint64_t rxMessages = 0;
    std::uint64_t txMessages = 0;
    std::uint64_t sequence = 0;
};

void sendCommand(const std::string& command)
{
    Descriptor descriptor(::open(midiCommandPath, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    if (descriptor.get() < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Connecting to MIDI service");
    }
    struct stat info{};
    if (::fstat(descriptor.get(), &info) != 0 || !S_ISFIFO(info.st_mode))
    {
        throw std::runtime_error("MIDI command endpoint is not a FIFO");
    }
    const auto bytes = command + '\n';
    ssize_t amount;
    do
    {
        amount = ::write(descriptor.get(), bytes.data(), bytes.size());
    } while (amount < 0 && errno == EINTR);
    if (amount != static_cast<ssize_t>(bytes.size()))
    {
        throw std::runtime_error("MIDI service command queue is full");
    }
}
#endif
} // namespace

int runMidiService(const std::string& dinDevice)
{
#if defined(__linux__)
    std::filesystem::create_directories(midiRuntimeDirectory);
    ::chmod(midiRuntimeDirectory, 0700);
    Descriptor ownership(
        ::open("/run/mimic-midi/service.lock", O_RDWR | O_CREAT | O_CLOEXEC, 0600));
    if (ownership.get() < 0 || ::flock(ownership.get(), LOCK_EX | LOCK_NB) != 0)
    {
        throw std::system_error(errno, std::generic_category(), "Acquiring MIDI service ownership");
    }
    if (::mkfifo(midiCommandPath, 0600) != 0 && errno != EEXIST)
    {
        throw std::system_error(errno, std::generic_category(), "Creating MIDI command endpoint");
    }
    Descriptor commands(::open(midiCommandPath, O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    struct stat info{};
    if (commands.get() < 0 || ::fstat(commands.get(), &info) != 0 || !S_ISFIFO(info.st_mode))
    {
        throw std::runtime_error("Could not open MIDI command FIFO");
    }
    stopping = 0;
    const auto previousInterrupt = std::signal(SIGINT, stopMidi);
    const auto previousTerminate = std::signal(SIGTERM, stopMidi);
    if (previousInterrupt == SIG_ERR || previousTerminate == SIG_ERR)
    {
        throw std::runtime_error("Could not install MIDI service signal handlers");
    }
    if (dinDevice.empty() || dinDevice.front() != '/')
    {
        throw std::invalid_argument("MIDI UART device path must be absolute");
    }
    MidiService service(dinDevice);
    service.refreshPorts();
    auto nextRefresh = Clock::now() + std::chrono::seconds(1);
    auto nextStatus = Clock::now();
    std::string pending;
    bool oversizedCommand = false;
    while (stopping == 0)
    {
        pollfd wait{commands.get(), POLLIN, 0};
        ::poll(&wait, 1, 2);
        std::array<char, 512> bytes{};
        const auto amount = ::read(commands.get(), bytes.data(), bytes.size());
        if (amount > 0)
        {
            for (ssize_t index = 0; index < amount; ++index)
            {
                const auto byte = bytes[static_cast<std::size_t>(index)];
                if (byte == '\n')
                {
                    service.processCommand(oversizedCommand ? "invalid-oversized-command"
                                                            : pending);
                    pending.clear();
                    oversizedCommand = false;
                }
                else if (!oversizedCommand && pending.size() < 256)
                {
                    pending += byte;
                }
                else
                {
                    oversizedCommand = true;
                }
            }
        }
        service.tick();
        const auto now = Clock::now();
        if (now >= nextRefresh)
        {
            service.refreshPorts();
            nextRefresh = now + std::chrono::seconds(1);
        }
        if (now >= nextStatus)
        {
            service.writeStatus(true);
            nextStatus = now + std::chrono::milliseconds(200);
        }
    }
    service.finishTestNotes();
    // A full 4096-byte UART queue takes about 1.3 seconds at MIDI baud. Give
    // our own note-offs a bounded drain window before closing the descriptor.
    for (int pass = 0; pass < 750; ++pass)
    {
        service.tick();
        if (!service.hasPendingOutput())
        {
            break;
        }
        ::poll(nullptr, 0, 2);
    }
    service.writeStatus(false);
    std::signal(SIGINT, previousInterrupt);
    std::signal(SIGTERM, previousTerminate);
    return 0;
#else
    (void)dinDevice;
    throw std::runtime_error("MIDI service requires Linux UART/raw MIDI devices");
#endif
}

int runMidiCommand(int argc, char** argv)
{
#if defined(__linux__)
    const std::string command = argv[1];
    if (command == "midi-panic" && argc == 2)
    {
        sendCommand("panic");
        return 0;
    }
    std::string input = "din";
    std::string output = "din";
    int channel = command == "midi-config" ? 0 : 10;
    int note = 36;
    int velocity = 96;
    int duration = 250;
    for (int index = 2; index < argc; index += 2)
    {
        if (index + 1 >= argc)
        {
            throw std::invalid_argument("Missing MIDI command option value");
        }
        const std::string option = argv[index];
        const std::string value = argv[index + 1];
        if (command == "midi-config" && (option == "--input" || option == "--output"))
        {
            if (!validPortId(value))
            {
                throw std::invalid_argument("Invalid MIDI port identifier");
            }
            (option == "--input" ? input : output) = value;
        }
        else if (option == "--channel")
        {
            channel = number(value, command == "midi-config" ? 0 : 1, 16);
        }
        else if (command == "midi-test" && option == "--note")
        {
            note = number(value, 0, 127);
        }
        else if (command == "midi-test" && option == "--velocity")
        {
            velocity = number(value, 1, 127);
        }
        else if (command == "midi-test" && option == "--ms")
        {
            duration = number(value, 50, 5000);
        }
        else
        {
            throw std::invalid_argument("Unknown MIDI command option: " + option);
        }
    }
    if (command == "midi-config")
    {
        sendCommand("config " + input + " " + output + " " + std::to_string(channel));
    }
    else if (command == "midi-test")
    {
        sendCommand("test " + std::to_string(channel) + " " + std::to_string(note) + " "
                    + std::to_string(velocity) + " " + std::to_string(duration));
    }
    else
    {
        throw std::invalid_argument("Unknown MIDI command");
    }
    return 0;
#else
    (void)argc;
    (void)argv;
    throw std::runtime_error("MIDI commands require the Linux MIDI service");
#endif
}
} // namespace mimic
