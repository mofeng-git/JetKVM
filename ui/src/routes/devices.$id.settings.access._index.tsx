import { useLoaderData, useNavigate } from "react-router";
import type { LoaderFunction } from "react-router";
import { ShieldCheckIcon } from "@heroicons/react/24/outline";
import { useCallback, useEffect, useState } from "react";

import api from "@/api";
import { SettingsPageHeader } from "@components/SettingsPageheader";
import { GridCard } from "@/components/Card";
import { Button, LinkButton } from "@/components/Button";
import { InputFieldWithLabel } from "@/components/InputField";
import { SelectMenuBasic } from "@/components/SelectMenuBasic";
import { SettingsItem } from "@components/SettingsItem";
import { SettingsSectionHeader } from "@/components/SettingsSectionHeader";
import { useDeviceUiNavigation } from "@/hooks/useAppNavigation";
import notifications from "@/notifications";
import { DEVICE_API } from "@/ui.config";
import { JsonRpcResponse, useJsonRpc } from "@/hooks/useJsonRpc";
import { isOnDevice } from "@/main";
import { TextAreaWithLabel } from "@components/TextArea";

import { LocalDevice } from "./devices.$id";
import { CloudState } from "./adopt";
import { useI18n } from "@/i18n";

export interface TLSState {
  mode: "self-signed" | "custom" | "disabled";
  certificate?: string;
  privateKey?: string;
}

const loader: LoaderFunction = async () => {
  if (isOnDevice) {
    const status = await api
      .GET(`${DEVICE_API}/device`)
      .then(res => res.json() as Promise<LocalDevice>);
    return status;
  }
  return null;
};

export default function SettingsAccessIndexRoute() {
  const loaderData = useLoaderData() as LocalDevice | null;
  const { t } = useI18n();

  const { navigateTo } = useDeviceUiNavigation();
  const navigate = useNavigate();

  const { send } = useJsonRpc();

  const [isAdopted, setAdopted] = useState(false);
  const [deviceId, setDeviceId] = useState<string | null>(null);
  const [cloudApiUrl, setCloudApiUrl] = useState("");
  const [cloudAppUrl, setCloudAppUrl] = useState("");

  // Use a simple string identifier for the selected provider
  const [selectedProvider, setSelectedProvider] = useState<string>("jetkvm");
  const [tlsMode, setTlsMode] = useState<string>("unknown");
  const [tlsCert, setTlsCert] = useState<string>("");
  const [tlsKey, setTlsKey] = useState<string>("");

  const getCloudState = useCallback(() => {
    send("getCloudState", {}, (resp: JsonRpcResponse) => {
      if ("error" in resp) return console.error(resp.error);
      const cloudState = resp.result as CloudState;
      setAdopted(cloudState.connected);
      setCloudApiUrl(cloudState.url);

      if (cloudState.appUrl) setCloudAppUrl(cloudState.appUrl);

      // Find if the API URL matches any of our predefined providers
      const isAPIJetKVMProd = cloudState.url === "https://api.jetkvm.com";
      const isAppJetKVMProd = cloudState.appUrl === "https://app.jetkvm.com";

      if (isAPIJetKVMProd && isAppJetKVMProd) {
        setSelectedProvider("jetkvm");
      } else {
        setSelectedProvider("custom");
      }
    });
  }, [send]);

  const getTLSState = useCallback(() => {
    send("getTLSState", {}, (resp: JsonRpcResponse) => {
      if ("error" in resp) return console.error(resp.error);
      const tlsState = resp.result as TLSState;

      setTlsMode(tlsState.mode);
      if (tlsState.certificate) setTlsCert(tlsState.certificate);
      if (tlsState.privateKey) setTlsKey(tlsState.privateKey);
    });
  }, [send]);

  const deregisterDevice = () => {
    send("deregisterDevice", {}, (resp: JsonRpcResponse) => {
      if ("error" in resp) {
        notifications.error(
          t("settings.access.notify.deregisterFail", { reason: resp.error.data || "Unknown error" }),
        );
        return;
      }

      getCloudState();
      // In cloud mode, we need to navigate to the device overview page, as we don't a connection anymore
      if (!isOnDevice) navigate("/");
      return;
    });
  };

  const onCloudAdoptClick = useCallback(
    (cloudApiUrl: string, cloudAppUrl: string) => {
      if (!deviceId) {
        notifications.error(t("settings.access.noDeviceId"));
        return;
      }

      send("setCloudUrl", { apiUrl: cloudApiUrl, appUrl: cloudAppUrl }, (resp: JsonRpcResponse) => {
        if ("error" in resp) {
          notifications.error(
            t("settings.access.notify.updateCloudUrlFail", { reason: resp.error.data || "Unknown error" }),
          );
          return;
        }

        const returnTo = new URL(window.location.href);
        returnTo.pathname = "/adopt";
        returnTo.search = "";
        returnTo.hash = "";
        window.location.href =
          cloudAppUrl +
          "/signup?deviceId=" +
          deviceId +
          `&returnTo=${returnTo.toString()}`;
      });
    },
    [deviceId, send],
  );

  // Handle provider selection change
  const handleProviderChange = (value: string) => {
    setSelectedProvider(value);

    // If selecting a predefined provider, update both URLs
    if (value === "jetkvm") {
      setCloudApiUrl("https://api.jetkvm.com");
      setCloudAppUrl("https://app.jetkvm.com");
    } else {
      if (cloudApiUrl || cloudAppUrl) return;
      setCloudApiUrl("");
      setCloudAppUrl("");
    }
  };

  // Function to update TLS state - accepts a mode parameter
  const updateTlsState = useCallback(
    (mode: string, cert?: string, key?: string) => {
      const state = { mode } as TLSState;
      if (cert && key) {
        state.certificate = cert;
        state.privateKey = key;
      }

      send("setTLSState", { state }, (resp: JsonRpcResponse) => {
        if ("error" in resp) {
          notifications.error(
            `Failed to update TLS settings: ${resp.error.data || "Unknown error"}`,
          );
          return;
        }

        notifications.success("TLS settings updated successfully");
      });
    }, [send]);

  // Handle TLS mode change
  const handleTlsModeChange = (value: string) => {
    setTlsMode(value);

    // For "disabled" and "self-signed" modes, immediately apply the settings
    if (value !== "custom") {
      updateTlsState(value);
    }
  };

  const handleTlsCertChange = (value: string) => {
    setTlsCert(value);
  };

  const handleTlsKeyChange = (value: string) => {
    setTlsKey(value);
  };

  // Update the custom TLS settings button click handler
  const handleCustomTlsUpdate = () => {
    updateTlsState(tlsMode, tlsCert, tlsKey);
  };

  // Fetch device ID and cloud state on component mount
  useEffect(() => {
    getCloudState();
    getTLSState();

    send("getDeviceID", {}, (resp: JsonRpcResponse) => {
      if ("error" in resp) return console.error(resp.error);
      setDeviceId(resp.result as string);
    });
  }, [send, getCloudState, getTLSState]);

  return (
    <div className="space-y-4">
      <SettingsPageHeader
        title={t("settings.access.title")}
        description={t("settings.access.desc")}
      />

      {loaderData?.authMode && (
        <>
          <div className="space-y-4">
            <SettingsSectionHeader
              title={t("settings.access.localTitle")}
              description={t("settings.access.localDesc")}
            />
            <>
              <SettingsItem
                title={t("settings.access.httpsTitle")}
                badge={t("settings.access.httpsBadge")}
                description={t("settings.access.httpsDesc")}
              >
                <SelectMenuBasic
                  size="SM"
                  value={tlsMode}
                  onChange={e => handleTlsModeChange(e.target.value)}
                  disabled={tlsMode === "unknown"}
                  options={[
                    { value: "disabled", label: t("settings.access.httpsOptions.disabled") },
                    { value: "self-signed", label: t("settings.access.httpsOptions.self") },
                    { value: "custom", label: t("settings.access.httpsOptions.custom") },
                  ]}
                />
              </SettingsItem>

              {tlsMode === "custom" && (
                <div className="mt-4 space-y-4">
                  <div className="space-y-4">
                    <SettingsItem
                      title={t("settings.access.tlsCertTitle")}
                      description={t("settings.access.tlsCertDesc")}
                    />
                    <div className="space-y-4">
                      <TextAreaWithLabel
                        label={t("settings.access.certLabel")}
                        rows={3}
                        placeholder={t("settings.access.certPlaceholder")}
                        value={tlsCert}
                        onChange={e => handleTlsCertChange(e.target.value)}
                      />
                    </div>

                    <div className="space-y-4">
                      <div className="space-y-4">
                        <TextAreaWithLabel
                          label={t("settings.access.keyLabel")}
                          description={t("settings.access.keyDesc")}
                          rows={3}
                          placeholder={t("settings.access.keyPlaceholder")}
                          value={tlsKey}
                          onChange={e => handleTlsKeyChange(e.target.value)}
                        />
                      </div>
                    </div>
                  </div>
                  <div className="flex items-center gap-x-2">
                    <Button
                      size="SM"
                      theme="primary"
                      text={t("settings.access.updateTls")}
                      onClick={handleCustomTlsUpdate}
                    />
                  </div>
                </div>
              )}

              <SettingsItem
                title={t("settings.access.authModeTitle")}
                description={t("settings.access.authModeCurr", { mode: loaderData.authMode === "password" ? t("settings.access.authPasswordProtected") : t("settings.access.authNoPassword") })}
              >
                {loaderData.authMode === "password" ? (
                  <Button
                    size="SM"
                    theme="light"
                    text={t("settings.access.disableProtection")}
                    onClick={() => {
                      navigateTo("./local-auth", { state: { init: "deletePassword" } });
                    }}
                  />
                ) : (
                  <Button
                    size="SM"
                    theme="light"
                    text={t("settings.access.enablePassword")}
                    onClick={() => {
                      navigateTo("./local-auth", { state: { init: "createPassword" } });
                    }}
                  />
                )}
              </SettingsItem>
            </>

            {loaderData.authMode === "password" && (
              <SettingsItem
                title={t("settings.access.changePassword")}
                description={t("settings.access.changePasswordDesc")}
              >
                <Button
                  size="SM"
                  theme="light"
                  text={t("settings.access.changePassword")}
                  onClick={() => {
                    navigateTo("./local-auth", { state: { init: "updatePassword" } });
                  }}
                />
              </SettingsItem>
            )}
          </div>
          <div className="h-px w-full bg-slate-800/10 dark:bg-slate-300/20" />
        </>
      )}

      <div className="space-y-4">
        <SettingsSectionHeader
          title={t("settings.access.remoteTitle")}
          description={t("settings.access.remoteDesc")}
        />

        <div className="space-y-4">
          {!isAdopted && (
            <>
              <SettingsItem
                title={t("settings.access.cloudProviderTitle")}
                description={t("settings.access.cloudProviderDesc")}
              >
                <SelectMenuBasic
                  size="SM"
                  value={selectedProvider}
                  onChange={e => handleProviderChange(e.target.value)}
                  options={[
                    { value: "jetkvm", label: t("settings.access.cloudProvider.jetkvm") },
                    { value: "custom", label: t("settings.access.cloudProvider.custom") },
                  ]}
                />
              </SettingsItem>

              {selectedProvider === "custom" && (
                <div className="mt-4 space-y-4">
                  <div className="flex items-end gap-x-2">
                    <InputFieldWithLabel
                      size="SM"
                      label={t("settings.access.cloudApiUrl")}
                      value={cloudApiUrl}
                      onChange={e => setCloudApiUrl(e.target.value)}
                      placeholder={t("settings.access.cloudApiPlaceholder")}
                    />
                  </div>
                  <div className="flex items-end gap-x-2">
                    <InputFieldWithLabel
                      size="SM"
                      label={t("settings.access.cloudAppUrl")}
                      value={cloudAppUrl}
                      onChange={e => setCloudAppUrl(e.target.value)}
                      placeholder={t("settings.access.cloudAppPlaceholder")}
                    />
                  </div>
                </div>
              )}
            </>
          )}

          {/* Show security info for JetKVM Cloud */}
          {selectedProvider === "jetkvm" && (
            <GridCard>
              <div className="flex items-start gap-x-4 p-4">
                <ShieldCheckIcon className="mt-1 h-8 w-8 shrink-0 text-blue-600 dark:text-blue-500" />
                <div className="space-y-3">
                  <div className="space-y-2">
                    <h3 className="text-base font-bold text-slate-900 dark:text-white">
                      {t("settings.access.cloudSecurityTitle")}
                    </h3>
                    <div>
                      <ul className="list-disc space-y-1 pl-5 text-xs text-slate-700 dark:text-slate-300">
                        {t("settings.access.cloudSecurityBullets").split("\n").map((x, i) => (
                          <li key={i}>{x}</li>
                        ))}
                      </ul>
                    </div>

                    <div className="text-xs text-slate-700 dark:text-slate-300">
                      {t("settings.access.cloudSecurityOpen")} {" "}
                      <a
                        href="https://github.com/jetkvm"
                        target="_blank"
                        rel="noopener noreferrer"
                        className="font-medium text-blue-600 hover:text-blue-800 dark:text-blue-500 dark:hover:text-blue-400"
                      >
                        GitHub
                      </a>
                      .
                    </div>
                  </div>
                  <hr className="block w-full border-slate-800/20 dark:border-slate-300/20" />

                  <div>
                    <LinkButton
                      to="https://jetkvm.com/docs/networking/remote-access"
                      size="SM"
                      theme="light"
                      text={t("settings.access.cloudSecurityLearn")}
                    />
                  </div>
                </div>
              </div>
            </GridCard>
          )}

          {!isAdopted ? (
            <div className="flex items-end gap-x-2">
              <Button
                onClick={() => onCloudAdoptClick(cloudApiUrl, cloudAppUrl)}
                size="SM"
                theme="primary"
                text={t("settings.access.adoptKvm")}
              />
            </div>
          ) : (
            <div>
              <div className="space-y-2">
                <p className="text-sm text-slate-600 dark:text-slate-300">
                  {t("settings.access.adopted")}
                </p>
                <div>
                  <Button
                    size="SM"
                    theme="light"
                    text={t("settings.access.deregister")}
                    className="text-red-600"
                    onClick={() => {
                      if (deviceId) {
                        if (
                          window.confirm(t("settings.access.confirmDeregister"))
                        ) {
                          deregisterDevice();
                        }
                      } else {
                        notifications.error(t("settings.access.noDeviceId"));
                      }
                    }}
                  />
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}

SettingsAccessIndexRoute.loader = loader;
