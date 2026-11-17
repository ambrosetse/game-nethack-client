/* Translation of core (NetHack-generated) text: nhb_core_text().
 *
 * The table is tools/core_text_table.txt, compiled in through the generated
 * core_text_table.inc.  godot/scripts/core/core_text.gd implements the same
 * algorithm on the same rows for use without the native extension; keep the
 * two in step.
 *
 *  - English (or an unset language) and empty strings pass through.
 *  - Multi-line text is split into paragraphs at blank lines; a paragraph is
 *    tried whole (lines joined, whitespace collapsed), else line by line.
 *  - A line keeps its leading/trailing whitespace and a trailing choice
 *    suffix (" [ynq]", " (y)").  The rest is looked up exactly, then with
 *    whitespace collapsed, then with the first letter's case swapped, then
 *    matched against the pattern rows in table order.
 *  - In a pattern, {n} is any non-empty text kept verbatim and {n:t} is
 *    translated: whole, else word by word (handling a trailing "," and "'s");
 *    if any word is unknown the capture stays English.
 *  - Anything not found comes back unchanged.
 */

#include "nhbridge.h"

#include <atomic>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Row {
    const char *text[5]; /* english, zh_TW, zh_CN, ko, ja */
};

const Row kRows[] = {
#include "core_text_table.inc"
};

const char *const kLangs[] = {"zh_TW", "zh_CN", "ko", "ja"};
constexpr int kLangCount = 4;
constexpr int kKo = 2;

std::atomic<int> g_lang{-1};

struct Segment {
    bool capture;
    std::string text; /* literal text, or "" for a capture */
    int num;
    bool translate;
};

struct Pattern {
    int row;
    std::vector<Segment> segs;
};

struct Tables {
    std::unordered_map<std::string, int> exact;
    std::vector<Pattern> patterns;
};

std::vector<Segment> parse_template(const std::string &s) {
    std::vector<Segment> out;
    std::string lit;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '{') {
            size_t j = i + 1;
            int num = 0;
            bool digits = false;
            while (j < s.size() && std::isdigit((unsigned char)s[j])) {
                num = num * 10 + (s[j] - '0');
                ++j;
                digits = true;
            }
            bool tr = false;
            if (digits && j + 1 < s.size() && s[j] == ':' && s[j + 1] == 't') {
                tr = true;
                j += 2;
            }
            if (digits && j < s.size() && s[j] == '}') {
                if (!lit.empty())
                    out.push_back({false, lit, 0, false});
                lit.clear();
                out.push_back({true, "", num, tr});
                i = j + 1;
                continue;
            }
        }
        lit += s[i++];
    }
    if (!lit.empty())
        out.push_back({false, lit, 0, false});
    return out;
}

const Tables &tables() {
    static const Tables t = [] {
        Tables r;
        int n = (int)(sizeof(kRows) / sizeof(kRows[0]));
        for (int i = 0; i < n; ++i) {
            std::string en = kRows[i].text[0];
            std::vector<Segment> segs = parse_template(en);
            bool pattern = false;
            for (const Segment &s : segs)
                pattern = pattern || s.capture;
            if (pattern)
                r.patterns.push_back({i, segs});
            else
                r.exact.emplace(en, i);
        }
        return r;
    }();
    return t;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string normalise(const std::string &s) {
    std::string out;
    bool pending = false;
    for (char c : s) {
        if (is_space(c)) {
            pending = !out.empty();
        } else {
            if (pending)
                out += ' ';
            pending = false;
            out += c;
        }
    }
    return out;
}

const char *row_text(int row, int lang) { return kRows[row].text[lang + 1]; }

/* exact, collapsed-whitespace and first-letter case swapped lookups */
bool lookup(const std::string &s, int lang, std::string &out) {
    const Tables &t = tables();
    auto it = t.exact.find(s);
    if (it == t.exact.end()) {
        std::string n = normalise(s);
        it = t.exact.find(n);
        if (it == t.exact.end() && !n.empty() && std::isalpha((unsigned char)n[0])) {
            unsigned char c = (unsigned char)n[0];
            n[0] = (char)(std::isupper(c) ? std::tolower(c) : std::toupper(c));
            it = t.exact.find(n);
        }
    }
    if (it == t.exact.end())
        return false;
    out = row_text(it->second, lang);
    return true;
}

bool ends_with(const std::string &s, const char *suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

/* A capture marked {n:t}: whole, else word by word; false if any word is
 * unknown. */
bool translate_capture(const std::string &cap, int lang, std::string &out) {
    if (lookup(cap, lang, out))
        return true;
    bool cjk = lang != kKo;
    std::string result;
    size_t i = 0;
    bool any = false;
    while (i < cap.size()) {
        while (i < cap.size() && cap[i] == ' ')
            ++i;
        size_t j = i;
        while (j < cap.size() && cap[j] != ' ')
            ++j;
        if (j == i)
            break;
        std::string word = cap.substr(i, j - i);
        i = j;
        std::string tail;
        if (word.size() > 1 && word.back() == ',') {
            word.pop_back();
            tail = cjk ? "\xe3\x80\x81" /* 、 */ : ",";
        }
        if (word.size() > 2 && ends_with(word, "'s")) {
            word.resize(word.size() - 2);
            static const char *const poss[] = {"\xe7\x9a\x84", "\xe7\x9a\x84", /* 的 */
                                               "\xec\x9d\x98" /* 의 */,
                                               "\xe3\x81\xae" /* の */};
            tail = poss[lang] + tail;
        }
        std::string w;
        if (!lookup(word, lang, w))
            return false;
        if (any && !cjk)
            result += ' ';
        result += w + tail;
        any = true;
    }
    if (!any)
        return false;
    out = result;
    return true;
}

/* Non-greedy backtracking match of segs[k..] against s from pos. */
bool match_from(const std::vector<Segment> &segs, size_t k, const std::string &s, size_t pos,
                std::vector<std::string> &caps) {
    if (k == segs.size())
        return pos == s.size();
    const Segment &seg = segs[k];
    if (!seg.capture) {
        if (s.compare(pos, seg.text.size(), seg.text) != 0)
            return false;
        return match_from(segs, k + 1, s, pos + seg.text.size(), caps);
    }
    if (pos >= s.size())
        return false;
    if (k + 1 == segs.size()) {
        caps[seg.num] = s.substr(pos);
        return true;
    }
    /* the next segment is literal text (the generator rejects adjacent
     * captures); try each place it occurs, shortest capture first */
    const std::string &next = segs[k + 1].text;
    for (size_t at = s.find(next, pos + 1); at != std::string::npos; at = s.find(next, at + 1)) {
        caps[seg.num] = s.substr(pos, at - pos);
        if (match_from(segs, k + 1, s, at, caps))
            return true;
    }
    return false;
}

bool match_patterns(const std::string &s, int lang, std::string &out) {
    for (const Pattern &p : tables().patterns) {
        std::vector<std::string> caps(16);
        bool ok = true;
        for (const Segment &seg : p.segs)
            ok = ok && seg.num < 16;
        if (!ok || !match_from(p.segs, 0, s, 0, caps))
            continue;
        std::vector<bool> trans(16, false);
        for (const Segment &seg : p.segs)
            if (seg.capture && seg.translate)
                trans[seg.num] = true;
        std::string result;
        for (const Segment &seg : parse_template(row_text(p.row, lang))) {
            if (!seg.capture) {
                result += seg.text;
            } else if (seg.num < 16) {
                std::string v = caps[seg.num];
                std::string t;
                if (trans[seg.num] && translate_capture(v, lang, t))
                    v = t;
                result += v;
            }
        }
        out = result;
        return true;
    }
    return false;
}

/* Split a trailing " [abc]" and/or " (y)" choice suffix off s. */
void split_suffix(const std::string &s, std::string &body, std::string &suffix) {
    body = s;
    suffix.clear();
    for (int round = 0; round < 2; ++round) {
        size_t end = body.size();
        while (end > 0 && is_space(body[end - 1]))
            --end;
        if (end == 0)
            break;
        char close = body[end - 1];
        char open = close == ']' ? '[' : close == ')' ? '(' : 0;
        if (!open)
            break;
        size_t start = body.rfind(open, end - 1);
        if (start == std::string::npos || start == 0 || body[start - 1] != ' ')
            break;
        /* "(y)" defaults are short; longer parentheses are part of the text */
        if (open == '(' && end - start > 5)
            break;
        size_t cut = start - 1;
        while (cut > 0 && is_space(body[cut - 1]))
            --cut;
        suffix = body.substr(cut) + suffix;
        body.resize(cut);
    }
}

/* One line; false when nothing was found. */
bool translate_line(const std::string &line, int lang, std::string &out) {
    size_t a = 0, b = line.size();
    while (a < b && is_space(line[a]))
        ++a;
    while (b > a && is_space(line[b - 1]))
        --b;
    if (a == b)
        return false;
    std::string core = line.substr(a, b - a);
    std::string body, suffix;
    split_suffix(core, body, suffix);
    if (body.empty()) {
        body = core;
        suffix.clear();
    }
    std::string t;
    if (!lookup(body, lang, t) && !match_patterns(normalise(body), lang, t))
        return false;
    out = line.substr(0, a) + t + suffix + line.substr(b);
    return true;
}

std::string translate(const std::string &s, int lang) {
    if (s.find('\n') == std::string::npos) {
        std::string out;
        return translate_line(s, lang, out) ? out : s;
    }
    std::vector<std::string> lines;
    size_t start = 0;
    for (;;) {
        size_t nl = s.find('\n', start);
        lines.push_back(s.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    std::vector<std::string> out;
    size_t i = 0;
    while (i < lines.size()) {
        if (normalise(lines[i]).empty()) {
            out.push_back(lines[i++]);
            continue;
        }
        size_t j = i;
        std::string para;
        while (j < lines.size() && !normalise(lines[j]).empty())
            para += lines[j++] + " ";
        std::string t;
        if (j - i > 1 && translate_line(normalise(para), lang, t)) {
            /* keep the first line's indentation */
            size_t k = 0;
            while (k < lines[i].size() && is_space(lines[i][k]))
                ++k;
            out.push_back(lines[i].substr(0, k) + t);
        } else {
            for (size_t k = i; k < j; ++k)
                out.push_back(translate_line(lines[k], lang, t) ? t : lines[k]);
        }
        i = j;
    }
    std::string joined;
    for (size_t k = 0; k < out.size(); ++k) {
        if (k)
            joined += '\n';
        joined += out[k];
    }
    return joined;
}

} // namespace

extern "C" {

void nhb_set_core_lang(const char *lang) {
    int idx = -1;
    for (int i = 0; lang && i < kLangCount; ++i)
        if (std::strcmp(lang, kLangs[i]) == 0)
            idx = i;
    g_lang = idx;
}

const char *nhb_core_text(const char *s) {
    int lang = g_lang;
    if (!s || !*s || lang < 0)
        return s;
    std::string out = translate(s, lang);
    if (out == s)
        return s;
    /* callers copy or use the result before translating much more text; a
     * per-thread ring keeps several results alive at once */
    static thread_local std::string ring[16];
    static thread_local int next = 0;
    std::string &slot = ring[next];
    next = (next + 1) % 16;
    slot = out;
    return slot.c_str();
}

} // extern "C"
