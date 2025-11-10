#!/usr/bin/env bash
set -euo pipefail

# Generates build/kvm_config.json from $KVM_CONFIG_JSON
# - Never prints the secret to stdout
# - Optionally validates JSON with jq if available

if [[ -z "${KVM_CONFIG_JSON:-}" ]]; then
  echo "[gen_kvm_config] Error: KVM_CONFIG_JSON is required but not set." >&2
  exit 1
fi

mkdir -p build

# Write the file without echoing contents
printf '%s' "${KVM_CONFIG_JSON}" > build/kvm_config.json

# Validate JSON if jq is available; avoid printing content
if command -v jq >/dev/null 2>&1; then
  if ! jq -e . build/kvm_config.json >/dev/null 2>&1; then
    echo "[gen_kvm_config] Error: KVM_CONFIG_JSON is not valid JSON." >&2
    rm -f build/kvm_config.json
    exit 1
  fi
fi

echo "[gen_kvm_config] Generated build/kvm_config.json"

