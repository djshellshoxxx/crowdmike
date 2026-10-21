#include "SessionMetadata.h"
#include <algorithm>

namespace crowdmike::rec {

juce::String markerTypeToString(MarkerType t) {
    switch (t) {
        case MarkerType::BigCheer: return "BigCheer";
        case MarkerType::Chant: return "Chant";
        case MarkerType::Applause: return "Applause";
        case MarkerType::PerformerMoment: return "PerformerMoment";
        case MarkerType::Problem: return "Problem";
        case MarkerType::Custom: break;
    }
    return "Custom";
}

MarkerType markerTypeFromString(const juce::String& s) {
    for (auto t : { MarkerType::BigCheer, MarkerType::Chant, MarkerType::Applause, MarkerType::PerformerMoment,
                    MarkerType::Problem })
        if (s == markerTypeToString(t)) return t;
    return MarkerType::Custom;
}

std::vector<SessionInfo::FileEntry> SessionInfo::filesForTrack(int t) const {
    std::vector<FileEntry> r;
    for (auto& f : files)
        if (f.track == t) r.push_back(f);
    std::sort(r.begin(), r.end(), [](auto& a, auto& b) { return a.part < b.part; });
    return r;
}

juce::var SessionMetadata::toVar(const SessionInfo& s) {
    auto* root = new juce::DynamicObject();
    juce::var rootVar(root);
    root->setProperty("format", "CrowdMikeSession");
    root->setProperty("formatVersion", 1);
    root->setProperty("appVersion", s.appVersion);
    root->setProperty("startTime", s.startTimeIso);
    root->setProperty("sampleRate", s.sampleRate);
    if (s.tempoBpm > 0.0) root->setProperty("tempoBpm", s.tempoBpm);
    root->setProperty("take", s.take);
    root->setProperty("scene", s.scene);
    root->setProperty("show", s.show);
    root->setProperty("venue", s.venue);
    root->setProperty("fileFormat", s.fileFormat);
    root->setProperty("startFrame", s.startFrame);
    root->setProperty("preRollFrames", s.preRollFrames);
    root->setProperty("totalFrames", s.totalFrames);

    juce::Array<juce::var> tracks;
    for (auto& t : s.tracks) {
        auto* o = new juce::DynamicObject();
        o->setProperty("track", t.track);
        o->setProperty("sourceId", t.sourceId);
        o->setProperty("name", t.name);
        o->setProperty("channels", t.numChannels);
        juce::Array<juce::var> chNames;
        if (t.numChannels == 1) chNames.add(t.name);
        else { chNames.add(t.name + " L"); chNames.add(t.name + " R"); }
        o->setProperty("channelNames", chNames);
        tracks.add(juce::var(o));
    }
    root->setProperty("tracks", tracks);

    juce::Array<juce::var> files;
    for (auto& f : s.files) {
        auto* o = new juce::DynamicObject();
        o->setProperty("name", f.fileName);
        o->setProperty("track", f.track);
        o->setProperty("sourceId", f.sourceId);
        o->setProperty("trackName", f.trackName);
        o->setProperty("channels", f.numChannels);
        o->setProperty("part", f.part);
        o->setProperty("startFrame", f.startFrame);
        o->setProperty("frames", f.numFrames);
        files.add(juce::var(o));
    }
    root->setProperty("files", files);

    juce::Array<juce::var> markers;
    for (auto& m : s.markers) {
        auto* o = new juce::DynamicObject();
        o->setProperty("frame", m.frame);
        o->setProperty("seconds", m.seconds);
        o->setProperty("label", m.label);
        o->setProperty("type", markerTypeToString(m.type));
        o->setProperty("scene", m.scene);
        o->setProperty("wallClock", m.wallClock.toISO8601(true));
        markers.add(juce::var(o));
    }
    root->setProperty("markers", markers);

    juce::Array<juce::var> drops;
    for (auto& d : s.dropouts) {
        auto* o = new juce::DynamicObject();
        o->setProperty("frame", d.frame);
        o->setProperty("seconds", s.sampleRate > 0 ? (double) d.frame / s.sampleRate : 0.0);
        o->setProperty("track", d.track);
        o->setProperty("frames", d.frames);
        drops.add(juce::var(o));
    }
    root->setProperty("dropouts", drops);
    root->setProperty("routeSnapshot", s.routeSnapshot);
    return rootVar;
}

bool SessionMetadata::write(const juce::File& json, const SessionInfo& s, juce::String& error) {
    const auto text = juce::JSON::toString(toVar(s), false);
    if (! json.getParentDirectory().isDirectory()) {
        error = "Session folder missing: " + json.getParentDirectory().getFullPathName();
        return false;
    }
    juce::TemporaryFile tmp(json);
    if (! tmp.getFile().replaceWithText(text, false, false, "\n") || ! tmp.getFile().existsAsFile()) {
        error = "Could not write session file " + json.getFullPathName();
        return false;
    }
    if (! tmp.overwriteTargetFileWithTemporary()) {
        error = "Could not replace session file " + json.getFullPathName();
        return false;
    }
    return true;
}

bool SessionMetadata::read(const juce::File& json, SessionInfo& out) {
    juce::var v;
    if (! json.existsAsFile() || juce::JSON::parse(json.loadFileAsString(), v).failed()) return false;
    if (v.getProperty("format", {}).toString() != "CrowdMikeSession") return false;
    out = SessionInfo();
    auto i64 = [](const juce::var& x) { return (juce::int64) x; };
    out.appVersion = v["appVersion"].toString();
    out.startTimeIso = v["startTime"].toString();
    out.sampleRate = (double) v["sampleRate"];
    out.tempoBpm = (double) v["tempoBpm"];
    out.take = (int) v["take"];
    out.scene = v["scene"].toString();
    out.show = v["show"].toString();
    out.venue = v["venue"].toString();
    out.fileFormat = v["fileFormat"].toString();
    out.startFrame = i64(v["startFrame"]);
    out.preRollFrames = i64(v["preRollFrames"]);
    out.totalFrames = i64(v["totalFrames"]);
    out.routeSnapshot = v["routeSnapshot"];
    if (auto* a = v["tracks"].getArray())
        for (auto& t : *a)
            out.tracks.push_back({ (int) t["track"], (int) t["sourceId"], t["name"].toString(), (int) t["channels"] });
    if (auto* a = v["files"].getArray())
        for (auto& f : *a) {
            SessionInfo::FileEntry e;
            e.fileName = f["name"].toString();
            e.file = json.getParentDirectory().getChildFile(e.fileName);
            e.track = (int) f["track"];
            e.sourceId = (int) f["sourceId"];
            e.trackName = f["trackName"].toString();
            e.numChannels = (int) f["channels"];
            e.part = (int) f["part"];
            e.startFrame = i64(f["startFrame"]);
            e.numFrames = i64(f["frames"]);
            out.files.push_back(e);
        }
    if (auto* a = v["markers"].getArray())
        for (auto& m : *a) {
            Marker k;
            k.frame = i64(m["frame"]);
            k.seconds = (double) m["seconds"];
            k.label = m["label"].toString();
            k.type = markerTypeFromString(m["type"].toString());
            k.scene = m["scene"].toString();
            k.wallClock = juce::Time::fromISO8601(m["wallClock"].toString());
            out.markers.push_back(k);
        }
    if (auto* a = v["dropouts"].getArray())
        for (auto& d : *a) out.dropouts.push_back({ i64(d["frame"]), (int) d["track"], (int) d["frames"] });
    return out.sampleRate > 0.0;
}

} // namespace crowdmike::rec
