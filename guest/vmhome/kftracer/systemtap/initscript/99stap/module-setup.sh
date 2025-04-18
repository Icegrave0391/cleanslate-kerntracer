#!/bin/bash

# NB: $moddir only works in install()
. /usr/lib/dracut/modules.d/99stap/params.conf

# Return 0 --> install stap module
# Return 1 --> skip stap module
# Return 255 --> install stap module only if explicitly requested
check() {
   # Do not include stap module if there are no scripts to include
   [ "$ONBOOT_SCRIPTS" ] || return 1
   # We're disabled by default: the initscript explicitly uses dracut's
   # '--add stap' when creating the initramfs. Otherwise, we might be
   # mistakenly included during e.g. kernel updates.
   return 255
}

# We don't depend on anything
depends() {
   echo ""
}

install() {

   # These programs are very likely to already be included by other
   # dracut modules so we're really not adding any weight
   dracut_install bash mkdir

   # The real payload...
   inst "$STAPRUN"
   inst "$STAPIO"
   for script in $ONBOOT_SCRIPTS; do
      inst "$CACHE_PATH/$script.ko"
   done

   # start-staprun.sh will need a copy of params.conf
   inst_simple "$moddir/params.conf" "/etc/systemtap-params.conf"
   inst_hook cmdline 01 "$moddir/start-staprun.sh"
}

