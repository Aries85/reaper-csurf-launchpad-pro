/*
** csurf_launchpadpro
** Launchpad Pro support
** Copyright (C) 2018
** License: LGPL.
*/
#define REAPERAPI_IMPLEMENT

#include "csurf.h"
#include "reaper_plugin_functions.h"
#include "reaper_plugin.h"

#ifndef _WIN32

#include "resource.h"
#include "../WDL/swell/swell-dlggen.h"
#include "res.rc_mac_dlg"
#include "../WDL/swell/swell-menugen.h"
#include "res.rc_mac_menu"

#endif

#include "common_functions.h"
#include "launchpad_version.h"
#include "launchpad_functions.h"
#include "utility_functions.h"

#define NUM_PLUGIN_PARAMS 6
// delay from last update in sirface mode that uses polling to update launchpad in ms
#define POLLING_SURFACE_NON_INTERRUPT_DELAY 2000

REAPER_PLUGIN_HINSTANCE g_hInst; // used for dialogs, if any
HWND g_hwnd;

/*
** Todo: automation status, automation mode setting using "auto" button, more
*/

static bool g_csurf_mcpmode = false; // we may wish to allow an action to set this

char firmwareVersion [50] = "\n";
bool device_connected = false;

enum launchpad_button : unsigned char {
    NO_BUTTON = 0x00,
    BUTTON_SHIFT = 0x50,
    BUTTON_CLICK = 0x46,
    BUTTON_UNDO = 0x3C,
    BUTTON_RECORD = 0x0A,
    BUTTON_PLAY_1 = 0x59,
    BUTTON_PLAY_2 = 0x4F,
    BUTTON_PLAY_3 = 0x45,
    BUTTON_PLAY_4 = 0x3B,
    BUTTON_PLAY_5 = 0x31,
    BUTTON_PLAY_6 = 0x27,
    BUTTON_PLAY_7 = 0x1D,
    BUTTON_PLAY_8 = 0x13,
    BUTTON_RECORD_ARM = 0x01,
    BUTTON_TRACK_SELECT = 0x02,
    BUTTON_MUTE = 0x03,
    BUTTON_SOLO = 0x04,
    BUTTON_VOLUME = 0x05,
    BUTTON_PAN = 0x06,
    BUTTON_SENDS = 0x07,
    BUTTON_STOP_CLIP = 0x08,
    BUTTON_ARROW_UP = 0x5B,
    BUTTON_ARROW_DOWN = 0x5C,
    BUTTON_ARROW_LEFT = 0x5D,
    BUTTON_ARROW_RIGHT = 0x5E,
    BUTTON_SESSION = 0x5F,
    BUTTON_NOTE = 0x60,
    BUTTON_DEVICE = 0x61,
    BUTTON_USER = 0x62
};

class CSurf_LaunchpadPro : public IReaperControlSurface {
    unsigned char SYSEX_RECEIVED_LIVE_LAYOUT_STATUS[7] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x10, 0x2E};
    unsigned char SYSEX_DEVICE_INQUIRY_BEGINNING[2] = {0xF0, 0x7E};

    const DWORD THRESHOLD_MOMENTARY = 300; // 250 MS to differentiate between toggle and momentary

    const int ACTION_TOGGLE_REPEAT = 1068;
    const int ACTION_TOGGLE_METRONOME = 40364;
    const int ACTION_UNDO = 40029;
    const int ACTION_REDO = 40030;
    const int ACTION_TRANSPORT_TOGGLE_PLAY_STOP = 40044;
    const int ACTION_START_STOP_RECORDING_AT_EDIT_CURSOR = 40046;
    const int ACTION_VIEW_RECORDING_SETTINGS_FOR_LAST_TOUCHED_TRACK = 40604;

    const unsigned char FADER_0 = 0x15;

    // Variable that drives almost everything, contains currently selected live mode
    surface_live_layout current_surface_live_layout = LL_SESSION;
    DWORD last_time_threshold_button_pressed = 0;
    // this is to mitigate problem when polling for update of device faders and their movement is disrupted by it
    DWORD last_time_change_from_device_mode_received = 0;
    DWORD last_time_change_from_sends_mode_received = 0;
    DWORD last_time_change_from_sends_pan_mode_received = 0;

    bool config_do_not_switch_to_live_mode_on_startup = false;
    bool includeMasterTrack = false;

    DWORD last_surface_update_time = timeGetTime();
    int lastActiveSend = 0;
    int config_surface_update_frequency = 300;
    bool state_loop = false;
    bool state_playing = false;
    bool state_recording = false;
    bool state_shift_pressed = false;

    int track_offset = 0;
    int device_mode_offset = 0;

    int m_midi_in_dev, m_midi_out_dev;
    midi_Output *m_midiout;
    midi_Input *m_midiin;


    char m_fader_touchstate;

    DWORD m_frameupd_lastrun;
    DWORD m_frameupd_lastrun2;
    int m_arrowstates, m_button_states;
    DWORD m_buttonstate_lastrun;
    DWORD m_pan_lasttouch;
    int m_tranz_anysolo_poop;
    char m_tranz_oldbuf[128];

    WDL_String descspace;
    char configtmp[1024];

    /**
     * Lauchpad Pro sends confirmation events on mode switch.
     * When receiving those events, disply will be updated.
     */
    void OnMIDIEvent(MIDI_event_t *evt) {
        if (isSysExStart(evt->midi_message[0])) {
            unsigned char sysExData[512];
            for (int i = 0; i < evt->size; i++) {
                sysExData[i] = evt->midi_message[i];
            }
            if (arrayEqual(SYSEX_DEVICE_INQUIRY_BEGINNING, sysExData, sizeof(SYSEX_DEVICE_INQUIRY_BEGINNING)/sizeof(SYSEX_DEVICE_INQUIRY_BEGINNING[0]))) {
                sprintf(firmwareVersion, "%d%d%d%d", sysExData[12] ,sysExData[13], sysExData[14], sysExData[15]);
                device_connected = true;
            } else if (arrayEqual(SYSEX_RECEIVED_LIVE_LAYOUT_STATUS, sysExData, sizeof(SYSEX_RECEIVED_LIVE_LAYOUT_STATUS)/sizeof(SYSEX_RECEIVED_LIVE_LAYOUT_STATUS[0]))) {
                surface_live_layout reported_switched_layout = static_cast<surface_live_layout>(sysExData[7]);
                switch (reported_switched_layout) {
                    case LL_SESSION:
                    case LL_FADER:
                    case LL_DRUM_RACK:
                    case LL_CHROMATIC_NOTE:
                    case LL_USER:
                    case LL_AUDIO:
                    case LL_RECORD_ARM:
                    case LL_TRACK_SELECT:
                    case LL_MUTE:
                    case LL_SOLO:
                    case LL_VOLUME:
                    case LL_PAN:
                    case LL_STOP_CLIP: {
                        current_surface_live_layout = reported_switched_layout;
                        break;
                    }
                    case LL_SENDS: {
                        if (!state_shift_pressed) {
                            current_surface_live_layout = reported_switched_layout;
                        } else {
                            current_surface_live_layout = LL_SENDS_PAN;
                        }
                    }
                    default:
                        break;
                }
                UpdateSurface();
            }
            // end of sysex
        } else if (isCCMessage(evt->midi_message[0])) {
            switch (current_surface_live_layout) {
                case LL_VOLUME: {
                    if ((evt->midi_message[1] >= FADER_0) && (evt->midi_message[1] < (FADER_0 + 8))) {
                        int numTracks = CountTracks(0);
                        int trackIndex = track_offset + (evt->midi_message[1] - FADER_0);
                        if (trackIndex < (numTracks + 1)) {
                            MediaTrack *mediaTrack = CSurf_TrackFromID(trackIndex + 1, g_csurf_mcpmode);
                            if (mediaTrack) CSurf_SetSurfaceVolume(mediaTrack, CSurf_OnVolumeChange(mediaTrack, charToVol(evt->midi_message[2]), false), this);
                            return;
                        }
                    }
                }
                    break;
                case LL_PAN: {
                    if ((evt->midi_message[1] >= FADER_0) && (evt->midi_message[1] < (FADER_0 + 8))) {
                        int numTracks = CountTracks(0);
                        int trackIndex = track_offset + (evt->midi_message[1] - FADER_0);
                        if (trackIndex < numTracks + 1) {
                            MediaTrack *mediaTrack = CSurf_TrackFromID(trackIndex + 1, g_csurf_mcpmode);
                            if (mediaTrack) CSurf_SetSurfacePan(mediaTrack, CSurf_OnPanChange(mediaTrack, charToPan(evt->midi_message[2]), false), this);
                            return;
                        }
                    }
                }
                    break;
                case LL_FADER: {
                    if ((evt->midi_message[1] >= FADER_0) && (evt->midi_message[1] < (FADER_0 + 8))) {
                        last_time_change_from_device_mode_received = timeGetTime();
                        MediaTrack *mediaTrack = GetSelectedTrack(0, 0);
                        if (mediaTrack) {
                            int fxParamCount = CountTCPFXParms(0, mediaTrack);
                            int paramIndex = evt->midi_message[1] - FADER_0 + device_mode_offset;
                            if (paramIndex < fxParamCount) {
                                int fxIndex;
                                int pluginParamIndex;
                                double minimumParamValue;
                                double maximumParamValue;
                                GetTCPFXParm(0, mediaTrack, paramIndex, &fxIndex, &pluginParamIndex);
                                TrackFX_GetParam(mediaTrack, fxIndex, pluginParamIndex, &minimumParamValue, &maximumParamValue);
                                double width = maximumParamValue - minimumParamValue; // TODO - hopefully should work both for positive and negative minimums
                                TrackFX_SetParam(mediaTrack, fxIndex, pluginParamIndex, minimumParamValue + ((evt->midi_message[2] * width / 127)));
                            }
                            return; // if this return is missing, then update is interrupted by fader status update performed by UpdateSurface() below
                        }
                    }
                }
                    break;
                case LL_SENDS: {
                    switch (evt->midi_message[1]) {
                        case BUTTON_PLAY_1:
                        case BUTTON_PLAY_2:
                        case BUTTON_PLAY_3:
                        case BUTTON_PLAY_4:
                        case BUTTON_PLAY_5:
                        case BUTTON_PLAY_6:
                        case BUTTON_PLAY_7:
                        case BUTTON_PLAY_8: {
                            unsigned char sendNumber = LAUNCHPAD_COLUMNS - ((evt->midi_message[1] - BUTTON_PLAY_8) / 10) - 1; // buttons in column have index different by 10 and are indexed bottom to top
                            lastActiveSend = sendNumber;
                            break;
                        }
                        default: {
                            if ((evt->midi_message[1] >= FADER_0) && (evt->midi_message[1] < (FADER_0 + 8))) {
                                last_time_change_from_sends_mode_received = timeGetTime();
                                int numTracks = CountTracks(0);
                                int trackIndex = track_offset + (evt->midi_message[1] - FADER_0);
                                if (trackIndex < numTracks + 1) {
                                    MediaTrack *mediaTrack = CSurf_TrackFromID(trackIndex + 1, g_csurf_mcpmode);
                                    if (mediaTrack) {
                                        CSurf_OnSendVolumeChange(mediaTrack, lastActiveSend, charToVol(evt->midi_message[2]), false);
                                        SetTrackSendInfo_Value(mediaTrack, 0, lastActiveSend, "D_VOL", charToVol(evt->midi_message[2]));
                                    }
                                    return;
                                }
                            }
                            break;
                        }
                    }

                }
                    break;
                case LL_SENDS_PAN: {
                    switch (evt->midi_message[1]) {
                        case BUTTON_PLAY_1:
                        case BUTTON_PLAY_2:
                        case BUTTON_PLAY_3:
                        case BUTTON_PLAY_4:
                        case BUTTON_PLAY_5:
                        case BUTTON_PLAY_6:
                        case BUTTON_PLAY_7:
                        case BUTTON_PLAY_8: {
                            unsigned char sendNumber =
                                    LAUNCHPAD_COLUMNS - ((evt->midi_message[1] - BUTTON_PLAY_8) / 10) -
                                    1; // buttons in column have index different by 10 and are indexed bottom to top
                            lastActiveSend = sendNumber;
                            break;
                        }
                        default: {
                            if ((evt->midi_message[1] >= FADER_0) && (evt->midi_message[1] < (FADER_0 + 8))) {
                                last_time_change_from_sends_pan_mode_received = timeGetTime();
                                int numTracks = CountTracks(0);
                                int trackIndex = track_offset + (evt->midi_message[1] - FADER_0);
                                if (trackIndex < numTracks + 1) {
                                    MediaTrack *mediaTrack = CSurf_TrackFromID(trackIndex + 1, g_csurf_mcpmode);
                                    if (mediaTrack) {
                                        CSurf_OnSendPanChange(mediaTrack, lastActiveSend,
                                                              charToPan(evt->midi_message[2]), false);
                                        SetTrackSendInfo_Value(mediaTrack, 0, lastActiveSend, "D_PAN",
                                                               charToPan(evt->midi_message[2]));
                                    }
                                    return;
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
                break;
                default:
                    break;
            }

            switch (evt->midi_message[1]) {
                case BUTTON_SHIFT:
                    state_shift_pressed = (evt->midi_message[2] == 127);
                    break;
                case BUTTON_CLICK:
                    if (evt->midi_message[2] == 127) {
                        // toggle metronome state
                        Main_OnCommand(ACTION_TOGGLE_METRONOME, 0);
                        // TODO - how to find metronome state?
                        break;
                    }
                case BUTTON_UNDO:
                    if (evt->midi_message[2] == 127) {
                        if (!state_shift_pressed) {
                            Main_OnCommand(ACTION_UNDO, 0);
                        } else {
                            Main_OnCommand(ACTION_REDO, 0);
                        }
                    }
                    break;
                case BUTTON_PLAY_7:
                    if ((current_surface_live_layout != LL_SENDS) && (current_surface_live_layout != LL_SENDS_PAN)) {
                        if (evt->midi_message[2] == 127) {
                            Main_OnCommand(ACTION_TOGGLE_REPEAT, 0);
                        }
                    }
                    break;
                case BUTTON_PLAY_8:
                    if ((current_surface_live_layout != LL_SENDS) && (current_surface_live_layout != LL_SENDS_PAN)) {
						            if (evt->midi_message[2] == 127) {
                            Main_OnCommand(ACTION_TRANSPORT_TOGGLE_PLAY_STOP, 0);
                        }
                    }
                    break;
                case BUTTON_RECORD_ARM:
                    handleButtonForLiveModeSwitch(BUTTON_RECORD_ARM, LL_RECORD_ARM, (evt->midi_message[2] == 127));
                    break;
                case BUTTON_TRACK_SELECT:
                    handleButtonForLiveModeSwitch(BUTTON_TRACK_SELECT, LL_TRACK_SELECT, (evt->midi_message[2] == 127));
                    break;
                case BUTTON_MUTE:
                    handleButtonForLiveModeSwitch(BUTTON_MUTE, LL_MUTE, (evt->midi_message[2] == 127));
                    break;
                case BUTTON_SOLO:
                    handleButtonForLiveModeSwitch(BUTTON_SOLO, LL_SOLO, (evt->midi_message[2] == 127));
                    break;
                case BUTTON_VOLUME:
                    handleButtonForLiveModeSwitch(BUTTON_VOLUME, LL_VOLUME, (evt->midi_message[2] == 127));
                    break;
                case BUTTON_PAN:
                    handleButtonForLiveModeSwitch(BUTTON_PAN, LL_PAN, (evt->midi_message[2] == 127));
                    break;
                case BUTTON_SENDS:
                    handleButtonForLiveModeSwitch(BUTTON_SENDS, LL_SENDS, (evt->midi_message[2] == 127));
                    break;
                case BUTTON_RECORD:
                    if (evt->midi_message[2] == 127) {
                        !state_shift_pressed ? Main_OnCommand(ACTION_START_STOP_RECORDING_AT_EDIT_CURSOR, 0) : Main_OnCommand(ACTION_VIEW_RECORDING_SETTINGS_FOR_LAST_TOUCHED_TRACK, 0);
                    }
                    break;
                case BUTTON_ARROW_RIGHT:
                    if (current_surface_live_layout != LL_FADER) {
                        int currentTrackCount = includeMasterTrack ? (CountTracks(0) + 1) : CountTracks(0);
                        if ((evt->midi_message[2] == 127) && (track_offset < (currentTrackCount - LAUNCHPAD_COLUMNS))) {
                            if (!state_shift_pressed) {
                                track_offset++;
                            } else {
                                // "End key" behavior
                                if (currentTrackCount > LAUNCHPAD_COLUMNS) {
                                    track_offset = currentTrackCount - LAUNCHPAD_COLUMNS;
                                }
                            }
                        }
                    } else {
                        MediaTrack *mediaTrack = GetSelectedTrack(0, 0);
                        if (mediaTrack) {
                            if (evt->midi_message[2] == 127) {
                                int fxParamCount = CountTCPFXParms(0, mediaTrack);
                                if (!state_shift_pressed) {
                                    if ((fxParamCount > LAUNCHPAD_COLUMNS) && (device_mode_offset < (fxParamCount - LAUNCHPAD_COLUMNS))) {
                                        device_mode_offset++;
                                    }
                                } else {
                                    // "End key" behavior
                                    if ((fxParamCount > LAUNCHPAD_COLUMNS) && (device_mode_offset < (fxParamCount - LAUNCHPAD_COLUMNS))) {
                                        device_mode_offset = fxParamCount - LAUNCHPAD_COLUMNS;
                                    }
                                }
                            }
                        }
                    }
                    break;
                case BUTTON_ARROW_LEFT:
                    // TODO - fix counting for master track
                    if (current_surface_live_layout != LL_FADER) {

                        if ((evt->midi_message[2] == 127) && (track_offset > 0)) {
                            if (!state_shift_pressed) {
                                track_offset--;
                            } else {
                                // "Home Key" behavior
                                track_offset = 0;
                            }
                        }
                    } else {
                        if ((evt->midi_message[2] == 127) && (device_mode_offset > 0)) {
                            if (!state_shift_pressed) {
                                device_mode_offset--;
                            } else {
                                device_mode_offset = 0;
                            }
                        }
                    }
                    break;
                case BUTTON_SESSION: {
                    if (evt->midi_message[2] == 127) {
                        launchpadSelectLiveLayout(m_midiout, LL_SESSION);
                    }
                }
                    break;
                case BUTTON_DEVICE: {
                    // special behavior first
                    if (evt->midi_message[2] == 127) {
                        if (CountSelectedTracks(0) != 1) {
                            fillGrid(COLOR_PINK_DIM);
                            Sleep(250);
                            fillGrid(COLOR_NONE);
                            Sleep(250);
                            fillGrid(COLOR_PINK_DIM);
                            Sleep(250);
                            fillGrid(COLOR_NONE);
                            return;
                        } else {
                            device_mode_offset = 0;
                        }
                    }
                    // common handling afterwards
                    handleButtonForLiveModeSwitch(BUTTON_DEVICE, LL_FADER, (evt->midi_message[2] == 127));
                }
                default:
                    break;
            }
            UpdateSurface();
        }// CC message
        else if (isNoteOnMessage(evt->midi_message[0])) {
            switch (current_surface_live_layout) {
                case LL_RECORD_ARM: {
                    const unsigned char FIRST_ROW_NOTE = 0x0B;
                    if (evt->midi_message[2] != 0) {
                        if ((evt->midi_message[1] >= FIRST_ROW_NOTE) && (evt->midi_message[1] < (FIRST_ROW_NOTE + 8))) {
                            int trackIndex = track_offset + evt->midi_message[1] - FIRST_ROW_NOTE;
                            int numTracks = includeMasterTrack ? (CountTracks(0) + 1) : CountTracks(0);
                            if (!includeMasterTrack) trackIndex++;
                            if (trackIndex < numTracks + 1) {
                                // skip master track
                                MediaTrack *mediaTrack = CSurf_TrackFromID(trackIndex, g_csurf_mcpmode);
                                double value = GetMediaTrackInfo_Value(mediaTrack, "I_RECARM");
                                bool arm = doubleIsCloseTo(value, 0);
                                if (mediaTrack) CSurf_SetSurfaceRecArm(mediaTrack, CSurf_OnRecArmChange(mediaTrack, arm), this);
                                return;
                            }
                        }
                        UpdateSurfaceIfInRecordArmMode();
                        break;
                    }
                } // LL_RECORD_ARM
                case LL_TRACK_SELECT: {
                    const unsigned char FIRST_ROW_NOTE = 0x0B;
                    if (evt->midi_message[2] != 0) {
                        if ((evt->midi_message[1] >= FIRST_ROW_NOTE) && (evt->midi_message[1] < (FIRST_ROW_NOTE + 8))) {
                            int trackIndex = track_offset + evt->midi_message[1] - FIRST_ROW_NOTE;
                            int numTracks = includeMasterTrack ? (CountTracks(0) + 1) : CountTracks(0);
                            // unselect all tracks
                            int selectedTrackCount = CountSelectedTracks(0);
                            for (int i = 0; i < selectedTrackCount; i++) {
                                MediaTrack *selectedTrack = GetSelectedTrack(0, i);
                                //if (selectedTrack) CSurf_SetSurfaceSelected(selectedTrack,false,this);
                                if (selectedTrack) CSurf_SetSurfaceSelected(selectedTrack, CSurf_OnSelectedChange(selectedTrack, false), this);
                            }
                            if (!includeMasterTrack) trackIndex++;
                            if (trackIndex < numTracks + 1) {
                                // skip master track
                                MediaTrack *mediaTrack = CSurf_TrackFromID(trackIndex, g_csurf_mcpmode);
                                if (mediaTrack) CSurf_SetSurfaceSelected(mediaTrack, CSurf_OnSelectedChange(mediaTrack, true), this);
                            }
                            UpdateSurfaceIfInTrackSelectMode();
                        }
                        break;
                    }
                }// LL_TRACK_SELECT
                case LL_MUTE: {
                    const unsigned char FIRST_ROW_NOTE = 0x0B;
                    if (evt->midi_message[2] != 0) {
                        if ((evt->midi_message[1] >= FIRST_ROW_NOTE) && (evt->midi_message[1] < (FIRST_ROW_NOTE + 8))) {
                            int trackIndex = track_offset + evt->midi_message[1] - FIRST_ROW_NOTE;
                            int numTracks = includeMasterTrack ? (CountTracks(0) + 1) : CountTracks(0);
                            if (!includeMasterTrack) trackIndex++;
                            // unselect all tracks
                            if (state_shift_pressed) {
                                // unmute all
                                for (int i = 0; i < numTracks + 1; i++) {
                                    MediaTrack *mediaTrack = CSurf_TrackFromID(trackIndex, g_csurf_mcpmode);
                                    //if (selectedTrack) CSurf_SetSurfaceSelected(selectedTrack,false,this);
                                    // TODO - try changing this to true
                                    if (mediaTrack) CSurf_SetSurfaceMute(mediaTrack, CSurf_OnMuteChange(mediaTrack, false), this);
                                }
                            } else {
                                // toggle mute for single track
                                if (trackIndex < numTracks + 1) {
                                    MediaTrack *mediaTrack = CSurf_TrackFromID(trackIndex, g_csurf_mcpmode);
                                    double value = GetMediaTrackInfo_Value(mediaTrack, "B_MUTE");
                                    bool mute = doubleIsCloseTo(value, 0);
                                    if (mediaTrack) CSurf_SetSurfaceMute(mediaTrack, CSurf_OnMuteChange(mediaTrack, mute), this);
                                    return;
                                }
                            }

                        }
                        UpdateSurfaceIfInMuteMode();
                        break;
                    }
                } // LL MUTE
                case LL_SOLO: {
                    const unsigned char FIRST_ROW_NOTE = 0x0B;
                    if (evt->midi_message[2] != 0) {
                        if ((evt->midi_message[1] >= FIRST_ROW_NOTE) && (evt->midi_message[1] < (FIRST_ROW_NOTE + 8))) {
                            int trackIndex = track_offset + evt->midi_message[1] - FIRST_ROW_NOTE;
                            int numTracks = includeMasterTrack ? (CountTracks(0) + 1) : CountTracks(0);
                            if (!includeMasterTrack) trackIndex++;
                            // unselect all tracks
                            if (state_shift_pressed) {
                                // unmute all
                                for (int i = 0; i < numTracks + 1; i++) {
                                    MediaTrack *mediaTrack = CSurf_TrackFromID(trackIndex, g_csurf_mcpmode);
                                    //if (selectedTrack) CSurf_SetSurfaceSelected(selectedTrack,false,this);
                                    if (mediaTrack) CSurf_SetSurfaceSolo(mediaTrack, CSurf_OnSoloChange(mediaTrack, false), this);
                                }
                            } else {
                                // toggle mute for single track
                                if (trackIndex < numTracks + 1) {
                                    MediaTrack *mediaTrack = CSurf_TrackFromID(trackIndex, g_csurf_mcpmode);
                                    double value = GetMediaTrackInfo_Value(mediaTrack, "I_SOLO");
                                    bool solo = doubleIsCloseTo(value, 0);
                                    if (mediaTrack) CSurf_SetSurfaceSolo(mediaTrack, CSurf_OnSoloChange(mediaTrack, solo), this);
                                    return;
                                }
                            }

                        }
                        UpdateSurfaceIfInSoloMode();
                        break;
                    }
                }
            }
        }// note on message
    }

    inline void handleButtonForLiveModeSwitch(launchpad_button button, surface_live_layout specialLiveMode, bool button_pressed) {
        // status will be set based on received status messages from Launchpad Pro
        if (button_pressed) {
            // handling for special case when switching from Sends mode to Sends Pan mode, which sahre same mode on Launchpad Pro
            if (state_shift_pressed && current_surface_live_layout == LL_SENDS && specialLiveMode == LL_SENDS) {
                current_surface_live_layout = LL_SENDS_PAN;
                last_time_threshold_button_pressed = timeGetTime();
            } else {
                // normal case
                if (current_surface_live_layout == specialLiveMode) {
                    launchpadSelectLiveLayout(m_midiout, LL_SESSION);
                } else {
//                if (current_surface_live_layout == LL_SESSION) {
                    launchpadSelectLiveLayout(m_midiout, specialLiveMode);
                    last_time_threshold_button_pressed = timeGetTime();
//                }
                }
            }
        } else {
            DWORD now = timeGetTime();
            if ((now - last_time_threshold_button_pressed) > THRESHOLD_MOMENTARY) {
                launchpadSelectLiveLayout(m_midiout, LL_SESSION);
            }
        }
    }

    const int COLOR_NONE = 0;
    const int COLOR_WHITE_DIM = 1;
    const int COLOR_WHITE_BRIGHT = 3;
    const int COLOR_RED_DIM = 7;
    const int COLOR_RED_BRIGHT = 5;
    const int COLOR_ORANGE_BRIGHT = 9;
    const int COLOR_ORANGE_DIM = 11;
    const int COLOR_YELLOW_BRIGHT = 13;
    const int COLOR_YELLOW_DIM = 15;
    const int COLOR_GREEN_BRIGHT = 21;
    const int COLOR_GREEN_DIM = 27;
    const int COLOR_TURQUOISE_BRIGHT = 37;
    const int COLOR_TURQUOISE_DIM = 39;
    const int COLOR_BLUE_BRIGHT = 45;
    const int COLOR_BLUE_DIM = 47;
    const int COLOR_VIOLET_BRIGHT = 49;
    const int COLOR_VIOLET_DIM = 51;
    const int COLOR_PINK_BRIGHT = 53;
    const int COLOR_PINK_DIM = 55;
    const int COLOR_SOME_DIM = 60;

    void UpdateSurface() {
        if (m_midiout) {
            UpdateButtons();
            UpdateSurfaceIfInDeviceMode();
            UpdateSurfaceIfInRecordArmMode();
            UpdateSurfaceIfInTrackSelectMode();
            UpdateSurfaceIfInMuteMode();
            UpdateSurfaceIfInSoloMode();
            UpdateSurfaceIfInVolumeMode();
            UpdateSurfaceIfInPanMode();
            UpdateSurfaceIfInSendsMode();
            UpdateSurfaceIfInSendsPanMode();
        }
    }

    void UpdateSurfaceIfInSendsPanMode() {
        unsigned char colors[LAUNCHPAD_COLUMNS] = {
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE)
        };
        if (m_midiout && (current_surface_live_layout == LL_SENDS_PAN) && ((timeGetTime() - last_time_change_from_sends_pan_mode_received) > POLLING_SURFACE_NON_INTERRUPT_DELAY)) {
            // update send buttons
            unsigned char activeColor;
            for (int i = 0; i < LAUNCHPAD_COLUMNS; i++) {
                colors[i] = (i == lastActiveSend) ? activeColor = (getSendColor(i) + 2) : getSendColor(i);
            }
            launchpad_light_button_row_or_column_sysex(m_midiout, false, BUTTON_PLAY_8, LAUNCHPAD_COLUMNS, colors);
            // update send faders

            unsigned char levels [] = {0, 0, 0, 0, 0, 0, 0, 0};
            int numTracks = CountTracks(0);
            int upperBound = ((track_offset + numTracks) < LAUNCHPAD_COLUMNS) ? numTracks : track_offset + LAUNCHPAD_COLUMNS;
            for (int i = track_offset; i < upperBound; i++) {
                MediaTrack *mediaTrack = CSurf_TrackFromID(i + 1, g_csurf_mcpmode);
                if (mediaTrack) {
                    int numSends = GetTrackNumSends(mediaTrack, 0);
                    double sendPan;
                    if (lastActiveSend < numSends) {
                        sendPan = GetTrackSendInfo_Value(mediaTrack, 0, lastActiveSend, "D_PAN");
                        launchpad_send_fader_sysex(m_midiout, FT_PAN, i - track_offset, panToChar(sendPan), activeColor);
                    }
                }
            }

        }
    }

    void UpdateSurfaceIfInSendsMode() {
        unsigned char colors[LAUNCHPAD_COLUMNS] = {
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE)
        };
        if (m_midiout && (current_surface_live_layout == LL_SENDS) && ((timeGetTime() - last_time_change_from_sends_mode_received) > POLLING_SURFACE_NON_INTERRUPT_DELAY)) {
            // update send buttons
            unsigned char activeColor;
            for (int i = 0; i < LAUNCHPAD_COLUMNS; i++) {
                colors[i] = (i == lastActiveSend) ? activeColor = (getSendColor(i) + 2) : getSendColor(i);
            }
            launchpad_light_button_row_or_column_sysex(m_midiout, false, BUTTON_PLAY_8, LAUNCHPAD_COLUMNS, colors);
            // update send faders

            unsigned char levels [LAUNCHPAD_COLUMNS] = {0, 0, 0, 0, 0, 0, 0, 0};
            int numTracks = CountTracks(0);
            int upperBound = ((track_offset + numTracks) < LAUNCHPAD_COLUMNS) ? numTracks : track_offset + LAUNCHPAD_COLUMNS;
            for (int i = track_offset; i < upperBound; i++) {
                MediaTrack *mediaTrack = CSurf_TrackFromID(i + 1, g_csurf_mcpmode);
                if (mediaTrack) {
                    int numSends = GetTrackNumSends(mediaTrack, 0);
                    double sendVolume;
                    if (lastActiveSend < numSends) {
                        sendVolume = GetTrackSendInfo_Value(mediaTrack, 0, lastActiveSend, "D_VOL");
                    } else {
                        sendVolume = 0;
                    }
                    levels[i - track_offset] = volToChar(sendVolume);

                }
            }
            launchpad_set_fader_row_sysex(m_midiout, FT_VOLUME, activeColor, 0, LAUNCHPAD_COLUMNS, levels);
        }
    }

    inline unsigned char getSendColor(int sendIndex) {
        return COLOR_SOME_DIM - (8 * sendIndex);
    }

    void UpdateSurfaceIfInDeviceMode() {
        unsigned char levels[LAUNCHPAD_COLUMNS] = {
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE)
        };
        if (m_midiout && (current_surface_live_layout == LL_FADER) && ((timeGetTime() - last_time_change_from_device_mode_received) > POLLING_SURFACE_NON_INTERRUPT_DELAY)) {
            MediaTrack *mediaTrack = GetSelectedTrack(0, 0);
            if (mediaTrack) {
                int numTCPFXParams = CountTCPFXParms(0, mediaTrack);

                int upperBound = (numTCPFXParams < LAUNCHPAD_COLUMNS) ? numTCPFXParams : device_mode_offset + LAUNCHPAD_COLUMNS;
                for (int i = device_mode_offset; i < upperBound; i++) {
                    int fxIndex;
                    int pluginParamIndex;
                    double minimumParamValue;
                    double maximumParamValue;
                    GetTCPFXParm(0, mediaTrack, i, &fxIndex, &pluginParamIndex);
                    double actualFxParamValue = TrackFX_GetParam(mediaTrack, fxIndex, pluginParamIndex, &minimumParamValue, &maximumParamValue);
                    double width = maximumParamValue - minimumParamValue; // TODO - hopefully should work both for positive and negative minimum
                    levels[i - device_mode_offset] = round(((actualFxParamValue - minimumParamValue) / width) * 127);
                }
            launchpad_set_fader_row_sysex(m_midiout, FT_VOLUME, COLOR_PINK_DIM, 0, LAUNCHPAD_COLUMNS, levels);
            }
        }
    }

    void UpdateSurfaceIfInRecordArmMode() {
        unsigned char colors[LAUNCHPAD_COLUMNS] = {
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE)
        };
        if (m_midiout && (current_surface_live_layout == LL_RECORD_ARM)) {
            if (current_surface_live_layout == LL_RECORD_ARM) {
                int numTracks = includeMasterTrack ? (CountTracks(0) + 1) : CountTracks(0);
                int upperBound = ((track_offset + LAUNCHPAD_COLUMNS) > numTracks) ? numTracks : track_offset + LAUNCHPAD_COLUMNS;
                for (int i = track_offset; i < upperBound; i++) {
                    MediaTrack *mediaTrack = CSurf_TrackFromID(i + 1, g_csurf_mcpmode);
                    double value = GetMediaTrackInfo_Value(mediaTrack, "I_RECARM");
                    if (doubleIsCloseTo(value, 0)) {
                        colors[i - track_offset] = COLOR_RED_DIM;
                    } else {
                        colors[i - track_offset] = COLOR_RED_BRIGHT;
                    }
                }
                launchpad_light_button_row_or_column_sysex(m_midiout, true, 0x0B, LAUNCHPAD_COLUMNS, colors);
            }
        }
    }

    void UpdateSurfaceIfInTrackSelectMode() {
        unsigned char colors[LAUNCHPAD_COLUMNS] = {
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE)
        };
        if (m_midiout && (current_surface_live_layout == LL_TRACK_SELECT)) {
            if (current_surface_live_layout == LL_TRACK_SELECT) {
                int numTracks = includeMasterTrack ? (CountTracks(0) + 1) : CountTracks(0);
                int upperBound = ((track_offset + LAUNCHPAD_COLUMNS) > numTracks) ? numTracks : track_offset + LAUNCHPAD_COLUMNS;
                for (int i = track_offset; i < upperBound; i++) {
                    MediaTrack *mediaTrack = CSurf_TrackFromID(i + 1, g_csurf_mcpmode);
                    double value = GetMediaTrackInfo_Value(mediaTrack, "I_SELECTED");
                    if (doubleIsCloseTo(value, 0)) {
                        colors[i - track_offset] = COLOR_TURQUOISE_DIM;
                    } else {
                        colors[i - track_offset] = COLOR_TURQUOISE_BRIGHT;
                    }
                }
                launchpad_light_button_row_or_column_sysex(m_midiout, true, 0x0B, LAUNCHPAD_COLUMNS, colors);
            }
        }
    }

    void UpdateSurfaceIfInMuteMode() {
        unsigned char colors[LAUNCHPAD_COLUMNS] = {
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE)
        };
        if (m_midiout && (current_surface_live_layout == LL_MUTE)) {
            if (current_surface_live_layout == LL_MUTE) {
                int numTracks = includeMasterTrack ? (CountTracks(0) + 1) : CountTracks(0);
                int upperBound = ((track_offset + LAUNCHPAD_COLUMNS) > numTracks) ? numTracks : track_offset + LAUNCHPAD_COLUMNS;
                for (int i = track_offset; i < upperBound; i++) {
                    MediaTrack *mediaTrack = CSurf_TrackFromID(i + 1, g_csurf_mcpmode);
                    bool muted = GetMediaTrackInfo_Value(mediaTrack, "B_MUTE");
                    if (muted) {
                        colors[i - track_offset] = COLOR_YELLOW_DIM;
                    } else {
                        colors[i - track_offset] = COLOR_YELLOW_BRIGHT;
                    }
                }
                launchpad_light_button_row_or_column_sysex(m_midiout, true, 0x0B, LAUNCHPAD_COLUMNS, colors);
            }
        }
    }

    void UpdateSurfaceIfInSoloMode() {
        unsigned char colors[LAUNCHPAD_COLUMNS] = {
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE)
        };
        if (m_midiout && (current_surface_live_layout == LL_SOLO)) {
            if (current_surface_live_layout == LL_SOLO) {
                int numTracks = includeMasterTrack ? (CountTracks(0) + 1) : CountTracks(0);
                int upperBound = ((track_offset + LAUNCHPAD_COLUMNS) > numTracks) ? numTracks : track_offset + LAUNCHPAD_COLUMNS;
                for (int i = track_offset; i < upperBound; i++) {
                    MediaTrack *mediaTrack = CSurf_TrackFromID(i + 1, g_csurf_mcpmode);
                    bool not_soloed = doubleIsCloseTo(GetMediaTrackInfo_Value(mediaTrack, "I_SOLO"), 0);
                    if (not_soloed) {
                        colors[i - track_offset] = COLOR_BLUE_DIM;
                    } else {
                        colors[i - track_offset] = COLOR_BLUE_BRIGHT;
                    }
                }
                launchpad_light_button_row_or_column_sysex(m_midiout, true, 0x0B, LAUNCHPAD_COLUMNS, colors);
            }
        }
    }

    void UpdateSurfaceIfInVolumeMode() {
        unsigned char levels[LAUNCHPAD_COLUMNS] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        if (m_midiout) {
            if (current_surface_live_layout == LL_VOLUME) {
                int numTracks = includeMasterTrack ? (CountTracks(0) + 1) : CountTracks(0);
//        int upperBound = ((track_offset + LAUNCHPAD_COLUMNS) > numTracks) ? numTracks : track_offset + LAUNCHPAD_COLUMNS;
                int upperBound = ((track_offset + numTracks) < LAUNCHPAD_COLUMNS) ? numTracks : track_offset + LAUNCHPAD_COLUMNS;
                for (int i = track_offset; i < upperBound; i++) {
                    MediaTrack *mediaTrack = CSurf_TrackFromID(i + 1, g_csurf_mcpmode);
                    double volume = 0;
                    double pan = 0;
                    GetTrackUIVolPan(mediaTrack, &volume, &pan);
                    // adding 1 to include Master Track
                    levels[i - track_offset] = volToChar(volume);
                }
                launchpad_set_fader_row_sysex(m_midiout, FT_VOLUME, COLOR_GREEN_BRIGHT, 0, LAUNCHPAD_COLUMNS, levels);
            }
        }
    }

    void UpdateSurfaceIfInPanMode() {
        if (m_midiout) {
            if (current_surface_live_layout == LL_PAN) {
                int numTracks = CountTracks(0);
                int upperBound = ((track_offset + LAUNCHPAD_COLUMNS) > numTracks) ? numTracks : track_offset + LAUNCHPAD_COLUMNS;
                for (int i = track_offset; i < upperBound; i++) {
					MediaTrack *mediaTrack = CSurf_TrackFromID(i + 1, g_csurf_mcpmode);
					if (mediaTrack) {
						double volume = 0;
						double pan = 0;
						GetTrackUIVolPan(mediaTrack, &volume, &pan);
						// adding 1 to include Master Track
						launchpad_send_fader_sysex(m_midiout, FT_PAN, i - track_offset, panToChar(pan), COLOR_ORANGE_BRIGHT);
					}
                }
            }
        }
    }

    void UpdateButtons() {
        unsigned char colors[LAUNCHPAD_COLUMNS] = {
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE),
                static_cast<unsigned char>(COLOR_NONE)
        };
        if (m_midiout) {
            // clear grid
//            if (current_surface_live_layout == LL_SESSION) {
                fillGrid(COLOR_NONE);
//            }
            // light bottom row
            colors[0] = (current_surface_live_layout == LL_RECORD_ARM) ? COLOR_RED_BRIGHT : COLOR_RED_DIM;
            colors[1] = (current_surface_live_layout == LL_TRACK_SELECT) ? COLOR_TURQUOISE_BRIGHT : COLOR_TURQUOISE_DIM;
            colors[2] = (current_surface_live_layout == LL_MUTE) ? COLOR_YELLOW_BRIGHT : COLOR_YELLOW_DIM;
            colors[3] = (current_surface_live_layout == LL_SOLO) ? COLOR_BLUE_BRIGHT : COLOR_BLUE_DIM;
            colors[4] = (current_surface_live_layout == LL_VOLUME) ? COLOR_GREEN_BRIGHT : COLOR_GREEN_DIM;
            colors[5] = (current_surface_live_layout == LL_PAN) ? COLOR_ORANGE_BRIGHT : COLOR_ORANGE_DIM;
            // sends have two modes
            colors[6] = (current_surface_live_layout == LL_SENDS) ? COLOR_WHITE_BRIGHT : COLOR_WHITE_DIM;
            colors[7] = COLOR_NONE; // Stop Clip
            launchpad_light_button_row_or_column_sysex(m_midiout, true, BUTTON_RECORD_ARM, 8, colors);

            // light upper row
            for (int i = 0; i < LAUNCHPAD_COLUMNS; i++) {
                colors[i] = static_cast<unsigned char>(COLOR_NONE);
            }
            if (current_surface_live_layout != LL_FADER) {
                colors[2] = (track_offset != 0) ? COLOR_GREEN_BRIGHT : COLOR_GREEN_DIM;
                colors[3] = (track_offset + LAUNCHPAD_COLUMNS < CountTracks(0)) ? COLOR_GREEN_BRIGHT : COLOR_GREEN_DIM;
            } else {
                // device mode scrolling
                colors[2] = (device_mode_offset != 0) ? COLOR_PINK_BRIGHT : COLOR_PINK_DIM;
                MediaTrack *mediaTrack = GetSelectedTrack(0, 0);
                if (mediaTrack) {
                    int numTCPFXParams = CountTCPFXParms(0, mediaTrack);
                    colors[3] = (device_mode_offset + LAUNCHPAD_COLUMNS < numTCPFXParams) ? COLOR_PINK_BRIGHT : COLOR_PINK_DIM;
                }
            }
            colors[4] = (current_surface_live_layout == LL_SESSION) ? COLOR_GREEN_BRIGHT : COLOR_GREEN_DIM;
            // colors[5] = (current_surface_live_layout == LL_CHROMATIC_NOTE) ? COLOR_TURQUOISE_BRIGHT : COLOR_TURQUOISE_DIM;
            colors[6] = (current_surface_live_layout == LL_FADER) ? COLOR_PINK_BRIGHT : COLOR_PINK_DIM;
            // colors[7] = (current_surface_live_layout == LL_USER) ? COLOR_VIOLET_BRIGHT : COLOR_VIOLET_DIM;
            launchpad_light_button_row_or_column_sysex(m_midiout, true, BUTTON_ARROW_UP, LAUNCHPAD_COLUMNS, colors);

            // light left row
            for (int i = 0; i < LAUNCHPAD_COLUMNS; i++) {
                colors[i] = static_cast<unsigned char>(COLOR_NONE);
            }

            colors[0] = state_shift_pressed ? COLOR_WHITE_BRIGHT : COLOR_WHITE_DIM;
            colors[1] = GetToggleCommandState(ACTION_TOGGLE_METRONOME) ? COLOR_GREEN_BRIGHT : COLOR_GREEN_DIM;
            colors[2] = COLOR_GREEN_DIM;
            colors[7] = state_recording ? COLOR_RED_BRIGHT : COLOR_RED_DIM;
            launchpad_light_button_row_or_column_sysex(m_midiout, false, BUTTON_RECORD, LAUNCHPAD_COLUMNS, colors);

            // light right row
            for (int i = 0; i < LAUNCHPAD_COLUMNS; i++) {
                colors[i] = static_cast<unsigned char>(COLOR_NONE);
            }
            // BUTTON_PLAY_7 is used for loop toggle
            // BUTTON_PLAY_8 is used for play toggle
            if ((current_surface_live_layout != LL_SENDS) && (current_surface_live_layout != LL_SENDS_PAN)) {
                colors[6] = state_loop ? COLOR_ORANGE_BRIGHT : COLOR_ORANGE_DIM;
                colors[7] = state_playing ? COLOR_GREEN_BRIGHT : COLOR_GREEN_DIM;
            }
            launchpad_light_button_row_or_column_sysex(m_midiout, false, BUTTON_PLAY_8, LAUNCHPAD_COLUMNS, colors);

        }

    }

public:
    CSurf_LaunchpadPro(int indev, int outdev, int do_not_switch_to_live_mode_on_startup, int updateFrequency, int *errStats) {
        m_midi_in_dev = indev;
        m_midi_out_dev = outdev;
        config_do_not_switch_to_live_mode_on_startup = do_not_switch_to_live_mode_on_startup;
        config_surface_update_frequency = updateFrequency;

        m_fader_touchstate = 0;

        m_frameupd_lastrun = 0;
        m_frameupd_lastrun2 = 0;
        m_arrowstates = 0;
        m_button_states = 0;
        m_buttonstate_lastrun = 0;
        m_pan_lasttouch = 0;
        memset(m_tranz_oldbuf, ' ', sizeof(m_tranz_oldbuf));
        m_tranz_anysolo_poop = 0;

        //create midi hardware access
        m_midiin = m_midi_in_dev >= 0 ? CreateMIDIInput(m_midi_in_dev) : NULL;
        m_midiout = m_midi_out_dev >= 0 ? CreateThreadedMIDIOutput(CreateMIDIOutput(m_midi_out_dev, false, NULL)) : NULL;

        if (errStats) {
            if (m_midi_in_dev >= 0 && !m_midiin) *errStats |= 1;
            if (m_midi_out_dev >= 0 && !m_midiout) *errStats |= 2;
        }

        if (m_midiin)
            m_midiin->start();

        // TODO - initialize buttons

        if (m_midiout && !config_do_not_switch_to_live_mode_on_startup) {
            launchpadSwitchLiveMode(m_midiout, true);
            launchpadSelectLiveLayout(m_midiout, LL_SESSION);
        }
        if (m_midiout) {
            sendDeviceInquiry(m_midiout);
        }
        //launchpadScrollText(m_midiout, "REAPER");

    }

    ~CSurf_LaunchpadPro() {
        if (m_midiout) {
            launchpadSwitchLiveMode(m_midiout, false);
            Sleep(5);
        }
        device_connected = false;
        delete m_midiout;
        delete m_midiin;
    }

    const char *GetTypeString() { return "LAUNCHPADPRO"; }

    const char *GetDescString() {
        descspace.Set("Novation Launchpad Pro");
        char tmp[512];
        sprintf(tmp, " (dev %d,%d)", m_midi_in_dev, m_midi_out_dev);
        descspace.Append(tmp);
        return descspace.Get();
    }

    const char *GetConfigString() // string of configuration data
    {
        sprintf(configtmp, "0 0 %d %d %d %d", m_midi_in_dev, m_midi_out_dev, config_do_not_switch_to_live_mode_on_startup, config_surface_update_frequency);
        return configtmp;
    }

    void CloseNoReset() {
		if (m_midiout) {
			launchpadSwitchLiveMode(m_midiout, false);
			Sleep(5);
		}
		device_connected = false;
        delete m_midiout;
        delete m_midiin;
        m_midiout = 0;
        m_midiin = 0;
    }

    void Run() {
        DWORD now = timeGetTime();
        if (m_midiout) {
            if ((now - last_surface_update_time) > config_surface_update_frequency) {
                switch (current_surface_live_layout) {
                    case LL_FADER: UpdateSurfaceIfInDeviceMode();
                    break;
                    case LL_SENDS: UpdateSurfaceIfInSendsMode();
                    break;
                    case LL_SENDS_PAN: UpdateSurfaceIfInSendsPanMode();
                    break;
                }
                last_surface_update_time = now;
            }
        }

        if (m_midiin) {
            m_midiin->SwapBufs(timeGetTime());
            int l = 0;
            MIDI_eventlist *list = m_midiin->GetReadBuf();
            MIDI_event_t *evts;
            while ((evts = list->EnumItems(&l))) OnMIDIEvent(evts);
        }
    }

    void SetTrackListChange() {} // not used

    void SetSurfaceVolume(MediaTrack *trackid, double volume) {
        UpdateSurfaceIfInVolumeMode();
    }

    void SetSurfacePan(MediaTrack *trackid, double pan) {
        UpdateSurfaceIfInPanMode();
    }

    void SetSurfaceMute(MediaTrack *trackid, bool mute) {
        UpdateSurfaceIfInMuteMode();
    }

    void SetSurfaceSelected(MediaTrack *trackid, bool selected) {
        UpdateSurfaceIfInTrackSelectMode();
    }

    void SetSurfaceSolo(MediaTrack *trackid, bool solo) {
        UpdateSurfaceIfInSoloMode();
    }

    void SetSurfaceRecArm(MediaTrack *trackid, bool recarm) {
        UpdateSurfaceIfInRecordArmMode();
    }

    void SetPlayState(bool play, bool pause, bool rec) {
        state_recording = rec;
        state_playing = play;
        UpdateSurface();
    }

    void SetRepeatState(bool rep) {
        state_loop = rep;
        UpdateSurface();
    }

    void SetTrackTitle(MediaTrack *trackid, const char *title) {}

    bool GetTouchState(MediaTrack *trackid, int isPan) {
        return false;
    }

    void SetAutoMode(int mode) {}

    void fillGrid(unsigned char color) {
        unsigned char colors[LAUNCHPAD_COLUMNS];

        if (m_midiout) {
            for (int i = 0; i < LAUNCHPAD_COLUMNS; i++) {
                colors[i] = color;
            }
            for (int i = 0; i < LAUNCHPAD_COLUMNS; i++) {
                launchpad_light_button_row_or_column_sysex(m_midiout, true, 0x0B + (10 * i), LAUNCHPAD_COLUMNS, colors);
            }
        }
    }

    void ResetCachedVolPanStates() {

    }

    void OnTrackSelection(MediaTrack *trackid) {
        // FIXME update also on track moving
        TrackList_UpdateAllExternalSurfaces();
        if (current_surface_live_layout == LL_FADER) {
            if (CountSelectedTracks(0) != 1) {
                fillGrid(COLOR_PINK_BRIGHT);
                Sleep(250);
                fillGrid(COLOR_NONE);
                Sleep(250);
                fillGrid(COLOR_PINK_BRIGHT);
                Sleep(250);
                fillGrid(COLOR_NONE);
                launchpadSelectLiveLayout(m_midiout, LL_SESSION);
            }
        }
        UpdateSurface();
    }

    bool IsKeyDown(int key) {
        return false;
    }


};


static void parseParms(const char *str, int parms[NUM_PLUGIN_PARAMS]) {
    parms[0] = 0;
    parms[1] = 9;
    parms[2] = parms[3] = -1;
    parms[4] = 0; // checkbox
    parms[5] = 0; // update fequency

    const char *p = str;
    if (p) {
        int x = 0;
        while (x < NUM_PLUGIN_PARAMS) {
            while (*p == ' ') p++;
            if ((*p < '0' || *p > '9') && *p != '-') break;
            parms[x++] = atoi(p);
            while (*p && *p != ' ') p++;
        }
    }
}


static IReaperControlSurface *createFunc(const char *type_string, const char *configString, int *errStats) {
    int parms[NUM_PLUGIN_PARAMS];
    parseParms(configString, parms);

    return new CSurf_LaunchpadPro(parms[2], parms[3], parms[4], parms[5], errStats);
}


static WDL_DLGRET dlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            int parms[NUM_PLUGIN_PARAMS];
            parseParms((const char *) lParam, parms);

        int n=GetNumMIDIInputs();
        int x = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM) "None");
        SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_SETITEMDATA, x, -1);
        x = SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM) "None");
        SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_SETITEMDATA, x, -1);
        for (x = 0; x < n; x++)
        {
          char buf[512];
            if (GetMIDIInputName(x, buf, sizeof(buf))) {
                int a = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM) buf);
                SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_SETITEMDATA, a, x);
                if (x == parms[2]) SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_SETCURSEL, a, 0);
          }
        }
        n=GetNumMIDIOutputs();
        for (x = 0; x < n; x++)
        {
          char buf[512];
          if (GetMIDIOutputName(x,buf,sizeof(buf)))
          {
            int a=SendDlgItemMessage(hwndDlg,IDC_COMBO3,CB_ADDSTRING,0,(LPARAM)buf);
            SendDlgItemMessage(hwndDlg,IDC_COMBO3,CB_SETITEMDATA,a,x);
            if (x == parms[3]) SendDlgItemMessage(hwndDlg,IDC_COMBO3,CB_SETCURSEL,a,0);
          }
        }
        if (parms[4]) {
            SendDlgItemMessage(hwndDlg, IDC_CHK_DO_NOT_SWITCH_TO_LIVE_MODE, BM_SETCHECK, BST_CHECKED, 0);
        }
        char updateFrequencyString [512] = "\n";
        int update_frequency = 300;
        if (parms[5]) update_frequency = parms[5];
        sprintf(updateFrequencyString, "%d", update_frequency);
        SetWindowText(GetDlgItem(hwndDlg, IDC_EDT_SURFACE_UPDATE_FREQUENCY), (LPSTR) updateFrequencyString);
        SetWindowText(GetDlgItem(hwndDlg, IDC_LBL_STATUS), device_connected ? "Device Connected" : "Device Not Connected");
        SetWindowText(GetDlgItem(hwndDlg, IDC_EDT_FIRMWARE_VERSION), firmwareVersion);
        SetWindowText(GetDlgItem(hwndDlg, IDC_EDT_PLUGIN_VERSION), LAUNCHPAD_PRO_PLUGIN_VERSION);
      }
    break;
    case WM_USER+1024:
      if (wParam > 1 && lParam)
      {
        char tmp[512];

                int indev = -1, outdev = -1, offs = 0, size = 9, do_not_switch_to_live = 0, surface_update_frequency = 0;
                int r = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_GETCURSEL, 0, 0);
                if (r != CB_ERR) indev = SendDlgItemMessage(hwndDlg, IDC_COMBO2, CB_GETITEMDATA, r, 0);
                r = SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_GETCURSEL, 0, 0);
                if (r != CB_ERR) outdev = SendDlgItemMessage(hwndDlg, IDC_COMBO3, CB_GETITEMDATA, r, 0);
                if (r != CB_ERR) do_not_switch_to_live = (SendDlgItemMessage(hwndDlg, IDC_CHK_DO_NOT_SWITCH_TO_LIVE_MODE, BM_GETCHECK, r, 0) == BST_CHECKED);
                char updateFrequencyString [128];
                GetWindowText(GetDlgItem(hwndDlg, IDC_EDT_SURFACE_UPDATE_FREQUENCY), updateFrequencyString, 128);
                if (r != CB_ERR) do_not_switch_to_live = (SendDlgItemMessage(hwndDlg, IDC_CHK_DO_NOT_SWITCH_TO_LIVE_MODE, BM_GETCHECK, r, 0) == BST_CHECKED);
                sprintf(tmp, "0 0 %d %d %d %d", indev, outdev, do_not_switch_to_live, atoi(updateFrequencyString));

                lstrcpyn((char *) lParam, tmp, wParam);

            }
            break;
    }
    return 0;
}

static HWND configFunc(const char *type_string, HWND parent, const char *initConfigString)
{
  return CreateDialogParam(g_hInst,MAKEINTRESOURCE(IDD_SURFACEEDIT_LAUNCHPADPRO), parent, dlgProc, (LPARAM)initConfigString);
}


reaper_csurf_reg_t csurf_launchpadpro_reg =
        {
                "LAUNCHPADPRO",
                "Novation Launchpad Pro",
                createFunc,
                configFunc,
        };

extern "C"
{

REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t *rec) {
    g_hInst = hInstance;

    if (!rec || rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc)
        return 0;

    g_hwnd = rec->hwnd_main;
    int errcnt = 0;
#define IMPAPI(x) if (!((*((void **)&(x)) = (void *)rec->GetFunc(#x)))) errcnt++;

    IMPAPI(DB2SLIDER)
    IMPAPI(SLIDER2DB)
    IMPAPI(GetNumMIDIInputs)
    IMPAPI(GetNumMIDIOutputs)
    IMPAPI(CountTracks)
    IMPAPI(CreateMIDIInput)
    IMPAPI(CreateMIDIOutput)
    IMPAPI(GetMIDIOutputName)
    IMPAPI(GetMIDIInputName)
    IMPAPI(GetSelectedTrack)
    IMPAPI(GetToggleCommandState)
    IMPAPI(GetTrackNumSends)
    IMPAPI(CountSelectedTracks)
    IMPAPI(CountTCPFXParms)
    IMPAPI(CSurf_TrackToID)
    IMPAPI(CSurf_TrackFromID)
    IMPAPI(CSurf_NumTracks)
    IMPAPI(CSurf_OnSendVolumeChange)
    IMPAPI(CSurf_OnSendPanChange)
    IMPAPI(CSurf_SetTrackListChange)
    IMPAPI(CSurf_SetSurfaceVolume)
    IMPAPI(CSurf_SetSurfacePan)
    IMPAPI(CSurf_SetSurfaceMute)
    IMPAPI(CSurf_SetSurfaceSelected)
    IMPAPI(CSurf_SetSurfaceSolo)
    IMPAPI(CSurf_SetSurfaceRecArm)
    IMPAPI(CSurf_GetTouchState)
    IMPAPI(CSurf_SetAutoMode)
    IMPAPI(CSurf_SetPlayState)
    IMPAPI(CSurf_SetRepeatState)
    IMPAPI(CSurf_OnVolumeChange)
    IMPAPI(CSurf_OnPanChange)
    IMPAPI(CSurf_OnMuteChange)
    IMPAPI(CSurf_OnSelectedChange)
    IMPAPI(CSurf_OnSoloChange)
    IMPAPI(CSurf_OnFXChange)
    IMPAPI(CSurf_OnRecArmChange)
    IMPAPI(CSurf_OnPlay)
    IMPAPI(CSurf_OnStop)
    IMPAPI(CSurf_OnFwd)
    IMPAPI(CSurf_OnRew)
    IMPAPI(CSurf_OnRecord)
    IMPAPI(CSurf_GoStart)
    IMPAPI(CSurf_GoEnd)
    IMPAPI(CSurf_OnArrow)
    IMPAPI(CSurf_OnTrackSelection)
    IMPAPI(CSurf_ResetAllCachedVolPanStates)
    IMPAPI(CSurf_ScrubAmt)
    IMPAPI(TrackList_UpdateAllExternalSurfaces)
    IMPAPI(kbd_OnMidiEvent)
    IMPAPI(GetMasterTrack)
    IMPAPI(GetMasterMuteSoloFlags)
    IMPAPI(GetTCPFXParm)
    IMPAPI(ClearAllRecArmed)
    IMPAPI(SetTrackAutomationMode)
    IMPAPI(GetTrackAutomationMode)
    IMPAPI(SoloAllTracks)
    IMPAPI(MuteAllTracks)
    IMPAPI(BypassFxAllTracks)
    IMPAPI(GetTrack)
    IMPAPI(GetTrackInfo)
    IMPAPI(GetMediaTrackInfo_Value)
    IMPAPI(GetTrackSendInfo_Value)
    IMPAPI(SetTrackSelected)
    IMPAPI(SetTrackSendInfo_Value)
    IMPAPI(SetAutomationMode)
    IMPAPI(UpdateTimeline)
    IMPAPI(Main_OnCommand)
    IMPAPI(Main_UpdateLoopInfo)
    IMPAPI(GetPlayState)
    IMPAPI(GetPlayPosition)
    IMPAPI(GetCursorPosition)
    IMPAPI(format_timestr_pos)
    IMPAPI(TimeMap2_timeToBeats)
    IMPAPI(Track_GetPeakInfo)
    IMPAPI(GetTrackUIVolPan)
    IMPAPI(GetSetRepeat)
    IMPAPI(mkvolpanstr)
    IMPAPI(mkvolstr)
    IMPAPI(mkpanstr)
    IMPAPI(MoveEditCursor)
    IMPAPI(adjustZoom)
    IMPAPI(GetHZoomLevel)
    IMPAPI(SetMediaTrackInfo_Value)

    IMPAPI(TrackFX_GetCount)
    IMPAPI(TrackFX_GetNumParams)
    IMPAPI(TrackFX_GetParam)
    IMPAPI(TrackFX_SetParam)
    IMPAPI(TrackFX_GetParamName)
    IMPAPI(TrackFX_FormatParamValue)
    IMPAPI(TrackFX_GetFXName)

    IMPAPI(GetTrackGUID)

    void *(*get_config_var)(const char *name, int *szout);
    int (*projectconfig_var_getoffs)(const char *name, int *szout);
    IMPAPI(get_config_var);
    IMPAPI(projectconfig_var_getoffs);
    IMPAPI(projectconfig_var_addr);
    if (errcnt) return 0;

    int sztmp;
#define IMPVAR(x, nm) if (!((*(void **)&(x)) = get_config_var(nm,&sztmp)) || sztmp != sizeof(*x)) errcnt++;
#define IMPVARP(x, nm, type) if (!((x) = projectconfig_var_getoffs(nm,&sztmp)) || sztmp != sizeof(type)) errcnt++;

    if (errcnt) return 0;

    rec->Register("csurf", &csurf_launchpadpro_reg);

    return 1;

}

};

#ifndef _WIN32 // let OS X use this threading step

#include "../WDL/mutex.h"
#include "../WDL/ptrlist.h"


class threadedMIDIOutput : public midi_Output {
public:
    threadedMIDIOutput(midi_Output *out) {
        m_output = out;
        m_quit = false;
        DWORD id;
        m_hThread = CreateThread(NULL, 0, threadProc, this, 0, &id);
    }

    virtual ~threadedMIDIOutput() {
        if (m_hThread) {
            m_quit = true;
            WaitForSingleObject(m_hThread, INFINITE);
            CloseHandle(m_hThread);
            m_hThread = 0;
            Sleep(30);
        }

        delete m_output;
        m_empty.Empty(true);
        m_full.Empty(true);
    }

    virtual void SendMsg(MIDI_event_t *msg, int frame_offset) // frame_offset can be <0 for "instant" if supported
    {
        if (!msg) return;

        WDL_HeapBuf *b = NULL;
        if (m_empty.GetSize()) {
            m_mutex.Enter();
            b = m_empty.Get(m_empty.GetSize() - 1);
            m_empty.Delete(m_empty.GetSize() - 1);
            m_mutex.Leave();
        }
        if (!b && m_empty.GetSize() + m_full.GetSize() < 500)
            b = new WDL_HeapBuf(256);

        if (b) {
            int sz = msg->size;
            if (sz < 3)sz = 3;
            int len = msg->midi_message + sz - (unsigned char *) msg;
            memcpy(b->Resize(len, false), msg, len);
            m_mutex.Enter();
            m_full.Add(b);
            m_mutex.Leave();
        }
    }

    virtual void Send(unsigned char status, unsigned char d1, unsigned char d2, int frame_offset) // frame_offset can be <0 for "instant" if supported
    {
        MIDI_event_t evt = {0, 3, status, d1, d2};
        SendMsg(&evt, frame_offset);
    }

    ///////////

    static DWORD WINAPI threadProc(LPVOID p) {
        WDL_HeapBuf *lastbuf = NULL;
        threadedMIDIOutput *_this = (threadedMIDIOutput *) p;
        unsigned int scnt = 0;
        for (;;) {
            if (_this->m_full.GetSize() || lastbuf) {
                _this->m_mutex.Enter();
                if (lastbuf) _this->m_empty.Add(lastbuf);
                lastbuf = _this->m_full.Get(0);
                _this->m_full.Delete(0);
                _this->m_mutex.Leave();

                if (lastbuf) _this->m_output->SendMsg((MIDI_event_t *) lastbuf->Get(), -1);
                scnt = 0;
            } else {
                Sleep(1);
                if (_this->m_quit && scnt++ > 3) break; //only quit once all messages have been sent
            }
        }
        delete lastbuf;
        return 0;
    }

    WDL_Mutex m_mutex;
    WDL_PtrList<WDL_HeapBuf> m_full, m_empty;

    HANDLE m_hThread;
    bool m_quit;
    midi_Output *m_output;
};


midi_Output *CreateThreadedMIDIOutput(midi_Output *output) {
    if (!output) return output;
    return new threadedMIDIOutput(output);
}

#else

// windows doesnt need it since we have threaded midi outputs now
midi_Output *CreateThreadedMIDIOutput(midi_Output *output)
{
  return output;
}

#endif
