import { Form, redirect, useActionData, useLoaderData } from "react-router";
import type { ActionFunction, ActionFunctionArgs, LoaderFunction, LoaderFunctionArgs } from "react-router";
import { ChevronLeftIcon } from "@heroicons/react/16/solid";

import { Button, LinkButton } from "@components/Button";
import Card from "@components/Card";
import { CardHeader } from "@components/CardHeader";
import DashboardNavbar from "@components/Header";
import { User } from "@/hooks/stores";
import { checkAuth } from "@/main";
import Fieldset from "@components/Fieldset";
import { CLOUD_API } from "@/ui.config";
import { useI18n } from "@/i18n";

interface LoaderData {
  device: { id: string; name: string; user: { googleId: string } };
  user: User;
}

const action: ActionFunction = async ({ request }: ActionFunctionArgs) => {
  const { deviceId } = Object.fromEntries(await request.formData());

  try {
    const res = await fetch(`${CLOUD_API}/devices/${deviceId}`, {
      method: "DELETE",
      credentials: "include",
      headers: { "Content-Type": "application/json" },
      mode: "cors",
    });

    if (!res.ok) {
      return { message: "__ERR__" };
    }
  } catch (e) {
    console.error(e);
    return { message: "__ERR__" };
  }

  return redirect("/devices");
};

const loader: LoaderFunction = async ({ params }: LoaderFunctionArgs) => {
  const user = await checkAuth();
  const { id } = params;

  try {
    const res = await fetch(`${CLOUD_API}/devices/${id}`, {
      method: "GET",
      credentials: "include",
      mode: "cors",
    });

    const { device } = (await res.json()) as {
      device: { id: string; name: string; user: { googleId: string } };
    };

    return { device, user };
  } catch (e) {
    console.error(e);
    return { devices: [] };
  }
};

export default function DevicesIdDeregister() {
  const { device, user } = useLoaderData() as LoaderData;
  const error = useActionData() as { message: string };
  const { t } = useI18n();

  return (
    <div className="grid min-h-screen grid-rows-(--grid-layout)">
      <DashboardNavbar
        isLoggedIn={!!user}
        primaryLinks={[{ title: t("devices.navCloud"), to: "/devices" }]}
        userEmail={user?.email}
        picture={user?.picture}
        kvmName={device?.name}
      />

      <div className="w-full h-full">
        <div className="mt-4">
          <div className="w-full h-full px-4 mx-auto space-y-6 sm:max-w-6xl sm:px-8 md:max-w-7xl md:px-12 lg:max-w-8xl">
            <div className="space-y-4">
              <LinkButton
                size="SM"
                theme="blank"
                LeadingIcon={ChevronLeftIcon}
                text={t("devices.deregister.back")}
                to="/devices"
              />
              <Card className="max-w-3xl p-6">
                <div className="max-w-xl space-y-4">
                  <CardHeader
                    headline={t("devices.deregister.headline", { name: device.name || device.id })}
                    description={t("devices.deregister.desc").split("\n").map((x, i) => (
                      <div key={i}>{x}</div>
                    ))}
                  />

                  <Fieldset>
                    <Form method="POST" className="max-w-sm space-y-1.5">
                      <div className="flex gap-x-2">
                        <input name="deviceId" type="hidden" value={device.id} />
                        <LinkButton
                          size="MD"
                          theme="light"
                          to="/devices"
                          text={t("devices.deregister.cancel")}
                          textAlign="center"
                        />
                        <Button
                          size="MD"
                          theme="danger"
                          type="submit"
                          text={t("devices.deregister.submit")}
                          textAlign="center"
                        />
                      </div>
                      {error?.message && (
                        <p className="text-sm text-red-500 dark:text-red-400">
                          {t("devices.deregister.error")}
                        </p>
                      )}
                    </Form>
                  </Fieldset>
                </div>
              </Card>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

DevicesIdDeregister.loader = loader;
DevicesIdDeregister.action = action;
