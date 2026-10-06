#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <functional>
#include <regex>
#include <string>
#include <vector>

namespace chime {
inline std::string trim(std::string s) {
    auto white = [](unsigned char c) { return std::isspace(c); };
    while (!s.empty() && white(static_cast<unsigned char>(s.back()))) s.pop_back();
    auto p = std::find_if_not(s.begin(), s.end(), white);
    s.erase(s.begin(), p);
    return s;
}
inline std::string lower(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline bool starts(const std::string &s, const std::string &prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}
enum class Channel { Talk, Whisper, Tell, Party, Shout, DM, Unknown };
struct Message {
    Channel channel = Channel::Unknown;
    std::string speaker;
    std::string text;
    bool outgoing = false;
    bool system = false;
};
inline const char *channelName(Channel c) {
    switch (c) {
    case Channel::Talk: return "Local";
    case Channel::Whisper: return "Whisper";
    case Channel::Tell: return "Tell";
    case Channel::Party: return "Party";
    case Channel::Shout: return "Shout";
    case Channel::DM: return "DM";
    default: return "Unknown";
    }
}
inline Channel getChannel(const std::string &s) {
    auto n = lower(trim(s));
    if (n == "talk" || n == "say" || n == "local") return Channel::Talk;
    if (n == "whisper") return Channel::Whisper;
    if (n == "tell" || n == "private") return Channel::Tell;
    if (n == "party" || n == "group") return Channel::Party;
    if (n == "shout") return Channel::Shout;
    if (n == "dm" || n == "dungeon master") return Channel::DM;
    return Channel::Unknown;
}
inline std::string stripColours(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '<' && i + 1 < s.size() &&
            (s[i + 1] == 'c' || s[i + 1] == 'C' || s.compare(i, 4, "</c>") == 0)) {
            auto end = s.find('>', i);
            if (end != std::string::npos && end - i < 24) { i = end + 1; continue; }
        }
        out.push_back(s[i++]);
    }
    return out;
}
// Both NWN chat-text logging and entire-window logging use a timestamped
// "Speaker: text" form. Local speech frequently has no [Talk] marker.
inline bool parseMessage(const std::string &raw, Message &m) {
    m = Message{};
    auto s = trim(stripColours(raw));
    if (starts(s, "\xEF\xBB\xBF")) s.erase(0, 3);
    bool chatMarker = false, timestamp = false;
    static const std::regex clock(R"(\b\d{1,2}:\d{2}:\d{2}\b)");
    for (int i = 0; i < 4; ++i) {
        if (starts(s, "[CHAT WINDOW TEXT]")) {
            s = trim(s.substr(18)); chatMarker = true; continue;
        }
        if (!s.empty() && s[0] == '[') {
            auto end = s.find(']');
            if (end != std::string::npos && end < 80 &&
                std::regex_search(s.substr(1, end - 1), clock)) {
                s = trim(s.substr(end + 1)); timestamp = true; continue;
            }
        }
        break;
    }
    if (!chatMarker && !timestamp) return false;
    auto popChannel = [&](std::string &part) {
        if (!part.empty() && part[0] == '[') {
            auto end = part.find(']');
            if (end != std::string::npos) {
                auto c = getChannel(part.substr(1, end - 1));
                if (c != Channel::Unknown) {
                    m.channel = c; part = trim(part.substr(end + 1));
                    if (!part.empty() && part[0] == ':') part = trim(part.substr(1));
                    return true;
                }
            }
        }
        return false;
    };
    popChannel(s);
    auto colon = s.find(':');
    if (colon == std::string::npos || colon == 0 || colon > 160) return false;
    m.speaker = trim(s.substr(0, colon));
    m.text = trim(s.substr(colon + 1));
    popChannel(m.text);
    if (m.channel == Channel::Unknown) m.channel = Channel::Talk;
    auto speaker = lower(m.speaker);
    if (starts(speaker, "to ")) {
        m.outgoing = true; m.speaker = trim(m.speaker.substr(3)); m.channel = Channel::Tell;
    } else if (starts(speaker, "from ")) {
        m.speaker = trim(m.speaker.substr(5)); m.channel = Channel::Tell;
    }
    if (m.speaker.empty() || m.text.empty()) return false;
    static const std::vector<std::string> systemSpeakers = {
        "server", "system", "event", "combat", "unknown speaker", "error", "debug",
        "warning", "notice", "minimum tumble ac bonus", "no monk/shield ac bonus"
    };
    if (std::find(systemSpeakers.begin(), systemSpeakers.end(), speaker) != systemSpeakers.end()) return false;
    if (m.speaker[0] == '[' || starts(m.speaker, "***")) return false;
    if (chatMarker) {
        // Combat lines can also carry the chat-window prefix. Exclude common
        // engine combat forms without excluding normal RP prose.
        static const std::regex combat(R"((\*(hit|miss|critical hit|parried|success|failure)\*|\bInitiative Roll\s*:|\bDamage Immunity absorbs\b|\bSpell Resistance\s*:|\bHealed \d+ hit))", std::regex::icase);
        if (std::regex_search(m.text, combat) || std::regex_search(m.speaker, combat)) return false;
        if (std::regex_search(m.speaker, std::regex(R"(\b(damages|attacks|casts|uses|attempts)\s+.+)", std::regex::icase))) return false;
    }
    // Scripted notices can resemble unlabelled local speech. Keep this list
    // narrow: words inside a player's message must never classify the speaker.
    static const std::vector<std::string> scriptedSpeakers = {
        "loading screen", "weather", "area description", "announcement",
        "viscara - jedi temple exterior"
    };
    m.system = std::find(scriptedSpeakers.begin(), scriptedSpeakers.end(),
                         lower(m.speaker)) != scriptedSpeakers.end();
    return true;
}
struct Filters {
    bool ignoreSystem = true;
    bool local = true, whispers = true, tells = true, party = false, shouts = false;
    std::vector<std::string> ownNames;
    std::string contains;
    bool matches(const Message &m) const {
        if (m.outgoing || (ignoreSystem && m.system)) return false;
        bool channel = (m.channel == Channel::Talk && local) ||
            (m.channel == Channel::Whisper && whispers) ||
            (m.channel == Channel::Tell && tells) ||
            (m.channel == Channel::Party && party) ||
            (m.channel == Channel::Shout && shouts);
        if (!channel) return false;
        auto name = lower(trim(m.speaker));
        for (const auto &own : ownNames) if (!trim(own).empty() && name == lower(trim(own))) return false;
        return contains.empty() || lower(m.text).find(lower(contains)) != std::string::npos;
    }
};
// A bounded session transcript independent of sound and own-name filters.
struct HistoryEntry { Message message; std::string time; };
struct ChatHistory {
    size_t limit = 10000;
    std::deque<HistoryEntry> entries;
    void add(const Message &message, const std::string &time) {
        if (!limit) return;
        entries.push_back({message,time});
        while (entries.size() > limit) entries.pop_front();
    }
    std::vector<std::string> speakers() const {
        std::vector<std::string> names;
        for (const auto &entry : entries) {
            auto found = std::find_if(names.begin(), names.end(), [&](const std::string &n) {
                return lower(n) == lower(entry.message.speaker);
            });
            if (found == names.end()) names.push_back(entry.message.speaker);
        }
        std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) { return lower(a)<lower(b); });
        return names;
    }
    std::string transcriptFor(const std::vector<std::string> &speakers, bool all=false) const {
        std::string out;
        for (const auto &entry : entries) {
            if (!all && std::none_of(speakers.begin(), speakers.end(), [&](const std::string &speaker) {
                return lower(entry.message.speaker) == lower(speaker);
            })) continue;
            out += "["+entry.time+"] ["+channelName(entry.message.channel)+"] "+
                (entry.message.outgoing ? "To " : "")+entry.message.speaker+": "+entry.message.text+"\r\n\r\n";
        }
        return out;
    }
    std::string transcript(const std::string &speaker) const {
        return transcriptFor({speaker},speaker.empty());
    }
};
inline std::vector<std::string> splitNames(const std::string &s) {
    std::vector<std::string> out;
    size_t begin = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ';') {
            auto name = trim(s.substr(begin, i - begin));
            if (!name.empty()) out.push_back(name);
            begin = i + 1;
        }
    }
    return out;
}
// Byte framing keeps UTF-8 code points and Windows CRLF pairs intact even
// when NWN writes a single chat line in several chunks.
struct LineBuffer {
    std::string partial;
    bool discardUntilNewline = false;
    void reset(bool discard = false) { partial.clear(); discardUntilNewline = discard; }
    void feed(const std::string &bytes, const std::function<void(const std::string &)> &onLine) {
        for (char c : bytes) {
            if (c == '\n') {
                if (!discardUntilNewline && !partial.empty()) {
                    if (partial.back() == '\r') partial.pop_back();
                    onLine(partial);
                }
                partial.clear(); discardUntilNewline = false;
            } else if (!discardUntilNewline) {
                partial.push_back(c);
                if (partial.size() > 131072) { partial.clear(); discardUntilNewline = true; }
            }
        }
    }
};
// The state machine is independent of Windows so reset, rotation, baseline,
// and partial-write behavior can be exercised before distributing the app.
struct TailCursor {
    bool initialized = false;
    uint64_t identity = 0, creation = 0, offset = 0;
    uint32_t volume = 0;
    std::string anchor;
    LineBuffer lines;
    void prepare(uint64_t size, uint64_t id, uint64_t created, uint32_t disk,
                 const std::string &atOldOffset, const std::string &atEnd, bool skip) {
        bool replaced = initialized && (identity != id || volume != disk || creation != created);
        if (skip) {
            offset = size; anchor = atEnd;
            lines.reset(!anchor.empty() && anchor.back() != '\n');
        } else {
            bool rewritten = initialized && (size < offset || (offset && atOldOffset != anchor));
            if (!initialized || replaced || rewritten) { offset = 0; anchor.clear(); lines.reset(); }
        }
        initialized = true; identity = id; volume = disk; creation = created;
    }
    void consume(const std::string &bytes, const std::string &newAnchor,
                 const std::function<void(const std::string &)> &onLine) {
        offset += bytes.size(); lines.feed(bytes, onLine); anchor = newAnchor;
    }
};
struct AlertGate {
    bool enabled = false;
    uint64_t cooldownMs = 4000;
    bool hasPlayed = false;
    uint64_t lastSound = 0;
    std::deque<std::pair<std::string, uint64_t>> seen;
    void setEnabled(bool on) { enabled = on; hasPlayed = false; seen.clear(); }
    bool accept(const std::string &line, uint64_t now, bool suppressed = false) {
        if (!enabled || suppressed) return false;
        while (!seen.empty() && now - seen.front().second > 3000) seen.pop_front();
        for (const auto &p : seen) if (p.first == line) return false;
        seen.emplace_back(line, now);
        if (seen.size() > 256) seen.pop_front();
        if (hasPlayed && now - lastSound < cooldownMs) return false;
        hasPlayed = true; lastSound = now; return true;
    }
};
// Update exactly one section/key. Preserve BOM, comments, unrelated settings,
// and the file's newline style. Add missing sections without duplicating them.
inline std::string setSetting(std::string text, const std::string &section,
                              const std::string &key, const std::string &value) {
    std::string bom;
    if (starts(text, "\xEF\xBB\xBF")) { bom = text.substr(0, 3); text.erase(0, 3); }
    const auto newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    bool trailing = !text.empty() && text.back() == '\n';
    std::vector<std::string> lines;
    size_t begin = 0;
    while (begin < text.size()) {
        auto end = text.find('\n', begin);
        if (end == std::string::npos) end = text.size();
        auto line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line); begin = end + 1;
    }
    bool inSection = false, foundSection = false, foundKey = false;
    size_t insertAt = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        auto clean = trim(lines[i]);
        if (!clean.empty() && clean[0] == '[') {
            auto end = clean.find(']');
            if (end != std::string::npos) {
                if (inSection) insertAt = i;
                inSection = lower(clean.substr(1, end - 1)) == lower(section);
                if (inSection) { foundSection = true; insertAt = lines.size(); }
            }
        }
        if (!inSection || clean.empty() || clean[0] == '#' || clean[0] == ';') continue;
        auto eq = lines[i].find('=');
        if (eq == std::string::npos || lower(trim(lines[i].substr(0, eq))) != lower(key)) continue;
        auto commentAt = lines[i].find_first_of("#;", eq + 1);
        auto prefix = lines[i].substr(0, eq + 1);
        auto comment = commentAt == std::string::npos ? "" : " " + lines[i].substr(commentAt);
        lines[i] = prefix + " " + value + comment;
        foundKey = true;
    }
    if (!foundSection) {
        if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
        lines.push_back("[" + section + "]");
        lines.push_back(key + " = " + value);
        trailing = true;
    } else if (!foundKey) {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), key + " = " + value);
    }
    std::string result = bom;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i + 1 < lines.size() || trailing) result += newline;
    }
    return result;
}
} // namespace chime
