import { ExclamationTriangleIcon } from "@heroicons/react/24/outline";

import EmptyCard from "@/components/EmptyCard";
import { useI18n } from "@/i18n";

export default function NotFoundPage() {
  const { t } = useI18n();
  return (
    <div className="h-full w-full">
      <div className="flex h-full items-center justify-center">
        <div className="w-full max-w-2xl">
          <EmptyCard
            IconElm={ExclamationTriangleIcon}
            headline={t("notFound.title")}
            description={t("notFound.desc")}
          />
        </div>
      </div>
    </div>
  );
}
