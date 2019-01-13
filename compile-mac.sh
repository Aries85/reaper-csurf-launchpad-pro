#!/bin/sh

# https://gist.github.com/gubatron/32f82053596c24b6bec6
# http://www.informit.com/articles/article.aspx?p=1819492

VERSION="1.0.0"
WDL=../WDL
LAUNCHPAD_PRO_OUTPUT_LIB_NAME=reaper_csurf_launchpadpro_${VERSION}.dylib
MOXF_OUTPUT_LIB_NAME=reaper_csurf_moxf_${VERSION}.dylib
# rm $REAPER_OUTPUT_LIB_NAME res.rc_mac_dlg res.rc_mac_menu /Users/pepik/Library/Application\ Support/REAPER/UserPlugins/${OUTPUT_LIB_NAME}

php ${WDL}/swell/mac_resgen.php res.rc

c++ \
  -pipe -fPIC -O2 -std=c++14 -dynamiclib \
  -IWDL -IWDL/WDL -DSWELL_PROVIDED_BY_APP \
  csurf_launchpadpro.cpp $WDL/swell/swell-modstub.mm \
  -framework AppKit \
  -o ${LAUNCHPAD_PRO_OUTPUT_LIB_NAME}

c++ \
  -pipe -fPIC -O2 -std=c++14 -dynamiclib \
  -IWDL -IWDL/WDL -DSWELL_PROVIDED_BY_APP \
  csurf_moxf.cpp $WDL/swell/swell-modstub.mm \
  -framework AppKit \
  -o ${MOXF_OUTPUT_LIB_NAME}
