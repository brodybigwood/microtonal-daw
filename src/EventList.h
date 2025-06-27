#pragma once

#include "pluginterfaces/vst/ivstevents.h"
#include <vector>

class EventList : public Steinberg::Vst::IEventList {
public:
    std::vector<Steinberg::Vst::Event> events;

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events.push_back(e);
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 idx, Steinberg::Vst::Event& e) override {
        if (idx < 0 || idx >= static_cast<Steinberg::int32>(events.size()))
            return Steinberg::kInvalidArgument;
        e = events[idx];
        return Steinberg::kResultOk;
    }

    Steinberg::int32 PLUGIN_API getEventCount() override {
        return static_cast<Steinberg::int32>(events.size());
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};
