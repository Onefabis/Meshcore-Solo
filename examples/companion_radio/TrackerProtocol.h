#pragma once
#include <cstring>
#include <cstddef>

namespace tracker {
static constexpr const char* HELP =
  "@[Handle] or all\n!kur\n!tikra [text]\n!power up\n!power down\n!power\n!interval <sec>\n!triangulate\n!help";
enum class Action { Ignore, Conversation, Locate, Alarm, PowerUp, PowerDown, PowerState, Interval, Triangulate, Help, Unknown };
struct Request { Action action; const char* text; };
inline const char* skipSpaces(const char* p) { while (*p == ' ' || *p == '\t') ++p; return p; }
inline Request parse(const char* body, const char* name) {
  body = skipSpaces(body);
  bool targeted = false;
  if (!strncmp(body, "@[", 2)) {
    const char* end = strchr(body + 2, ']');
    if (!end || (end[1] && end[1] != ' ' && end[1] != '\t')) return {Action::Ignore, body};
    if (strlen(name) != size_t(end - body - 2) || strncmp(body + 2, name, end - body - 2)) return {Action::Ignore, body};
    targeted = true;
    body = skipSpaces(end + 1);
  }
  if (*body != '!') return {targeted && *body ? Action::Conversation : Action::Ignore, body};
  const char* args = body;
  while (*args && *args != ' ' && *args != '\t') ++args;
  size_t len = args - body;
  args = skipSpaces(args);
  if (len == 4 && !strncmp(body, "!kur", len) && !*args) return {Action::Locate, args};
  if (len == 6 && !strncmp(body, "!tikra", len)) return {Action::Alarm, args};
  if (len == 5 && !strncmp(body, "!help", len) && !*args) return {Action::Help, args};
  if (((len == 12 && !strncmp(body, "!triangulate", len)) ||
       (len == 12 && !strncmp(body, "!traingulate", len))) && !*args) return {Action::Triangulate, args};
  if (len == 9 && !strncmp(body, "!interval", len)) return {Action::Interval, args};
  if (len == 6 && !strncmp(body, "!power", len)) {
    if (!*args) return {Action::PowerState, args};
    if (!strcmp(args, "up")) return {Action::PowerUp, args};
    if (!strcmp(args, "down")) return {Action::PowerDown, args};
  }
  return {Action::Unknown, body}; // Commands, including unknown commands, never chirp.
}
}
