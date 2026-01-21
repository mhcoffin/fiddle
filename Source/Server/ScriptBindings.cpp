#include "ScriptBindings.h"
#include "midi_event.pb.h"
#include <scriptstdstring.h>

namespace fiddle {

// Bridge functions for Protobuf access
static uint32_t Note_GetID(const fiddle::Note *n) { return n->id(); }
static uint32_t Note_GetNoteNumber(const fiddle::Note *n) {
  return n->note_number();
}
static uint32_t Note_GetChannel(const fiddle::Note *n) { return n->channel(); }
static uint32_t Note_GetStartVelocity(const fiddle::Note *n) {
  return n->start_velocity();
}
static uint32_t Note_GetEndVelocity(const fiddle::Note *n) {
  return n->end_velocity();
}

static float Note_GetDimension(const std::string &name, const fiddle::Note *n) {
  auto it = n->notation_dimensions().find(name);
  if (it != n->notation_dimensions().end()) {
    return it->second;
  }
  return 0.0f;
}

static std::string Note_GetTechnique(const std::string &name,
                                     const fiddle::Note *n) {
  auto it = n->notation_techniques().find(name);
  if (it != n->notation_techniques().end()) {
    return it->second;
  }
  return "";
}

static uint32_t Subnote_GetID(const fiddle::Subnote *s) { return s->id(); }
static uint32_t Subnote_GetNoteNumber(const fiddle::Subnote *s) {
  return s->note_number();
}
static uint32_t Subnote_GetChannel(const fiddle::Subnote *s) {
  return s->channel();
}
static uint32_t Subnote_GetVelocity(const fiddle::Subnote *s) {
  return s->velocity();
}
static bool Subnote_GetIsFirst(const fiddle::Subnote *s) {
  return s->is_first();
}
static bool Subnote_GetIsLast(const fiddle::Subnote *s) { return s->is_last(); }

void ScriptBindings::RegisterFiddleAPI(asIScriptEngine *engine) {
  // Register string type
  RegisterStdString(engine);

  // Register Note type
  engine->RegisterObjectType("Note", 0, asOBJ_REF | asOBJ_NOCOUNT);
  engine->RegisterObjectMethod("Note", "uint get_id() const",
                               asFUNCTION(Note_GetID), asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod("Note", "uint get_note_number() const",
                               asFUNCTION(Note_GetNoteNumber),
                               asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod("Note", "uint get_channel() const",
                               asFUNCTION(Note_GetChannel),
                               asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod("Note", "uint get_start_velocity() const",
                               asFUNCTION(Note_GetStartVelocity),
                               asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod("Note", "uint get_end_velocity() const",
                               asFUNCTION(Note_GetEndVelocity),
                               asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod(
      "Note", "float get_dimension(const string &in) const",
      asFUNCTION(Note_GetDimension), asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod(
      "Note", "string get_technique(const string &in) const",
      asFUNCTION(Note_GetTechnique), asCALL_CDECL_OBJLAST);

  // Register Subnote type
  engine->RegisterObjectType("Subnote", 0, asOBJ_REF | asOBJ_NOCOUNT);
  engine->RegisterObjectMethod("Subnote", "uint get_id() const",
                               asFUNCTION(Subnote_GetID), asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod("Subnote", "uint get_note_number() const",
                               asFUNCTION(Subnote_GetNoteNumber),
                               asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod("Subnote", "uint get_channel() const",
                               asFUNCTION(Subnote_GetChannel),
                               asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod("Subnote", "uint get_velocity() const",
                               asFUNCTION(Subnote_GetVelocity),
                               asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod("Subnote", "bool get_is_first() const",
                               asFUNCTION(Subnote_GetIsFirst),
                               asCALL_CDECL_OBJLAST);
  engine->RegisterObjectMethod("Subnote", "bool get_is_last() const",
                               asFUNCTION(Subnote_GetIsLast),
                               asCALL_CDECL_OBJLAST);

  // Register Global Print function
  engine->RegisterGlobalFunction("void print(const string &in)",
                                 asFUNCTION(ScriptBindings::Print),
                                 asCALL_CDECL);
}

static std::function<void(const std::string &)> g_printCallback;

void ScriptBindings::Print(const std::string &msg) {
  if (g_printCallback) {
    g_printCallback(msg);
  }
}

void SetPrintCallback(std::function<void(const std::string &)> cb) {
  g_printCallback = cb;
}

} // namespace fiddle
