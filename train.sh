#!/bin/bash

# Name: train.sh
# Author: Nikita Neverov (BMTLab)
# Version: 1.0.1
# Date: 2026-10-17
# License: MIT
#
# Description:
#   The training run behind the two PGO profiles of crosshair (see Makefile):
#   the work the program does in use, a few hundred times over.
#   Each round starts it in the foreground, as the game launcher does,
#   waits until its window is mapped (the PID file then holds its PID),
#   and stops it with SIGTERM, once with the default look
#   and once with every option given.
#   Once per run it also toggles a background crosshair on and off,
#   and it walks the help and the error paths.
#   It needs the X display, and the crosshair flashes in the screen centre.
#
# Usage:
#   train.sh BINARY [ROUNDS]   (ROUNDS defaults to 100)
#
# Exit Codes:
#   0 - Training finished.
#   1 - A crosshair did not map its window within 5 seconds.

set -o errexit -o nounset -o pipefail

#######################################
# Wait until the PID file names PID, which the program writes once its
# window is mapped.
#
# Arguments:
#   1: PID of the crosshair started.
#   2: Path of its PID file.
#
# Returns:
#   0 once the file names PID; 1 after 5 seconds.
#######################################
function __train_wait_mapped() {
  local -r pid="$1"
  local -r pid_file="$2"

  local -i attempt
  for ((attempt = 0; attempt < 2500; attempt++)); do
    if [[ "$(cat "${pid_file}" 2>/dev/null)" == "${pid}" ]]; then
      return 0
    fi
    sleep 0.002
  done

  printf 'train: pid %s mapped no window within 5 s\n' "${pid}" >&2
  return 1
}

#######################################
# Start BINARY in the foreground with ARGS, wait for its window,
# and stop it the way the game launcher does.
#
# Arguments:
#   1: Program to run.
#   2: Path of its PID file.
#   3+: Extra options.
#######################################
function __train_show_once() {
  local -r binary="$1"
  local -r pid_file="$2"
  shift 2

  rm -f "${pid_file}"
  "${binary}" -f "$@" &
  local -r pid="$!"
  __train_wait_mapped "${pid}" "${pid_file}"
  kill -TERM "${pid}"
  wait "${pid}"
}

#######################################
# Main entry point.
#
# Arguments:
#   1: Program to train (a crosshair build).
#   2: Number of rounds (default 100).
#######################################
function train() {
  local -r binary="$1"
  local -r rounds="${2:-100}"
  local -r pid_file="${XDG_RUNTIME_DIR:-/tmp}/crosshair.pid"

  if [[ -z "${XDG_RUNTIME_DIR:-}" ]]; then
    printf 'train: XDG_RUNTIME_DIR is not set\n' >&2
    return 1
  fi

  local -i round
  for ((round = 0; round < rounds; round++)); do
    __train_show_once "${binary}" "${pid_file}"
    __train_show_once "${binary}" "${pid_file}" --size 15 --thickness 5 --color '#00ff00'
  done

  # The toggle: start in the background, then stop it again.
  rm -f "${pid_file}"
  "${binary}" >/dev/null
  local -i attempt
  for ((attempt = 0; attempt < 2500; attempt++)); do
    [[ -s "${pid_file}" ]] && break
    sleep 0.002
  done
  "${binary}" >/dev/null

  # Help and refused options, the cold paths.
  "${binary}" --help >/dev/null
  "${binary}" --size 0 2>/dev/null || true
  "${binary}" --color red 2>/dev/null || true
  "${binary}" --thickness 9 --size 3 2>/dev/null || true
}

train "$@"
