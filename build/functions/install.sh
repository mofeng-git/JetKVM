#!/bin/bash

# --- 预准备 ---

prepare_dns_and_mirrors() {
    echo "信息：在 chroot 环境中准备 DNS 和更换软件源..."
    run_in_chroot "
    mkdir -p /run/systemd/resolve/ \\
    && touch /run/systemd/resolve/stub-resolv.conf \\
    && printf '%s\\n' 'nameserver 1.1.1.1' 'nameserver 1.0.0.1' > /etc/resolv.conf \\
    && echo '信息：尝试更换镜像源...' \\
    && bash <(curl -sSL https://gitee.com/SuperManito/LinuxMirrors/raw/main/ChangeMirrors.sh) \\
        --source mirrors.ustc.edu.cn --upgrade-software false --web-protocol http || echo '警告：更换镜像源脚本执行失败，可能网络不通或脚本已更改'
    "
}

delete_armbian_verify(){
    echo "信息：在 chroot 环境中修改 Armbian 软件源..."
    run_in_chroot "echo 'deb http://mirrors.ustc.edu.cn/armbian bullseye main bullseye-utils bullseye-desktop' > /etc/apt/sources.list.d/armbian.list"
}

prepare_external_binaries() {
    local platform="$1" # linux/armhf or linux/amd64 or linux/aarch64
    # 如果在 GitHub Actions 环境下，使用 silentwind0/jetkvm-stage-0，否则用阿里云镜像
    if is_github_actions; then
        local docker_image="silentwind0/jetkvm-stage-0"
    else
        local docker_image="registry.cn-hangzhou.aliyuncs.com/silentwind/jetkvm-stage-0"
    fi

    echo "信息：准备外部预编译二进制文件 (平台: $platform)..."
    ensure_dir "$PREBUILT_DIR"

    echo "信息：拉取 Docker 镜像 $docker_image (平台: $platform)..."
    sudo docker pull --platform "$platform" "$docker_image" || { echo "错误：拉取 Docker 镜像 $docker_image 失败" >&2; exit 1; }

    echo "信息：创建 Docker 容器 $DOCKER_CONTAINER_NAME ..."
    sudo docker create --name "$DOCKER_CONTAINER_NAME" "$docker_image" || { echo "错误：创建 Docker 容器 $DOCKER_CONTAINER_NAME 失败" >&2; exit 1; }

    echo "信息：从 Docker 容器导出文件到 $PREBUILT_DIR ..."
    sudo docker export "$DOCKER_CONTAINER_NAME" | sudo tar -xf - -C "$PREBUILT_DIR" || { echo "错误：导出并解压 Docker 容器内容失败" >&2; exit 1; }

    echo "信息：预编译二进制文件准备完成，存放于 $PREBUILT_DIR"

    # 删除 Docker 容器
    sudo docker rm -f "$DOCKER_CONTAINER_NAME" || { echo "错误：删除 Docker 容器 $DOCKER_CONTAINER_NAME 失败" >&2; exit 1; }    
}

config_base_files() {
    local platform_id="$1" # e.g., "onecloud", "cumebox2"
    echo "信息：配置基础文件和目录结构 ($platform_id)..."

    echo "信息：创建 JetKVM 相关目录..."
    ensure_dir "$ROOTFS/userdata/jetkvm/images"
    ensure_dir "$ROOTFS/app"


    echo "信息：复制默认配置文件..."
    sudo cp build/kvm_config.json "$ROOTFS/userdata/" || { echo "错误：复制默认配置文件失败" >&2; exit 1; }

    # 尝试下载或使用本地 rc.local 文件
    download_rc_local "$platform_id" || echo "信息：rc.local 文件不存在，跳过"
    if [ -f "$SRCPATH/image/$platform_id/rc.local" ]; then
        echo "信息：复制设备特定的 rc.local 文件..."
        sudo cp "$SRCPATH/image/$platform_id/rc.local" "$ROOTFS/etc/"
    fi

    echo "信息：从预编译目录复制二进制文件和库..."
    sudo cp "$PREBUILT_DIR/opt/mpp-libs/"* "$ROOTFS/lib/"*-linux-*/ || echo "警告：复制 /opt/mpp-libs/ 失败，可能源目录或目标目录不存在或不匹配"
    sudo cp "$PREBUILT_DIR/out/jetkvm_app" "$ROOTFS/app/" || { echo "错误：复制 jetkvm_app 二进制文件失败" >&2; exit 1; }
    
    # 禁用 apt-file
 	if [ -f "$ROOTFS/etc/apt/apt.conf.d/50apt-file.conf" ]; then
        echo "信息：禁用 apt-file 配置..."
        sudo mv "$ROOTFS/etc/apt/apt.conf.d/50apt-file.conf" "$ROOTFS/etc/apt/apt.conf.d/50apt-file.conf.disabled"
    fi
    echo "信息：基础文件配置完成。"
}

# --- KVMD 安装与配置 ---

install_base_packages() {
    echo "信息：在 chroot 环境中更新源并安装基础软件包..."
    run_in_chroot "
    apt-get update && \\
    apt install -y --no-install-recommends \\
        libx264-164 libturbojpeg0 libyuv0 ca-certificates tzdata jq apache2-utils nano kmod unzip network-manager && \\
    apt clean && \\
    rm -rf /var/lib/apt/lists/*
    "
}

configure_network() {
    local network_type="$1" # "systemd-networkd" or others (default network-manager)
    if [ "$network_type" = "systemd-networkd" ]; then
        echo "信息：在 chroot 环境中配置 systemd-networkd..."
        
        # onecloud 与 onecloud-pro 均启用基于 SN 的 MAC 地址生成
        if [ "$TARGET_DEVICE_NAME" = "onecloud" ] || [ "$TARGET_DEVICE_NAME" = "onecloud-pro" ]; then
            echo "信息：为 ${TARGET_DEVICE_NAME} 平台配置基于 SN 的 MAC 地址生成机制..."
            
            # 复制MAC地址生成脚本
            sudo cp "$SCRIPT_DIR/scripts/generate-random-mac.sh" "$ROOTFS/usr/local/bin/"
            sudo chmod +x "$ROOTFS/usr/local/bin/generate-random-mac.sh"
            
            # 复制systemd服务文件
            sudo cp "$SCRIPT_DIR/services/kvmd-generate-mac.service" "$ROOTFS/etc/systemd/system/"
            
            # 创建初始网络配置文件（不包含MAC地址，将由脚本生成）
            run_in_chroot "
            echo -e '[Match]\\nName=eth0\\n\\n[Network]\\nDHCP=yes' > /etc/systemd/network/99-eth0.network && \\
            systemctl mask NetworkManager && \\
            systemctl unmask systemd-networkd && \\
            systemctl enable systemd-networkd systemd-resolved && \\
            systemctl enable kvmd-generate-mac.service
            "
            echo "信息：${TARGET_DEVICE_NAME} 基于 SN 的 MAC 地址生成机制配置完成"
        fi
    else
        echo "信息：使用默认的网络管理器 (NetworkManager)..."
        # 可能需要确保 NetworkManager 是启用的 (通常默认是)
        run_in_chroot "systemctl enable NetworkManager"
    fi
}

configure_kvmd_core() {
     echo "信息：在 chroot 环境中配置 JetKVM 服务..."
     
     # 复制 JetKVM 首次运行脚本和服务
     echo "信息：配置 JetKVM 首次运行初始化服务..."
     sudo cp "build/services/jetkvm.service" "$ROOTFS/etc/systemd/system/"
     
     # 安装 JetKVM 但不执行需要在首次运行时完成的操作
     run_in_chroot "
     chmod +x /app/jetkvm_app && \\
     systemctl enable jetkvm.service
     "
     
     echo "信息：JetKVM 服务配置完成"
}

configure_system() {
    echo "信息：在 chroot 环境中配置系统级设置 (sudoers, udev, services)..."
    run_in_chroot "
    echo 'libcomposite' >> /etc/modules && \\
    echo 'net.ipv4.ip_forward = 1' > /etc/sysctl.d/99-kvmd-extra.conf && \\
    rm /etc/resolv.conf && \\
    printf '%s\\n' 'nameserver 1.1.1.1' 'nameserver 1.0.0.1' > /etc/resolv.conf && \
    systemctl enable kvmd-gostc && \
    echo "1.0.0" > /version && \
    ln -sf /usr/share/zoneinfo/Asia/Shanghai /etc/localtime
    "
}

install_gostc() {
    local arch="$1" # armhf, aarch64, x86_64
    local gostc_arch="$arch"
    local gostc_version="v2.0.8-beta.2"
    
    # 根据架构映射下载文件名
    case "$arch" in
        armhf) gostc_arch="arm_7" ;;
        aarch64) gostc_arch="arm64_v8.0" ;;
        x86_64|amd64) gostc_arch="amd64_v1" ;;
        *) echo "错误：不支持的架构 $arch"; exit 1 ;;
    esac
    
    echo "信息：在 chroot 环境中下载并安装 gostc ($gostc_arch)..."
    run_in_chroot "
    mkdir -p /tmp/gostc && cd /tmp/gostc && \\
    curl -L https://github.com/mofeng-git/gostc-open/releases/download/${gostc_version}/gostc_linux_${gostc_arch}.tar.gz -o gostc.tar.gz && \\
    tar -xzf gostc.tar.gz && \\
    mv gostc /usr/bin/ && \\
    chmod +x /usr/bin/gostc && \\
    cd / && rm -rf /tmp/gostc
    "
    
    echo "信息：创建 gostc systemd 服务文件..."
    run_in_chroot "
    cat > /etc/systemd/system/kvmd-gostc.service << 'EOF'
[Unit]
Description=基于FRP开发的内网穿透 客户端/节点
ConditionFileIsExecutable=/usr/bin/gostc
After=network.target

[Service]
StartLimitInterval=5
StartLimitBurst=10
ExecStart=/usr/bin/gostc \"-web-addr\" \"0.0.0.0:18080\"
WorkingDirectory=/usr/bin
Restart=always
RestartSec=10
EnvironmentFile=-/etc/sysconfig/gostc

[Install]
WantedBy=multi-user.target
EOF
    "
    
    echo "信息：gostc 安装和配置完成"
}

apply_kvmd_tweaks() {
    local arch="$1" # armhf, aarch64, x86_64
    local device_type="$2" # "gpio" or "video1" or other
    local atx_setting=""
    local hid_setting=""

    echo "信息：根据架构 ($arch) 和设备类型 ($device_type) 调整 KVMD 配置..."
    # 配置视频设备
    if [[ "$device_type" == *"video1"* ]]; then
        echo "信息：视频设备类型为 video1，设置视频设备为 /dev/video1..."
        run_in_chroot "sed -i 's|/dev/video0|/dev/video1|g' /userdata/kvm_config.json"
    else
            echo "信息：使用默认视频设备 /dev/video0..."
    fi
    echo "信息：KVMD 配置调整完成。"
}

# --- 整体安装流程 ---
install_and_configure_kvmd() {
    local arch="$1"         # 架构: armhf, aarch64, x86_64/amd64
    local device_type="$2"  # 设备特性: "gpio", "video1", "" (空或其他)
    local network_type="$3" # 网络配置: "systemd-networkd", "" (默认 network-manager)
    local host_arch=""      # Docker 平台架构: arm, aarch64, amd64

    # 映射架构名称
    case "$arch" in
        armhf) host_arch="arm" ;;
        aarch64) host_arch="arm64" ;; # docker aarch64 平台名是 arm64
        x86_64|amd64) host_arch="amd64"; arch="x86_64" ;; # 统一内部使用 x86_64
        *) echo "错误：不支持的架构 $arch"; exit 1 ;;
    esac


    prepare_external_binaries "linux/$host_arch"
    config_base_files "$TARGET_DEVICE_NAME" # 使用全局变量传递设备名

    # 特定设备的额外文件配置 (如果存在)
    # 将设备名中的连字符转换为下划线以匹配函数名
    local device_func_name="${TARGET_DEVICE_NAME//-/_}"
    if declare -f "config_${device_func_name}_files" > /dev/null; then
        echo "信息：执行特定设备的文件配置函数 config_${device_func_name}_files ..."
        "config_${device_func_name}_files"
    fi

    # 某些镜像可能需要准备DNS和换源
    if [[ "$NEED_PREPARE_DNS" = true ]]; then
        prepare_dns_and_mirrors
    fi
    # 可选：强制使用特定armbian源
    # delete_armbian_verify

    # 执行安装步骤
    install_base_packages
    configure_network "$network_type"
    configure_kvmd_core
    install_gostc "$arch" # 安装 gostc
    configure_system
    apply_kvmd_tweaks "$arch" "$device_type"

    run_in_chroot "df -h" # 显示最终磁盘使用情况
    echo "信息：JetKVM 安装和配置完成。"
} 