#!/bin/bash
# ═══════════════════════════════════════════════════════════
#  Clawd Mochi — Claude Code Hook Script
#  Usage: bash esp32-hook.sh <event_name>
#
#  Claude Code 通过 stdin 传入 JSON，此脚本提取状态和 token 信息，
#  然后异步通知 ESP32。
# ═══════════════════════════════════════════════════════════

EVENT="$1"
ESP32_IP="${CLAWD_MOCHI_IP:-192.168.4.1}"

# 从 stdin 读取 Claude Code 传入的 JSON
INPUT=$(cat)

# 映射事件到 ESP32 状态
case "$EVENT" in
  UserPromptSubmit)
    STATE="thinking"
    ;;
  PreToolUse|PostToolUse|SubagentStart)
    STATE="coding"
    ;;
  PostToolUseFailure|StopFailure)
    STATE="error"
    ;;
  Stop)
    STATE="done"
    ;;
  SessionEnd)
    STATE="sleeping"
    ;;
  SessionStart)
    STATE="idle"
    ;;
  *)
    # 其他事件不通知
    exit 0
    ;;
esac

# 尝试从 JSON 中提取 token 数（如果有）
# Claude Code 的 usage 格式: {"usage":{"input_tokens":N,"output_tokens":N}}
TOKENS=0
if command -v python3 &>/dev/null; then
  TOKENS=$(echo "$INPUT" | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    u = d.get('usage', {})
    t = u.get('input_tokens', 0) + u.get('output_tokens', 0)
    print(t if t > 0 else 0)
except:
    print(0)
" 2>/dev/null)
fi
[ -z "$TOKENS" ] && TOKENS=0

# 异步通知 ESP32（& 不阻塞 Claude Code）
curl -s "http://${ESP32_IP}/status?state=${STATE}&tokens=${TOKENS}" &>/dev/null &
