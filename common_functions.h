#ifndef _COMMON_FUNCTIONS_H
#define _COMMON_FUNCTIONS_H

/**class IMidiMessageHandler() {
  protected
}*/

inline bool isCCMessage(unsigned char x) {
    // control message on any channel
    return ((x & 0xF0) == 0xB0);
}

inline bool isSysExStart(unsigned char x) {
    return ((x & 0xFF) == 0xF0);
}

inline bool isNoteOnMessage(unsigned char x) {
    return ((x & 0xF0) == 0x90);
}

/** Compare contents of arrays. Arrays must have same length */
bool arrayEqual(unsigned char *array1, unsigned char *array2, unsigned int length) {
    unsigned char *currentElement1 = array1;
    unsigned char *currentElement2 = array2;
    for (int i = 0; i < length; i++) {
        if (*currentElement1 != *currentElement2) {
            return false;
        }
        if (i < length) {
            currentElement1++;
            currentElement2++;
        }
        //currentElement1++;
        //currentElement2++;
    }
    return true;
}

void sendMidiMessage(midi_Output *m_midiout, int byte_count, unsigned char *data) {
    MIDI_event_t evt;
    evt.frame_offset = 0;
    evt.size = 0;
    unsigned char *currentByte = data;
    for (int i = 0; i < byte_count; i++) {
        evt.midi_message[evt.size++] = *currentByte;
        if (i < byte_count) currentByte++;
    }
    m_midiout->SendMsg(&evt, -1);
}

void sendDeviceInquiry(midi_Output *m_midiout) {
    unsigned char data [] = { 0xF0, 0x7E, 0x00, 0x06, 0x01, 0xF7 };
    sendMidiMessage(m_midiout, sizeof(data)/sizeof(data[0]), data);
}

#endif
