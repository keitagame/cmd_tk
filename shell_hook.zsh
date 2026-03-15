#!/usr/bin/env zsh
# cmd_tracker シェルフック for Zsh
# ~/.zshrc に以下を追加:
#   source /path/to/shell_hook.zsh

_CMD_TRACKER_BIN="${_CMD_TRACKER_BIN:-cmd_tracker}"
_cmd_tracker_start_time=0
_cmd_tracker_last_cmd=""

preexec() {
    _cmd_tracker_start_time=$(($(date +%s%N) / 1000000))
    # コマンドの最初の単語 (コマンド名) だけ取得
    _cmd_tracker_last_cmd="${1%% *}"
}

precmd() {
    local now=$(( $(date +%s%N) / 1000000 ))
    local cmd="$_cmd_tracker_last_cmd"

    if (( _cmd_tracker_start_time > 0 )) && [[ -n "$cmd" ]]; then
        case "$cmd" in
            cmd_tracker|''|cd|ls|pwd|exit|history|source)
                ;;
            *)
                local elapsed=$(( (now - _cmd_tracker_start_time) ))
                local elapsed_f=$(printf "%.3f" $(( elapsed / 1000.0 )))
                "$_CMD_TRACKER_BIN" record "$cmd" "$elapsed_f" &>/dev/null &
                ;;
        esac
    fi

    _cmd_tracker_start_time=0
    _cmd_tracker_last_cmd=""
}

alias cmdstat='cmd_tracker dashboard'
alias cmdtop='cmd_tracker top 10'
alias cmdlog='cmd_tracker stats'

echo "[cmd_tracker] Zsh フック有効化 ✓  (cmdstat でダッシュボード起動)"