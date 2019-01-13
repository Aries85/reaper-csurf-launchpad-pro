#ifndef _MOXF_FUNCTIONS_H
#define _MOXF_FUNCTIONS_H

#include "common_functions.h"

/**
 * Send SysEx message with text, padded with spaces and increasing index byte
 */
void moxfSendSysexMessageWithText(midi_Output *m_midiout, unsigned char* sysexBeginning, int sysexBeginningSize, unsigned char *text, int textSize, int maxCharacters, int indexOfIndexElement, int deltaOfIndexElement) {
  int sysexSize = sysexBeginningSize + maxCharacters + 1; // message always needs to send full text line, adding 1 byte for SysEx end
  unsigned char displayParameterSysex [sysexSize];

  // copy beginning of SysEx
  for (int i = 0; i < sysexBeginningSize; i++) {
    displayParameterSysex[i] = sysexBeginning[i];
  }
  displayParameterSysex[indexOfIndexElement] += deltaOfIndexElement; // set parameter index

  // copy message text
  unsigned char *current_char = text;
  int i = sysexBeginningSize;
  while ((*current_char != '\0') && (i < (sysexBeginningSize + maxCharacters))) {
      displayParameterSysex[i] = *current_char;
      current_char++;
      i++;
  }

  // fill rest of the line with spaces
  if (i < (sysexBeginningSize + maxCharacters)) {
    for (int j = i; j < (sysexBeginningSize + maxCharacters); j++) {
      displayParameterSysex[j] = 0x20; // ASCII space character
    }
  }
  displayParameterSysex[sysexSize - 1] = 0xF7; // append SysEx end byte

  sendMidiMessage(m_midiout, sysexSize, displayParameterSysex);
}

const unsigned char SYSEX_MOXF_DISPLAY_PARAMETER_NAME [9] = { 0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x00, 0x01, 0x30, 0x09 };

void moxfDisplayParameterName(midi_Output *m_midiout, unsigned char parameterIndex, unsigned char *text, int character_count) {
  moxfSendSysexMessageWithText(
    m_midiout,
    (unsigned char *)SYSEX_MOXF_DISPLAY_PARAMETER_NAME,
    sizeof(SYSEX_MOXF_DISPLAY_PARAMETER_NAME)/sizeof(unsigned char),
    text,
    character_count,
    24,
    7,
    parameterIndex
  );
}

/**
 * 0x30 - parameter value index
 * 0x09 - display parameter value
 */
const unsigned char SYSEX_MOXF_DISPLAY_PARAMETER_VALUE [9] = { 0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x00, 0x01, 0x30, 0x21 };

void moxfDisplayParameterValue(midi_Output *m_midiout, unsigned char parameterIndex, unsigned char *text, int character_count) {
  moxfSendSysexMessageWithText(
    m_midiout,
    (unsigned char *)SYSEX_MOXF_DISPLAY_PARAMETER_VALUE,
    sizeof(SYSEX_MOXF_DISPLAY_PARAMETER_VALUE)/sizeof(unsigned char),
    text,
    character_count,
    10,
    7,
    parameterIndex
  );
}

const unsigned char SYSEX_MOXF_DISPLAY_BUTTON_TEXT [9] = { 0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x00, 0x01, 0x60, 0x00 };

void moxfDisplayButtonText(midi_Output *m_midiout, unsigned char buttonIndex, unsigned char *text, int character_count) {
  moxfSendSysexMessageWithText(
    m_midiout,
    (unsigned char *)SYSEX_MOXF_DISPLAY_BUTTON_TEXT,
    sizeof(SYSEX_MOXF_DISPLAY_BUTTON_TEXT)/sizeof(unsigned char),
    text,
    character_count,
    6,
    8,
    buttonIndex * 6
  );
}

const unsigned char SYSEX_MOXF_DISPLAY_BUTTON_TEXT = { 0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x00, 01 61 00 48 5A 6F 6F 6D 2B F7

#endif
