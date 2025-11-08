FROM registry.cn-hangzhou.aliyuncs.com/silentwind/jetkvm-stage-0 AS builder

FROM debian:bookworm-slim AS runner

ARG TARGETARCH

COPY --from=builder /out/jetkvm_app /app/jetkvm_app
COPY --from=builder /opt/mpp-libs/ /usr/lib/
COPY build/kvm_config.json /etc/jetkvm/kvm_config.template.json
COPY build/init.sh /init.sh

RUN apt-get update && DEBIAN_FRONTEND=noninteractive \
    apt-get install -y --no-install-recommends \
      libx264-164 libturbojpeg0 libyuv0 ca-certificates tzdata jq apache2-utils nano && \
    apt clean && \
    rm -rf /var/lib/apt/lists/* && \
    mkdir -p /userdata/jetkvm/images && \
    chmod +x /app/jetkvm_app && \
    chmod +x /init.sh && \
    ldconfig || true

ENTRYPOINT ["/init.sh"]
