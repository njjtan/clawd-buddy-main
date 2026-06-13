#!/bin/bash
# ═══════════════════════════════════════════════════════════
#  Clawd Mochi — Hook Installer
#  将 hooks 注册到 ~/.claude/settings.json
#
#  用法: bash install.sh [--uninstall]
# ═══════════════════════════════════════════════════════════

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HOOK_SCRIPT="$SCRIPT_DIR/esp32-hook.sh"
SETTINGS="$HOME/.claude/settings.json"
MARKER="esp32-hook.sh"

# 要注册的事件列表
EVENTS=(
  "SessionStart"
  "SessionEnd"
  "UserPromptSubmit"
  "PreToolUse"
  "PostToolUse"
  "PostToolUseFailure"
  "Stop"
  "StopFailure"
  "SubagentStart"
  "SubagentStop"
)

# ── 卸载模式 ──────────────────────────────────────────────────
if [ "$1" = "--uninstall" ]; then
  if [ ! -f "$SETTINGS" ]; then
    echo "settings.json not found, nothing to uninstall."
    exit 0
  fi

  # 用 python3 安全地移除 hooks
  python3 -c "
import json, sys

with open('$SETTINGS', 'r') as f:
    settings = json.load(f)

if 'hooks' not in settings:
    print('No hooks found.')
    sys.exit(0)

removed = 0
for event in list(settings['hooks'].keys()):
    entries = settings['hooks'][event]
    if not isinstance(entries, list):
        continue
    new_entries = []
    for entry in entries:
        if not isinstance(entry, dict):
            new_entries.append(entry)
            continue
        # Check nested hooks format
        if 'hooks' in entry and isinstance(entry['hooks'], list):
            new_hooks = [h for h in entry['hooks']
                        if not (isinstance(h, dict) and '$MARKER' in h.get('command', ''))]
            if new_hooks:
                entry['hooks'] = new_hooks
                new_entries.append(entry)
            else:
                removed += 1
        # Check flat command format
        elif 'command' in entry and '$MARKER' in entry.get('command', ''):
            removed += 1
        else:
            new_entries.append(entry)

    if new_entries:
        settings['hooks'][event] = new_entries
    else:
        del settings['hooks'][event]

with open('$SETTINGS', 'w') as f:
    json.dump(settings, f, indent=2)

print(f'Removed {removed} Clawd Mochi hooks.')
"
  echo "Done. Restart Claude Code for changes to take effect."
  exit 0
fi

# ── 安装模式 ──────────────────────────────────────────────────

# 确保 settings.json 存在
if [ ! -f "$SETTINGS" ]; then
  mkdir -p "$(dirname "$SETTINGS")"
  echo '{"hooks":{}}' > "$SETTINGS"
  echo "Created new settings.json"
fi

# 用 python3 安全地合并 hooks
python3 -c "
import json, sys

settings_path = '$SETTINGS'
hook_script = '$HOOK_SCRIPT'
marker = '$MARKER'
events = $(printf '%s\n' "${EVENTS[@]}" | python3 -c "import sys,json; print(json.dumps([l.strip() for l in sys.stdin]))")

with open(settings_path, 'r') as f:
    settings = json.load(f)

if 'hooks' not in settings:
    settings['hooks'] = {}

added = 0
skipped = 0

for event in events:
    if event not in settings['hooks']:
        settings['hooks'][event] = []

    entries = settings['hooks'][event]
    if not isinstance(entries, list):
        settings['hooks'][event] = [entries]
        entries = settings['hooks'][event]

    # Check if already registered
    already = False
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        # Nested format
        if 'hooks' in entry and isinstance(entry['hooks'], list):
            for h in entry['hooks']:
                if isinstance(h, dict) and marker in h.get('command', ''):
                    already = True
                    break
        # Flat format
        if 'command' in entry and marker in entry.get('command', ''):
            already = True
        if already:
            break

    if already:
        skipped += 1
        continue

    # Add new hook entry (nested format)
    hook_cmd = 'bash \"' + hook_script + '\" ' + event
    settings['hooks'][event].append({
        'matcher': '',
        'hooks': [{
            'type': 'command',
            'command': hook_cmd,
            'async': True,
            'timeout': 5
        }]
    })
    added += 1

with open(settings_path, 'w') as f:
    json.dump(settings, f, indent=2)

print(f'Added: {added} hooks, Skipped: {skipped} (already registered)')
"

echo ""
echo "Hook script: $HOOK_SCRIPT"
echo "Settings:    $SETTINGS"
echo ""
echo "Registered events: ${EVENTS[*]}"
echo ""
echo "Done! Restart Claude Code for hooks to take effect."
echo ""
echo "Tip: Set CLAWD_MOCHI_IP env var if your ESP32 has a different IP:"
echo "  export CLAWD_MOCHI_IP=192.168.1.100"
