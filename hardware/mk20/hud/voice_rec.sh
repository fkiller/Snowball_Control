#!/bin/sh

PID_FILE="/tmp/arecord.pid"
DEFAULT_WAV="/tmp/snowball_voice.wav"

case "$1" in
  start)
    TARGET="${2:-$DEFAULT_WAV}"
    killall -9 arecord 2>/dev/null
    rm -f "$PID_FILE" "$TARGET"
    (
      trap '' HUP
      exec arecord -D hw:0,0 -f S16_LE -r 16000 -c 1 -t wav "$TARGET" < /dev/null > /dev/null 2>&1
    ) &
    REC_PID=$!
    echo "$REC_PID" > "$PID_FILE"
    sleep 0.1
    if kill -0 "$REC_PID" 2>/dev/null; then
      echo "STARTED:$REC_PID"
      exit 0
    else
      echo "FAILED_TO_START"
      exit 1
    fi
    ;;

  stop)
    if [ -f "$PID_FILE" ]; then
      REC_PID=$(cat "$PID_FILE" 2>/dev/null)
      if [ -n "$REC_PID" ]; then
        kill -2 "$REC_PID" 2>/dev/null
        for i in 1 2 3 4 5; do
          if ! kill -0 "$REC_PID" 2>/dev/null; then
            break
          fi
          usleep 100000 2>/dev/null || sleep 0.1
        done
        kill -9 "$REC_PID" 2>/dev/null
      fi
      rm -f "$PID_FILE"
    else
      killall -9 arecord 2>/dev/null
    fi
    sync
    echo "STOPPED"
    exit 0
    ;;

  status)
    if [ -f "$PID_FILE" ]; then
      REC_PID=$(cat "$PID_FILE" 2>/dev/null)
      if kill -0 "$REC_PID" 2>/dev/null; then
        echo "RECORDING:$REC_PID"
        exit 0
      fi
    fi
    echo "IDLE"
    exit 0
    ;;

  *)
    echo "Usage: $0 {start [wav_path]|stop|status}"
    exit 1
    ;;
esac