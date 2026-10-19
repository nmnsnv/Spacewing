// Preserve undocumented field numbers and byte layout.
#include "gesture.h"
#include "result.h"
#include <mach/mach_time.h>

namespace spacewing::workspace {
namespace {
CGEventField field(unsigned value) { return static_cast<CGEventField>(value); }
template <typename T> void little(std::vector<uint8_t> &bytes, T input) {
    auto value = static_cast<std::make_unsigned_t<T>>(input);
    for (size_t i = 0; i < sizeof(T); ++i)
        bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
CFRef<CGEventRef> augment(CGEventRef event) {
    CFRef<CFDataRef> data(CGEventCreateData(nullptr, event));
    if (!data)
        throw Error("Cannot serialize Dock gesture");
    const uint8_t *start = CFDataGetBytePtr(data.get());
    std::vector<uint8_t> bytes(start, start + CFDataGetLength(data.get()));
    if (bytes.size() < 4 || bytes[0] || bytes[1] || bytes[2] || bytes[3] != 2)
        throw Error("Unrecognized CGEvent serialization; no gesture posted");
    const auto payload = gesturePayload(event);
    bytes.insert(bytes.end(), {static_cast<uint8_t>(payload.size() >> 8),
                               static_cast<uint8_t>(payload.size()), 0x10, 0x6d});
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    CFRef<CFDataRef> combined(CFDataCreate(nullptr, bytes.data(), bytes.size()));
    CFRef<CGEventRef> result(CGEventCreateFromData(nullptr, combined.get()));
    if (!result)
        throw Error("Cannot attach Dock gesture payload");
    return result;
}
} // namespace

int32_t fixed1616(double value) {
    auto result = static_cast<int32_t>(static_cast<int64_t>(value * 65536));
    return result == 0 && value != 0 ? (value > 0 ? 1 : -1) : result;
}
std::vector<uint8_t> gesturePayload(CGEventRef event) {
    const auto phase = CGEventGetIntegerValueField(event, field(132));
    const auto vx = CGEventGetDoubleValueField(event, field(129));
    const auto vy = CGEventGetDoubleValueField(event, field(130));
    const bool velocity = vx != 0 || vy != 0 || phase == 4;
    std::vector<uint8_t> bytes;
    bytes.reserve(velocity ? 96 : 68);
    auto timestamp = CGEventGetTimestamp(event);
    little<uint64_t>(bytes, timestamp ? timestamp : mach_absolute_time());
    little<uint64_t>(bytes, 0);
    little<uint32_t>(bytes, 0);
    little<uint32_t>(bytes, 0);
    little<uint32_t>(bytes, velocity ? 2 : 1);
    little<uint32_t>(bytes, 40);
    little<uint32_t>(bytes, 23);
    little<uint32_t>(bytes, (static_cast<uint32_t>(phase) & 0xff) << 24);
    little<uint32_t>(bytes, 0);
    little<int32_t>(bytes, fixed1616(CGEventGetDoubleValueField(event, field(125))));
    little<int32_t>(bytes, fixed1616(CGEventGetDoubleValueField(event, field(126))));
    little<int32_t>(bytes, 0);
    little<uint32_t>(bytes, CGEventGetIntegerValueField(event, field(115)));
    little<uint16_t>(bytes, CGEventGetIntegerValueField(event, field(123)));
    little<uint16_t>(bytes, 3);
    little<int32_t>(bytes, fixed1616(CGEventGetDoubleValueField(event, field(124))));
    if (velocity) {
        little<uint32_t>(bytes, 28);
        little<uint32_t>(bytes, 9);
        little<uint32_t>(bytes, 0);
        little<uint32_t>(bytes, 1);
        little<int32_t>(bytes, fixed1616(vx));
        little<int32_t>(bytes, fixed1616(vy));
        little<int32_t>(bytes, 0);
    }
    return bytes;
}

CFRef<CGEventRef> dockEvent(int phase, int direction, bool augmented, bool natural) {
    CFRef<CGEventRef> event(CGEventCreate(nullptr));
    if (!event)
        throw Error("Cannot allocate Dock gesture");
    CGEventSetIntegerValueField(event.get(), field(55), 30);
    CGEventSetIntegerValueField(event.get(), field(110), 23);
    CGEventSetIntegerValueField(event.get(), field(123), 1);
    CGEventSetIntegerValueField(event.get(), field(132), phase);
    if (augmented) {
        double sign = -direction * (natural ? 1.0 : -1.0);
        CGEventSetDoubleValueField(event.get(), field(124), sign * 1e-4);
        CGEventSetDoubleValueField(event.get(), field(125), 0.1);
        if (phase == 4)
            CGEventSetDoubleValueField(event.get(), field(129), sign * 9999);
    } else {
        CGEventSetDoubleValueField(event.get(), field(124), direction * 1e-4);
        CGEventSetDoubleValueField(event.get(), field(129), direction * 2000);
        CGEventSetDoubleValueField(event.get(), field(130), direction * 2000);
    }
    return event;
}

std::vector<CFRef<CGEventRef>> gestureEvents(int direction, bool augmented, bool natural) {
    std::vector<CFRef<CGEventRef>> events;
    events.reserve(augmented ? 6 : 3);
    for (int phase : {1, 2, 4}) {
        auto event = dockEvent(phase, direction, augmented, natural);
        if (augmented)
            event = augment(event.get());
        CGEventSetIntegerValueField(event.get(), kCGEventSourceUserData, eventTag);
        events.push_back(std::move(event));
        if (augmented) {
            CFRef<CGEventRef> companion(CGEventCreate(nullptr));
            if (!companion)
                throw Error("Cannot allocate companion gesture");
            CGEventSetIntegerValueField(companion.get(), field(55), 29);
            CGEventSetIntegerValueField(companion.get(), kCGEventSourceUserData, eventTag);
            events.push_back(std::move(companion));
        }
    }
    return events;
}
bool naturalScrolling() {
    CFRef<CFPropertyListRef> value(CFPreferencesCopyAppValue(
        CFSTR("com.apple.swipescrolldirection"), kCFPreferencesAnyApplication));
    return !value || CFGetTypeID(value.get()) != CFBooleanGetTypeID() ||
           CFBooleanGetValue(static_cast<CFBooleanRef>(value.get()));
}
void postGesture(const std::vector<CFRef<CGEventRef>> &events) {
    for (const auto &event : events)
        CGEventPost(kCGSessionEventTap, event.get());
}
} // namespace spacewing::workspace
