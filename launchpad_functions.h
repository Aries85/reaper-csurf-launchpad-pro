#ifndef _LAUNCHPAD_FUNCTIONS_H
#define _LAUNCHPAD_FUNCTIONS_H

#define LAUNCHPAD_COLUMNS 8



enum surface_live_layout : unsigned char {
    LL_SESSION = 0x00,
    LL_DRUM_RACK = 0x01,
    LL_CHROMATIC_NOTE = 0x02,
    LL_USER = 0x03,
    LL_AUDIO = 0x04,
    LL_FADER = 0x05,
    LL_RECORD_ARM = 0x06,
    LL_TRACK_SELECT = 0x07,
    LL_MUTE = 0x08,
    LL_SOLO = 0x09,
    LL_VOLUME = 0x0A,
    LL_PAN = 0x0B,
    LL_SENDS = 0x0C,
    LL_STOP_CLIP = 0x0D,
    // this is custom one, it is not recognized or sent by Launchpad Pro
            LL_SENDS_PAN = 0x0E
};

enum fader_type : unsigned char {
    FT_VOLUME = 0,
    FT_PAN = 1
};



void launchpad_send_fader_sysex(midi_Output *m_midiout, fader_type ftype, unsigned char index, unsigned char value, unsigned char color) {
    struct {
        MIDI_event_t evt;
        char data[512];
    }
            live_mode;
    live_mode.evt.frame_offset = 0;
    live_mode.evt.size = 0;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0xF0;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x00;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x20;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x29;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x02;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x10;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x2B;
    live_mode.evt.midi_message[live_mode.evt.size++] = index;
    live_mode.evt.midi_message[live_mode.evt.size++] = ftype;
    live_mode.evt.midi_message[live_mode.evt.size++] = color;
    live_mode.evt.midi_message[live_mode.evt.size++] = value;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0xF7;

    m_midiout->SendMsg(&live_mode.evt, -1);
}

/** Fader index is zero-based */
void launchpad_set_fader_row_sysex(midi_Output *m_midiout, fader_type ftype, unsigned char color, unsigned char start_fader_index, unsigned char num_params, unsigned char *levels) {
	struct {
		MIDI_event_t evt;
		char data[512];
	}
	row_sysex;

	row_sysex.evt.frame_offset = 0;
	row_sysex.evt.size = 0;
	row_sysex.evt.midi_message[row_sysex.evt.size++] = 0xF0;
	row_sysex.evt.midi_message[row_sysex.evt.size++] = 0x00;
	row_sysex.evt.midi_message[row_sysex.evt.size++] = 0x20;
	row_sysex.evt.midi_message[row_sysex.evt.size++] = 0x29;
	row_sysex.evt.midi_message[row_sysex.evt.size++] = 0x02;
	row_sysex.evt.midi_message[row_sysex.evt.size++] = 0x10;
	row_sysex.evt.midi_message[row_sysex.evt.size++] = 0x2B;
    for (int i = 0; i < num_params; i++) {
		row_sysex.evt.midi_message[row_sysex.evt.size++] = start_fader_index + i;
		row_sysex.evt.midi_message[row_sysex.evt.size++] = ftype;
		row_sysex.evt.midi_message[row_sysex.evt.size++] = color;
		row_sysex.evt.midi_message[row_sysex.evt.size++] = *(levels + (sizeof(unsigned char)*i));
    }
	row_sysex.evt.midi_message[row_sysex.evt.size++] = 0xF7;

    m_midiout->SendMsg(&row_sysex.evt, -1);
}

/**
 * Light buttons using SysEx message. It lights button row by sysex.
 * - row - if true, it will take colors as they were for buttons in row starting from row_note_start, left to right
 *       - if false, it will take colors as they were for buttons in column starting from row_note_start, top to bottom
 * There is parameter for starting note.
 * Button numbering is as in Programmer mode.
 */
void launchpad_light_button_row_or_column_sysex(midi_Output *m_midiout, bool row, unsigned char row_note_start,
                                                unsigned char num_colors,
                                                unsigned char *colors) {
    unsigned char current_note = row_note_start;
    unsigned char *current_color = colors;

    struct {
        MIDI_event_t evt;
        char data[512];
    }
            live_mode;
    live_mode.evt.frame_offset = 0;
    live_mode.evt.size = 0;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0xF0;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x00;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x20;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x29;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x02;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x10;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x0A; // light LEDs
    if (row) {
        for (int i = 0; i < num_colors; i++) {
            live_mode.evt.midi_message[live_mode.evt.size++] = current_note;
            live_mode.evt.midi_message[live_mode.evt.size++] = *current_color;
            if (i < num_colors) current_color++;
            //current_color++;
            current_note++;
        }
    } else {
        // Launchpad numbers notes bottom to top, we are iterating top to bottom
        current_note += (LAUNCHPAD_COLUMNS - 1) * 10;
        for (int i = num_colors; i > 0; i--) {
            live_mode.evt.midi_message[live_mode.evt.size++] = current_note;
            live_mode.evt.midi_message[live_mode.evt.size++] = *current_color;
            if (i > 0) current_color++;
            //current_color++;
            current_note -= 10;
        }
    }
    live_mode.evt.midi_message[live_mode.evt.size++] = 0xF7;

    m_midiout->SendMsg(&live_mode.evt, -1);
}

void launchpadSwitchLiveMode(midi_Output *m_midiout, bool enable) {
    struct {
        MIDI_event_t evt;
        char data[512];
    }
            live_mode;
    live_mode.evt.frame_offset = 0;
    live_mode.evt.size = 0;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0xF0;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x00;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x20;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x29;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x02;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x10;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x21;
    live_mode.evt.midi_message[live_mode.evt.size++] = !enable;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0xF7;

    m_midiout->SendMsg(&live_mode.evt, -1);
}


void launchpadSelectLiveLayout(midi_Output *m_midiout, surface_live_layout layout) {
    struct {
        MIDI_event_t evt;
        char data[512];
    }
            layout_msg;
    layout_msg.evt.frame_offset = 0;
    layout_msg.evt.size = 0;
    layout_msg.evt.midi_message[layout_msg.evt.size++] = 0xF0;
    layout_msg.evt.midi_message[layout_msg.evt.size++] = 0x00;
    layout_msg.evt.midi_message[layout_msg.evt.size++] = 0x20;
    layout_msg.evt.midi_message[layout_msg.evt.size++] = 0x29;
    layout_msg.evt.midi_message[layout_msg.evt.size++] = 0x02;
    layout_msg.evt.midi_message[layout_msg.evt.size++] = 0x10;
    layout_msg.evt.midi_message[layout_msg.evt.size++] = 0x22; // select Live layout_msg
    layout_msg.evt.midi_message[layout_msg.evt.size++] = layout;
    layout_msg.evt.midi_message[layout_msg.evt.size++] = 0xF7;

    m_midiout->SendMsg(&layout_msg.evt, -1);
}

/** Scroll text on Reaper display using given midi output and using null-terminated string */
void launchpadScrollText(midi_Output *m_midiout, char *text) {
    struct {
        MIDI_event_t evt;
        char data[512];
    }
            live_mode;
    live_mode.evt.frame_offset = 0;
    live_mode.evt.size = 0;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0xF0;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x00;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x20;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x29;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x02;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x10;
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x14; // scroll text
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x16; // light green
    live_mode.evt.midi_message[live_mode.evt.size++] = 0x00; // no loop
    char *current_char = text;
    while (*current_char != '\0') {
        live_mode.evt.midi_message[live_mode.evt.size++] = *current_char;
        current_char++;
    }
    live_mode.evt.midi_message[live_mode.evt.size++] = 0xF7;

    m_midiout->SendMsg(&live_mode.evt, -1);
}

char faderCCs[8] = {0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C};

void launchpad_send_cc_message(midi_Output *m_midiout, char cc_number, char cc_value) {
    struct {
        MIDI_event_t evt;
        char data[512];
    }
            cc_message;
    cc_message.evt.frame_offset = 0;
    cc_message.evt.size = 0;
    cc_message.evt.midi_message[cc_message.evt.size++] = 0xB0; // CC Message on channel 1
    cc_message.evt.midi_message[cc_message.evt.size++] = cc_number;
    cc_message.evt.midi_message[cc_message.evt.size++] = cc_value;

    m_midiout->SendMsg(&cc_message.evt, -1);
}

void launchpad_set_fader_value(midi_Output *m_midiout, char index, char value) {
    launchpad_send_cc_message(m_midiout, faderCCs[index], value);
}

#endif
