import { useLocation, useSearchParams } from "react-router";

import AuthLayout from "@components/AuthLayout";
import { useI18n } from "@/i18n";

export default function SignupRoute() {
  const { t } = useI18n();
  const [sq] = useSearchParams();
  const location = useLocation();
  const deviceId = sq.get("deviceId") || location.state?.deviceId;

  if (deviceId) {
    return (
      <AuthLayout
        showCounter={true}
        title={t("signup.titleDevice")}
        description={t("signup.descDevice")}
        action={t("signup.actionConnect")}
        cta={t("signup.ctaHave")}
        ctaHref={`/login?${sq.toString()}`}
      />
    );
  }

  return (
    <AuthLayout
      title={t("signup.title")}
      description={t("signup.desc")}
      action={t("signup.actionCreate")}
      // Header CTA
      cta={t("signup.ctaHave")}
      ctaHref={`/login?${sq.toString()}`}
    />
  );
}
