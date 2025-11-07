import React from "react";

import { cx } from "@/cva.config";
import KeyboardAndMouseConnectedIcon from "@/assets/keyboard-and-mouse-connected.png";
import LoadingSpinner from "@components/LoadingSpinner";
import StatusCard from "@components/StatusCards";
import { USBStates } from "@/hooks/stores";
import { useI18n } from "@/i18n";

type StatusProps = Record<
  USBStates,
  {
    icon: React.FC<{ className: string | undefined }>;
    iconClassName: string;
    statusIndicatorClassName: string;
  }
>;

const usbStateKeyMap: Record<USBStates, string> = {
  configured: "status.usb.configured",
  attached: "status.usb.attached",
  addressed: "status.usb.addressed",
  "not attached": "status.usb.notAttached",
  suspended: "status.usb.suspended",
};
const StatusCardProps: StatusProps = {
  configured: {
    icon: ({ className }) => (
      <img className={cx(className)} src={KeyboardAndMouseConnectedIcon} alt="" />
    ),
    iconClassName: "h-5 w-5 shrink-0",
    statusIndicatorClassName: "bg-green-500 border-green-600",
  },
  attached: {
    icon: ({ className }) => <LoadingSpinner className={cx(className)} />,
    iconClassName: "h-5 w-5 text-blue-500",
    statusIndicatorClassName: "bg-slate-300 border-slate-400",
  },
  addressed: {
    icon: ({ className }) => <LoadingSpinner className={cx(className)} />,
    iconClassName: "h-5 w-5 text-blue-500",
    statusIndicatorClassName: "bg-slate-300 border-slate-400",
  },
  "not attached": {
    icon: ({ className }) => (
      <img className={cx(className)} src={KeyboardAndMouseConnectedIcon} alt="" />
    ),
    iconClassName: "h-5 w-5 opacity-50 grayscale filter",
    statusIndicatorClassName: "bg-slate-300 border-slate-400",
  },
  suspended: {
    icon: ({ className }) => (
      <img className={cx(className)} src={KeyboardAndMouseConnectedIcon} alt="" />
    ),
    iconClassName: "h-5 w-5 opacity-50 grayscale filter",
    statusIndicatorClassName: "bg-green-500 border-green-600",
  },
};

export default function USBStateStatus({
  state,
  peerConnectionState,
}: {
  state: USBStates;
  peerConnectionState?: RTCPeerConnectionState | null;
}) {
  const { t } = useI18n();
  const props = StatusCardProps[state];
  if (!props) {
    console.warn("Unsupported USB state: ", state);
    return;
  }

  // If the peer connection is not connected, show the USB cable as disconnected
  if (peerConnectionState !== "connected") {
    const {
      icon: Icon,
      iconClassName,
      statusIndicatorClassName,
    } = StatusCardProps["not attached"];

    return (
      <StatusCard
        title={t("status.usbTitle")}
        status={t("status.usb.notAttached")}
        icon={Icon}
        iconClassName={iconClassName}
        statusIndicatorClassName={statusIndicatorClassName}
      />
    );
  }

  return (
    <StatusCard
      title={t("status.usbTitle")}
      status={t(usbStateKeyMap[state])}
      {...StatusCardProps[state]}
    />
  );
}
