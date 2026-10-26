#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CORE="$HERE/openconsole_host.py"
PLAN=$(python3 "$CORE" --plan) || {
  MESSAGE="OpenConsole could not safely validate this USB and the host firmware. No boot settings were changed. See the troubleshooting guide."
  if command -v zenity >/dev/null 2>&1; then
    zenity --error --title="OpenConsole" --text="$MESSAGE"
  elif command -v kdialog >/dev/null 2>&1; then
    kdialog --title "OpenConsole" --error "$MESSAGE"
  else
    printf '%s\n' "$MESSAGE" >&2
  fi
  exit 2
}
FIELDS=$(printf '%s' "$PLAN" | python3 -c 'import json,sys;p=json.load(sys.stdin);print(p["entry"] or "");print(p["mediaToken"]);print(p["device"]);print(p["architecture"]);print(p["directBootAvailable"])')
ENTRY=$(printf '%s\n' "$FIELDS" | sed -n '1p')
TOKEN=$(printf '%s\n' "$FIELDS" | sed -n '2p')
DEVICE=$(printf '%s\n' "$FIELDS" | sed -n '3p')
ARCH=$(printf '%s\n' "$FIELDS" | sed -n '4p')
DIRECT=$(printf '%s\n' "$FIELDS" | sed -n '5p')
if [ "$DIRECT" != True ]; then
  MESSAGE="No exact OpenConsole firmware entry was found for $DEVICE. Open firmware settings and choose the USB manually? No boot settings will be changed."
  if command -v zenity >/dev/null 2>&1; then
    zenity --question --title="OpenConsole — firmware settings fallback" --ok-label="Open firmware settings" --cancel-label="Cancel" --text="$MESSAGE" || exit 0
  elif command -v kdialog >/dev/null 2>&1; then
    kdialog --title "OpenConsole — firmware settings fallback" --warningyesno "$MESSAGE" --yes-label "Open firmware settings" --no-label "Cancel" || exit 0
  else
    printf '%s\nType FIRMWARE to continue, or press Enter to cancel: ' "$MESSAGE"; read -r answer; [ "$answer" = FIRMWARE ] || exit 0
  fi
  systemctl reboot --firmware-setup
  exit $?
fi
MESSAGE="Boot OpenConsole from $DEVICE ($ARCH) on the next restart only?\n\nYour normal boot order will not be changed. Save your work before continuing."
if command -v zenity >/dev/null 2>&1; then
  zenity --question --title="OpenConsole" --ok-label="Prepare next restart" --cancel-label="Cancel" --text="$MESSAGE" || exit 0
elif command -v kdialog >/dev/null 2>&1; then
  kdialog --title "OpenConsole" --yesno "$MESSAGE" --yes-label "Prepare next restart" --no-label "Cancel" || exit 0
else
  printf '%b\nType OPENCONSOLE to prepare the next restart, or press Enter to cancel: ' "$MESSAGE"
  read -r answer; [ "$answer" = OPENCONSOLE ] || exit 0
fi
pkexec python3 "$CORE" --arm --entry "$ENTRY" --expected-token "$TOKEN"
FINAL="OpenConsole is prepared for the next restart. Restart now? Unsaved work may be lost."
if command -v zenity >/dev/null 2>&1; then
  zenity --question --title="OpenConsole is ready" --ok-label="Restart now" --cancel-label="Later" --text="$FINAL" || exit 0
elif command -v kdialog >/dev/null 2>&1; then
  kdialog --title "OpenConsole is ready" --yesno "$FINAL" --yes-label "Restart now" --no-label "Later" || exit 0
else
  printf '%s\nType RESTART to restart now, or press Enter to restart later: ' "$FINAL"
  read -r answer; [ "$answer" = RESTART ] || exit 0
fi
systemctl reboot
