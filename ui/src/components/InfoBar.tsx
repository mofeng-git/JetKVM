import { useEffect, useMemo } from "react";
import { useI18n } from "@/i18n";

import { cx } from "@/cva.config";
import {
  useHidStore,
  useMouseStore,
  useRTCStore,
  useSettingsStore,
  useVideoStore,
  VideoState
} from "@/hooks/stores";
import { keys, modifiers } from "@/keyboardMappings";
import { useHidRpc } from "@/hooks/useHidRpc";

export default function InfoBar() {
  const { t } = useI18n();
  const { keysDownState } = useHidStore();
  const { mouseX, mouseY, mouseMove } = useMouseStore();
  const { rpcHidStatus } = useHidRpc();

  const videoClientSize = useVideoStore(
    (state: VideoState) => `${Math.round(state.clientWidth)}x${Math.round(state.clientHeight)}`,
  );

  const videoSize = useVideoStore(
    (state: VideoState) => `${Math.round(state.width)}x${Math.round(state.height)}`,
  );

  const { rpcDataChannel } = useRTCStore();
  const { debugMode, mouseMode, showPressedKeys } = useSettingsStore();
  const { isPasteInProgress } = useHidStore();

  useEffect(() => {
    if (!rpcDataChannel) return;
    rpcDataChannel.onclose = () => console.log("rpcDataChannel has closed");
    rpcDataChannel.onerror = (e: Event) =>
      console.error(`Error on DataChannel '${rpcDataChannel.label}': ${e}`);
  }, [rpcDataChannel]);

  const { keyboardLedState, usbState } = useHidStore();
  const { isTurnServerInUse } = useRTCStore();
  const { hdmiState } = useVideoStore();

  const displayKeys = useMemo(() => {
    if (!showPressedKeys)
      return "";

    const activeModifierMask = keysDownState.modifier || 0;
    const keysDown = keysDownState.keys || [];
    const modifierNames = Object.entries(modifiers).filter(([_, mask]) => (activeModifierMask & mask) !== 0).map(([name, _]) => name);
    const keyNames = Object.entries(keys).filter(([_, value]) => keysDown.includes(value)).map(([name, _]) => name);

    return [...modifierNames, ...keyNames].join(", ");
  }, [keysDownState, showPressedKeys]);

  return (
    <div className="bg-white border-t border-t-slate-800/30 text-slate-800 dark:border-t-slate-300/20 dark:bg-slate-900 dark:text-slate-300">
      <div className="flex flex-wrap items-stretch justify-between gap-1">
        <div className="flex items-center">
          <div className="flex flex-wrap items-center pl-2 gap-x-4">
            {debugMode ? (
              <div className="flex">
                <span className="text-xs font-semibold">{t("infobar.resolution")}</span>{" "}
                <span className="text-xs">{videoSize}</span>
              </div>
            ) : null}

            {debugMode ? (
              <div className="flex">
                <span className="text-xs font-semibold">{t("infobar.videoSize")} </span>
                <span className="text-xs">{videoClientSize}</span>
              </div>
            ) : null}

            {(debugMode && mouseMode == "absolute") ? (
              <div className="flex w-[118px] items-center gap-x-1">
                <span className="text-xs font-semibold">{t("infobar.pointer")}</span>
                <span className="text-xs">
                  {mouseX},{mouseY}
                </span>
              </div>
            ) : null}

            {(debugMode && mouseMode == "relative") ? (
              <div className="flex w-[118px] items-center gap-x-1">
                <span className="text-xs font-semibold">{t("infobar.lastMove")}</span>
                <span className="text-xs">
                  {mouseMove ?
                    `${mouseMove.x},${mouseMove.y} ${mouseMove.buttons ? `(${mouseMove.buttons})` : ""}` :
                    t("infobar.na")}
                </span>
              </div>
            ) : null}

            {debugMode && (
              <div className="flex w-[156px] items-center gap-x-1">
                <span className="text-xs font-semibold">{t("infobar.usbState")}</span>
                <span className="text-xs">{usbState}</span>
              </div>
            )}
            {debugMode && (
              <div className="flex w-[156px] items-center gap-x-1">
                <span className="text-xs font-semibold">{t("infobar.hdmiState")}</span>
                <span className="text-xs">{hdmiState}</span>
              </div>
            )}
            {debugMode && (
              <div className="flex w-[156px] items-center gap-x-1">
                <span className="text-xs font-semibold">{t("infobar.hidrpcState")}</span>
                <span className="text-xs">{rpcHidStatus}</span>
              </div>
            )}
            {isPasteInProgress && (
              <div className="flex w-[156px] items-center gap-x-1">
                <span className="text-xs font-semibold">{t("infobar.pasteMode")}</span>
                <span className="text-xs">{t("infobar.enabled")}</span>
              </div>
            )}
            {showPressedKeys && (
              <div className="flex items-center gap-x-1">
                <span className="text-xs font-semibold">{t("infobar.keys")}</span>
                <h2 className="text-xs">
                  {displayKeys}
                </h2>
              </div>
            )}
          </div>
        </div>
        <div className="flex items-center divide-x first:divide-l divide-slate-800/20 dark:divide-slate-300/20">
          {isTurnServerInUse && (
            <div className="shrink-0 p-1 px-1.5 text-xs text-black dark:text-white">
              {t("infobar.relayed")}
            </div>
          )}

          <div
            className={cx(
              "shrink-0 p-1 px-1.5 text-xs",
              keyboardLedState.caps_lock
                ? "text-black dark:text-white"
                : "text-slate-800/20 dark:text-slate-300/20",
            )}
          >
            {t("infobar.caps")}
          </div>
          <div
            className={cx(
              "shrink-0 p-1 px-1.5 text-xs",
              keyboardLedState.num_lock
                ? "text-black dark:text-white"
                : "text-slate-800/20 dark:text-slate-300/20",
            )}
          >
            {t("infobar.num")}
          </div>
          <div
            className={cx(
              "shrink-0 p-1 px-1.5 text-xs",
              keyboardLedState.scroll_lock
                ? "text-black dark:text-white"
                : "text-slate-800/20 dark:text-slate-300/20",
            )}
          >
            {t("infobar.scroll")}
          </div>
          {keyboardLedState.compose ? (
            <div className="shrink-0 p-1 px-1.5 text-xs">
              {t("infobar.compose")}
            </div>
          ) : null}
          {keyboardLedState.kana ? (
            <div className="shrink-0 p-1 px-1.5 text-xs">
              {t("infobar.kana")}
            </div>
          ) : null}
          {keyboardLedState.shift ? (
            <div className="shrink-0 p-1 px-1.5 text-xs">
              {t("infobar.shift")}
            </div>
          ) : null}
        </div>
      </div>
    </div>
  );
}
