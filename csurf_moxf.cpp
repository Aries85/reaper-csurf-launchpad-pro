/*
** csurf_moxf
** Launchpad Pro support
** Copyright (C) 2019
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
#include "moxf_functions.h"
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

enum moxf_button : unsigned char {
    NO_BUTTON = 0x00,
    BUTTON_F1 = 0x36
};

class CSurf_MOXF : public IReaperControlSurface {

    unsigned char SYSEX_DEVICE_INQUIRY_BEGINNING[2] = {0xF0, 0x7E};
    unsigned char SYSEX_YAMAHA_DEVICE_INQUIRY_REPLY [8] = { 0xF0, 0x7E, 0x7F, 0x06, 0x02, 0x43, 0x00, 0x41 };
    unsigned char SYSEX_YAMAHA_REMOTE_MODE_SWITCH [9] = { 0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x01, 0x01, 0x56, 0x00 }; // next byte is status - 01 - on, 00 - off
    // 06:10:27.964	From Port 2	SysEx		Universal Non-Real Time 15 bytes	F0 7E 7F 06 02 43 00 41 4C 06 01 00 00 7F F7


    const DWORD THRESHOLD_MOMENTARY = 300; // 250 MS to differentiate between toggle and momentary

    const int ACTION_TOGGLE_REPEAT = 1068;
    const int ACTION_TOGGLE_METRONOME = 40364;
    const int ACTION_UNDO = 40029;
    const int ACTION_REDO = 40030;
    const int ACTION_TRANSPORT_TOGGLE_PLAY_STOP = 40044;
    const int ACTION_START_STOP_RECORDING_AT_EDIT_CURSOR = 40046;
    const int ACTION_VIEW_RECORDING_SETTINGS_FOR_LAST_TOUCHED_TRACK = 40604;
    const int ACTION_INSERT_VIRTUAL_INSTRUMENT_ON_NEW_TRACK = 40701;

    const int device_mode = 0;

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
    bool moxf_in_daw_remote_mode = true;

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

    const int MOXF_CC_REMOTE_KNOB_ROW_1_BANK_1 = 0x10; // knobs in banks are indexed like they were next to each other, 3 banks,
    //then knobs in second row continue
    const int MOXF_CC_REMOTE_KNOB_ROW_2_BANK_3 = 0x27; // last knob
    const int MOXF_DATA_KONB = 0x3C;

    const int MOXF_KNOB_BIT_RESOLUTION = 9; // TODO maybe parametrize
    const int MOXF_KNOB_NUM_STEPS = (2 << MOXF_KNOB_BIT_RESOLUTION) - 1;

    enum device_mode : unsigned char {
      MODE_STANDARD = 0,
      MODE_SINGLE_TRACK_MIX
    };

    // TODO fader
    // TODO display buffer and diff sending, force redraw on entering remote mode
    // TODO fix entering remote mode

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
            if (arrayEqual(SYSEX_YAMAHA_DEVICE_INQUIRY_REPLY, sysExData, sizeof(SYSEX_YAMAHA_DEVICE_INQUIRY_REPLY)/sizeof(SYSEX_YAMAHA_DEVICE_INQUIRY_REPLY[0]))) {
              // TODO - get firmware revision
              device_connected = true;
              if ((sysExData[8] == 0x44) && (sysExData[9] == 0x06)) {
                sprintf(firmwareVersion, "MOX6");
              } else if ((sysExData[8] == 0x45) &&(sysExData[9] == 0x06)) {
                sprintf(firmwareVersion, "MOX8");
              } else if ((sysExData[8] == 0x4C) &&(sysExData[9] == 0x06)) {
                sprintf(firmwareVersion, "MOXF6");
              } else if ((sysExData[8] == 0x4D) &&(sysExData[9] == 0x06)) {
                // documentation was outdated, guessing this one
                sprintf(firmwareVersion, "MOXF8");
              } else {
                sprintf(firmwareVersion, "Unknown - is this really Yamaha MOX/MOXF?");
                device_connected = false;
              }

            } else if (arrayEqual(SYSEX_YAMAHA_REMOTE_MODE_SWITCH, sysExData, sizeof(SYSEX_YAMAHA_REMOTE_MODE_SWITCH)/sizeof(SYSEX_YAMAHA_REMOTE_MODE_SWITCH[0]))) {
                moxf_in_daw_remote_mode = sysExData[9];
            }
            // end of sysex
        } else if (isCCMessage(evt->midi_message[0])) {


            if ((evt->midi_message[1] >= MOXF_CC_REMOTE_KNOB_ROW_1_BANK_1) && (evt->midi_message[1] < (MOXF_CC_REMOTE_KNOB_ROW_2_BANK_3))) {
              int tcpParameterIndex = evt->midi_message[1] - MOXF_CC_REMOTE_KNOB_ROW_1_BANK_1;
              int stepDiff = evt->midi_message[2]; // number of step for difference
              // knobs send values 1 - 63 on positive change and 65 - 127
              if ((stepDiff / 64) == 1) {
                // negative value
                stepDiff = 0 - (stepDiff - 64);
              }

              last_time_change_from_device_mode_received = timeGetTime();


              MediaTrack *mediaTrack = GetSelectedTrack(0, 0);
              if (mediaTrack) {
                  int fxParamCount = CountTCPFXParms(0, mediaTrack);
                  if (tcpParameterIndex < fxParamCount) {
                      int fxIndex;
                      int pluginParamIndex;
                      double minimumParamValue;
                      double maximumParamValue;
                      GetTCPFXParm(0, mediaTrack, tcpParameterIndex, &fxIndex, &pluginParamIndex);
                      double curVal = TrackFX_GetParam(mediaTrack, fxIndex, pluginParamIndex, &minimumParamValue, &maximumParamValue);
                      double valueWidth = maximumParamValue - minimumParamValue;
                      double valuePerStep = valueWidth / MOXF_KNOB_NUM_STEPS;
                      int currentStep = curVal / valuePerStep;
                      int newStepValue = 0; // new value in steps, will be updated in following lines
                      if ((currentStep + stepDiff) >= MOXF_KNOB_NUM_STEPS) {
                        newStepValue = MOXF_KNOB_NUM_STEPS - 1; // maximum value
                      } else if ((currentStep + stepDiff) < 0) {
                        newStepValue = 0;
                      } else {
                        newStepValue = currentStep + stepDiff;
                      }
                      TrackFX_SetParam(mediaTrack, fxIndex, pluginParamIndex, minimumParamValue + (newStepValue * valuePerStep));
                  }
              }
            }
            /*switch (current_surface_live_layout) {
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
                    break;*/

        }// CC message
        else if (isNoteOnMessage(evt->midi_message[0])) {
          switch (evt->midi_message[1]) {
            case BUTTON_F1: Main_OnCommand(ACTION_INSERT_VIRTUAL_INSTRUMENT_ON_NEW_TRACK, 0);
            break;
          }
        }// note on message
    }

public:
    CSurf_MOXF(int indev, int outdev, int do_not_switch_to_live_mode_on_startup, int updateFrequency, int *errStats) {
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
            //launchpadSwitchLiveMode(m_midiout, true);
            //launchpadSelectLiveLayout(m_midiout, LL_SESSION);
        }
        if (m_midiout) {
            sendDeviceInquiry(m_midiout);
        }
        //launchpadScrollText(m_midiout, "REAPER");

    }

    ~CSurf_MOXF() {
        if (m_midiout) {
            //launchpadSwitchLiveMode(m_midiout, false);
            Sleep(5);
        }
        device_connected = false;
        delete m_midiout;
        delete m_midiin;
    }

    const char *GetTypeString() { return "MOXF"; }

    const char *GetDescString() {
        descspace.Set("Yamaha MOX/MOXF");
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
      // TODO deinitialize here
			Sleep(5);
		}
        delete m_midiout;
        delete m_midiin;
        m_midiout = 0;
        m_midiin = 0;
    }

    void Run() {
        DWORD now = timeGetTime();
        if (m_midiout) {
            if ((now - last_surface_update_time) > config_surface_update_frequency) {
              if (moxf_in_daw_remote_mode) {
                moxfUpdateParameterNames();
                moxfUpdateParameterValues();
              }
                // switch (current_surface_live_layout) {
                //     case LL_FADER: UpdateSurfaceIfInDeviceMode();
                //     break;
                //     case LL_SENDS: UpdateSurfaceIfInSendsMode();
                //     break;
                //     case LL_SENDS_PAN: UpdateSurfaceIfInSendsPanMode();
                //     break;
                // }
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
        // UpdateSurfaceIfInVolumeMode();
    }

    void SetSurfacePan(MediaTrack *trackid, double pan) {
        // UpdateSurfaceIfInPanMode();
    }

    void SetSurfaceMute(MediaTrack *trackid, bool mute) {
        // UpdateSurfaceIfInMuteMode();
    }

    void SetSurfaceSelected(MediaTrack *trackid, bool selected) {
        // UpdateSurfaceIfInTrackSelectMode();
    }

    void SetSurfaceSolo(MediaTrack *trackid, bool solo) {
        // UpdateSurfaceIfInSoloMode();
    }

    void SetSurfaceRecArm(MediaTrack *trackid, bool recarm) {
        // UpdateSurfaceIfInRecordArmMode();
    }

    void SetPlayState(bool play, bool pause, bool rec) {
        state_recording = rec;
        state_playing = play;
        // UpdateSurface();
    }

    void SetRepeatState(bool rep) {
        state_loop = rep;
        // UpdateSurface();
    }

    void SetTrackTitle(MediaTrack *trackid, const char *title) {
      if (moxf_in_daw_remote_mode) {
        if (CountSelectedTracks(0) == 1) {
          MediaTrack *selectedTrack = GetSelectedTrack(0, 0);
          if (selectedTrack) {
            unsigned char trackName[24];
            GetTrackName(selectedTrack, (char *)trackName, 24);
            moxfDisplayTrackName(m_midiout, trackName, 24);
          }
        }
      }
    }

    bool GetTouchState(MediaTrack *trackid, int isPan) {
        return false;
    }

    void SetAutoMode(int mode) {}


    void ResetCachedVolPanStates() {

    }

    void OnTrackSelection(MediaTrack *trackid) {
      if (moxf_in_daw_remote_mode) {
        if (CountSelectedTracks(0) == 1) {
          MediaTrack *selectedTrack = GetSelectedTrack(0, 0);
          if (selectedTrack) {
            unsigned char trackName[24];
            GetTrackName(selectedTrack, (char *)trackName, 24);
            moxfDisplayTrackName(m_midiout, trackName, 24);
          }
        }
        moxfUpdateParameterNames();
        moxfUpdateParameterValues();
        // FIXME update also on track moving
        TrackList_UpdateAllExternalSurfaces();
      }
    }

    bool IsKeyDown(int key) {
        return false;
    }

    void moxfUpdateParameterNames() {
      if (CountSelectedTracks(0) == 1) {
        MediaTrack *selectedTrack = GetSelectedTrack(0, 0);
        if (selectedTrack) {
          // display TCPFX parameter names
          int fxParamCount = CountTCPFXParms(0, selectedTrack);
          for (int paramIndex = 0; paramIndex < fxParamCount; paramIndex++) {
            int fxIndex;
            int pluginParamIndex;
            double minimumParamValue;
            double maximumParamValue;
            GetTCPFXParm(0, selectedTrack, paramIndex, &fxIndex, &pluginParamIndex);
            unsigned char text[24];
            TrackFX_GetParamName(selectedTrack, fxIndex, pluginParamIndex, (char *)text, 24);
            moxfDisplayParameterName(m_midiout, paramIndex, (unsigned char *)text, 24);
            // double actualFxParamValue = TrackFX_GetParam(mediaTrack, fxIndex, pluginParamIndex, &minimumParamValue, &maximumParamValue);
            // sprintf(text, "%d,%d)", m_midi_in_dev, m_midi_out_dev);
          }
          if (fxParamCount < 24) {
            for (int i = fxParamCount; i < 24; i++) {
              unsigned char text[24] = "                      \0";
              moxfDisplayParameterName(m_midiout, i, (unsigned char *)text, 24);
            }
          }
        }
      }
    }

    void moxfUpdateParameterValues() {
      // TODO maybe check just for changes
      if (CountSelectedTracks(0) == 1) {
        MediaTrack *selectedTrack = GetSelectedTrack(0, 0);
        if (selectedTrack) {
          // display TCPFX parameter names
          int fxParamCount = CountTCPFXParms(0, selectedTrack);
          for (int paramIndex = 0; paramIndex < fxParamCount; paramIndex++) {
            int fxIndex;
            int pluginParamIndex;
            double minimumParamValue;
            double maximumParamValue;
            GetTCPFXParm(0, selectedTrack, paramIndex, &fxIndex, &pluginParamIndex);
            unsigned char text[10];
            TrackFX_GetFormattedParamValue(selectedTrack, fxIndex, pluginParamIndex, (char*)text, 10);
            moxfDisplayParameterValue(m_midiout, paramIndex, (unsigned char *)text, 10);
            // double actualFxParamValue = TrackFX_GetParam(mediaTrack, fxIndex, pluginParamIndex, &minimumParamValue, &maximumParamValue);
            // sprintf(text, "%d,%d)", m_midi_in_dev, m_midi_out_dev);
          }
          if (fxParamCount < 24) {
            for (int i = fxParamCount; i < 24; i++) {
              unsigned char text[10] = "        \0";
              moxfDisplayParameterValue(m_midiout, i, (unsigned char *)text, 10);
            }
          }
        }
      }
    }

};// end of class


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

    return new CSurf_MOXF(parms[2], parms[3], parms[4], parms[5], errStats);
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
                "MOXF",
                "Yamaha MOX/MOXF",
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
    IMPAPI(GetTrackName)
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
    IMPAPI(TrackFX_GetFormattedParamValue)

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
