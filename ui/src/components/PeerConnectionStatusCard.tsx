import StatusCard from "@components/StatusCards";
import { useI18n } from "@/i18n";

const peerStatusKeyMap = {
  connected: "status.peer.connected",
  connecting: "status.peer.connecting",
  disconnected: "status.peer.disconnected",
  error: "status.peer.error",
  closing: "status.peer.closing",
  failed: "status.peer.failed",
  closed: "status.peer.closed",
  new: "status.peer.new",
} as Record<RTCPeerConnectionState | "error" | "closing", string>;

export type PeerConnections = keyof typeof peerStatusKeyMap;

type StatusProps = Record<
  PeerConnections,
  {
    statusIndicatorClassName: string;
  }
>;

export default function PeerConnectionStatusCard({
  state,
  title,
}: {
  state?: RTCPeerConnectionState | null;
  title?: string;
}) {
  if (!state) return null;
  const { t } = useI18n();
  const StatusCardProps: StatusProps = {
    connected: {
      statusIndicatorClassName: "bg-green-500 border-green-600",
    },
    connecting: {
      statusIndicatorClassName: "bg-slate-300 border-slate-400",
    },
    disconnected: {
      statusIndicatorClassName: "bg-slate-300 border-slate-400",
    },
    error: {
      statusIndicatorClassName: "bg-red-500 border-red-600",
    },
    closing: {
      statusIndicatorClassName: "bg-slate-300 border-slate-400",
    },
    failed: {
      statusIndicatorClassName: "bg-red-500 border-red-600",
    },
    closed: {
      statusIndicatorClassName: "bg-slate-300 border-slate-400",
    },
    ["new"]: {
      statusIndicatorClassName: "bg-slate-300 border-slate-400",
    },
  };
  const props = StatusCardProps[state];
  if (!props) return;

  return (
    <StatusCard
      title={title || t("status.peerTitle")}
      status={t(peerStatusKeyMap[state])}
      {...StatusCardProps[state]}
    />
  );
}
