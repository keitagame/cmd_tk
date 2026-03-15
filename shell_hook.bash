#!/usr/bin/env bash
# cmd_tracker シェルフック for Bash
# ~/.bashrc に以下を追加:
#   source /path/to/shell_hook.bash
#
# または cmd_tracker install を実行してください。

_CMD_TRACKER_BIN="${_CMD_TRACKER_BIN:-cmd_tracker}"
_cmd_tracker_start_time=""
_cmd_tracker_last_cmd=""

# コマンド実行前: 時刻を記録
_cmd_tracker_preexec() {
    _cmd_tracker_start_time=$(date +%s%N 2>/dev/null || date +%s)
    # コマンドの最初の単語 (コマンド名) だけ取得
    _cmd_tracker_last_cmd=$(echo "${BASH_COMMAND}" | awk '{print $1}')
}

# コマンド実行後: 経過時間を計算して記録
_cmd_tracker_precmd() {
    local exit_code=$?
    local now
    now=$(date +%s%N 2>/dev/null || date +%s)

    if [[ -n "$_cmd_tracker_start_time" && -n "$_cmd_tracker_last_cmd" ]]; then
        local cmd="$_cmd_tracker_last_cmd"

        # cmd_tracker 自体・空文字・内部コマンドは除外
        case "$cmd" in
            cmd_tracker|_cmd_tracker*|''|cd|ls|pwd|exit|history|source|\.|:)
                ;;
            *)
                local elapsed
                if [[ ${#now} -gt 10 ]]; then
                    # ナノ秒精度
                    elapsed=$(echo "scale=3; ($now - $_cmd_tracker_start_time) / 1000000000" | bc 2>/dev/null || echo "0")
                else
                    elapsed=$((now - _cmd_tracker_start_time))
                fi
                # バックグラウンドで記録 (ユーザー体験に影響しない)
                "$_CMD_TRACKER_BIN" record "$cmd" "${elapsed:-0}" &>/dev/null &
                ;;
        esac
    fi

    _cmd_tracker_start_time=""
    _cmd_tracker_last_cmd=""
    return $exit_code
}

# Bash の DEBUG trap と PROMPT_COMMAND を設定
trap '_cmd_tracker_preexec' DEBUG
if [[ -n "$PROMPT_COMMAND" ]]; then
    PROMPT_COMMAND="_cmd_tracker_precmd; $PROMPT_COMMAND"
else
    PROMPT_COMMAND="_cmd_tracker_precmd"
fi

# エイリアス: ダッシュボードを簡単に開く
alias cmdstat='cmd_tracker dashboard'
alias cmdtop='cmd_tracker top 10'
alias cmdlog='cmd_tracker stats'

echo "[cmd_tracker] バッシュフック有効化 ✓  (cmdstat でダッシュボード起動)"