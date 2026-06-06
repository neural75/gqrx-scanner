#!/bin/bash
# Intercept a specific app's audio stream without affecting its output.
# PipeWire-native version — uses only tools from pipewire-bin.
#
# Dependencies (all from one package):
#   pw-cli, pw-link, pw-loopback, pw-cat  →  pipewire-bin
#   Install:  sudo apt install pipewire-bin
#
# How it works (PipeWire object model):
#   Node       = any audio object: sink, source, or stream (app playing audio)
#   Port       = input/output endpoint on a Node
#   Link       = connection between two Ports (output → input)
#   Application streams have "media.class = Stream/Output/Audio"
#   Audio sinks have "media.class = Audio/Sink"
#   Monitor ports (output ports on sinks that carry looped-back audio) have
#     the port name prefix "monitor_"
#
# On attach we:
#   1. Find the target stream Node by matching its application.name
#   2. Find its current sink by parsing pw-link -l for playback connections
#   3. Create a null sink (another Node with media.class=Audio/Sink)
#   4. Disconnect the stream's output ports from the old sink
#   5. Reconnect them to the null sink
#   6. Create a loopback from the null sink's monitor back to the original sink
#   Now audio flows: app → null sink → monitor → loopback → speakers

NULL_SINK_NAME="gqrx-scanner-intercept"
LOOPBACK_NAME="gqrx-scanner-monitor"
STATE_DIR="${XDG_RUNTIME_DIR:-/tmp}/gqrx-scanner-audio"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Create the state directory (stores stream node name, old sink, null node id)
ensure_state_dir() {
    mkdir -p "$STATE_DIR"
}

# ---------------------------------------------------------------------------
# Strip trailing whitespace from a string.
# ---------------------------------------------------------------------------
_trim() {
    echo "$1" | awk '{sub(/[ \t]+$/, ""); print}'
}

# ---------------------------------------------------------------------------
# Common awk logic: parse pw-cli list-objects Node output with a line-based
# state machine.  For each node with media.class="Stream/Output/Audio" we
# emit "<id> <app_name_or_node_name>".
# The output format from pw-cli list-objects Node is:
#   <TAB>id N, type ...<TAB><TAB>key = "value"
# ---------------------------------------------------------------------------
_pw_list_streams_awk() {
    awk '
    function emit() {
        if (in_node && media_class == "Stream/Output/Audio") {
            name = (app_name != "" ? app_name : node_name)
            printf "%s\t%s\n", node_id, name
        }
    }
    /^[ \t]+id [0-9]+,/ {
        emit()
        node_id = $0
        sub(/^[ \t]+id /, "", node_id)
        sub(/,.*/, "", node_id)
        in_node = 1
        app_name = ""
        node_name = ""
        media_class = ""
    }
    in_node && /media\.class = / {
        gsub(/^[ \t]*[^=]*= /, "", $0)
        gsub(/^"|"$|[,]/, "", $0)
        media_class = $0
    }
    in_node && /application\.name = / {
        gsub(/^[ \t]*[^=]*= /, "", $0)
        gsub(/^"|"$|[,]/, "", $0)
        app_name = $0
    }
    in_node && /node\.name = / {
        gsub(/^[ \t]*[^=]*= /, "", $0)
        gsub(/^"|"$|[,]/, "", $0)
        node_name = $0
    }
    END { emit() }
    '
}

# ---------------------------------------------------------------------------
# List all stream Nodes (media.class = "Stream/Output/Audio") with their
# numeric id and application.name.  Output format (aligned for terminal):
#   INDEX  APPLICATION
# ---------------------------------------------------------------------------
list_processes() {
    printf "%-8s %s\n" "ID" "APPLICATION"
    pw-cli list-objects Node 2>/dev/null | _pw_list_streams_awk | while IFS=$'\t' read id name; do
        printf "%-8s %s\n" "$id" "$name"
    done
}

# ---------------------------------------------------------------------------
# Find a stream Node name by matching a substring against application.name
# or node.name.  Prints the first match; exits 1 if none.
#
# PipeWire nodes have a "node.name" property (unique identifier, e.g.
# "GQRX") and an "application.name" (human-readable, e.g. "GQRX" or
# "OSS Emulation[dsd]").  We match the user-provided substring against
# application.name first, then fall back to node.name.
# ---------------------------------------------------------------------------
get_stream_node_name() {
    local pattern="$1"
    local result

    result=$(pw-cli list-objects Node 2>/dev/null | awk -v pat="$pattern" '
    function emit() {
        if (in_node && media_class == "Stream/Output/Audio") {
            if (app_name != "" && tolower(app_name) ~ tolower(pat)) {
                print node_name
                found = 1
            } else if (node_name != "" && tolower(node_name) ~ tolower(pat)) {
                print node_name
                found = 1
            }
        }
    }
    /^[ \t]+id [0-9]+,/ {
        if (found) exit
        emit()
        node_id = $0; sub(/^[ \t]+id /, "", node_id); sub(/,.*/, "", node_id)
        in_node = 1; app_name = ""; node_name = ""; media_class = ""
    }
    in_node && /media\.class = / { gsub(/^[ \t]*[^=]*= /, "", $0); gsub(/^"|"$|[,]/, "", $0); media_class = $0 }
    in_node && /application\.name = / { gsub(/^[ \t]*[^=]*= /, "", $0); gsub(/^"|"$|[,]/, "", $0); app_name = $0 }
    in_node && /node\.name = / { gsub(/^[ \t]*[^=]*= /, "", $0); gsub(/^"|"$|[,]/, "", $0); node_name = $0 }
    END { if (!found) emit() }
    ')

    if [ -z "$result" ]; then
        echo "[ ERROR ] No active audio stream matches '$pattern'." >&2
        echo "         Use '$0 list-processes' to see available streams." >&2
        return 1
    fi
    echo "$result"
}

# ---------------------------------------------------------------------------
# Given a stream node name, find the sink it is currently playing to.
# Parses pw-link -l output:
#
#   GQRX:output_FL
#     |-> alsa_output.pci-...:playback_FL         ← sink connection
#     |-> PulseAudio Volume Control:input_FL      ← recorder, not a sink
#
# We look for |-> (output → input) connections where the target port
# starts with "playback" — those are connections to a sink's input ports.
# The first such connection gives us the sink node name.
# ---------------------------------------------------------------------------
get_stream_sink_name() {
    local stream="$1"
    local result
    result=$(pw-link -l 2>/dev/null | awk -v s="$stream" '
    BEGIN { found = 0; sink = "" }
    index($0, s ":") == 1 {
        cur = $0
        sub(/[ \t]+$/, "", cur)
        found = 1
        next
    }
    found && /^[^ \t]/ { found = 0 }
    found && /^[ \t]+\|-> / {
        target = $0
        sub(/^[ \t]+\|-> /, "", target)
        split(target, a, ":")
        if (a[2] ~ /^playback/) {
            sink = a[1]
            found = 0
        }
    }
    END { if (sink != "") print sink }
    ')
    if [ -z "$result" ]; then
        echo "[ ERROR ] Could not find sink for stream '$stream'." >&2
        return 1
    fi
    echo "$result"
}

# ---------------------------------------------------------------------------
# Find any stream connected to our null sink (for recovery when state file
# is missing).  Parses |<- entries for the null sink's playback ports:
#
#   gqrx-scanner-intercept:playback_FL
#     |<- GQRX:output_FL                         ← stream feeding this port
# ---------------------------------------------------------------------------
find_stream_on_null_sink() {
    pw-link -l 2>/dev/null | awk -v s="$NULL_SINK_NAME" '
    /^[^ \t]/ { cur = $0; sub(/[ \t]+$/, "", cur) }
    index(cur, s ":") == 1 && /^[ \t]+\|<- / {
        src = $0
        sub(/^[ \t]+\|<- /, "", src)
        split(src, a, ":")
        if (!seen[a[1]]++) print a[1]
    }' | head -1
}

# ---------------------------------------------------------------------------
# Wait for a PipeWire node with the given name to appear (up to ~2 sec).
# ---------------------------------------------------------------------------
wait_for_node() {
    local name="$1"
    for i in $(seq 1 20); do
        pw-cli list-objects Node 2>/dev/null | grep -q "node\.name = \"$name\"" && return 0
        sleep 0.1
    done
    return 1
}

# ---------------------------------------------------------------------------
# Create the null sink and return its node ID.
# The adapter factory "support.null-audio-sink" creates a virtual sink that
# accepts audio but discards it (no hardware output).  It also creates a
# monitor source so we can capture the audio stream.
#
# object.linger=true  — keeps the node alive after pw-cli exits
# audio.position=[FL FR]  — stereo ports (matching most apps)
# ---------------------------------------------------------------------------
create_null_sink() {
    pw-cli create-node adapter '{
        factory.name=support.null-audio-sink
        node.name='"$NULL_SINK_NAME"'
        node.description="Gqrx Scanner Intercept"
        media.class=Audio/Sink
        object.linger=true
        audio.position=[FL FR]
    }' >/dev/null 2>&1 || return 1

    # pw-cli create-node does not print the node ID in one-shot mode.
    # We find it by searching for the node name we just created.
    pw-cli list-objects Node 2>/dev/null | awk -v name="$NULL_SINK_NAME" '
    /^[ \t]+id [0-9]+,/ {
        node_id = $0; sub(/^[ \t]+id /, "", node_id); sub(/,.*/, "", node_id)
        in_node = 1; node_name = ""
    }
    in_node && /node\.name = / {
        gsub(/^[ \t]*[^=]*= /, "", $0); gsub(/^"|"$|[,]/, "", $0)
        if ($0 == name) { print node_id; exit }
    }
    in_node && /^[ \t]*$/ { in_node = 0 }
    '
}

# ---------------------------------------------------------------------------
# Move a stream from its current sink to a target sink.
# Iterates each output port of the stream, disconnects playback links to
# the old sink, and reconnects them to the new sink.
#
# We only move playback_* connections (the actual sink output).  Recorder
# connections (e.g. to pavucontrol or other apps listening via the stream's
# monitor) are left untouched.
# ---------------------------------------------------------------------------
move_stream() {
    local stream="$1"
    local new_sink="$2"

    pw-link -l 2>/dev/null | awk -v s="$stream" -v sink="$new_sink" '
    BEGIN { out_port = "" }
    index($0, s ":") == 1 {
        out_port = $0
        sub(/[ \t]+$/, "", out_port)
        next
    }
    out_port != "" && /^[^ \t]/ { out_port = "" }
    out_port != "" && /^[ \t]+\|-> / {
        target = $0
        sub(/^[ \t]+\|-> /, "", target)
        split(target, a, ":")
        if (a[2] ~ /^playback/) {
            print out_port "\t" target
        }
    }
    ' | while IFS=$'\t' read out_port target; do
        # Disconnect from old sink
        pw-link -d "$out_port" "$target" 2>/dev/null || {
            echo "[ WARNING ] Could not disconnect $out_port from $target" >&2
            continue
        }
        # Connect to new sink (keep the same port suffix, e.g. playback_FL)
        new_target="${new_sink}:${target#*:}"
        pw-link "$out_port" "$new_target" 2>/dev/null || {
            echo "[ ERROR ] Could not connect $out_port to $new_target" >&2
            echo "         Check available ports: pw-link -i '^$new_sink:'" >&2
            # Attempt fallback: connect to the new sink without specifying port
            # (PipeWire will auto-select if only one port pair)
            pw-link "$out_port" "${new_sink}:playback_FL" 2>/dev/null && continue
            pw-link "$out_port" "${new_sink}:playback_FR" 2>/dev/null && continue
            echo "[ ERROR ] Failed to connect $out_port to $new_sink" >&2
        }
    done
}

# ---------------------------------------------------------------------------
# Find the first available non-null Audio/Sink (for detach recovery).
# ---------------------------------------------------------------------------
find_default_sink() {
    local result
    result=$(pw-cli list-objects Node 2>/dev/null | awk -v skip="$NULL_SINK_NAME" '
    /^[ \t]+id [0-9]+,/ {
        if (found) next
        node_id = $0; sub(/^[ \t]+id /, "", node_id); sub(/,.*/, "", node_id)
        in_node = 1; node_name = ""; media_class = ""
    }
    in_node && /media\.class = / { gsub(/^[ \t]*[^=]*= /, "", $0); gsub(/^"|"$|[,]/, "", $0); media_class = $0 }
    in_node && /node\.name = / { gsub(/^[ \t]*[^=]*= /, "", $0); gsub(/^"|"$|[,]/, "", $0); node_name = $0 }
    in_node && /^[ \t]*$/ && media_class == "Audio/Sink" && node_name != skip {
        print node_name
        found = 1
    }
    END { if (!found) print "" }')
    if [ -z "$result" ]; then
        echo "[ ERROR ] No audio output sink available!" >&2
        return 1
    fi
    echo "$result"
}

# ---------------------------------------------------------------------------
# Find and destroy any null-audio-sink nodes with our name (for cleanup).
# ---------------------------------------------------------------------------
_cleanup_null_sinks() {
    pw-cli list-objects Node 2>/dev/null | awk -v name="$NULL_SINK_NAME" '
    /^[ \t]+id [0-9]+,/ {
        node_id = $0; sub(/^[ \t]+id /, "", node_id); sub(/,.*/, "", node_id)
        in_node = 1; node_name = ""
    }
    in_node && /node\.name = / {
        gsub(/^[ \t]*[^=]*= /, "", $0); gsub(/^"|"$|[,]/, "", $0)
        if ($0 == name) { print node_id; next }
    }
    ' | sort -u | while read id; do
        pw-cli destroy "$id" 2>/dev/null && echo "Destroyed null sink node ID $id"
    done
}

# ---------------------------------------------------------------------------
# Main dispatch
# ---------------------------------------------------------------------------

case "${1:-}" in
    list-processes)
        list_processes
        ;;

    attach)
        if [ -z "${2:-}" ]; then
            echo "Usage: $0 attach <application-name-substring>" >&2
            exit 1
        fi

        # Remove stale state before re-attaching
        if [ -f "$STATE_DIR/state" ]; then
            echo "Re-attaching: detaching first..."
            "$0" detach
        fi

        pattern="$2"

        # Find the stream node name
        stream_name=$(get_stream_node_name "$pattern") || exit 1
        echo "Found stream: $stream_name"

        # Find current sink
        orig_sink=$(get_stream_sink_name "$stream_name") || exit 1
        echo "Stream currently playing to sink: $orig_sink"

        ensure_state_dir

        # Create null sink
        echo "Creating null audio sink '$NULL_SINK_NAME'..."
        null_node_id=$(create_null_sink)
        if [ -z "$null_node_id" ]; then
            echo "[ ERROR ] Could not create null audio sink." >&2
            echo "         Is PipeWire running?" >&2
            exit 1
        fi
        echo "Null sink created (node ID: $null_node_id)"

        # Wait for the null sink to be fully registered
        wait_for_node "$NULL_SINK_NAME" || {
            echo "[ ERROR ] Null sink did not appear in time." >&2
            pw-cli destroy "$null_node_id" 2>/dev/null
            exit 1
        }

        # Start loopback: capture from null sink's monitor → original sink
        # Same as the PA script — pw-loopback is already a PipeWire tool.
        cap_props="stream.capture.sink=true target.object=$NULL_SINK_NAME"
        pw-loopback \
            -n "$LOOPBACK_NAME" \
            -i "$cap_props" \
            -P "$orig_sink" &>/dev/null &
        loop_pid=$!
        disown
        sleep 0.5

        # Move the stream's output ports from original sink → null sink
        echo "Moving stream '$stream_name' to null sink..."
        move_stream "$stream_name" "$NULL_SINK_NAME"

        # Save state for detach
        echo "stream_node_name='$stream_name'"   > "$STATE_DIR/state"
        echo "orig_sink_name='$orig_sink'"       >> "$STATE_DIR/state"
        echo "null_node_id='$null_node_id'"      >> "$STATE_DIR/state"
        echo "loop_pid='$loop_pid'"              >> "$STATE_DIR/state"

        echo "-----"
        echo "Audio interception active."
        echo "App audio still plays through original sink."
        echo "Run '$0 detach' to restore."
        ;;

    detach)
        state_file="$STATE_DIR/state"
        if [ ! -f "$state_file" ]; then
            # Recovery path: state file missing, try to find orphaned stream
            stream=$(find_stream_on_null_sink)
            if [ -z "$stream" ]; then
                echo "[ ERROR ] No active interception found." >&2
                exit 1
            fi
            echo "Recovering: stream '$stream' connected to null sink"
            # Find a suitable default sink
            default_sink=$(find_default_sink) || exit 1
            echo "Moving stream '$stream' to '$default_sink'..."
            move_stream "$stream" "$default_sink"
        else
            source "$state_file"
            # Restore stream to original sink
            echo "Restoring stream '$stream_node_name' to '$orig_sink_name'..."
            move_stream "$stream_node_name" "$orig_sink_name"
            # Kill loopback
            kill "$loop_pid" 2>/dev/null; wait "$loop_pid" 2>/dev/null
            # Destroy null sink
            pw-cli destroy "$null_node_id" 2>/dev/null || {
                echo "[ WARNING ] Could not destroy null sink (ID $null_node_id)" >&2
            }
            rm -f "$state_file"
        fi
        echo "Interception turned down."
        ;;

    cleanup)
        rm -f "$STATE_DIR/state"
        pkill -x "pw-loopback" 2>/dev/null
        _cleanup_null_sinks
        echo "All interception state cleaned up."
        ;;

    *)
        echo "Usage: $0 [list-processes | attach <substring> | detach | cleanup]"
        echo ""
        echo "Commands:"
        echo "  list-processes              List active audio streams with their IDs"
        echo "  attach <substring>          Intercept audio from matching stream"
        echo "  detach                      Restore stream to original sink"
        echo "  cleanup                     Remove all leftover null sinks and processes"
        echo ""
        echo "Dependencies: pipewire-bin (pw-cli, pw-link, pw-loopback)"
        exit 1
        ;;
esac
