// Default Fiddle Script
// This script is called by the Fiddle Server for every musical event.

void processNote(Note@ note) {
    print("Note Started: " + note.get_note_number() + " (velocity: " + note.get_start_velocity() + ")");
    
    // Example: Read notation dimensions
    float legato = note.get_dimension("Legato");
    string legatoTech = note.get_technique("Legato");
    float vibrato = note.get_dimension("Vibrato");
    string vibratoTech = note.get_technique("Vibrato");

    if (legato > 0) {
        print("  -> Legato active: " + legatoTech + " (" + legato + ")");
    }
    if (vibrato > 0) {
        print("  -> Vibrato: " + vibratoTech + " (" + vibrato + ")");
    }
}

void processSubnote(Subnote@ subnote) {
    if (subnote.get_is_first()) {
        print("Subnote Sequence Start: " + subnote.get_note_number());
    }
    // Periodic processing logic goes here
    // Access subnote properties registered in ScriptBindings
    uint id = subnote.get_id();
    uint pitch = subnote.get_note_number();
    float velocity = subnote.get_velocity();
    
    // Logic can be added here to transform the stream
}
