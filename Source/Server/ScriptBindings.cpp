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
  return (uint32_t)(n->start_velocity() * 127.0f);
}
static uint32_t Note_GetEndVelocity(const fiddle::Note *n) {
  return (uint32_t)(n->end_velocity() * 127.0f);
}

static uint32_t Subnote_GetID(const fiddle::Subnote *s) { return s->id(); }
static uint32_t Subnote_GetNoteNumber(const fiddle::Subnote *s) {
  return s->note_number();
}
static uint32_t Subnote_GetChannel(const fiddle::Subnote *s) {
  return s->channel();
}
static float Subnote_GetVelocity(const fiddle::Subnote *s) {
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
  engine->RegisterObjectMethod("Subnote", "float get_velocity() const",
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
