// Bare-bone REAPER extension
//
// 1. Grab reaper_plugin.h from https://raw.githubusercontent.com/reaper-oss/sws/master/reaper/reaper_plugin.h
// 2. Grab reaper_plugin_functions.h by running the REAPER action "[developer] Write C++ API functions header"
// 3. Grab WDL: git clone https://github.com/justinfrankel/WDL.git
// 4. Build then copy or link the binary file into <REAPER resource directory>/UserPlugins
//
// Linux
// =====
//
// c++ -pipe -fPIC -O2 -std=c++14 -IWDL -DSWELL_PROVIDED_BY_APP -DSWELL_TARGET_GDK -shared main.cpp -o reaper_barbone.so
//
// macOS
// =====
//
// c++ -pipe -fPIC -O2 -std=c++14 -stdlib=libc++ -IWDL/WDL/swell -dynamiclib main.cpp -o reaper_barbone.dylib
//
// Windows
// =======
//
// (Use the VS Command Prompt matching your REAPER architecture, eg. x64 to use the 64-bit compiler)
// cl /nologo /O2 /Z7 /Zo /W3 /DUNICODE main.cpp /link /DEBUG /OPT:REF /PDBALTPATH:%_PDB% /DLL /OUT:reaper_barebone.dll

#define REAPERAPI_IMPLEMENT
#include "reaper_plugin_functions.h"

#include <cstdio>

extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
  REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t *rec)
{
  if(!rec) {
    // cleanup code here

    return 0;
  }

  if(rec->caller_version != REAPER_PLUGIN_VERSION)
    return 0;
  
  // see also https://gist.github.com/cfillion/350356a62c61a1a2640024f8dc6c6770
  ShowConsoleMsg = rec->GetFunc("ShowConsoleMsg");
  
  if(!ShowConsoleMsg) {
    fprintf(stderr, "[reaper_barebone] Unable to import ShowConsoleMsg\n");
    return 0;
  }

  // initialization code here
  ShowConsoleMsg("Hello World!\n");

  return 1;
}
