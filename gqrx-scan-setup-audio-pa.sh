#!/bin/bash
# Intercept a specific app's audio stream without affecting its output.

NULL_SINK_NAME="gqrx-scanner-intercept"
LOOPBACK_NAME="gqrx-scanner-monitor"
FIFO_PATH="${XDG_RUNTIME_DIR:-/tmp}/gqrx-scanner-audio.fifo"
RECORD_FORMAT="--channels=1 --format=u8 --rate=8000"
STATE_DIR="${XDG_RUNTIME_DIR:-/tmp}/gqrx-scanner-audio"

# PA object model:
#   Sink        = audio output (speakers, HDMI, null sink)
#   Source      = audio input (mic, monitor of a sink)
#   Sink Input  = a playing stream: app → some Sink
#   Source Output = a recording stream: some Source → app
#
# On attach we:
#   1. read the app's Sink Input to find its current Sink (e.g. speakers)
#   2. create a null Sink and move the app's Sink Input to it
#   3. create a loopback: null Sink's monitor Source → original Sink
#   now audio flows: app → null Sink → monitor → loopback → speakers
#   we can record from the null Sink's monitor (only that app's audio)

# create the state directory (stores app's original Sink for detach restore)
ensure_state_dir() {
    mkdir -p "$STATE_DIR"
}

# print all Sink Inputs (playing apps) with their index, binary, and display name
list_processes() {
    printf "%-8s %-30s %s\n" "INDEX" "PROCESS" "APPLICATION"
    pactl list sink-inputs | awk -v RS='Sink Input #' '
        NF {
            id = $1
            binary = ""
            app = ""
            for (i=2; i<=NF; i++) {
                if ($i == "application.process.binary") binary = $(i+2)
                if ($i == "application.name") app = $(i+2)
            }
            gsub(/"/, "", binary)
            gsub(/"/, "", app)
            printf "%-8s %-30s %s\n", id, binary, app
        }'
}

# given a substring (process binary), find the Sink Input #ID
get_sink_input_id() {
    local pattern="$1"
    pactl list sink-inputs | awk -v RS='Sink Input #' -v pat="$pattern" '
        $0 ~ pat {print $1+0; exit}'
}

# given a process substring, find which Sink that Sink Input is playing to
get_original_sink_id() {
    local pattern="$1"
    pactl list sink-inputs | awk -v RS='Sink Input #' -v pat="$pattern" '
        $0 ~ pat {
            for (i=1; i<=NF; i++)
                if ($i == "Sink:") print $(i+1)
            exit
        }'
}

# resolve a numeric Sink ID to a human Sink name (e.g. "alsa_output.pci-...")
sink_id_to_name() {
    local id="$1"
    pactl list sinks short | awk -v id="$id" '$1 == id {print $2}'
}

# recovery: find any Sink Input still connected to our null Sink (when state file is lost)
find_input_on_null_sink() {
    pactl list sink-inputs | awk -v RS='Sink Input #' -v sink="$NULL_SINK_NAME" '
        $0 ~ "Sink: " {
            for (i=1; i<=NF; i++)
                if ($i == "Sink:") { sid = $(i+1); break }
            cmd = "pactl list sinks short | awk '\''$1 == " sid " {print $2}'\''"
            cmd | getline sname
            close(cmd)
            if (sname == sink) { print $1; exit }
        }'
}

case "${1:-}" in
    list-processes)
        list_processes
        ;;
    attach)
        if [ -z "${2:-}" ]; then
            echo "Usage: $0 attach <process-substring>" >&2
            exit 1
        fi

        if [ -f "$STATE_DIR/state" ]; then
            echo "Re-attaching: detaching first..."
            "$0" detach
        fi

        pattern="$2"

        id=$(get_sink_input_id "$pattern")
        if [ -z "$id" ]; then
            echo "No sink input found matching: $pattern" >&2
            exit 1
        fi

        orig_sink_id=$(get_original_sink_id "$pattern")
        orig_sink_name=$(sink_id_to_name "$orig_sink_id")
        echo "Found sink input #$id (sink $orig_sink_id: $orig_sink_name)"

        ensure_state_dir

        # load null sink
        null_module_id=$(pactl load-module module-null-sink sink_name="$NULL_SINK_NAME")

        for i in $(seq 1 20); do
            pactl list sources short | grep -q "${NULL_SINK_NAME}.monitor" && break
            sleep 0.1
        done

        # pw-loopback: capture from null sink's monitor, play to original sink
        cap_props="stream.capture.sink=true target.object=$NULL_SINK_NAME"
        pw-loopback \
            -n "$LOOPBACK_NAME" \
            -i "$cap_props" \
            -o "media.class=Stream/Output/Audio" \
            -P "$orig_sink_name" &>/dev/null &
        loop_pid=$!
        disown
        sleep 0.5  # let pw-loopback settle

        # create FIFO and start recording from null sink's monitor in raw u8 mono 8kHz
        # disabled: use popen() in the C program instead
        # mkfifo "$FIFO_PATH"
        # sh -c 'pacat --record -d "$1" --channels=1 --format=u8 --rate=8000 --file-format=raw > "$2"' \
        #     _ "${NULL_SINK_NAME}.monitor" "$FIFO_PATH" &>/dev/null &
        # recorder_pid=$!

        echo "sink_input_id=$id"          > "$STATE_DIR/state"
        echo "orig_sink_id=$orig_sink_id" >> "$STATE_DIR/state"
        echo "null_module_id=$null_module_id" >> "$STATE_DIR/state"
        echo "loop_pid=$loop_pid"         >> "$STATE_DIR/state"
        # echo "recorder_pid=$recorder_pid" >> "$STATE_DIR/state"

        pactl move-sink-input "$id" "$NULL_SINK_NAME"

        echo "Capturing from: ${NULL_SINK_NAME}.monitor"
        echo "App audio still plays through original sink."
        # echo "Recording to FIFO: $FIFO_PATH (u8 mono 8kHz)"
        echo "Run '$0 detach' to restore."
        ;;
    detach)
        state_file="$STATE_DIR/state"
        if [ ! -f "$state_file" ]; then
            id=$(find_input_on_null_sink)
            if [ -z "$id" ]; then
                echo "No active interception state found and no sink input connected to $NULL_SINK_NAME." >&2
                exit 1
            fi
            echo "Recovering: moving sink input #$id to default sink"
            pactl move-sink-input "$id" "$(pactl get-default-sink)"
        else
            source "$state_file"
            pactl move-sink-input "$sink_input_id" "$orig_sink_id"
            kill "$loop_pid" 2>/dev/null; wait "$loop_pid" 2>/dev/null
            # kill "$recorder_pid" 2>/dev/null; wait "$recorder_pid" 2>/dev/null
            pactl unload-module "$null_module_id" 2>/dev/null
            rm -f "$state_file"  # "$FIFO_PATH"
        fi
        echo "Interception turned down."
        ;;
    cleanup)
        rm -f "$STATE_DIR/state"  # "$FIFO_PATH"
        pkill -x "pw-loopback" 2>/dev/null
        # pkill -x "pacat" 2>/dev/null
        pactl list modules short 2>/dev/null | awk '/module-null-sink/ {print $1}' \
            | while read m; do pactl unload-module "$m" 2>/dev/null; done
        echo "All interception state and modules cleaned up."
        ;;
    *)
        echo "Usage: $0 [list-processes | attach <process-substring> | detach | cleanup]"
        exit 1
        ;;
esac
