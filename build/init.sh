#!/usr/bin/env bash
set -euo pipefail

CONFIG_PATH="/userdata/kvm_config.json"
TEMPLATE_PATH="/etc/jetkvm/kvm_config.template.json"
IMAGES_DIR="/userdata/jetkvm/images"

mkdir -p "${IMAGES_DIR}"

echo "1.0.0" > /version

# Copy template if not exists or empty
if [[ ! -s "${CONFIG_PATH}" ]]; then
  echo "[init] provisioning default config from template"
  cp -f "${TEMPLATE_PATH}" "${CONFIG_PATH}"
fi

# Apply environment variable overrides to config via jq
filter='.'

# VIDEONUM -> /dev/videoN
if [[ -n "${VIDEONUM:-}" ]]; then
  filter+=" | .video.device=\"/dev/video${VIDEONUM}\""
fi

# VIDEOFORMAT -> MJPG|YUYV
if [[ -n "${VIDEOFORMAT:-}" ]]; then
  fmt=$(echo "$VIDEOFORMAT" | tr '[:upper:]' '[:lower:]')
  case "$fmt" in
    mjpg|mjpeg) fmtOut="MJPG" ;;
    yuyv|yuy2) fmtOut="YUYV" ;;
    *) fmtOut="MJPG" ;;
  esac
  filter+=" | .video.format=\"${fmtOut}\""
fi

# Resolution / FPS / Bitrate
if [[ -n "${VIDEOWIDTH:-}" ]]; then
  filter+=" | .video.width=${VIDEOWIDTH}"
fi
if [[ -n "${VIDEOHEIGHT:-}" ]]; then
  filter+=" | .video.height=${VIDEOHEIGHT}"
fi
if [[ -n "${VIDEOFPS:-}" ]]; then
  filter+=" | .video.fps=${VIDEOFPS}"
fi
if [[ -n "${VIDEOBITRATE:-}" ]]; then
  filter+=" | .video.bitrate_kbps=${VIDEOBITRATE}"
fi

# Ensure backend/encoder defaults remain
filter+=" | (.video.backend //= \"uvc\") | (.video.encoder //= \"x264\")"

tmpcfg=$(mktemp)
jq -c "$filter" "${CONFIG_PATH}" > "$tmpcfg" && mv "$tmpcfg" "${CONFIG_PATH}"

# PASSWORD -> bcrypt hash via apache2-utils (htpasswd -B)
if [[ -n "${PASSWORD:-}" ]]; then
  echo "[init] configuring local password auth"
  hash=$(htpasswd -nbB user "$PASSWORD" | cut -d: -f2)
  tmpcfg=$(mktemp)
  jq -c --arg hp "$hash" '.localAuthMode="password" | .hashed_password=$hp' "${CONFIG_PATH}" > "$tmpcfg" && mv "$tmpcfg" "${CONFIG_PATH}"
fi

# HTTPPORT -> runtime port via env the server reads (default 8080)
PORT_DEFAULT=8080
export JETKVM_HTTP_PORT="${HTTPPORT:-${PORT_DEFAULT}}"

exec /app/jetkvm_app "$@"
