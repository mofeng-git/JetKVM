import { useLocation, useSearchParams } from "react-router";

import AuthLayout from "@components/AuthLayout";
import { useI18n } from "@/i18n";

export default function LoginRoute() {
  const { t } = useI18n();
  const [sq] = useSearchParams();
  const location = useLocation();
  const deviceId = sq.get("deviceId") || location.state?.deviceId;

  if (deviceId) {
    return (
      <AuthLayout
        showCounter={true}
        title={t("login.titleDevice")}
        description={t("login.descDevice")}
        action={t("login.actionConnect")}
        // Header CTA
        cta={t("login.ctaNoAccount")}
        ctaHref={`/signup?${sq.toString()}`}
      />
    );
  }

  return (
    <AuthLayout
      title={t("login.title")}
      description={t("login.desc")}
      action={t("login.actionLogin")}
      // Header CTA
      cta={t("login.ctaNew")}
      ctaHref={`/signup?${sq.toString()}`}
    />
  );
}
