import { useLoaderData, useRevalidator } from "react-router";
import type { LoaderFunction } from "react-router";
import { LuMonitorSmartphone } from "react-icons/lu";
import { ArrowRightIcon } from "@heroicons/react/16/solid";
import { useInterval } from "usehooks-ts";

import DashboardNavbar from "@components/Header";
import EmptyCard from "@components/EmptyCard";
import KvmCard from "@components/KvmCard";
import { LinkButton } from "@components/Button";
import { User } from "@/hooks/stores";
import { checkAuth } from "@/main";
import { CLOUD_API } from "@/ui.config";
import { useI18n } from "@/i18n";

interface LoaderData {
  devices: { id: string; name: string; online: boolean; lastSeen: string }[];
  user: User;
}

const loader: LoaderFunction = async () => {
  const user = await checkAuth();

  try {
    const res = await fetch(`${CLOUD_API}/devices`, {
      method: "GET",
      credentials: "include",
      mode: "cors",
    });

    const { devices } = await res.json();
    return { devices, user };
  } catch (e) {
    console.error(e);
    return { devices: [] };
  }
};

export default function DevicesRoute() {
  const { devices, user } = useLoaderData() as LoaderData;
  const revalidate = useRevalidator();
  useInterval(revalidate.revalidate, 4000);
  const { t } = useI18n();
  return (
    <div className="relative h-full">
      <div className="grid h-full select-none grid-rows-(--grid-headerBody)">
        <DashboardNavbar
          isLoggedIn={!!user}
          primaryLinks={[{ title: t("devices.navCloud"), to: "/devices" }]}
          userEmail={user?.email}
          picture={user?.picture}
        />

        <div className="flex h-full overflow-hidden">
          <div className="mx-auto h-full w-full space-y-6 px-4 sm:max-w-6xl sm:px-8 md:max-w-7xl md:px-12 lg:max-w-8xl">
            <div className="mt-8 flex items-center justify-between border-b border-b-slate-800/20 pb-4 dark:border-b-slate-300/20">
              <div>
                <h1 className="text-xl font-bold text-black dark:text-white">
                  {t("devices.title")}
                </h1>
                <p className="text-base text-slate-700 dark:text-slate-400">
                  {t("devices.desc")}
                </p>
              </div>
            </div>

            {devices.length === 0 ? (
              <div className="max-w-3xl">
                <EmptyCard
                  IconElm={LuMonitorSmartphone}
                  headline={t("devices.empty.headline")}
                  description={t("devices.empty.desc")}
                  BtnElm={
                    <LinkButton
                      to="https://jetkvm.com/docs/networking/remote-access"
                      size="SM"
                      theme="primary"
                      TrailingIcon={ArrowRightIcon}
                      text={t("common.learnMore")}
                    />
                  }
                />
              </div>
            ) : (
              <>
                <div className="grid gap-4 sm:grid-cols-2 md:grid-cols-3">
                  {devices.map(x => {
                    return (
                      <KvmCard
                        key={x.id}
                        id={x.id}
                        title={x.name ?? x.id}
                        lastSeen={x.lastSeen ? new Date(x.lastSeen) : null}
                        online={x.online}
                      />
                    );
                  })}
                </div>
              </>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

DevicesRoute.loader = loader;
